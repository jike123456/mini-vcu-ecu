#include "vehicle_state.h"

#include "FreeRTOS.h"
#include "task.h"

static VehicleStateSnapshot_t s_vehicle_state;

static void transition_to(VehicleStateId_t next, uint32_t now_ms)
{
    if (s_vehicle_state.state != next)
    {
        s_vehicle_state.state = next;
        s_vehicle_state.transition_count++;
        s_vehicle_state.last_transition_ms = now_ms;
    }
}

void VehicleState_Init(uint32_t now_ms)
{
    taskENTER_CRITICAL();
    s_vehicle_state.state = VCU_STATE_INIT;
    s_vehicle_state.fault_flags = 0u;
    s_vehicle_state.transition_count = 0u;
    s_vehicle_state.last_transition_ms = now_ms;
    transition_to(VCU_STATE_STANDBY, now_ms);
    taskEXIT_CRITICAL();
}

void VehicleState_OnValidCommand(bool drive_requested, uint32_t now_ms)
{
    taskENTER_CRITICAL();
    s_vehicle_state.fault_flags &= (uint8_t)~VCU_FAULT_COMMAND_TIMEOUT;
    if ((s_vehicle_state.fault_flags & VCU_FAULT_FATAL) != 0u)
    {
        transition_to(VCU_STATE_FAULT, now_ms);
    }
    else if ((s_vehicle_state.fault_flags & VCU_FAULT_SAFETY_ACTIVE) != 0u)
    {
        transition_to(VCU_STATE_SAFE_STOP, now_ms);
    }
    else
    {
        transition_to(drive_requested ? VCU_STATE_DRIVE : VCU_STATE_STANDBY, now_ms);
    }
    taskEXIT_CRITICAL();
}

void VehicleState_OnCommandTimeout(uint32_t now_ms)
{
    taskENTER_CRITICAL();
    s_vehicle_state.fault_flags |= VCU_FAULT_COMMAND_TIMEOUT;
    if ((s_vehicle_state.fault_flags & VCU_FAULT_FATAL) != 0u)
    {
        transition_to(VCU_STATE_FAULT, now_ms);
    }
    else
    {
        transition_to(VCU_STATE_SAFE_STOP, now_ms);
    }
    taskEXIT_CRITICAL();
}

void VehicleState_SetFatalFault(bool active, uint32_t now_ms)
{
    taskENTER_CRITICAL();
    if (active)
    {
        s_vehicle_state.fault_flags |= VCU_FAULT_FATAL;
        transition_to(VCU_STATE_FAULT, now_ms);
    }
    else
    {
        if ((s_vehicle_state.fault_flags & VCU_FAULT_FATAL) != 0u)
        {
            s_vehicle_state.fault_flags &= (uint8_t)~VCU_FAULT_FATAL;
            transition_to((s_vehicle_state.fault_flags &
                           (VCU_FAULT_COMMAND_TIMEOUT | VCU_FAULT_SAFETY_ACTIVE)) != 0u
                              ? VCU_STATE_SAFE_STOP
                              : VCU_STATE_STANDBY,
                          now_ms);
        }
    }
    taskEXIT_CRITICAL();
}

void VehicleState_SetSafetyFault(bool active, uint32_t now_ms)
{
    taskENTER_CRITICAL();
    if (active)
    {
        s_vehicle_state.fault_flags |= VCU_FAULT_SAFETY_ACTIVE;
        transition_to((s_vehicle_state.fault_flags & VCU_FAULT_FATAL) != 0u
                          ? VCU_STATE_FAULT
                          : VCU_STATE_SAFE_STOP,
                      now_ms);
    }
    else
    {
        if ((s_vehicle_state.fault_flags & VCU_FAULT_SAFETY_ACTIVE) != 0u)
        {
            s_vehicle_state.fault_flags &= (uint8_t)~VCU_FAULT_SAFETY_ACTIVE;
            if ((s_vehicle_state.fault_flags & VCU_FAULT_FATAL) != 0u)
            {
                transition_to(VCU_STATE_FAULT, now_ms);
            }
            else
            {
                transition_to((s_vehicle_state.fault_flags & VCU_FAULT_COMMAND_TIMEOUT) != 0u
                                  ? VCU_STATE_SAFE_STOP
                                  : VCU_STATE_STANDBY,
                              now_ms);
            }
        }
    }
    taskEXIT_CRITICAL();
}

VehicleStateSnapshot_t VehicleState_GetSnapshot(void)
{
    VehicleStateSnapshot_t snapshot;

    taskENTER_CRITICAL();
    snapshot = s_vehicle_state;
    taskEXIT_CRITICAL();
    return snapshot;
}

bool VehicleState_DriveAllowed(void)
{
    bool allowed;

    taskENTER_CRITICAL();
    allowed = (s_vehicle_state.state == VCU_STATE_DRIVE) &&
              (s_vehicle_state.fault_flags == 0u);
    taskEXIT_CRITICAL();
    return allowed;
}
