#include "boot_image.h"
#include "boot_diag.h"
#include "stm32f4xx.h"

int main(void)
{
    bool programming_requested = BootDiag_ConsumeRequest();

    if (!programming_requested &&
        (BootImage_Validate() == BOOT_IMAGE_VALID))
    {
        BootImage_JumpToApplication();
    }

    /* A requested programming session, or an invalid/uncommitted application,
     * keeps the ECU recoverable through the diagnostic link. */
    BootDiag_Run();
    for (;;) { __WFI(); }
}
