# Changes in vscp-demo-olimex-stm32-P107-blinky

Every change that goes into a release is listed here, newest release first.
Add each change under **Unreleased** when it is made. When a release is
tagged, rename **Unreleased** to the version and date, and start a new
**Unreleased** section above it.

## Unreleased

- First version: the VSCP Blinky node (LED, button, millisecond counter, four row decision matrix) on the Olimex STM32-P107 (STM32F107VC), registers saved in a 24LC256 EEPROM
- Ethernet PHY LAN8710A (board revision E) or Micrel KS8721 (older revisions), selected with `BLINKY_PHY` in `vscp-projdefs.h` or found at start
- Transports selected in `vscp-projdefs.h`: VSCP link protocol server (TCP/IP, three clients), VSCP frames over UDP and on the VSCP multicast group, VSCP JSON events over MQTT; lwIP 2.1.3 without an RTOS
- DHCP or a static address, MAC address and GUID from the unique id of the chip, event time from SNTP
- The node works without an Ethernet link; the network starts when the link comes up
- CAN4VSCP gateway (CAN1 on PD0/PD1, 125 kbit/s by default): frames on the bus are sent over the IP transports, Level I events (also over Level II) from the IP transports and the node's own Level I events are sent on the bus; `BLINKY_CAN_LOOPBACK` test mode and a `canloop` test
- `tools/blinky_test.py` tests a node over each transport
