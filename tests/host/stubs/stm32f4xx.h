#ifndef HOST_STM32_H
#define HOST_STM32_H
#include <stdint.h>
typedef struct { uint32_t VTOR; } HostScb;
extern HostScb host_scb;
#define SCB (&host_scb)
void NVIC_SystemReset(void);
#define __disable_irq() ((void)0)
#define __DSB() ((void)0)
#endif
