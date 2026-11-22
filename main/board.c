/*
  board.c - Olimex STM32-P107: clocks, LEDs, buttons, log port

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#include <string.h>

#include "board.h"

/*
  25 MHz crystal:
    PLL2 = 25 / 5 * 8  = 40 MHz
    PLL  = PLL2 / 5 * 9 = 72 MHz (system clock)
    PLL3 = 25 / 5 * 10 = 50 MHz -> MCO (PA8) -> LAN8710A RMII reference clock
*/
static void
clock_init(void)
{
  RCC_OscInitTypeDef osc = { 0 };
  RCC_ClkInitTypeDef clk = { 0 };

  osc.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState            = RCC_HSE_ON;
  osc.HSEPredivValue      = RCC_HSE_PREDIV_DIV5;
  osc.Prediv1Source       = RCC_PREDIV1_SOURCE_PLL2;
  osc.PLL.PLLState        = RCC_PLL_ON;
  osc.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLMUL          = RCC_PLL_MUL9;
  osc.PLL2.PLL2State      = RCC_PLL2_ON;
  osc.PLL2.PLL2MUL        = RCC_PLL2_MUL8;
  osc.PLL2.HSEPrediv2Value = RCC_HSE_PREDIV2_DIV5;
  if (HAL_OK != HAL_RCC_OscConfig(&osc)) {
    for (;;) {
    }
  }

  clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2; // 36 MHz
  clk.APB2CLKDivider = RCC_HCLK_DIV1; // 72 MHz
  if (HAL_OK != HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2)) {
    for (;;) {
    }
  }

  // PLL3 = 50 MHz for the PHY on MCO (PA8)
  __HAL_RCC_PLLI2S_CONFIG(RCC_PLLI2S_MUL10);
  __HAL_RCC_PLLI2S_ENABLE();
  while (!__HAL_RCC_GET_FLAG(RCC_FLAG_PLLI2SRDY)) {
  }
  HAL_RCC_MCOConfig(RCC_MCO, RCC_MCO1SOURCE_PLL3CLK, RCC_MCODIV_1);
}

// USART2 on PD5 (TX) / PD6 (RX), the RS232 connector
static void
log_init(void)
{
  GPIO_InitTypeDef gpio = { 0 };
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_USART2_CLK_ENABLE();
  __HAL_AFIO_REMAP_USART2_ENABLE();

  gpio.Pin   = GPIO_PIN_5;
  gpio.Mode  = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOD, &gpio);
  gpio.Pin  = GPIO_PIN_6;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOD, &gpio);

  USART2->BRR = (HAL_RCC_GetPCLK1Freq() + 115200 / 2) / 115200;
  USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

// Called by printf (syscalls.c)
void
board_log_putc(char c)
{
  if ('\n' == c) {
    board_log_putc('\r');
  }
  while (!(USART2->SR & USART_SR_TXE)) {
  }
  USART2->DR = (uint8_t) c;
}

void
board_init(void)
{
  GPIO_InitTypeDef gpio = { 0 };

  clock_init();
  log_init();

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  // LEDs
  gpio.Pin   = LED_STAT1_PIN | LED_STAT2_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &gpio);
  board_led_stat1(false);
  board_led_stat2(false);

  // WKUP button (the board has a pull resistor)
  gpio.Pin  = GPIO_PIN_0;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio);
}

static void
led(GPIO_TypeDef *port, uint16_t pin, bool on)
{
  HAL_GPIO_WritePin(port, pin, (on == BLINKY_LED_ACTIVE_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void
board_led_stat1(bool on)
{
  led(LED_STAT1_PORT, LED_STAT1_PIN, on);
}

void
board_led_stat2(bool on)
{
  led(LED_STAT2_PORT, LED_STAT2_PIN, on);
}

bool
board_button(void)
{
  return (GPIO_PIN_SET == HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0)) == BLINKY_BUTTON_PRESSED_HIGH;
}

void
board_unique_id(uint8_t id[12])
{
  memcpy(id, (const void *) UID_BASE, 12);
}

void
board_restart(void)
{
  NVIC_SystemReset();
}
