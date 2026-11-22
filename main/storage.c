/*
  storage.c - Saved registers in the 24LC256 EEPROM

  The data is stored with a magic number, its size and a checksum at
  address 0, so an empty or old EEPROM is not used. Writes are delayed by
  the caller (one second after the last change).

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#include <stdio.h>
#include <string.h>

#include "blinky.h"
#include "eeprom.h"

#define STORAGE_ADDR  0
#define STORAGE_MAGIC 0x56534350u // "VSCP"

typedef struct {
  uint32_t magic;
  uint16_t size;
  uint16_t checksum;
} storage_header_t;

static uint16_t
checksum(const uint8_t *p, size_t size)
{
  uint16_t sum = 0x5555;
  for (size_t i = 0; i < size; i++) {
    sum = (uint16_t) ((sum << 1) | (sum >> 15)) ^ p[i];
  }
  return sum;
}

bool
storage_load(void *data, size_t size)
{
  storage_header_t header;
  uint8_t buf[256];
  if ((size > sizeof(buf)) || !eeprom_read(STORAGE_ADDR, &header, sizeof(header)) ||
      (STORAGE_MAGIC != header.magic) || (size != header.size) ||
      !eeprom_read(STORAGE_ADDR + sizeof(header), buf, size) || (checksum(buf, size) != header.checksum)) {
    return false;
  }
  memcpy(data, buf, size);
  return true;
}

void
storage_save(const void *data, size_t size)
{
  storage_header_t header = { STORAGE_MAGIC, (uint16_t) size, checksum(data, size) };
  // Data first, header last: an interrupted save leaves the old header and so is not used
  header.magic = 0;
  if (!eeprom_write(STORAGE_ADDR, &header, sizeof(header)) ||
      !eeprom_write(STORAGE_ADDR + sizeof(header), data, size)) {
    printf("Could not save the registers (no EEPROM?)\n");
    return;
  }
  header.magic = STORAGE_MAGIC;
  eeprom_write(STORAGE_ADDR, &header, sizeof(header));
}
