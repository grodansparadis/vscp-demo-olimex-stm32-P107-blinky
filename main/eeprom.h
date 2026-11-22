/*
  eeprom.h - 24LC256 I2C EEPROM (32 KB) on I2C1, PB8 SCL / PB9 SDA

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#ifndef _EEPROM_H_
#define _EEPROM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*!
  Start I2C1 and look for the EEPROM (address 0x50-0x57).
  @return true if it answers
*/
bool
eeprom_init(void);

bool
eeprom_read(uint16_t addr, void *data, size_t size);

/*!
  Write (in 64 byte pages, waits for each write cycle, about 5 ms).
*/
bool
eeprom_write(uint16_t addr, const void *data, size_t size);

#endif
