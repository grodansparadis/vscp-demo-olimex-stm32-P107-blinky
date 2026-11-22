/*
  vscp-projdefs.h - Project definitions for the Olimex STM32-P107 VSCP Blinky

  The transports and the network settings are selected here. Every BLINKY_*
  setting can also be set from CMake (add_compile_definitions() in
  blinky_local.cmake, or -D on the command line) without editing this file.

  This file is also read by the vscp-firmware code (firmware helper,
  VSCP link protocol and Level II firmware).

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#ifndef _VSCP_PROJDEFS_H_
#define _VSCP_PROJDEFS_H_

// ---------------------------------------------------------------------------
//                                Node type
// ---------------------------------------------------------------------------

/*
  0 = Ethernet node (Level II) with the IP transports below and, with
      BLINKY_ENABLE_CAN, a CAN4VSCP gateway.
  1 = CAN4VSCP node only (Level I): no Ethernet, no IP transports. The node
      finds a free nickname on the bus when it starts (nickname discovery)
      and keeps it in the EEPROM.
*/
#ifndef BLINKY_CAN_NODE
#define BLINKY_CAN_NODE 0
#endif

// ---------------------------------------------------------------------------
//                          Transports (1 = on, 0 = off)
// ---------------------------------------------------------------------------

#ifndef BLINKY_ENABLE_LINK
#define BLINKY_ENABLE_LINK 1 // VSCP link protocol (TCP/IP) server
#endif
#ifndef BLINKY_ENABLE_UDP
#define BLINKY_ENABLE_UDP 1 // VSCP frames over UDP
#endif
#ifndef BLINKY_ENABLE_MULTICAST
#define BLINKY_ENABLE_MULTICAST 1 // VSCP frames on the VSCP multicast group
#endif
#ifndef BLINKY_ENABLE_MQTT
#define BLINKY_ENABLE_MQTT 0 // VSCP JSON events over MQTT (set the broker below)
#endif
#ifndef BLINKY_ENABLE_CAN
#define BLINKY_ENABLE_CAN 1 // CAN4VSCP gateway (CAN1 on PD0/PD1)
#endif

// A CAN4VSCP node has no IP transports
#if BLINKY_CAN_NODE
#undef BLINKY_ENABLE_LINK
#undef BLINKY_ENABLE_UDP
#undef BLINKY_ENABLE_MULTICAST
#undef BLINKY_ENABLE_MQTT
#undef BLINKY_ENABLE_CAN
#define BLINKY_ENABLE_LINK      0
#define BLINKY_ENABLE_UDP       0
#define BLINKY_ENABLE_MULTICAST 0
#define BLINKY_ENABLE_MQTT      0
#define BLINKY_ENABLE_CAN       1
#endif

// ---------------------------------------------------------------------------
//                                   CAN
// ---------------------------------------------------------------------------

#ifndef BLINKY_CAN_BITRATE
#define BLINKY_CAN_BITRATE 125000 // CAN4VSCP standard. 125000, 250000, 500000 or 1000000
#endif
#ifndef BLINKY_CAN_LOOPBACK
#define BLINKY_CAN_LOOPBACK 0 // 1 = test: the CAN controller receives its own frames, nothing on the bus
#endif

// ---------------------------------------------------------------------------
//                              Ethernet PHY
// ---------------------------------------------------------------------------

#define BLINKY_PHY_AUTO     0 // Find a LAN8710A or a KS8721
#define BLINKY_PHY_LAN8710A 1 // SMSC/Microchip LAN8710A (STM32-P107 revision E)
#define BLINKY_PHY_KS8721   2 // Micrel KS8721 (older STM32-P107 revisions)

#ifndef BLINKY_PHY
#define BLINKY_PHY BLINKY_PHY_AUTO
#endif

// ---------------------------------------------------------------------------
//                                 Network
// ---------------------------------------------------------------------------

#ifndef BLINKY_IP
#define BLINKY_IP "" // Static IPv4 address, "" = DHCP
#endif
#ifndef BLINKY_NETMASK
#define BLINKY_NETMASK "255.255.255.0"
#endif
#ifndef BLINKY_GATEWAY
#define BLINKY_GATEWAY ""
#endif
#ifndef BLINKY_SNTP_SERVER
#define BLINKY_SNTP_SERVER "216.239.35.0" // time.google.com, "" = no SNTP
#endif

// VSCP link protocol
#ifndef BLINKY_LINK_PORT
#define BLINKY_LINK_PORT 9598
#endif
#ifndef BLINKY_LINK_MAX_CLIENTS
#define BLINKY_LINK_MAX_CLIENTS 3
#endif
#ifndef BLINKY_LINK_USER
#define BLINKY_LINK_USER "vscp"
#endif
#ifndef BLINKY_LINK_PASSWORD
#define BLINKY_LINK_PASSWORD "secret"
#endif

// UDP
#ifndef BLINKY_UDP_PORT
#define BLINKY_UDP_PORT 33333
#endif
#ifndef BLINKY_UDP_REMOTE_HOST
#define BLINKY_UDP_REMOTE_HOST "255.255.255.255" // Events are sent here
#endif

// Multicast
#ifndef BLINKY_MULTICAST_GROUP
#define BLINKY_MULTICAST_GROUP "224.0.23.158"
#endif
#ifndef BLINKY_MULTICAST_PORT
#define BLINKY_MULTICAST_PORT 44444
#endif
#ifndef BLINKY_MULTICAST_TTL
#define BLINKY_MULTICAST_TTL 1
#endif

// MQTT
#ifndef BLINKY_MQTT_BROKER
#define BLINKY_MQTT_BROKER "192.168.1.9" // IPv4 address
#endif
#ifndef BLINKY_MQTT_PORT
#define BLINKY_MQTT_PORT 1883
#endif
#ifndef BLINKY_MQTT_USER
#define BLINKY_MQTT_USER ""
#endif
#ifndef BLINKY_MQTT_PASSWORD
#define BLINKY_MQTT_PASSWORD ""
#endif
#ifndef BLINKY_MQTT_PUBLISH_TOPIC
#define BLINKY_MQTT_PUBLISH_TOPIC "vscp/{guid}/{class}/{type}"
#endif
#ifndef BLINKY_MQTT_SUBSCRIBE_TOPIC
#define BLINKY_MQTT_SUBSCRIBE_TOPIC "vscp/{guid}/in"
#endif

// ---------------------------------------------------------------------------
//                              vscp-firmware
// ---------------------------------------------------------------------------

#include <stdio.h>

// Log output from the vscp-firmware code (off)
#define LOGSTR(...) // printf(__VA_ARGS__)

#define VSCP_FWHLP_BINARY_FRAME_SUPPORT // UDP/multicast frames
#if BLINKY_ENABLE_MQTT
#define VSCP_FWHLP_JSON_SUPPORT // MQTT (VSCP JSON events, cJSON)
#endif

// Level II firmware
#define THIS_FIRMWARE_DEVICE_NAME     "VSCP Olimex STM32-P107 blinky"
#define THIS_FIRMWARE_MAJOR_VERSION   (0u)
#define THIS_FIRMWARE_MINOR_VERSION   (1u)
#define THIS_FIRMWARE_RELEASE_VERSION (0u)
#define THIS_FIRMWARE_BUILD_VERSION   (0u)

#define THIS_FIRMWARE_ENABLE_ERROR_REPORTING

// VSCP link protocol
#define THIS_FIRMWARE_TCPIP_LINK_MAX_BUFFER         512u
#define THIS_FIRMWARE_TCPIP_LINK_ENABLE_RCVLOOP_CMD 1

#endif // _VSCP_PROJDEFS_H_
