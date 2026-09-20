#ifndef FAULT_MANAGER_H
#define FAULT_MANAGER_H

#include <stdint.h>

#define FAULT_INJECT_UNDERVOLTAGE   (1u << 0)
#define FAULT_INJECT_ENCODER        (1u << 1)
#define FAULT_INJECT_IMU            (1u << 2)
#define FAULT_INJECT_TASK           (1u << 3)
#define FAULT_INJECT_CAN            (1u << 4)
#define FAULT_INJECT_ALL            (0x1Fu)

void FaultManager_Init(void);
void FaultManager_SetInjectionMask(uint8_t mask);
uint8_t FaultManager_GetInjectionMask(void);
void FaultManager_SetObserved(uint8_t source_mask, uint8_t active);
void FaultManager_Service(uint32_t now_ms);

#endif /* FAULT_MANAGER_H */
