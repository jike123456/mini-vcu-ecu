#ifndef NVM_DTC_H
#define NVM_DTC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool restored;
    bool storage_full;
    uint32_t latest_sequence;
    uint32_t valid_records;
    uint32_t crc_errors;
    uint32_t write_errors;
} NvMDtcStats_t;

void NvMDtc_Init(void);
void NvMDtc_Service(uint32_t now_ms, bool safe_to_write);
NvMDtcStats_t NvMDtc_GetStats(void);

#endif /* NVM_DTC_H */
