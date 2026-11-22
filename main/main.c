/*
  main.c - VSCP Blinky demo for the Olimex STM32-P107

  Starts the board (clocks, LEDs, button, log on the RS232 port), the
  24LC256 EEPROM, Ethernet with lwIP, the VSCP node (blinky.c) and the
  transports selected in vscp-projdefs.h: VSCP link protocol server
  (TCP/IP), UDP, multicast and MQTT. Everything runs in the main loop;
  there is no RTOS.

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"

#include "vscp-compiler.h"
#include "vscp-projdefs.h"

#include "blinky.h"
#include "board.h"
#include "eeprom.h"

int
main(void)
{
  HAL_Init();
  board_init();

  printf("\nVSCP Blinky demo for the Olimex STM32-P107 %d.%d.%d\n",
         THIS_FIRMWARE_MAJOR_VERSION,
         THIS_FIRMWARE_MINOR_VERSION,
         THIS_FIRMWARE_RELEASE_VERSION);
  printf("Transports:%s%s%s%s\n",
         BLINKY_ENABLE_LINK ? " link" : "",
         BLINKY_ENABLE_UDP ? " udp" : "",
         BLINKY_ENABLE_MULTICAST ? " multicast" : "",
         BLINKY_ENABLE_MQTT ? " mqtt" : "");

  /*
    MAC address: the STM32F107 has none of its own. A locally administered
    address is made from the unique id of the chip (02:xx:xx:xx:xx:xx).
    GUID: FF:FF:FF:FF:FF:FF:FF:FE:<MAC>:00:00
    https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_globally_unique_identifiers
  */
  uint8_t uid[12];
  board_unique_id(uid);
  uint8_t mac[6] = { 0x02, (uint8_t) (uid[0] ^ uid[5] ^ uid[10]), (uint8_t) (uid[1] ^ uid[6] ^ uid[11]),
                     (uint8_t) (uid[2] ^ uid[7]), (uint8_t) (uid[3] ^ uid[8]), (uint8_t) (uid[4] ^ uid[9]) };
  uint8_t guid[16];
  memset(guid, 0xff, 7);
  guid[7] = 0xfe;
  memcpy(guid + 8, mac, 6);
  guid[14] = 0;
  guid[15] = 0;
  printf("MAC %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  eeprom_init();
  network_init(mac);
  vscp_core_init(guid);

  bool started = false;
  for (;;) {
    if (network_work() && !started) {
      // The node has an IP address
      link_server_start();
      udp_transport_start();
      mqtt_transport_start();
      started = true;
    }
    if (started) {
      link_server_work();
      udp_transport_work();
      mqtt_transport_work();
    }
    vscp_core_work();
  }
}
