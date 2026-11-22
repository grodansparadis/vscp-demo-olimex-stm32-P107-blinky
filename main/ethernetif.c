/*
  ethernetif.c - lwIP Ethernet driver for the STM32F107 MAC and the LAN8710A/KS8721 PHY (RMII)

  Based on the ethernetif.c of the STM32CubeF1 LwIP examples
  (Copyright (c) 2016 STMicroelectronics, BSD-3-Clause), changed for:

  - RMII and the PHY of the Olimex STM32-P107 (LAN8710A on revision E, Micrel
    KS8721 on older revisions); the PHY and its address are found at run time
  - no waiting for an Ethernet link at start: the MAC is set up at once and
    the link is followed from the main loop (ethernetif_link_work), with
    auto-negotiation when the link comes up
  - all multicast frames are accepted (VSCP multicast group)

  This file is part of the VSCP (https://www.vscp.org)
*/

#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"

#include "lwip/etharp.h"
#include "lwip/igmp.h"
#include "lwip/netif.h"
#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

#include "ethernetif.h"
#include "vscp-projdefs.h"

#define IFNAME0 's'
#define IFNAME1 't'

#define LINK_CHECK_MS    500
#define AUTONEG_TIMEOUT  5000
#define PHY_ID1_REG      0x02 // PHY identifier registers
#define PHY_ID2_REG      0x03

ETH_HandleTypeDef heth;

__ALIGN_BEGIN ETH_DMADescTypeDef DMARxDscrTab[ETH_RXBUFNB] __ALIGN_END;
__ALIGN_BEGIN ETH_DMADescTypeDef DMATxDscrTab[ETH_TXBUFNB] __ALIGN_END;
__ALIGN_BEGIN uint8_t Rx_Buff[ETH_RXBUFNB][ETH_RX_BUF_SIZE] __ALIGN_END;
__ALIGN_BEGIN uint8_t Tx_Buff[ETH_TXBUFNB][ETH_TX_BUF_SIZE] __ALIGN_END;

static uint8_t s_mac[6];

typedef enum { LINK_DOWN, LINK_AUTONEG, LINK_UP } link_state_t;
static link_state_t s_link = LINK_DOWN;
static uint32_t s_link_time;

/*
  RMII pins (not remapped):
    PA1 REF_CLK, PA2 MDIO, PA7 CRS_DV, PC1 MDC, PC4 RXD0, PC5 RXD1,
    PB11 TX_EN, PB12 TXD0, PB13 TXD1
  The 50 MHz reference clock comes from MCO (PA8), set up in board.c.
*/
void
HAL_ETH_MspInit(ETH_HandleTypeDef *h)
{
  GPIO_InitTypeDef gpio = { 0 };

  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  // Outputs: MDIO PA2, MDC PC1, TX_EN PB11, TXD0 PB12, TXD1 PB13
  gpio.Mode  = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Pin   = GPIO_PIN_2;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_1;
  HAL_GPIO_Init(GPIOC, &gpio);
  gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
  HAL_GPIO_Init(GPIOB, &gpio);

  // Inputs: REF_CLK PA1, CRS_DV PA7, RXD0 PC4, RXD1 PC5
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  gpio.Pin  = GPIO_PIN_1 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5;
  HAL_GPIO_Init(GPIOC, &gpio);

  __HAL_RCC_ETH_CLK_ENABLE();
}

/*
  Find the PHY on the MDIO bus (the address depends on its strap pins).

  The STM32-P107 has a LAN8710A (revision E) or a Micrel KS8721 (older
  revisions); BLINKY_PHY in vscp-projdefs.h selects one of them or either.
  Both use the standard registers and report speed and duplex in register
  31 bits 4:2 the same way (PHY_SR in stm32f1xx_hal_conf.h).
*/
#define PHY_IS_LAN8710A(id1, id2) ((0x0007 == (id1)) && (0xc0f0 == ((id2) & 0xfff0)))
#define PHY_IS_KS8721(id1, id2)   ((0x0022 == (id1)) && (0x1610 == ((id2) & 0xfff0)))

static bool
phy_find(void)
{
  for (uint32_t addr = 0; addr < 32; addr++) {
    uint32_t id1 = 0xffff, id2 = 0xffff;
    heth.Init.PhyAddress = addr;
    if ((HAL_OK != HAL_ETH_ReadPHYRegister(&heth, PHY_ID1_REG, &id1)) || (0xffff == id1) || (0 == id1)) {
      continue;
    }
    HAL_ETH_ReadPHYRegister(&heth, PHY_ID2_REG, &id2);
    bool lan8710a = PHY_IS_LAN8710A(id1, id2);
    bool ks8721   = PHY_IS_KS8721(id1, id2);
#if BLINKY_PHY == BLINKY_PHY_LAN8710A
    bool wanted = lan8710a;
#elif BLINKY_PHY == BLINKY_PHY_KS8721
    bool wanted = ks8721;
#else
    bool wanted = lan8710a || ks8721;
#endif
    const char *name = lan8710a ? "LAN8710A" : (ks8721 ? "KS8721" : "unknown");
    if (!wanted) {
      printf("Ethernet: %s PHY (id %04lx:%04lx) at address %lu is not the one selected (BLINKY_PHY)\n",
             name, id1, id2, addr);
      continue;
    }
    printf("Ethernet: %s PHY (id %04lx:%04lx) at address %lu\n", name, id1, id2, addr);
    return true;
  }
  heth.Init.PhyAddress = LAN8710A_PHY_ADDRESS;
  printf("Ethernet: no PHY found\n");
  return false;
}

static void
low_level_init(struct netif *netif)
{
  heth.Instance             = ETH;
  heth.Init.MACAddr         = s_mac;
  heth.Init.AutoNegotiation = ETH_AUTONEGOTIATION_DISABLE; // Done when the link is up
  heth.Init.Speed           = ETH_SPEED_100M;
  heth.Init.DuplexMode      = ETH_MODE_FULLDUPLEX;
  heth.Init.MediaInterface  = ETH_MEDIA_INTERFACE_RMII;
  heth.Init.RxMode          = ETH_RXPOLLING_MODE;
  heth.Init.ChecksumMode    = ETH_CHECKSUM_BY_SOFTWARE;
  heth.Init.PhyAddress      = LAN8710A_PHY_ADDRESS;

  if (HAL_OK != HAL_ETH_Init(&heth)) {
    printf("Ethernet: MAC init failed\n");
  }
  phy_find();

  // Accept all multicast frames (the VSCP multicast group)
  heth.Instance->MACFFR |= ETH_MACFFR_PAM;

  HAL_ETH_DMATxDescListInit(&heth, DMATxDscrTab, &Tx_Buff[0][0], ETH_TXBUFNB);
  HAL_ETH_DMARxDescListInit(&heth, DMARxDscrTab, &Rx_Buff[0][0], ETH_RXBUFNB);

  netif->hwaddr_len = ETH_HWADDR_LEN;
  memcpy(netif->hwaddr, s_mac, 6);
  netif->mtu   = 1500;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;
}

static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
  err_t errval;
  struct pbuf *q;
  uint8_t *buffer                   = (uint8_t *) (heth.TxDesc->Buffer1Addr);
  __IO ETH_DMADescTypeDef *DmaTxDesc = heth.TxDesc;
  uint32_t framelength = 0, bufferoffset = 0, byteslefttocopy, payloadoffset;

  if (LINK_UP != s_link) {
    return ERR_IF;
  }

  for (q = p; q != NULL; q = q->next) {
    if ((DmaTxDesc->Status & ETH_DMATXDESC_OWN) != (uint32_t) RESET) {
      errval = ERR_USE; // The DMA still owns the buffer
      goto error;
    }
    byteslefttocopy = q->len;
    payloadoffset   = 0;
    while ((byteslefttocopy + bufferoffset) > ETH_TX_BUF_SIZE) {
      memcpy(buffer + bufferoffset, (uint8_t *) q->payload + payloadoffset, ETH_TX_BUF_SIZE - bufferoffset);
      DmaTxDesc = (ETH_DMADescTypeDef *) (DmaTxDesc->Buffer2NextDescAddr);
      if ((DmaTxDesc->Status & ETH_DMATXDESC_OWN) != (uint32_t) RESET) {
        errval = ERR_USE;
        goto error;
      }
      buffer = (uint8_t *) (DmaTxDesc->Buffer1Addr);
      byteslefttocopy -= ETH_TX_BUF_SIZE - bufferoffset;
      payloadoffset += ETH_TX_BUF_SIZE - bufferoffset;
      framelength += ETH_TX_BUF_SIZE - bufferoffset;
      bufferoffset = 0;
    }
    memcpy(buffer + bufferoffset, (uint8_t *) q->payload + payloadoffset, byteslefttocopy);
    bufferoffset += byteslefttocopy;
    framelength += byteslefttocopy;
  }

  HAL_ETH_TransmitFrame(&heth, framelength);
  errval = ERR_OK;

error:
  // Transmit underflow: clear it and resume transmission
  if ((heth.Instance->DMASR & ETH_DMASR_TUS) != (uint32_t) RESET) {
    heth.Instance->DMASR   = ETH_DMASR_TUS;
    heth.Instance->DMATPDR = 0;
  }
  return errval;
}

static struct pbuf *
low_level_input(struct netif *netif)
{
  struct pbuf *p = NULL, *q;
  uint8_t *buffer;
  __IO ETH_DMADescTypeDef *dmarxdesc;
  uint32_t bufferoffset = 0, payloadoffset, byteslefttocopy;

  if (HAL_OK != HAL_ETH_GetReceivedFrame(&heth)) {
    return NULL;
  }
  uint16_t len = heth.RxFrameInfos.length;
  buffer       = (uint8_t *) heth.RxFrameInfos.buffer;
  if (len > 0) {
    p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
  }

  if (p != NULL) {
    dmarxdesc = heth.RxFrameInfos.FSRxDesc;
    for (q = p; q != NULL; q = q->next) {
      byteslefttocopy = q->len;
      payloadoffset   = 0;
      while ((byteslefttocopy + bufferoffset) > ETH_RX_BUF_SIZE) {
        memcpy((uint8_t *) q->payload + payloadoffset, buffer + bufferoffset, ETH_RX_BUF_SIZE - bufferoffset);
        dmarxdesc = (ETH_DMADescTypeDef *) (dmarxdesc->Buffer2NextDescAddr);
        buffer    = (uint8_t *) (dmarxdesc->Buffer1Addr);
        byteslefttocopy -= ETH_RX_BUF_SIZE - bufferoffset;
        payloadoffset += ETH_RX_BUF_SIZE - bufferoffset;
        bufferoffset = 0;
      }
      memcpy((uint8_t *) q->payload + payloadoffset, buffer + bufferoffset, byteslefttocopy);
      bufferoffset += byteslefttocopy;
    }
  }

  // Give the buffers back to the DMA (also when no pbuf was free)
  dmarxdesc = heth.RxFrameInfos.FSRxDesc;
  for (uint32_t i = 0; i < heth.RxFrameInfos.SegCount; i++) {
    dmarxdesc->Status |= ETH_DMARXDESC_OWN;
    dmarxdesc = (ETH_DMADescTypeDef *) (dmarxdesc->Buffer2NextDescAddr);
  }
  heth.RxFrameInfos.SegCount = 0;

  // Receive buffer unavailable: clear it and resume reception
  if ((heth.Instance->DMASR & ETH_DMASR_RBUS) != (uint32_t) RESET) {
    heth.Instance->DMASR   = ETH_DMASR_RBUS;
    heth.Instance->DMARPDR = 0;
  }
  return p;
}

void
ethernetif_input(struct netif *netif)
{
  struct pbuf *p;
  // All received frames
  while ((LINK_UP == s_link) && (NULL != (p = low_level_input(netif)))) {
    if (ERR_OK != netif->input(p, netif)) {
      pbuf_free(p);
    }
  }
}

// IGMP: all multicast frames are accepted (MACFFR PAM), nothing to set
static err_t
igmp_mac_filter(struct netif *netif, const ip4_addr_t *group, enum netif_mac_filter_action action)
{
  return ERR_OK;
}

err_t
ethernetif_init(struct netif *netif)
{
  netif->hostname   = "vscp-blinky";
  netif->name[0]    = IFNAME0;
  netif->name[1]    = IFNAME1;
  netif->output     = etharp_output;
  netif->linkoutput = low_level_output;
  netif_set_igmp_mac_filter(netif, igmp_mac_filter);
  low_level_init(netif);
  return ERR_OK;
}

void
ethernetif_set_mac(const uint8_t *mac)
{
  memcpy(s_mac, mac, 6);
}

/*
  Follow the Ethernet link: auto-negotiation when it comes up, then the MAC
  is set to the negotiated speed and duplex and started.
*/
void
ethernetif_link_work(struct netif *netif)
{
  uint32_t bsr = 0, sr = 0;

  if ((LINK_AUTONEG != s_link) && ((HAL_GetTick() - s_link_time) < LINK_CHECK_MS)) {
    return;
  }

  switch (s_link) {

    case LINK_DOWN:
      s_link_time = HAL_GetTick();
      if ((HAL_OK == HAL_ETH_ReadPHYRegister(&heth, PHY_BSR, &bsr)) && (bsr & PHY_LINKED_STATUS)) {
        HAL_ETH_WritePHYRegister(&heth, PHY_BCR, PHY_AUTONEGOTIATION | PHY_RESTART_AUTONEGOTIATION);
        s_link = LINK_AUTONEG;
      }
      break;

    case LINK_AUTONEG:
      if ((HAL_OK != HAL_ETH_ReadPHYRegister(&heth, PHY_BSR, &bsr)) || !(bsr & PHY_AUTONEGO_COMPLETE)) {
        if ((HAL_GetTick() - s_link_time) > AUTONEG_TIMEOUT) {
          s_link = LINK_DOWN; // Try again
        }
        break;
      }
      HAL_ETH_ReadPHYRegister(&heth, PHY_SR, &sr);
      heth.Init.DuplexMode = (sr & PHY_DUPLEX_STATUS) ? ETH_MODE_FULLDUPLEX : ETH_MODE_HALFDUPLEX;
      heth.Init.Speed      = (sr & PHY_SPEED_STATUS) ? ETH_SPEED_10M : ETH_SPEED_100M;
      HAL_ETH_ConfigMAC(&heth, NULL);
      HAL_ETH_Start(&heth);
      s_link      = LINK_UP;
      s_link_time = HAL_GetTick();
      printf("Ethernet link up, %s Mbit/s %s duplex\n",
             (ETH_SPEED_100M == heth.Init.Speed) ? "100" : "10",
             (ETH_MODE_FULLDUPLEX == heth.Init.DuplexMode) ? "full" : "half");
      netif_set_link_up(netif);
      break;

    case LINK_UP:
      s_link_time = HAL_GetTick();
      if ((HAL_OK == HAL_ETH_ReadPHYRegister(&heth, PHY_BSR, &bsr)) && !(bsr & PHY_LINKED_STATUS)) {
        // The link status bit is latched low: read it again
        HAL_ETH_ReadPHYRegister(&heth, PHY_BSR, &bsr);
        if (!(bsr & PHY_LINKED_STATUS)) {
          printf("Ethernet link down\n");
          HAL_ETH_Stop(&heth);
          s_link = LINK_DOWN;
          netif_set_link_down(netif);
        }
      }
      break;
  }
}

bool
ethernetif_link_up(void)
{
  return LINK_UP == s_link;
}
