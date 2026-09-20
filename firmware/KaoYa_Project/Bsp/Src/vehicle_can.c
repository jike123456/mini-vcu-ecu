#include "vehicle_can.h"

#include <stddef.h>

static int16_t prvReadI16(const uint8_t *data)
{
    return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static void prvWriteI16(uint8_t *data, int16_t value)
{
    uint16_t raw = (uint16_t)value;
    data[0] = (uint8_t)raw;
    data[1] = (uint8_t)(raw >> 8U);
}

uint8_t VehicleCan_Crc8SaeJ1850(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0xFFU;
    uint8_t index;

    if (data == NULL) {
        return 0U;
    }

    for (index = 0U; index < length; index++) {
        uint8_t bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++) {
            crc = ((crc & 0x80U) != 0U)
                      ? (uint8_t)((uint8_t)(crc << 1U) ^ 0x1DU)
                      : (uint8_t)(crc << 1U);
        }
    }

    return (uint8_t)(crc ^ 0xFFU);
}

VehicleCanDecodeStatus_t VehicleCan_DecodeCommand(
    VehicleCanRxContext_t *context,
    const uint8_t *data,
    uint8_t dlc,
    VehicleCanCommand_t *command)
{
    uint8_t alive;

    if ((context == NULL) || (data == NULL) || (command == NULL) ||
        (dlc != VEHICLE_CAN_DLC)) {
        if (context != NULL) {
            context->length_errors++;
        }
        return VEHICLE_CAN_DECODE_LENGTH;
    }

    if (VehicleCan_Crc8SaeJ1850(data, 7U) != data[7]) {
        context->crc_errors++;
        return VEHICLE_CAN_DECODE_CRC;
    }

    alive = (uint8_t)(data[6] & 0x0FU);
    if ((data[6] & 0xF0U) != 0U) {
        context->value_errors++;
        return VEHICLE_CAN_DECODE_VALUE;
    }

    if (context->counter_valid) {
        uint8_t expected = (uint8_t)((context->last_counter + 1U) & 0x0FU);
        if (alive != expected) {
            /* Resynchronize after reporting one discontinuity. */
            context->last_counter = alive;
            context->counter_errors++;
            return VEHICLE_CAN_DECODE_COUNTER;
        }
    }
    context->counter_valid = true;
    context->last_counter = alive;

    if ((data[4] > 1U) || (data[5] > 2U)) {
        context->value_errors++;
        return VEHICLE_CAN_DECODE_VALUE;
    }

    command->target_vx_mmps = prvReadI16(&data[0]);
    command->target_wz_mradps = prvReadI16(&data[2]);
    command->ignition = data[4];
    command->gear = data[5];
    command->alive_counter = alive;
    context->accepted++;
    return VEHICLE_CAN_DECODE_OK;
}

void VehicleCan_EncodeMotionStatus(
    int16_t left_wheel_mmps,
    int16_t right_wheel_mmps,
    uint8_t vehicle_state,
    uint8_t fault_flags,
    uint8_t alive_counter,
    uint8_t data[VEHICLE_CAN_DLC])
{
    if (data == NULL) {
        return;
    }

    prvWriteI16(&data[0], left_wheel_mmps);
    prvWriteI16(&data[2], right_wheel_mmps);
    data[4] = vehicle_state;
    data[5] = fault_flags;
    data[6] = (uint8_t)(alive_counter & 0x0FU);
    data[7] = VehicleCan_Crc8SaeJ1850(data, 7U);
}
