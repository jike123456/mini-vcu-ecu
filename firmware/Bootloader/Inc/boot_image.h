#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    BOOT_IMAGE_VALID = 0,
    BOOT_IMAGE_BAD_MANIFEST,
    BOOT_IMAGE_BAD_VECTOR,
    BOOT_IMAGE_BAD_CRC
} BootImageStatus_t;

uint32_t BootImage_Crc32(const uint8_t *data, uint32_t length);
BootImageStatus_t BootImage_Validate(void);
void BootImage_JumpToApplication(void);

#endif
