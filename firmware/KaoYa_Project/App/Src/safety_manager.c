#include "safety_manager.h"

#include "FreeRTOS.h"
#include "task.h"
#include "motor.h"
#include "vehicle_state.h"

static uint8_t s_inhibit_mask;

void SafetyManager_Init(void)
{
    taskENTER_CRITICAL();
    s_inhibit_mask = SAFETY_INHIBIT_NOT_DRIVE;
    taskEXIT_CRITICAL();
    Motor_ApplyPwm(0, 0);
}

void SafetyManager_SetInhibit(uint8_t reason, bool active)
{
    taskENTER_CRITICAL();
    if (active)
    {
        s_inhibit_mask |= reason;
    }
    else
    {
        s_inhibit_mask &= (uint8_t)~reason;
    }
    taskEXIT_CRITICAL();
}

uint8_t SafetyManager_GetInhibitMask(void)
{
    uint8_t mask;

    taskENTER_CRITICAL();
    mask = s_inhibit_mask;
    taskEXIT_CRITICAL();
    return mask;
}

bool SafetyManager_ApplyMotorPwm(int16_t requested_left, int16_t requested_right)
{
    uint8_t mask = SafetyManager_GetInhibitMask();

    if ((mask != 0u) || !VehicleState_DriveAllowed())
    {
        Motor_ApplyPwm(0, 0);
        return false;
    }

    Motor_ApplyPwm(requested_left, requested_right);
    return true;
}

void SafetyManager_ForceStop(void)
{
    Motor_ApplyPwm(0, 0);
}
