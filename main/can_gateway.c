/*
  can_gateway.c - CAN4VSCP gateway (CAN1 on PD0 RX / PD1 TX)

  Connects a CAN4VSCP bus (VSCP Level I events on CAN) with the IP
  transports:

  - CAN -> IP: every frame on the bus becomes a VSCP event that is sent
    over all IP transports (link protocol clients, UDP, multicast, MQTT) and
    given to the Blinky node. Its GUID is the interface GUID: the GUID of
    the node with the nickname of the CAN node in the last byte.
  - IP -> CAN: Level I events received from the IP transports are sent on
    the bus, also Level I events over Level II (class 512-1023, the GUID of
    the destination in data bytes 0-15), so a client can talk to the nodes
    on the bus (register read/write, ...).
  - The Level I events of the Blinky node are sent on the bus with its
    nickname: it is also a node on the CAN segment.

  CAN4VSCP: 29-bit identifier = priority (3 bits) | hard coded (1) |
  class (9) | type (8) | nickname of the sender (8), up to 8 data bytes.
  https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_over_can

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"

#include "vscp-compiler.h"
#include "vscp-projdefs.h"

#include <vscp-firmware-helper.h>
#include <vscp.h>

#include "blinky.h"

#if BLINKY_ENABLE_CAN

static CAN_HandleTypeDef s_can;
static bool s_started;
static uint32_t s_rx_frames, s_tx_frames, s_tx_dropped;

void
HAL_CAN_MspInit(CAN_HandleTypeDef *hcan)
{
  GPIO_InitTypeDef gpio = { 0 };
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_CAN1_CLK_ENABLE();
  __HAL_AFIO_REMAP_CAN1_3(); // CAN1 on PD0 (RX) / PD1 (TX)

  gpio.Pin  = GPIO_PIN_0;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOD, &gpio);
  gpio.Pin   = GPIO_PIN_1;
  gpio.Mode  = GPIO_MODE_AF_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOD, &gpio);
}

void
can_gateway_init(void)
{
  CAN_FilterTypeDef filter = { 0 };

  /*
    Bit timing from the 36 MHz APB1 clock: 16 time quanta per bit
    (1 + 13 + 2), sample point 87.5 % as CAN4VSCP recommends.
  */
  s_can.Instance                  = CAN1;
  s_can.Init.Prescaler            = 36000000 / (16 * BLINKY_CAN_BITRATE);
  s_can.Init.Mode                 = BLINKY_CAN_LOOPBACK ? CAN_MODE_SILENT_LOOPBACK : CAN_MODE_NORMAL;
  s_can.Init.SyncJumpWidth        = CAN_SJW_1TQ;
  s_can.Init.TimeSeg1             = CAN_BS1_13TQ;
  s_can.Init.TimeSeg2             = CAN_BS2_2TQ;
  s_can.Init.TimeTriggeredMode    = DISABLE;
  s_can.Init.AutoBusOff           = ENABLE; // Recover from bus off by itself
  s_can.Init.AutoWakeUp           = DISABLE;
  s_can.Init.AutoRetransmission   = ENABLE;
  s_can.Init.ReceiveFifoLocked    = DISABLE;
  s_can.Init.TransmitFifoPriority = ENABLE; // Send in the order queued
  if (HAL_OK != HAL_CAN_Init(&s_can)) {
    printf("CAN: init failed\n");
    return;
  }

  // Accept all frames (extended ones are used, standard ones are ignored)
  filter.FilterBank           = 0;
  filter.FilterMode           = CAN_FILTERMODE_IDMASK;
  filter.FilterScale          = CAN_FILTERSCALE_32BIT;
  filter.FilterFIFOAssignment = CAN_RX_FIFO0;
  filter.FilterActivation     = ENABLE;
  filter.SlaveStartFilterBank = 14;
  if ((HAL_OK != HAL_CAN_ConfigFilter(&s_can, &filter)) || (HAL_OK != HAL_CAN_Start(&s_can))) {
    printf("CAN: start failed\n");
    return;
  }
  s_started = true;
  printf("CAN4VSCP %s: %lu bit/s on PD0/PD1%s\n",
         BLINKY_CAN_NODE ? "node" : "gateway",
         (unsigned long) BLINKY_CAN_BITRATE,
         BLINKY_CAN_LOOPBACK ? " (loopback test mode)" : "");
}

// A frame from the bus: to the IP transports and the node
static void
receive(const CAN_RxHeaderTypeDef *hdr, const uint8_t *data)
{
  if ((CAN_ID_EXT != hdr->IDE) || (CAN_RTR_DATA != hdr->RTR)) {
    return; // Not CAN4VSCP
  }

  vscp_event_t *pev = vscp_fwhlp_newEvent();
  if (NULL == pev) {
    return;
  }
  uint32_t id     = hdr->ExtId;
  pev->head       = ((id >> 26) & 0x07) << 5; // Priority
  pev->vscp_class = (id >> 16) & 0x1ff;
  pev->vscp_type  = (id >> 8) & 0xff;
  memcpy(pev->GUID, vscp_core_guid(), 16);
  pev->GUID[14] = 0;
  pev->GUID[15] = id & 0xff; // Nickname of the sender
  pev->head |= VSCP_HEADER16_FRAME_VERSION_UNIX_NS;
  pev->year         = 0xffff;
  pev->month        = 0xff;
  pev->timestamp_ns = vscp_core_timestamp_ns();
  pev->sizeData     = hdr->DLC;
  if (pev->sizeData) {
    pev->pdata = VSCP_MALLOC(pev->sizeData);
    if (NULL == pev->pdata) {
      vscp_fwhlp_deleteEvent(&pev);
      return;
    }
    memcpy(pev->pdata, data, pev->sizeData);
  }
  s_rx_frames++;

  // To the IP side (they copy the event)
  link_server_send_event(pev);
  udp_transport_send_event(pev);
  mqtt_transport_send_event(pev);

#if BLINKY_CAN_LOOPBACK
  // Loopback test: the frame is our own. On a real bus a node does not get
  // its own frames, so it is not given to the node.
  vscp_fwhlp_deleteEvent(&pev);
#else
  // To the node (protocol events for its nickname, decision matrix); marked
  // so it is not sent back on the bus
  pev->obid = BLINKY_OBID_FROM_CAN;
  if (VSCP_ERROR_SUCCESS != vscp_core_post(pev)) {
    vscp_fwhlp_deleteEvent(&pev);
  }
#endif
}

void
can_gateway_work(void)
{
  CAN_RxHeaderTypeDef hdr;
  uint8_t data[8];
  if (!s_started) {
    return;
  }
  while (HAL_CAN_GetRxFifoFillLevel(&s_can, CAN_RX_FIFO0)) {
    if (HAL_OK == HAL_CAN_GetRxMessage(&s_can, CAN_RX_FIFO0, &hdr, data)) {
      receive(&hdr, data);
    }
  }
}

void
can_gateway_send_event(const vscp_event_t *pev)
{
  CAN_TxHeaderTypeDef hdr = { 0 };
  uint32_t mailbox;
  uint16_t vscp_class = pev->vscp_class;
  const uint8_t *data = pev->pdata;
  uint16_t size       = pev->sizeData;

  if (!s_started) {
    return;
  }

  if ((vscp_class >= 512) && (vscp_class < 1024)) {
    // Level I event over Level II: data bytes 0-15 are the GUID of the destination
    if (size < 16) {
      return;
    }
    vscp_class -= 512;
    data += 16;
    size -= 16;
  }
  else if (vscp_class >= 512) {
    return; // Level II event: not for the bus
  }
  if (size > 8) {
    return;
  }

  hdr.ExtId = ((uint32_t) ((pev->head >> 5) & 0x07) << 26) | ((uint32_t) vscp_class << 16) |
              ((uint32_t) pev->vscp_type << 8) | (vscp_core_nickname() & 0xff);
  hdr.IDE   = CAN_ID_EXT;
  hdr.RTR   = CAN_RTR_DATA;
  hdr.DLC   = size;
  if ((0 == HAL_CAN_GetTxMailboxesFreeLevel(&s_can)) ||
      (HAL_OK != HAL_CAN_AddTxMessage(&s_can, &hdr, (uint8_t *) data, &mailbox))) {
    s_tx_dropped++; // No free mailbox (no other node acknowledges, bus off, ...)
    return;
  }
  s_tx_frames++;
}

#else

void
can_gateway_init(void)
{
}

void
can_gateway_work(void)
{
}

void
can_gateway_send_event(const vscp_event_t *pev)
{
}

#endif
