/*
  stm32f1xx_it.c - Interrupt and exception handlers
*/

#include <stdio.h>

#include "stm32f1xx_hal.h"

void
SysTick_Handler(void)
{
  HAL_IncTick();
}

void
NMI_Handler(void)
{
}

void
HardFault_Handler(void)
{
  printf("HardFault\n");
  for (;;) {
  }
}

void
MemManage_Handler(void)
{
  for (;;) {
  }
}

void
BusFault_Handler(void)
{
  for (;;) {
  }
}

void
UsageFault_Handler(void)
{
  for (;;) {
  }
}
