#ifndef HOST_BOOT_LAYOUT_H
#define HOST_BOOT_LAYOUT_H
/* Runner copies the real shared header under this unambiguous name. */
#include "production_boot_layout.h"
extern uint32_t host_boot_request;
#undef BOOT_REQUEST_ADDRESS
#define BOOT_REQUEST_ADDRESS ((uintptr_t)&host_boot_request)
#endif
