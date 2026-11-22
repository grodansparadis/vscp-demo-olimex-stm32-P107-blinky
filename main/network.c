/*
  network.c - lwIP on the STM32F107 Ethernet: DHCP or a static address, SNTP

  lwIP runs without an operating system (NO_SYS): received frames, link
  changes and the lwIP timers are handled from the main loop.

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"

#include "lwip/apps/sntp.h"
#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#include "vscp-compiler.h"
#include "vscp-projdefs.h"

#include "blinky.h"
#include "board.h"
#include "ethernetif.h"

static struct netif s_netif;
static bool s_have_ip;

uint32_t
millis(void)
{
  return HAL_GetTick();
}

// lwIP time base
u32_t
sys_now(void)
{
  return HAL_GetTick();
}

// SNTP (lwipopts.h SNTP_SET_SYSTEM_TIME_US)
void
blinky_sntp_set_time(unsigned long sec, unsigned long us)
{
  static bool first = true;
  vscp_core_set_time((uint64_t) sec * 1000000000ull + (uint64_t) us * 1000ull);
  if (first) {
    printf("SNTP: time set (%lu)\n", sec);
    first = false;
  }
}

// Address changes (DHCP or static)
static void
status_callback(struct netif *netif)
{
  if (!ip4_addr_isany_val(*netif_ip4_addr(netif))) {
    uint8_t ip[4];
    memcpy(ip, &netif_ip4_addr(netif)->addr, 4); // Network byte order = MSB first
    printf("IP address %s\n", ip4addr_ntoa(netif_ip4_addr(netif)));
    vscp_core_set_ip(ip);
    s_have_ip = true;
  }
  else {
    uint8_t none[4] = { 0 };
    vscp_core_set_ip(none);
    s_have_ip = false;
  }
}

static void
link_callback(struct netif *netif)
{
  if (netif_is_link_up(netif)) {
    netif_set_up(netif); // Also needed for DHCP
    if (!strlen(BLINKY_IP)) {
      printf("DHCP: asking for an address\n");
      dhcp_start(netif);
    }
  }
  else {
    if (!strlen(BLINKY_IP)) {
      dhcp_stop(netif);
    }
    netif_set_down(netif);
  }
}

void
network_init(const uint8_t *mac)
{
  ip4_addr_t ip, mask, gw;

  lwip_init();

  ip4_addr_set_zero(&ip);
  ip4_addr_set_zero(&mask);
  ip4_addr_set_zero(&gw);
  if (strlen(BLINKY_IP)) {
    ip4addr_aton(BLINKY_IP, &ip);
    ip4addr_aton(BLINKY_NETMASK, &mask);
    ip4addr_aton(BLINKY_GATEWAY, &gw);
  }

  ethernetif_set_mac(mac);
  netif_add(&s_netif, &ip, &mask, &gw, NULL, ethernetif_init, ethernet_input);
  netif_set_default(&s_netif);
  netif_set_status_callback(&s_netif, status_callback);
  netif_set_link_callback(&s_netif, link_callback);

  if (strlen(BLINKY_SNTP_SERVER)) {
    ip4_addr_t server;
    ip4addr_aton(BLINKY_SNTP_SERVER, &server);
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setserver(0, &server);
    sntp_init();
  }
}

bool
network_work(void)
{
  static uint32_t last_led;
  static bool led;

  ethernetif_link_work(&s_netif);
  ethernetif_input(&s_netif);
  sys_check_timeouts();

  // STAT2: blinks without an address, on with one
  if (s_have_ip) {
    if (!led) {
      led = true;
      board_led_stat2(true);
    }
  }
  else if ((millis() - last_led) >= 250) {
    last_led = millis();
    led      = !led;
    board_led_stat2(led);
  }
  return s_have_ip;
}
