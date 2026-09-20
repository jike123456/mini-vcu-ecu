#include "dtc_manager.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "log_task.h"

#define DTC_CONFIRM_THRESHOLD  (3u)
#define DTC_HEAL_THRESHOLD     (10u)

static DtcRecord_t s_records[DTC_ID_COUNT];
static DtcFreezeFrame_t s_latest_context;
static uint32_t s_generation;

static uint32_t code_for_id(DtcId_t id)
{
    static const uint32_t codes[DTC_ID_COUNT] = {
        DTC_CODE_COMMAND_TIMEOUT,
        DTC_CODE_COMMAND_CRC,
        DTC_CODE_COMMAND_COUNTER,
        DTC_CODE_COMMAND_VALUE,
        DTC_CODE_BATTERY_UNDERVOLTAGE,
        DTC_CODE_ENCODER_PLAUSIBILITY,
        DTC_CODE_IMU_COMMUNICATION,
        DTC_CODE_TASK_MONITOR,
        DTC_CODE_CAN_CONTROLLER
    };
    return ((uint32_t)id < (uint32_t)DTC_ID_COUNT) ? codes[id] : 0u;
}

void DtcManager_Init(void)
{
    uint32_t index;

    taskENTER_CRITICAL();
    (void)memset(s_records, 0, sizeof(s_records));
    (void)memset(&s_latest_context, 0, sizeof(s_latest_context));
    s_generation = 0u;
    for (index = 0u; index < (uint32_t)DTC_ID_COUNT; index++)
    {
        s_records[index].code = code_for_id((DtcId_t)index);
    }
    taskEXIT_CRITICAL();
}

void DtcManager_UpdateContext(int16_t command_vx_mmps,
                              int16_t command_wz_mradps,
                              int16_t left_wheel_mmps,
                              int16_t right_wheel_mmps,
                              uint8_t vehicle_state,
                              uint32_t now_ms)
{
    taskENTER_CRITICAL();
    s_latest_context.timestamp_ms = now_ms;
    s_latest_context.vehicle_state = vehicle_state;
    s_latest_context.command_vx_mmps = command_vx_mmps;
    s_latest_context.command_wz_mradps = command_wz_mradps;
    s_latest_context.left_wheel_mmps = left_wheel_mmps;
    s_latest_context.right_wheel_mmps = right_wheel_mmps;
    taskEXIT_CRITICAL();
}

void DtcManager_Report(DtcId_t id, bool failed, uint32_t now_ms)
{
    DtcRecord_t *record;
    bool newly_confirmed = false;
    uint32_t confirmed_code = 0u;
    uint8_t old_status;
    uint8_t old_occurrence;
    bool old_freeze_valid;

    if ((uint32_t)id >= (uint32_t)DTC_ID_COUNT) return;

    taskENTER_CRITICAL();
    record = &s_records[id];
    old_status = record->status;
    old_occurrence = record->occurrence_count;
    old_freeze_valid = record->freeze_frame_valid;
    if (failed)
    {
        record->pass_count = 0u;
        if (record->failure_count < DTC_CONFIRM_THRESHOLD)
        {
            record->failure_count++;
        }
        record->status |= DTC_STATUS_TEST_FAILED |
                          DTC_STATUS_TEST_FAILED_THIS_CYCLE |
                          DTC_STATUS_TEST_FAILED_SINCE_CLEAR;

        if (record->failure_count >= DTC_CONFIRM_THRESHOLD)
        {
            if ((record->status & DTC_STATUS_CONFIRMED) == 0u)
            {
                newly_confirmed = true;
                confirmed_code = record->code;
                if (record->occurrence_count < 0xFFu)
                {
                    record->occurrence_count++;
                }
                s_latest_context.timestamp_ms = now_ms;
                record->freeze_frame = s_latest_context;
                record->freeze_frame_valid = true;
            }
            record->status |= DTC_STATUS_PENDING | DTC_STATUS_CONFIRMED;
        }
    }
    else
    {
        record->failure_count = 0u;
        if ((record->status & DTC_STATUS_TEST_FAILED) != 0u)
        {
            if (record->pass_count < DTC_HEAL_THRESHOLD)
            {
                record->pass_count++;
            }
            if (record->pass_count >= DTC_HEAL_THRESHOLD)
            {
                record->status &= (uint8_t)~(DTC_STATUS_TEST_FAILED |
                                             DTC_STATUS_PENDING);
            }
        }
        else
        {
            record->pass_count = 0u;
        }
    }
    if ((record->status != old_status) ||
        (record->occurrence_count != old_occurrence) ||
        (record->freeze_frame_valid != old_freeze_valid))
    {
        s_generation++;
    }
    taskEXIT_CRITICAL();

    if (newly_confirmed)
    {
        LOGWARN_T("DTC", "confirmed code=0x%06lX", (unsigned long)confirmed_code);
    }
}

bool DtcManager_GetRecord(DtcId_t id, DtcRecord_t *record)
{
    if (((uint32_t)id >= (uint32_t)DTC_ID_COUNT) || (record == NULL)) return false;

    taskENTER_CRITICAL();
    *record = s_records[id];
    taskEXIT_CRITICAL();
    return true;
}

uint8_t DtcManager_GetActiveCount(void)
{
    uint8_t count = 0u;
    uint32_t index;

    taskENTER_CRITICAL();
    for (index = 0u; index < (uint32_t)DTC_ID_COUNT; index++)
    {
        uint8_t status = s_records[index].status;
        if (((status & DTC_STATUS_CONFIRMED) != 0u) &&
            ((status & DTC_STATUS_TEST_FAILED) != 0u))
        {
            count++;
        }
    }
    taskEXIT_CRITICAL();
    return count;
}

uint8_t DtcManager_GetConfirmedCount(void)
{
    uint8_t count = 0u;
    uint32_t index;

    taskENTER_CRITICAL();
    for (index = 0u; index < (uint32_t)DTC_ID_COUNT; index++)
    {
        if ((s_records[index].status & DTC_STATUS_CONFIRMED) != 0u)
        {
            count++;
        }
    }
    taskEXIT_CRITICAL();
    return count;
}

uint32_t DtcManager_GetGeneration(void)
{
    uint32_t generation;

    taskENTER_CRITICAL();
    generation = s_generation;
    taskEXIT_CRITICAL();
    return generation;
}

bool DtcManager_RestoreRecord(DtcId_t id, uint8_t status,
                              uint8_t occurrence_count,
                              bool freeze_frame_valid,
                              const DtcFreezeFrame_t *freeze_frame)
{
    DtcRecord_t *record;

    if (((uint32_t)id >= (uint32_t)DTC_ID_COUNT) ||
        (freeze_frame_valid && (freeze_frame == NULL)))
    {
        return false;
    }

    taskENTER_CRITICAL();
    record = &s_records[id];
    record->status = status;
    record->occurrence_count = occurrence_count;
    record->failure_count = 0u;
    record->pass_count = 0u;
    record->freeze_frame_valid = freeze_frame_valid;
    if (freeze_frame_valid)
    {
        record->freeze_frame = *freeze_frame;
    }
    else
    {
        (void)memset(&record->freeze_frame, 0, sizeof(record->freeze_frame));
    }
    s_generation++;
    taskEXIT_CRITICAL();
    return true;
}

bool DtcManager_ClearAll(bool clear_condition_met)
{
    uint32_t index;

    if (!clear_condition_met) return false;

    taskENTER_CRITICAL();
    for (index = 0u; index < (uint32_t)DTC_ID_COUNT; index++)
    {
        uint32_t code = s_records[index].code;
        (void)memset(&s_records[index], 0, sizeof(s_records[index]));
        s_records[index].code = code;
    }
    s_generation++;
    taskEXIT_CRITICAL();
    LOGINFO_T("DTC", "all records cleared");
    return true;
}
