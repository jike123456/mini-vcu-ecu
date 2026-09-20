#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
#define portTICK_PERIOD_MS 1u
void Host_EnterCritical(void);
void Host_ExitCritical(void);
#define taskENTER_CRITICAL() Host_EnterCritical()
#define taskEXIT_CRITICAL() Host_ExitCritical()
#endif
