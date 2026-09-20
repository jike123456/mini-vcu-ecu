#include "fault_manager.h"

#include "FreeRTOS.h"
#include "task.h"
#include "dtc_manager.h"
#include "safety_manager.h"
#include "vehicle_state.h"

static uint8_t s_injection_mask;
static uint8_t s_observed_mask;

void FaultManager_Init(void)
{
    taskENTER_CRITICAL();
    s_injection_mask = 0u;
    s_observed_mask = 0u;
    taskEXIT_CRITICAL();
}

void FaultManager_SetInjectionMask(uint8_t mask)
{
    taskENTER_CRITICAL();
    s_injection_mask = (uint8_t)(mask & FAULT_INJECT_ALL);
    taskEXIT_CRITICAL();
}

uint8_t FaultManager_GetInjectionMask(void)
{
    uint8_t mask;
    taskENTER_CRITICAL();
    mask = s_injection_mask;
    taskEXIT_CRITICAL();
    return mask;
}

void FaultManager_SetObserved(uint8_t source_mask, uint8_t active)
{
    taskENTER_CRITICAL();
    if (active != 0u) s_observed_mask |= (uint8_t)(source_mask & FAULT_INJECT_ALL);
    else s_observed_mask &= (uint8_t)~(source_mask & FAULT_INJECT_ALL);
    taskEXIT_CRITICAL();
}

void FaultManager_Service(uint32_t now_ms)
{
    uint8_t active;
    uint8_t safety_critical;
    uint8_t fatal;

    taskENTER_CRITICAL();
    active = (uint8_t)(s_injection_mask | s_observed_mask);
    taskEXIT_CRITICAL();

    DtcManager_Report(DTC_ID_BATTERY_UNDERVOLTAGE,
                      (active & FAULT_INJECT_UNDERVOLTAGE) != 0u, now_ms);
    DtcManager_Report(DTC_ID_ENCODER_PLAUSIBILITY,
                      (active & FAULT_INJECT_ENCODER) != 0u, now_ms);
    DtcManager_Report(DTC_ID_IMU_COMMUNICATION,
                      (active & FAULT_INJECT_IMU) != 0u, now_ms);
    DtcManager_Report(DTC_ID_TASK_MONITOR,
                      (active & FAULT_INJECT_TASK) != 0u, now_ms);
    DtcManager_Report(DTC_ID_CAN_CONTROLLER,
                      (active & FAULT_INJECT_CAN) != 0u, now_ms);

    safety_critical = (uint8_t)(active & (FAULT_INJECT_UNDERVOLTAGE |
                                         FAULT_INJECT_ENCODER |
                                         FAULT_INJECT_CAN));
    fatal = (uint8_t)(active & FAULT_INJECT_TASK);

    SafetyManager_SetInhibit(SAFETY_INHIBIT_DIAGNOSTIC,
                             (safety_critical != 0u) || (fatal != 0u));
    VehicleState_SetSafetyFault(safety_critical != 0u, now_ms);
    VehicleState_SetFatalFault(fatal != 0u, now_ms);
}
