/*
  lwipopts.h - lwIP settings for the Olimex STM32-P107 VSCP Blinky

  Bare metal (NO_SYS): lwIP is called from the main loop only, never from
  an interrupt. Sized for the 64 KB RAM of the STM32F107VC.
*/

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#include "vscp-projdefs.h"

#define NO_SYS                 1
#define SYS_LIGHTWEIGHT_PROT   0
#define LWIP_NETCONN           0
#define LWIP_SOCKET            0
#define LWIP_RAND()            ((u32_t) rand())

// Memory
#define MEM_ALIGNMENT          4
#define MEM_SIZE               (12 * 1024)
#define MEMP_NUM_PBUF          16
#define MEMP_NUM_UDP_PCB       6  // DHCP, SNTP, UDP, multicast, spare
#define MEMP_NUM_TCP_PCB       (BLINKY_LINK_MAX_CLIENTS + 3) // Link clients, MQTT, closing
#define MEMP_NUM_TCP_PCB_LISTEN 2
#define MEMP_NUM_TCP_SEG       32
#define MEMP_NUM_SYS_TIMEOUT   (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 4) // + SNTP, MQTT
#define PBUF_POOL_SIZE         6
#define PBUF_POOL_BUFSIZE      1536

// Protocols
#define LWIP_IPV4              1
#define LWIP_IPV6              0
#define LWIP_ARP               1
#define LWIP_ETHERNET          1
#define LWIP_ICMP              1
#define LWIP_DHCP              1
#define LWIP_DNS               0
#define LWIP_UDP               1
#define LWIP_TCP               1
#define LWIP_IGMP              1 // Multicast
#define LWIP_BROADCAST_PBUF_FLAGS 1
#define IP_SOF_BROADCAST       1
#define IP_SOF_BROADCAST_RECV  1
#define LWIP_MULTICAST_TX_OPTIONS 1

// TCP
#define TCP_MSS                1460
#define TCP_SND_BUF            (2 * TCP_MSS)
#define TCP_SND_QUEUELEN       (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND                (2 * TCP_MSS)
#define LWIP_TCP_KEEPALIVE     1

// Netif
#define LWIP_NETIF_HOSTNAME    1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1

// Checksums in software (simple and the same for all frames)
#define CHECKSUM_GEN_IP        1
#define CHECKSUM_GEN_UDP       1
#define CHECKSUM_GEN_TCP       1
#define CHECKSUM_GEN_ICMP      1
#define CHECKSUM_CHECK_IP      1
#define CHECKSUM_CHECK_UDP     1
#define CHECKSUM_CHECK_TCP     1

// SNTP (time for the event timestamps)
#define SNTP_SERVER_DNS        0
#define SNTP_UPDATE_DELAY      3600000 // Once an hour
void blinky_sntp_set_time(unsigned long sec, unsigned long us);
#define SNTP_SET_SYSTEM_TIME_US(sec, us) blinky_sntp_set_time((sec), (us))

// MQTT client
#define MQTT_OUTPUT_RINGBUF_SIZE    1024
#define MQTT_VAR_HEADER_BUFFER_LEN  256
#define MQTT_REQ_MAX_IN_FLIGHT      4

// Statistics and debug off
#define LWIP_STATS             0
#define LWIP_DEBUG             0

#endif
