#ifndef VEHICLE_CAN_H
#define VEHICLE_CAN_H

#include <stdbool.h>
#include <stdint.h>

#define VEHICLE_CAN_ID_COMMAND         0x100U
#define VEHICLE_CAN_ID_MOTION_STATUS   0x180U
#define VEHICLE_CAN_ID_TEST_FAULT      0x101U
#define VEHICLE_CAN_DLC                8U

typedef enum {
    VEHICLE_CAN_DECODE_OK = 0,
    VEHICLE_CAN_DECODE_LENGTH,
    VEHICLE_CAN_DECODE_CRC,
    VEHICLE_CAN_DECODE_COUNTER,
    VEHICLE_CAN_DECODE_VALUE
} VehicleCanDecodeStatus_t;

typedef struct {
    int16_t target_vx_mmps;
    int16_t target_wz_mradps;
    uint8_t ignition;
    uint8_t gear;
    uint8_t alive_counter;
} VehicleCanCommand_t;

typedef struct {
    bool counter_valid;
    uint8_t last_counter;
    uint32_t accepted;
    uint32_t length_errors;
    uint32_t crc_errors;
    uint32_t counter_errors;
    uint32_t value_errors;
} VehicleCanRxContext_t;

uint8_t VehicleCan_Crc8SaeJ1850(const uint8_t *data, uint8_t length);

VehicleCanDecodeStatus_t VehicleCan_DecodeCommand(
    VehicleCanRxContext_t *context,
    const uint8_t *data,
    uint8_t dlc,
    VehicleCanCommand_t *command);

void VehicleCan_EncodeMotionStatus(
    int16_t left_wheel_mmps,
    int16_t right_wheel_mmps,
    uint8_t vehicle_state,
    uint8_t fault_flags,
    uint8_t alive_counter,
    uint8_t data[VEHICLE_CAN_DLC]);

#endif /* VEHICLE_CAN_H */
