#ifndef __CAN_BUS_H
#define __CAN_BUS_H

#include <stdbool.h>
#include <stdint.h>

/* 0 = normal physical bus, 1 = MCU internal loopback regression. */
#define CAN_BUS_INTERNAL_LOOPBACK  1U
#define CAN_BUS_BITRATE            500000U

typedef struct {
    uint16_t std_id;
    uint8_t dlc;
    uint8_t data[8];
} CanBusFrame_t;

typedef struct {
    uint32_t tx_ok;
    uint32_t tx_fail;
    uint32_t rx_ok;
    uint32_t rx_drop;
    uint32_t error_irq;
    uint32_t last_esr;
    uint32_t init_status;
    uint32_t mcr;
    uint32_t msr;
    uint32_t tsr;
} CanBusStats_t;

typedef enum {
    CAN_BUS_INIT_NOT_STARTED = 0,
    CAN_BUS_INIT_QUEUE_FAILED,
    CAN_BUS_INIT_SLEEP_TIMEOUT,
    CAN_BUS_INIT_ENTER_TIMEOUT,
    CAN_BUS_INIT_LEAVE_TIMEOUT,
    CAN_BUS_INIT_READY
} CanBusInitStatus_t;

bool CanBus_Init(void);
bool CanBus_SendStd(uint16_t std_id, const uint8_t *data, uint8_t dlc);
bool CanBus_Receive(CanBusFrame_t *frame, uint32_t timeout_ms);
void CanBus_GetStats(CanBusStats_t *stats);

/* Called only by the vector handlers in stm32f4xx_it.c. */
void CanBus_Rx0IrqHandler(void);
void CanBus_ErrorIrqHandler(void);

#endif /* __CAN_BUS_H */
