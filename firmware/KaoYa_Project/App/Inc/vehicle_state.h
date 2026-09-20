#ifndef VEHICLE_STATE_H
#define VEHICLE_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VCU_STATE_INIT = 0,
    VCU_STATE_STANDBY = 1,
    VCU_STATE_DRIVE = 2,
    VCU_STATE_SAFE_STOP = 3,
    VCU_STATE_FAULT = 4
} VehicleStateId_t;

#define VCU_FAULT_COMMAND_TIMEOUT  (1u << 0)
#define VCU_FAULT_DTC_ACTIVE       (1u << 1)
#define VCU_FAULT_DTC_STORED       (1u << 2)
#define VCU_FAULT_SAFETY_ACTIVE    (1u << 3)
#define VCU_FAULT_FATAL            (1u << 7)

typedef struct {
    VehicleStateId_t state;
    uint8_t fault_flags;
    uint32_t transition_count;
    uint32_t last_transition_ms;
} VehicleStateSnapshot_t;

void VehicleState_Init(uint32_t now_ms);
void VehicleState_OnValidCommand(bool drive_requested, uint32_t now_ms);
void VehicleState_OnCommandTimeout(uint32_t now_ms);
void VehicleState_SetFatalFault(bool active, uint32_t now_ms);
void VehicleState_SetSafetyFault(bool active, uint32_t now_ms);
VehicleStateSnapshot_t VehicleState_GetSnapshot(void);
bool VehicleState_DriveAllowed(void);

#endif /* VEHICLE_STATE_H */
