# VSCP Blinky demo for the Olimex STM32-P107 (STM32F107VC, Ethernet)
cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake)
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

project(vscp-blinky C ASM)
set(CMAKE_C_STANDARD 11)

# Local settings that are not committed (static IP, MQTT broker, ...).
# Every BLINKY_* setting in main/vscp-projdefs.h can be set here, see
# blinky_local.cmake.example.
include(${CMAKE_CURRENT_LIST_DIR}/blinky_local.cmake OPTIONAL)

# ---------------------------------------------------------------------------
#                              Dependencies
# ---------------------------------------------------------------------------

include(FetchContent)

# vscp-firmware. Use -DFETCHCONTENT_SOURCE_DIR_VSCP_FIRMWARE=<path> for a local checkout.
FetchContent_Declare(
  vscp_firmware
  URL https://codeload.github.com/grodansparadis/vscp-firmware/tar.gz/3da4bf2649ac8992cd81fae1cd9ae35ef1f9a79c
  URL_HASH SHA256=9ef5a4bf180430231990c867db41f8f7ab1196cbd588f78bd4a985db0c1d2e08
  SOURCE_SUBDIR __sources_only__
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ST CMSIS device files (STM32F1), HAL driver and CMSIS core
FetchContent_Declare(
  cmsis_device_f1
  GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis_device_f1.git
  GIT_TAG 8a76309ed1250d817e9c888c4417171d2ba3ba63 # v4.3.5
  GIT_SHALLOW FALSE
  SOURCE_SUBDIR __sources_only__)
FetchContent_Declare(
  stm32f1xx_hal_driver
  GIT_REPOSITORY https://github.com/STMicroelectronics/stm32f1xx_hal_driver.git
  GIT_TAG 77fbb30b7a1d02533980400083e48c559aae5a4f # v1.1.10
  SOURCE_SUBDIR __sources_only__)
FetchContent_Declare(
  cmsis_core
  GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis_core.git
  GIT_TAG 1a2f783395dd72778a98ebe7771f36ca1c30a35e # v5.9.0
  SOURCE_SUBDIR __sources_only__)

# lwIP TCP/IP stack
FetchContent_Declare(
  lwip
  GIT_REPOSITORY https://github.com/lwip-tcpip/lwip.git
  GIT_TAG 6ca936f6b588cee702c638eee75c2436e6cf75de # STABLE-2_1_3_RELEASE
  SOURCE_SUBDIR __sources_only__)

# cJSON (VSCP JSON events over MQTT)
FetchContent_Declare(
  cjson
  URL https://codeload.github.com/DaveGamble/cJSON/tar.gz/b2890c8d76bbb64e710585ebc0a917196b9c67e7
  URL_HASH SHA256=c3866048d822428ab289d20842c75a39c92c26e967fed2f0706f6f15e6894082
  SOURCE_SUBDIR __sources_only__
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(vscp_firmware cmsis_device_f1 stm32f1xx_hal_driver cmsis_core lwip cjson)

set(VSCP_SRC ${vscp_firmware_SOURCE_DIR}/src)
set(HAL ${stm32f1xx_hal_driver_SOURCE_DIR})
set(DEV ${cmsis_device_f1_SOURCE_DIR})
set(LWIP ${lwip_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
#                                Firmware
# ---------------------------------------------------------------------------

file(GLOB LWIP_CORE ${LWIP}/core/*.c ${LWIP}/core/ipv4/*.c)

set(HAL_MODULES hal hal_cortex hal_rcc hal_rcc_ex hal_gpio hal_gpio_ex hal_flash hal_flash_ex hal_dma hal_eth
                hal_i2c hal_pwr hal_can)
set(HAL_SRC)
foreach(m ${HAL_MODULES})
  list(APPEND HAL_SRC ${HAL}/Src/stm32f1xx_${m}.c)
endforeach()

set(VSCP_FW_SRC
    ${VSCP_SRC}/vscp-fifo.c
    ${VSCP_SRC}/vscp-crc.c
    ${VSCP_SRC}/vscp-firmware-helper.c
    ${VSCP_SRC}/vscp-firmware-level2.c
    ${VSCP_SRC}/vscp-link-protocol.c)

add_executable(vscp-blinky
  main/main.c
  main/board.c
  main/network.c
  main/ethernetif.c
  main/eeprom.c
  main/storage.c
  main/blinky.c
  main/link_server.c
  main/udp_transport.c
  main/can_gateway.c
  main/mqtt_transport.c
  main/syscalls.c
  main/stm32f1xx_it.c
  ${DEV}/Source/Templates/system_stm32f1xx.c
  ${DEV}/Source/Templates/gcc/startup_stm32f107xc.s
  ${HAL_SRC}
  ${LWIP_CORE}
  ${LWIP}/netif/ethernet.c
  ${LWIP}/apps/mqtt/mqtt.c
  ${LWIP}/apps/sntp/sntp.c
  ${VSCP_FW_SRC}
  ${cjson_SOURCE_DIR}/cJSON.c)

target_include_directories(vscp-blinky PRIVATE
  main
  ${VSCP_SRC}
  ${cjson_SOURCE_DIR}
  ${HAL}/Inc
  ${DEV}/Include
  ${cmsis_core_SOURCE_DIR}/Include
  ${LWIP}/include)

# Settings from vscp-projdefs.h set here, for example
#   -DBLINKY_DEFINES="BLINKY_ENABLE_MQTT=1;BLINKY_MQTT_BROKER=\"192.168.1.9\""
set(BLINKY_DEFINES "" CACHE STRING "BLINKY_* settings (vscp-projdefs.h), separated by ;")

target_compile_definitions(vscp-blinky PRIVATE STM32F107xC USE_HAL_DRIVER ${BLINKY_DEFINES})
target_compile_options(vscp-blinky PRIVATE -Wall -g3)

# Third party code: no warnings
set_source_files_properties(${HAL_SRC} ${LWIP_CORE} ${LWIP}/netif/ethernet.c ${LWIP}/apps/mqtt/mqtt.c
                            ${LWIP}/apps/sntp/sntp.c ${VSCP_FW_SRC} ${cjson_SOURCE_DIR}/cJSON.c
                            ${DEV}/Source/Templates/system_stm32f1xx.c PROPERTIES COMPILE_OPTIONS "-w")

target_link_options(vscp-blinky PRIVATE
  -T${CMAKE_CURRENT_LIST_DIR}/STM32F107VCTx_FLASH.ld
  -Wl,-Map=${CMAKE_BINARY_DIR}/vscp-blinky.map
  -Wl,--print-memory-usage)

add_custom_command(TARGET vscp-blinky POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary vscp-blinky.elf vscp-blinky.bin
  COMMAND ${CMAKE_OBJCOPY} -O ihex vscp-blinky.elf vscp-blinky.hex
  COMMAND ${CMAKE_SIZE} vscp-blinky.elf
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(vscp-blinky PROPERTIES SUFFIX .elf)
