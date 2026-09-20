#ifndef COMMAND_BROKER_H
#define COMMAND_BROKER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CMD_SOURCE_NONE = 0,
    CMD_SOURCE_UART_LEGACY,
    CMD_SOURCE_VIRTUAL_CAN
} CmdSource_t;

typedef struct {
    int16_t vx_mmps;
    int16_t vy_mmps;
    int16_t wz_mradps;
    uint8_t ignition;
    uint8_t gear;
    bool drive_enable;
    uint32_t rx_tick;
    bool valid;
    CmdSource_t source;
} CmdVel_t;

void CommandBroker_Publish(const CmdVel_t *command);
CmdVel_t CommandBroker_GetLatest(void);

#endif /* COMMAND_BROKER_H */
