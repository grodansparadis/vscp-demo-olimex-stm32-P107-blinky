/*
  arch/cc.h - lwIP platform definitions (GCC, Cortex-M3)
*/
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdio.h>
#include <stdlib.h>

#define LWIP_PLATFORM_DIAG(x)                                                                                          \
  do {                                                                                                                 \
    printf x;                                                                                                          \
  } while (0)

#define LWIP_PLATFORM_ASSERT(x)                                                                                        \
  do {                                                                                                                 \
    printf("lwIP assert \"%s\" at line %d in %s\n", x, __LINE__, __FILE__);                                         \
  } while (0)

#define LWIP_NO_STDINT_H 0
#define LWIP_NO_INTTYPES_H 0
#define LWIP_TIMEVAL_PRIVATE 0
#include <sys/time.h>

#endif
