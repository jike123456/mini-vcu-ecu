#ifndef __CAN_TASK_H
#define __CAN_TASK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t pass_count;
    uint32_t fail_count;
    uint8_t last_sequence;
    bool initialized;
    uint32_t init_status;
    uint32_t bus_tx_ok;
    uint32_t bus_tx_fail;
    uint32_t bus_rx_ok;
    uint32_t bus_rx_drop;
    uint32_t mcr;
    uint32_t msr;
    uint32_t tsr;
    uint32_t esr;
} CanSelfTestStats_t;

bool KaoYaApp_CanInit(void);
void KaoYaApp_CanTask(void *argument);
void CanTask_GetSelfTestStats(CanSelfTestStats_t *stats);

#endif /* __CAN_TASK_H */
