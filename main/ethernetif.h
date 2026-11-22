/*
  ethernetif.h - lwIP Ethernet driver (STM32F107 MAC, LAN8710A PHY, RMII)
*/

#ifndef ETHERNETIF_H
#define ETHERNETIF_H

#include <stdbool.h>

#include "lwip/err.h"
#include "lwip/netif.h"

void
ethernetif_set_mac(const uint8_t *mac); // Before netif_add()
err_t
ethernetif_init(struct netif *netif);
void
ethernetif_input(struct netif *netif); // Main loop: received frames
void
ethernetif_link_work(struct netif *netif); // Main loop: link up/down
bool
ethernetif_link_up(void);

#endif
