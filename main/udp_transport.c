/*
  udp_transport.c - VSCP frames over UDP and the VSCP multicast group (lwIP raw API)

  Every datagram is one VSCP frame (version 0, not encrypted):
  packet type, header, timestamp, date/time, class, type, GUID, data size,
  data and CRC. Received frames are given to the node; events from the node
  are sent as frames to the UDP remote host (broadcast by default) and to
  the multicast group (joined with IGMP).

  https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_over_udp
  https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_multicast

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#include <stdio.h>
#include <string.h>

#include "lwip/igmp.h"
#include "lwip/ip_addr.h"
#include "lwip/udp.h"

#include "vscp-compiler.h"
#include "vscp-projdefs.h"

#include <vscp-firmware-helper.h>
#include <vscp.h>

#include "blinky.h"

#if BLINKY_ENABLE_UDP || BLINKY_ENABLE_MULTICAST

#define UDP_MAX_FRAME (1 + 35 + VSCP_MAX_DATA + 2) // Type, header, data, CRC

typedef struct {
  const char *name;
  struct udp_pcb *pcb;
  ip_addr_t to; // Events from the node are sent here
  u16_t port;
} channel_t;

static channel_t s_udp   = { .name = "UDP" };
static channel_t s_mcast = { .name = "multicast" };

// lwIP: a frame from the network, give the event to the node
static void
receive(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
  static uint8_t buf[UDP_MAX_FRAME];
  channel_t *ch = (channel_t *) arg;
  u16_t len     = pbuf_copy_partial(p, buf, sizeof(buf), 0);
  pbuf_free(p);
  if (0 == len) {
    return;
  }

  // The low nibble of the packet type is the encryption (not supported)
  if (buf[0] & 0x0f) {
    printf("%s: encrypted frame from %s ignored\n", ch->name, ipaddr_ntoa(addr));
    return;
  }

  vscp_event_t *pev = vscp_fwhlp_newEvent();
  if (NULL == pev) {
    return;
  }
  int rv = vscp_fwhlp_getEventFromFrame(pev, buf, len);
  if (VSCP_ERROR_SUCCESS != rv) {
    printf("%s: bad frame from %s (%d)\n", ch->name, ipaddr_ntoa(addr), rv);
    vscp_fwhlp_deleteEvent(&pev);
    return;
  }

  // Our own events (broadcast/multicast that comes back)
  if ((0 == memcmp(pev->GUID, vscp_core_guid(), 16)) || (VSCP_ERROR_SUCCESS != vscp_core_post(pev))) {
    vscp_fwhlp_deleteEvent(&pev);
  }
}

static struct udp_pcb *
open_pcb(channel_t *ch, u16_t port)
{
  struct udp_pcb *pcb = udp_new();
  if ((NULL == pcb) || (ERR_OK != udp_bind(pcb, IP_ANY_TYPE, port))) {
    printf("%s: could not open port %u\n", ch->name, port);
    if (pcb) {
      udp_remove(pcb);
    }
    return NULL;
  }
  udp_recv(pcb, receive, ch);
  return pcb;
}

void
udp_transport_send_event(const vscp_event_t *pev)
{
  static uint8_t frame[UDP_MAX_FRAME];
  if ((NULL == s_udp.pcb) && (NULL == s_mcast.pcb)) {
    return;
  }
  size_t size = vscp_fwhlp_getFrameSizeFromEvent((vscp_event_t *) pev);
  if ((0 == size) || (size > sizeof(frame)) ||
      (VSCP_ERROR_SUCCESS != vscp_fwhlp_writeEventToFrame(frame, sizeof(frame), 0, pev))) {
    printf("Could not make a frame for event %d/%d\n", pev->vscp_class, pev->vscp_type);
    return;
  }
  channel_t *channels[] = { &s_udp, &s_mcast };
  for (int i = 0; i < 2; i++) {
    if (NULL == channels[i]->pcb) {
      continue;
    }
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t) size, PBUF_RAM);
    if (NULL == p) {
      continue;
    }
    memcpy(p->payload, frame, size);
    udp_sendto(channels[i]->pcb, p, &channels[i]->to, channels[i]->port);
    pbuf_free(p);
  }
}

void
udp_transport_start(void)
{
#if BLINKY_ENABLE_UDP
  if (ipaddr_aton(BLINKY_UDP_REMOTE_HOST, &s_udp.to) && (NULL != (s_udp.pcb = open_pcb(&s_udp, BLINKY_UDP_PORT)))) {
    ip_set_option(s_udp.pcb, SOF_BROADCAST);
    s_udp.port = BLINKY_UDP_PORT;
    printf("UDP: port %d, events to %s\n", BLINKY_UDP_PORT, BLINKY_UDP_REMOTE_HOST);
  }
#endif

#if BLINKY_ENABLE_MULTICAST
  if (ipaddr_aton(BLINKY_MULTICAST_GROUP, &s_mcast.to)) {
    if (ERR_OK != igmp_joingroup(IP4_ADDR_ANY4, ip_2_ip4(&s_mcast.to))) {
      printf("Multicast: could not join %s\n", BLINKY_MULTICAST_GROUP);
    }
    else if (NULL != (s_mcast.pcb = open_pcb(&s_mcast, BLINKY_MULTICAST_PORT))) {
      udp_set_multicast_ttl(s_mcast.pcb, BLINKY_MULTICAST_TTL);
      s_mcast.port = BLINKY_MULTICAST_PORT;
      printf("Multicast: group %s port %d\n", BLINKY_MULTICAST_GROUP, BLINKY_MULTICAST_PORT);
    }
  }
#endif
}

void
udp_transport_work(void)
{
  // Received frames come in through lwIP callbacks (network_work)
}

#else

void
udp_transport_send_event(const vscp_event_t *pev)
{
}

void
udp_transport_start(void)
{
}

void
udp_transport_work(void)
{
}

#endif
