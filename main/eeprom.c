/*
  eeprom.c - 24LC256 I2C EEPROM (32 KB) on I2C1, PB8 SCL / PB9 SDA (remapped)

  64 byte pages; a write cycle takes up to 5 ms, the end is found by
  polling the device (it does not acknowledge while it writes).

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"

#include "eeprom.h"

#define EEPROM_PAGE_SIZE 64
#define EEPROM_SIZE      32768
#define I2C_TIMEOUT_MS   50
#define WRITE_CYCLE_MS   10 // 24LC256: 5 ms max

static I2C_HandleTypeDef s_i2c;
static uint16_t s_addr; // 7-bit address << 1
static bool s_ok;

void
HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
  GPIO_InitTypeDef gpio = { 0 };
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_AFIO_REMAP_I2C1_ENABLE(); // PB8/PB9
  gpio.Pin   = GPIO_PIN_8 | GPIO_PIN_9;
  gpio.Mode  = GPIO_MODE_AF_OD;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);
  __HAL_RCC_I2C1_CLK_ENABLE();
}

/*
  Free the bus: a device that was reset in the middle of a read can hold
  SDA low. Up to nine clock pulses and a stop condition release it.
*/
static void
bus_recover(void)
{
  GPIO_InitTypeDef gpio = { 0 };
  __HAL_RCC_GPIOB_CLK_ENABLE();
  gpio.Pin   = GPIO_PIN_8 | GPIO_PIN_9;
  gpio.Mode  = GPIO_MODE_OUTPUT_OD;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
  HAL_GPIO_Init(GPIOB, &gpio);
  for (int i = 0; (i < 9) && (GPIO_PIN_RESET == HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9)); i++) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
    HAL_Delay(1);
  }
  // Stop: SDA low -> high while SCL is high
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
  HAL_Delay(1);
}

bool
eeprom_init(void)
{
  bus_recover();

  s_i2c.Instance             = I2C1;
  s_i2c.Init.ClockSpeed      = 100000;
  s_i2c.Init.DutyCycle       = I2C_DUTYCYCLE_2;
  s_i2c.Init.OwnAddress1     = 0;
  s_i2c.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
  s_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  s_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  s_i2c.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
  if (HAL_OK != HAL_I2C_Init(&s_i2c)) {
    printf("EEPROM: I2C init failed\n");
    return false;
  }

  // The address pins A0-A2 select 0x50-0x57
  for (uint16_t a = 0x50; a <= 0x57; a++) {
    if (HAL_OK == HAL_I2C_IsDeviceReady(&s_i2c, a << 1, 2, I2C_TIMEOUT_MS)) {
      s_addr = a << 1;
      s_ok   = true;
      printf("EEPROM: 24LC256 at 0x%02x\n", a);
      return true;
    }
  }
  printf("EEPROM: no answer (0x50-0x57), registers are not saved\n");
  return false;
}

bool
eeprom_read(uint16_t addr, void *data, size_t size)
{
  if (!s_ok || (addr + size > EEPROM_SIZE)) {
    return false;
  }
  return HAL_OK == HAL_I2C_Mem_Read(&s_i2c, s_addr, addr, I2C_MEMADD_SIZE_16BIT, data, size, I2C_TIMEOUT_MS * 4);
}

bool
eeprom_write(uint16_t addr, const void *data, size_t size)
{
  const uint8_t *p = data;
  if (!s_ok || (addr + size > EEPROM_SIZE)) {
    return false;
  }
  while (size) {
    // A write must not cross a page boundary
    size_t n = EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE);
    if (n > size) {
      n = size;
    }
    if (HAL_OK != HAL_I2C_Mem_Write(&s_i2c, s_addr, addr, I2C_MEMADD_SIZE_16BIT, (uint8_t *) p, n, I2C_TIMEOUT_MS)) {
      return false;
    }
    // Wait for the write cycle: the device does not answer until it is done.
    // (Polled on time: HAL_I2C_IsDeviceReady() tries back to back without a delay.)
    uint32_t start = HAL_GetTick();
    while (HAL_OK != HAL_I2C_IsDeviceReady(&s_i2c, s_addr, 1, 2)) {
      if ((HAL_GetTick() - start) > WRITE_CYCLE_MS) {
        return false;
      }
    }
    addr += n;
    p += n;
    size -= n;
  }
  return true;
}
