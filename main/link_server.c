/*
  link_server.c - VSCP link protocol server (TCP/IP), lwIP raw API

  Up to BLINKY_LINK_MAX_CLIENTS clients, each with a link protocol context.
  Events the client sends are given to the node (vscp_core_post); events
  from the node are put in the out queue of every logged in client whose
  filter lets them through, and the client gets them with 'retr' or in a
  'rcvloop'. Everything runs from the main loop (lwIP NO_SYS).

  https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_tcpiplink

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/tcp.h"

#include "vscp-compiler.h"
#include "vscp-projdefs.h"

#include <vscp-fifo.h>
#include <vscp-firmware-helper.h>
#include <vscp-link-protocol.h>
#include <vscp.h>

#include "blinky.h"

#if BLINKY_ENABLE_LINK

#define LINK_FIFO_SIZE     16
#define LINK_RCVLOOP_OK_MS 1000 // '+OK' this often in a receive loop without events
#define LINK_ROOM_FOR_EVENT 300 // Send buffer room needed before an event is written in a receive loop

#define LINK_WELCOME                                                                                                   \
  "Welcome to the VSCP Olimex STM32-P107 Blinky demo node\r\n"                                                         \
  "Copyright (C) 2026 Ake Hedman, the VSCP project, https://www.vscp.org\r\n"                                         \
  "+OK\r\n"

#define LINK_MSG_MAX_CLIENTS "-OK - Max number of clients reached. Disconnecting.\r\n"

typedef struct {
  vscp_link_ctx_t link;                          // Link protocol context
  struct tcp_pcb *pcb;                           // The connection (NULL = free slot)
  int close;                                     // Close the connection
  int output;                                    // Data written, call tcp_output()
  char buf[THIS_FIRMWARE_TCPIP_LINK_MAX_BUFFER]; // Received text
  size_t filled;
} client_t;

static client_t s_clients[BLINKY_LINK_MAX_CLIENTS];
static struct tcp_pcb *s_listen;
static int s_next_id = 1;

// ****************************************************************************
//                           Link protocol callbacks
// ****************************************************************************

static int
cb_write_client(vscp_link_ctx_t *pctx, const char *msg)
{
  client_t *c = (client_t *) pctx->user_data;
  size_t len  = strlen(msg);
  if ((NULL == c->pcb) || (len > tcp_sndbuf(c->pcb))) {
    return VSCP_ERROR_TRM_FULL; // Not room for it now (TCP_SND_BUF in lwipopts.h)
  }
  if (ERR_OK != tcp_write(c->pcb, msg, (u16_t) len, TCP_WRITE_FLAG_COPY)) {
    return VSCP_ERROR_WRITE_ERROR;
  }
  c->output = 1; // tcp_output() after the work
  return VSCP_ERROR_SUCCESS;
}

static int
cb_disconnect(vscp_link_ctx_t *pctx)
{
  ((client_t *) pctx->user_data)->close = 1;
  return VSCP_ERROR_SUCCESS;
}

static int
cb_quit(vscp_link_ctx_t *pctx)
{
  cb_write_client(pctx, VSCP_LINK_MSG_GOODBYE);
  ((client_t *) pctx->user_data)->close = 1;
  return VSCP_ERROR_SUCCESS;
}

static int
cb_welcome(vscp_link_ctx_t *pctx)
{
  return cb_write_client(pctx, LINK_WELCOME);
}

static int
cb_check_user(vscp_link_ctx_t *pctx, const char *arg)
{
  while (*arg && (' ' == *arg)) {
    arg++;
  }
  strncpy(pctx->user, arg, sizeof(pctx->user) - 1);
  pctx->user[sizeof(pctx->user) - 1] = '\0';
  return cb_write_client(pctx, VSCP_LINK_MSG_USENAME_OK);
}

static int
cb_check_password(vscp_link_ctx_t *pctx, const char *arg)
{
  if ('\0' == pctx->user[0]) {
    cb_write_client(pctx, VSCP_LINK_MSG_NEED_USERNAME);
    return VSCP_ERROR_CREDENTIALS;
  }
  while (*arg && (' ' == *arg)) {
    arg++;
  }
  if ((0 == strcmp(pctx->user, BLINKY_LINK_USER)) && (0 == strcmp(arg, BLINKY_LINK_PASSWORD))) {
    pctx->bValidated = 1;
    pctx->privLevel  = 15;
    printf("Link client %d: logged in as %s\n", pctx->id, pctx->user);
    return cb_write_client(pctx, VSCP_LINK_MSG_PASSWORD_OK);
  }
  pctx->user[0]    = '\0';
  pctx->bValidated = 0;
  pctx->privLevel  = 0;
  printf("Link client %d: wrong user or password\n", pctx->id);
  cb_write_client(pctx, VSCP_LINK_MSG_PASSWORD_ERROR);
  return VSCP_ERROR_CREDENTIALS;
}

static int
cb_check_authenticated(vscp_link_ctx_t *pctx)
{
  return pctx->bValidated ? VSCP_ERROR_SUCCESS : VSCP_ERROR_INVALID_PERMISSION;
}

static int
cb_check_privilege(vscp_link_ctx_t *pctx, uint8_t priv)
{
  return (pctx->privLevel >= priv) ? VSCP_ERROR_SUCCESS : VSCP_ERROR_INVALID_PERMISSION;
}

// An event from the client: give it to the node (which owns it on success)
static int
cb_send(vscp_link_ctx_t *pctx, vscp_event_t *pev)
{
  uint16_t size = pev->sizeData;
  if (VSCP_ERROR_SUCCESS != vscp_core_post(pev)) {
    pctx->statistics.cntOverruns++;
    return VSCP_ERROR_TRM_FULL; // The library deletes the event
  }
  pctx->statistics.cntReceiveFrames++;
  pctx->statistics.cntReceiveData += size;
  return VSCP_ERROR_SUCCESS;
}

static int
cb_chkdata(vscp_link_ctx_t *pctx, uint16_t *pcount)
{
  *pcount = LINK_FIFO_SIZE - vscp_fifo_getFree(&pctx->fifoEventsOut);
  return VSCP_ERROR_SUCCESS;
}

static int
cb_clrall(vscp_link_ctx_t *pctx)
{
  vscp_fifo_clear(&pctx->fifoEventsOut); // Deletes the events
  return VSCP_ERROR_SUCCESS;
}

static int
cb_retr(vscp_link_ctx_t *pctx, vscp_event_t **ppev)
{
  if (!vscp_fifo_read(&pctx->fifoEventsOut, ppev) || (NULL == *ppev)) {
    return VSCP_ERROR_RCV_EMPTY;
  }
  pctx->statistics.cntTransmitFrames++;
  pctx->statistics.cntTransmitData += (*ppev)->sizeData;
  return VSCP_ERROR_SUCCESS;
}

static int
cb_rcvloop(vscp_link_ctx_t *pctx, vscp_event_t **ppev)
{
  int rv = cb_retr(pctx, ppev);
  if (VSCP_ERROR_SUCCESS == rv) {
    pctx->last_rcvloop_time = millis();
    return rv;
  }
  // '+OK' every second when there are no events
  if ((millis() - pctx->last_rcvloop_time) >= LINK_RCVLOOP_OK_MS) {
    pctx->last_rcvloop_time = millis();
    return VSCP_ERROR_TIMEOUT;
  }
  return rv;
}

static int
cb_enable_rcvloop(vscp_link_ctx_t *pctx, int enable)
{
  pctx->bRcvLoop          = enable ? 1 : 0;
  pctx->last_rcvloop_time = millis();
  return VSCP_ERROR_SUCCESS;
}

static int
cb_get_rcvloop_status(vscp_link_ctx_t *pctx, int *pstatus)
{
  *pstatus = pctx->bRcvLoop;
  return VSCP_ERROR_SUCCESS;
}

static int
cb_get_guid(vscp_link_ctx_t *pctx, uint8_t *pguid)
{
  memcpy(pguid, pctx->guid, 16);
  return VSCP_ERROR_SUCCESS;
}

static int
cb_set_guid(vscp_link_ctx_t *pctx, uint8_t *pguid)
{
  memcpy(pctx->guid, pguid, 16); // GUID of this channel
  return VSCP_ERROR_SUCCESS;
}

static int
cb_get_version(vscp_link_ctx_t *pctx, uint8_t *pversion)
{
  pversion[0] = THIS_FIRMWARE_MAJOR_VERSION;
  pversion[1] = THIS_FIRMWARE_MINOR_VERSION;
  pversion[2] = THIS_FIRMWARE_RELEASE_VERSION;
  pversion[3] = THIS_FIRMWARE_BUILD_VERSION;
  return VSCP_ERROR_SUCCESS;
}

static int
cb_get_channel_id(vscp_link_ctx_t *pctx, uint16_t *pchid)
{
  *pchid = (uint16_t) pctx->id;
  return VSCP_ERROR_SUCCESS;
}

static int
cb_wcyd(vscp_link_ctx_t *pctx, uint64_t *pwcyd)
{
  *pwcyd = VSCP_SERVER_CAPABILITY_TCPIP | VSCP_SERVER_CAPABILITY_DECISION_MATRIX | VSCP_SERVER_CAPABILITY_IP4;
#if BLINKY_ENABLE_UDP
  *pwcyd |= VSCP_SERVER_CAPABILITY_UDP;
#endif
#if BLINKY_ENABLE_MULTICAST
  *pwcyd |= VSCP_SERVER_CAPABILITY_MULTICAST_CHANNEL;
#endif
  if (BLINKY_LINK_MAX_CLIENTS > 1) {
    *pwcyd |= VSCP_SERVER_CAPABILITY_TWO_CONNECTIONS;
  }
  return VSCP_ERROR_SUCCESS;
}

static int
cb_statistics(vscp_link_ctx_t *pctx, vscp_statistics_t *ps)
{
  memcpy(ps, &pctx->statistics, sizeof(vscp_statistics_t));
  return VSCP_ERROR_SUCCESS;
}

static int
cb_info(vscp_link_ctx_t *pctx, vscp_status_t *ps)
{
  memcpy(ps, &pctx->status, sizeof(vscp_status_t));
  return VSCP_ERROR_SUCCESS;
}

static int
cb_set_filter(vscp_link_ctx_t *pctx, vscpEventFilter *pf)
{
  pctx->filter.filter_class    = pf->filter_class;
  pctx->filter.filter_type     = pf->filter_type;
  pctx->filter.filter_priority = pf->filter_priority;
  memcpy(pctx->filter.filter_GUID, pf->filter_GUID, 16);
  return VSCP_ERROR_SUCCESS;
}

static int
cb_set_mask(vscp_link_ctx_t *pctx, vscpEventFilter *pf)
{
  pctx->filter.mask_class    = pf->mask_class;
  pctx->filter.mask_type     = pf->mask_type;
  pctx->filter.mask_priority = pf->mask_priority;
  memcpy(pctx->filter.mask_GUID, pf->mask_GUID, 16);
  return VSCP_ERROR_SUCCESS;
}

static uint16_t
cb_get_interface_count(vscp_link_ctx_t *pctx)
{
  return BLINKY_ENABLE_CAN ? 2 : 1; // The node, the CAN4VSCP bus
}

static int
cb_get_interface(vscp_link_ctx_t *pctx, uint16_t idx, vscp_interface_info_t *pif)
{
  if (idx >= cb_get_interface_count(pctx)) {
    return VSCP_ERROR_UNKNOWN_ITEM;
  }
  pif->idx = idx;
  memcpy(pif->guid, vscp_core_guid(), 16);
  if (0 == idx) {
    pif->type = VSCP_INTERFACE_TYPE_LEVEL2DRV;
    strncpy(pif->description, "Blinky node", sizeof(pif->description) - 1);
  }
  else {
    // Events from the bus: this GUID with the nickname of the CAN node in the last byte
    pif->type = VSCP_INTERFACE_TYPE_LEVEL1DRV;
    strncpy(pif->description, "CAN4VSCP bus", sizeof(pif->description) - 1);
  }
  return VSCP_ERROR_SUCCESS;
}

static int
cb_close_interface(vscp_link_ctx_t *pctx, uint8_t *pguid)
{
  return VSCP_ERROR_NOT_SUPPORTED;
}

static int
cb_shutdown(vscp_link_ctx_t *pctx)
{
  return VSCP_ERROR_NOT_SUPPORTED;
}

static int
cb_restart(vscp_link_ctx_t *pctx)
{
  cb_write_client(pctx, VSCP_LINK_MSG_OK);
  vscp_core_request_restart(); // Saves pending changes first
  return VSCP_ERROR_SUCCESS;
}

static const vscp_link_ops_t s_link_ops = {
  .write_client        = cb_write_client,
  .disconnect          = cb_disconnect,
  .quit                = cb_quit,
  .welcome             = cb_welcome,
  .check_user          = cb_check_user,
  .check_password      = cb_check_password,
  .check_authenticated = cb_check_authenticated,
  .check_privilege     = cb_check_privilege,
  .send                = cb_send,
  .chkdata             = cb_chkdata,
  .clrall              = cb_clrall,
  .retr                = cb_retr,
  .rcvloop             = cb_rcvloop,
  .enable_rcvloop      = cb_enable_rcvloop,
  .get_rcvloop_status  = cb_get_rcvloop_status,
  .get_guid            = cb_get_guid,
  .set_guid            = cb_set_guid,
  .get_version         = cb_get_version,
  .get_channel_id      = cb_get_channel_id,
  .wcyd                = cb_wcyd,
  .statistics          = cb_statistics,
  .info                = cb_info,
  .set_filter          = cb_set_filter,
  .set_mask            = cb_set_mask,
  .get_interface_count = cb_get_interface_count,
  .get_interface       = cb_get_interface,
  .close_interface     = cb_close_interface,
  .shutdown            = cb_shutdown,
  .restart             = cb_restart,
};

// ****************************************************************************
//                                  Clients
// ****************************************************************************

static void
client_free(client_t *c)
{
  printf("Link client %d: disconnected\n", c->link.id);
  c->pcb = NULL;
  vscp_fifo_deinit(&c->link.fifoEventsOut); // Deletes the queued events
}

static void
client_close(client_t *c)
{
  if (NULL != c->pcb) {
    tcp_arg(c->pcb, NULL);
    tcp_recv(c->pcb, NULL);
    tcp_err(c->pcb, NULL);
    if (ERR_OK != tcp_close(c->pcb)) {
      tcp_abort(c->pcb);
    }
    client_free(c);
  }
}

// lwIP: the connection is gone (reset); the pcb is already freed
static void
client_err(void *arg, err_t err)
{
  client_t *c = (client_t *) arg;
  if ((NULL != c) && (NULL != c->pcb)) {
    client_free(c);
  }
}

// lwIP: received text (or the client closed the connection: p == NULL)
static err_t
client_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
  client_t *c = (client_t *) arg;
  if (NULL == p) {
    client_close(c);
    return ERR_OK;
  }

  // Handle every complete line
  for (struct pbuf *q = p; q; q = q->next) {
    const char *data = (const char *) q->payload;
    for (u16_t i = 0; i < q->len; i++) {
      if (c->filled >= sizeof(c->buf) - 1) {
        c->filled = 0; // Too long line
        cb_write_client(&c->link, VSCP_LINK_MSG_BUFFER_FULL);
      }
      c->buf[c->filled++] = data[i];
      if ('\n' == data[i]) {
        c->buf[c->filled - 1] = '\0';
        c->filled             = 0;
        if (!c->close) {
          vscp_link_parser(&c->link, c->buf);
        }
      }
    }
  }
  tcp_recved(pcb, p->tot_len);
  pbuf_free(p);

  if (c->output && (NULL != c->pcb)) {
    c->output = 0;
    tcp_output(c->pcb);
  }
  return ERR_OK;
}

// lwIP: a new connection
static err_t
server_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
  client_t *c = NULL;
  if ((ERR_OK != err) || (NULL == pcb)) {
    return ERR_VAL;
  }
  for (int i = 0; i < BLINKY_LINK_MAX_CLIENTS; i++) {
    if (NULL == s_clients[i].pcb) {
      c = &s_clients[i];
      break;
    }
  }
  if (NULL == c) {
    tcp_write(pcb, LINK_MSG_MAX_CLIENTS, strlen(LINK_MSG_MAX_CLIENTS), TCP_WRITE_FLAG_COPY);
    tcp_close(pcb);
    return ERR_OK;
  }

  printf("Link client %d: connected from %s\n", s_next_id, ipaddr_ntoa(&pcb->remote_ip));
  memset(&c->link, 0, sizeof(c->link));
  c->pcb            = pcb;
  c->close          = 0;
  c->output         = 0;
  c->filled         = 0;
  c->link.ops       = &s_link_ops;
  c->link.id        = s_next_id++;
  c->link.sock      = c->link.id;
  c->link.user_data = c;
  memcpy(c->link.guid, vscp_core_guid(), 16);
  vscp_fifo_init(&c->link.fifoEventsOut, LINK_FIFO_SIZE);

  ip_set_option(pcb, SOF_KEEPALIVE);
  tcp_nagle_disable(pcb);
  tcp_arg(pcb, c);
  tcp_recv(pcb, client_recv);
  tcp_err(pcb, client_err);

  vscp_link_connect(&c->link); // Welcome
  tcp_output(pcb);
  return ERR_OK;
}

// ****************************************************************************
//                                Public API
// ****************************************************************************

void
link_server_send_event(const vscp_event_t *pev)
{
  for (int i = 0; i < BLINKY_LINK_MAX_CLIENTS; i++) {
    client_t *c = &s_clients[i];
    if ((NULL == c->pcb) || !c->link.bValidated) {
      continue;
    }
    if (vscp_fwhlp_doLevel2Filter(pev, &c->link.filter)) {
      vscp_event_t *pnew = vscp_fwhlp_mkEventCopy(pev);
      if ((NULL == pnew) || !vscp_fifo_write(&c->link.fifoEventsOut, pnew)) {
        c->link.statistics.cntOverruns++;
        vscp_fwhlp_deleteEvent(&pnew);
      }
    }
  }
}

void
link_server_start(void)
{
  s_listen = tcp_new();
  if ((NULL == s_listen) || (ERR_OK != tcp_bind(s_listen, IP_ADDR_ANY, BLINKY_LINK_PORT))) {
    printf("Link: could not listen on port %d\n", BLINKY_LINK_PORT);
    return;
  }
  s_listen = tcp_listen_with_backlog(s_listen, 2);
  tcp_accept(s_listen, server_accept);
  printf("VSCP link protocol server on port %d (%d clients)\n", BLINKY_LINK_PORT, BLINKY_LINK_MAX_CLIENTS);
}

void
link_server_work(void)
{
  for (int i = 0; i < BLINKY_LINK_MAX_CLIENTS; i++) {
    client_t *c = &s_clients[i];
    if (NULL == c->pcb) {
      continue;
    }
    // Events to a client in a receive loop, when there is room to send them
    if (!c->close && (tcp_sndbuf(c->pcb) > LINK_ROOM_FOR_EVENT)) {
      vscp_link_idle_worker(&c->link);
    }
    if (c->output && (NULL != c->pcb)) {
      c->output = 0;
      tcp_output(c->pcb);
    }
    if (c->close && (NULL != c->pcb)) {
      client_close(c);
    }
  }
}

#else // BLINKY_ENABLE_LINK

void
link_server_send_event(const vscp_event_t *pev)
{
}

void
link_server_start(void)
{
}

void
link_server_work(void)
{
}

#endif
