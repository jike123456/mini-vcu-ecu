#ifndef BOOT_LAYOUT_H
#define BOOT_LAYOUT_H

#include <stdint.h>

/* STM32F407ZG 1 MiB Flash layout. Keep these values in sync with
 * tools/boot_image.py and the linker/scatter files. */
#define BOOT_FLASH_BASE_ADDRESS       (0x08000000UL)
#define BOOT_FLASH_END_ADDRESS        (0x08100000UL)

/* Sectors 0..4: 16K + 16K + 16K + 16K + 64K = 128 KiB. */
#define BOOT_LOADER_BASE_ADDRESS      (0x08000000UL)
#define BOOT_LOADER_END_ADDRESS       (0x08020000UL)

/* Sectors 5..10 contain the application. The last 32 bytes are reserved
 * for a manifest that is programmed only after image verification. */
#define BOOT_APPLICATION_BASE_ADDRESS (0x08020000UL)
#define BOOT_MANIFEST_ADDRESS         (0x080DFFE0UL)
#define BOOT_APPLICATION_END_ADDRESS  BOOT_MANIFEST_ADDRESS

/* Sector 11 remains owned by the existing DTC NvM implementation. */
#define BOOT_NVM_BASE_ADDRESS         (0x080E0000UL)
#define BOOT_NVM_END_ADDRESS          (0x08100000UL)

#define BOOT_IMAGE_MAGIC              (0x4159414BUL) /* "KAYA" in LE */
#define BOOT_MANIFEST_FORMAT_VERSION  (1UL)

/* A software reset preserves SRAM on STM32F407.  The application writes this
 * one-shot token immediately before NVIC_SystemReset(); the bootloader clears
 * it before making its boot decision.  It is deliberately outside all linked
 * RW/ZI data so neither C runtime startup overwrites it. */
#define BOOT_REQUEST_ADDRESS          (0x2001FFF0UL)
#define BOOT_REQUEST_MAGIC            (0x424F4F54UL) /* "BOOT" */

typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t image_start;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t software_version;
    uint32_t flags;
    uint32_t manifest_crc32;
} BootImageManifest_t;

#endif
