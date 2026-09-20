#include "boot_image.h"

#include "boot_layout.h"
#include "stm32f4xx.h"

#define BOOT_SRAM_BASE_ADDRESS  (0x20000000UL)
#define BOOT_SRAM_END_ADDRESS   (0x20020000UL)

static bool is_manifest_valid(const BootImageManifest_t *manifest)
{
    uint32_t maximum_size = BOOT_APPLICATION_END_ADDRESS -
                            BOOT_APPLICATION_BASE_ADDRESS;

    if ((manifest->magic != BOOT_IMAGE_MAGIC) ||
        (manifest->format_version != BOOT_MANIFEST_FORMAT_VERSION) ||
        (manifest->image_start != BOOT_APPLICATION_BASE_ADDRESS) ||
        (manifest->image_size < 8UL) ||
        (manifest->image_size > maximum_size))
    {
        return false;
    }
    return BootImage_Crc32((const uint8_t *)manifest,
                           sizeof(BootImageManifest_t) - sizeof(uint32_t)) ==
           manifest->manifest_crc32;
}

static bool is_vector_table_valid(const BootImageManifest_t *manifest)
{
    const uint32_t *vectors = (const uint32_t *)BOOT_APPLICATION_BASE_ADDRESS;
    uint32_t initial_stack = vectors[0];
    uint32_t reset_vector = vectors[1];
    uint32_t reset_address = reset_vector & ~1UL;
    uint32_t image_end = BOOT_APPLICATION_BASE_ADDRESS + manifest->image_size;

    if ((initial_stack <= BOOT_SRAM_BASE_ADDRESS) ||
        (initial_stack > BOOT_SRAM_END_ADDRESS) ||
        ((initial_stack & 0x3UL) != 0UL))
    {
        return false;
    }
    if (((reset_vector & 1UL) == 0UL) ||
        (reset_address < BOOT_APPLICATION_BASE_ADDRESS) ||
        (reset_address >= image_end))
    {
        return false;
    }
    return true;
}

uint32_t BootImage_Crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t index;

    for (index = 0UL; index < length; index++)
    {
        uint32_t bit;
        crc ^= data[index];
        for (bit = 0UL; bit < 8UL; bit++)
        {
            uint32_t mask = 0UL - (crc & 1UL);
            crc = (crc >> 1UL) ^ (0xEDB88320UL & mask);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

BootImageStatus_t BootImage_Validate(void)
{
    const BootImageManifest_t *manifest =
        (const BootImageManifest_t *)BOOT_MANIFEST_ADDRESS;

    if (!is_manifest_valid(manifest))
    {
        return BOOT_IMAGE_BAD_MANIFEST;
    }
    if (!is_vector_table_valid(manifest))
    {
        return BOOT_IMAGE_BAD_VECTOR;
    }
    if (BootImage_Crc32((const uint8_t *)BOOT_APPLICATION_BASE_ADDRESS,
                        manifest->image_size) != manifest->image_crc32)
    {
        return BOOT_IMAGE_BAD_CRC;
    }
    return BOOT_IMAGE_VALID;
}

#if defined(__CC_ARM)
__asm static void jump_to_reset_handler(uint32_t initial_stack,
                                         uint32_t reset_handler)
{
    MSR MSP, r0
    BX  r1
}
#elif defined(__GNUC__)
static __attribute__((noreturn)) void jump_to_reset_handler(
    uint32_t initial_stack, uint32_t reset_handler)
{
    __asm volatile("msr msp, %0\n"
                   "bx %1\n"
                   : : "r" (initial_stack), "r" (reset_handler) : "memory");
    __builtin_unreachable();
}
#else
#error Unsupported compiler for application jump
#endif

void BootImage_JumpToApplication(void)
{
    const uint32_t *vectors = (const uint32_t *)BOOT_APPLICATION_BASE_ADDRESS;
    uint32_t index;

    __disable_irq();
    SysTick->CTRL = 0UL;
    SysTick->LOAD = 0UL;
    SysTick->VAL = 0UL;

    for (index = 0UL; index < 8UL; index++)
    {
        NVIC->ICER[index] = 0xFFFFFFFFUL;
        NVIC->ICPR[index] = 0xFFFFFFFFUL;
    }

    SCB->VTOR = BOOT_APPLICATION_BASE_ADDRESS;
    __DSB();
    __ISB();
    jump_to_reset_handler(vectors[0], vectors[1]);

    for (;;)
    {
        __NOP();
    }
}
