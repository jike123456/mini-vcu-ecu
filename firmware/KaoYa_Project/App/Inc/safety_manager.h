#ifndef SAFETY_MANAGER_H
#define SAFETY_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#define SAFETY_INHIBIT_NOT_DRIVE    (1u << 0)
#define SAFETY_INHIBIT_CMD_TIMEOUT  (1u << 1)
#define SAFETY_INHIBIT_FATAL        (1u << 2)
#define SAFETY_INHIBIT_DIAGNOSTIC   (1u << 3)

void SafetyManager_Init(void);
void SafetyManager_SetInhibit(uint8_t reason, bool active);
uint8_t SafetyManager_GetInhibitMask(void);
bool SafetyManager_ApplyMotorPwm(int16_t requested_left, int16_t requested_right);
void SafetyManager_ForceStop(void);

#endif /* SAFETY_MANAGER_H */
