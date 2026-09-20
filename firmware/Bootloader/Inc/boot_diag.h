#ifndef BOOT_DIAG_H
#define BOOT_DIAG_H

#include <stdbool.h>

/* Returns true once and clears the SRAM request token. */
bool BootDiag_ConsumeRequest(void);

/* Blocking diagnostic server. It returns only when a verified image has been
 * committed and the positive TransferExit response has left USART1. */
void BootDiag_Run(void);

#endif
