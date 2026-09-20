#ifndef DTC_MANAGER_H
#define DTC_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DTC_ID_COMMAND_TIMEOUT = 0,
    DTC_ID_COMMAND_CRC,
    DTC_ID_COMMAND_COUNTER,
    DTC_ID_COMMAND_VALUE,
    DTC_ID_BATTERY_UNDERVOLTAGE,
    DTC_ID_ENCODER_PLAUSIBILITY,
    DTC_ID_IMU_COMMUNICATION,
    DTC_ID_TASK_MONITOR,
    DTC_ID_CAN_CONTROLLER,
    DTC_ID_COUNT
} DtcId_t;

#define DTC_CODE_COMMAND_TIMEOUT  (0xC00100UL)
#define DTC_CODE_COMMAND_CRC      (0xC00200UL)
#define DTC_CODE_COMMAND_COUNTER  (0xC00300UL)
#define DTC_CODE_COMMAND_VALUE    (0xC00400UL)
#define DTC_CODE_BATTERY_UNDERVOLTAGE (0xC00500UL)
#define DTC_CODE_ENCODER_PLAUSIBILITY (0xC00600UL)
#define DTC_CODE_IMU_COMMUNICATION    (0xC00700UL)
#define DTC_CODE_TASK_MONITOR         (0xC00800UL)
#define DTC_CODE_CAN_CONTROLLER       (0xC00900UL)

/* ISO 14229 DTC status-byte bits used by the later UDS layer. */
#define DTC_STATUS_TEST_FAILED                    (1u << 0)
#define DTC_STATUS_TEST_FAILED_THIS_CYCLE         (1u << 1)
#define DTC_STATUS_PENDING                        (1u << 2)
#define DTC_STATUS_CONFIRMED                      (1u << 3)
#define DTC_STATUS_TEST_FAILED_SINCE_CLEAR        (1u << 5)

typedef struct {
    uint32_t timestamp_ms;
    uint8_t vehicle_state;
    int16_t command_vx_mmps;
    int16_t command_wz_mradps;
    int16_t left_wheel_mmps;
    int16_t right_wheel_mmps;
} DtcFreezeFrame_t;

typedef struct {
    uint32_t code;
    uint8_t status;
    uint8_t occurrence_count;
    uint8_t failure_count;
    uint8_t pass_count;
    bool freeze_frame_valid;
    DtcFreezeFrame_t freeze_frame;
} DtcRecord_t;

void DtcManager_Init(void);
void DtcManager_UpdateContext(int16_t command_vx_mmps,
                              int16_t command_wz_mradps,
                              int16_t left_wheel_mmps,
                              int16_t right_wheel_mmps,
                              uint8_t vehicle_state,
                              uint32_t now_ms);
void DtcManager_Report(DtcId_t id, bool failed, uint32_t now_ms);
bool DtcManager_GetRecord(DtcId_t id, DtcRecord_t *record);
uint8_t DtcManager_GetActiveCount(void);
uint8_t DtcManager_GetConfirmedCount(void);
uint32_t DtcManager_GetGeneration(void);
bool DtcManager_RestoreRecord(DtcId_t id, uint8_t status,
                              uint8_t occurrence_count,
                              bool freeze_frame_valid,
                              const DtcFreezeFrame_t *freeze_frame);
bool DtcManager_ClearAll(bool clear_condition_met);

#endif /* DTC_MANAGER_H */
