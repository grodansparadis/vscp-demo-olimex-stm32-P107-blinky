/*
  board.h - Olimex STM32-P107: clocks, LEDs, buttons, log port

  STM32F107VC, 25 MHz crystal, 72 MHz system clock.
  STAT1 (green) LED  PC6   Blinky LED
  STAT2 (yellow) LED PC7   Network: blinks without an address, on with one
  WKUP button        PA0   Blinky button (pressed = high)
  TAMPER button      PC13  Not used
  RS232 (USART2)     PD5 TX, PD6 RX (remapped), 115200 8N1, log output
  Ethernet           LAN8710A (rev E) or KS8721 (older) PHY, RMII, 50 MHz clock from MCO (PA8)
  24LC256 EEPROM     I2C1 PB8 SCL, PB9 SDA (remapped)
  CAN1 transceiver   PD0 RX, PD1 TX (not used by the demo)

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#ifndef _BOARD_H_
#define _BOARD_H_

#include <stdbool.h>
#include <stdint.h>

#include "stm32f1xx_hal.h"

#define LED_STAT1_PORT GPIOC
#define LED_STAT1_PIN  GPIO_PIN_6
#define LED_STAT2_PORT GPIOC
#define LED_STAT2_PIN  GPIO_PIN_7

#ifndef BLINKY_LED_ACTIVE_HIGH
#define BLINKY_LED_ACTIVE_HIGH 1
#endif
#ifndef BLINKY_BUTTON_PRESSED_HIGH
#define BLINKY_BUTTON_PRESSED_HIGH 1 // WKUP reads high when pressed
#endif

/*!
  System clock (72 MHz), 50 MHz on MCO for the PHY, GPIO, log port.
*/
void
board_init(void);

void
board_led_stat1(bool on);
void
board_led_stat2(bool on);

/*!
  @return true when the WKUP button is pressed
*/
bool
board_button(void);

/*!
  Unique id of the chip (96 bits)
*/
void
board_unique_id(uint8_t id[12]);

/*!
  Restart the board
*/
void
board_restart(void);

#endif
