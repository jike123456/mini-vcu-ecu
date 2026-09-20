#include "nvm_dtc.h"

#include <string.h>

#include "stm32f4xx_hal.h"
#include "iwdg.h"
#include "dtc_manager.h"
#include "log_task.h"

#define NVM_DTC_FLASH_BASE        (0x080E0000UL) /* STM32F407 1 MB sector 11 */
#define NVM_DTC_FLASH_END         (0x08100000UL)
#define NVM_DTC_MAGIC             (0x4B445443UL) /* "KDTC" */
#define NVM_DTC_VERSION           (2u)
#define NVM_DTC_PAYLOAD_SIZE      (171u) /* 9 DTC records x 19 bytes */
#define NVM_DTC_SLOT_SIZE         (192u)
#define NVM_DTC_CRC_OFFSET        (184u)
#define NVM_DTC_COMMIT_OFFSET     (188u)
#define NVM_DTC_COMMIT_WORD       (0xA55AA55AUL)
#define NVM_DTC_SAVE_INTERVAL_MS  (5000u)

static uint32_t s_next_address = NVM_DTC_FLASH_BASE;
static uint32_t s_last_saved_generation;
static uint32_t s_last_save_ms;
static NvMDtcStats_t s_stats;

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8u);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

static int16_t read_i16(const uint8_t *data)
{
    return (int16_t)read_u16(data);
}

static void write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8u);
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8u);
    data[2] = (uint8_t)(value >> 16u);
    data[3] = (uint8_t)(value >> 24u);
}

static void write_i16(uint8_t *data, int16_t value)
{
    write_u16(data, (uint16_t)value);
}

static uint32_t crc32_ieee(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t index;

    for (index = 0u; index < length; index++)
    {
        uint8_t bit;
        crc ^= data[index];
        for (bit = 0u; bit < 8u; bit++)
        {
            crc = ((crc & 1u) != 0u) ? ((crc >> 1u) ^ 0xEDB88320u)
                                     : (crc >> 1u);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static bool slot_valid(const uint8_t *slot)
{
    if (read_u32(&slot[0]) != NVM_DTC_MAGIC) return false;
    if (read_u16(&slot[4]) != NVM_DTC_VERSION) return false;
    if (read_u16(&slot[6]) != NVM_DTC_PAYLOAD_SIZE) return false;
    if (read_u32(&slot[NVM_DTC_COMMIT_OFFSET]) != NVM_DTC_COMMIT_WORD) return false;
    return crc32_ieee(slot, NVM_DTC_CRC_OFFSET) ==
           read_u32(&slot[NVM_DTC_CRC_OFFSET]);
}

static void restore_slot(const uint8_t *slot)
{
    uint32_t index;
    uint32_t offset = 12u;

    for (index = 0u; index < (uint32_t)DTC_ID_COUNT; index++)
    {
        DtcFreezeFrame_t freeze;
        uint32_t expected_code;
        uint32_t stored_code;
        uint8_t status;
        uint8_t occurrence;
        bool freeze_valid;
        DtcRecord_t current;

        stored_code = ((uint32_t)slot[offset] << 16u) |
                      ((uint32_t)slot[offset + 1u] << 8u) |
                      (uint32_t)slot[offset + 2u];
        status = slot[offset + 3u];
        occurrence = slot[offset + 4u];
        freeze_valid = (slot[offset + 5u] != 0u);
        freeze.timestamp_ms = read_u32(&slot[offset + 6u]);
        freeze.vehicle_state = slot[offset + 10u];
        freeze.command_vx_mmps = read_i16(&slot[offset + 11u]);
        freeze.command_wz_mradps = read_i16(&slot[offset + 13u]);
        freeze.left_wheel_mmps = read_i16(&slot[offset + 15u]);
        freeze.right_wheel_mmps = read_i16(&slot[offset + 17u]);

        (void)DtcManager_GetRecord((DtcId_t)index, &current);
        expected_code = current.code;
        if (stored_code == expected_code)
        {
            /* Current-failure bits are re-evaluated in the new operation cycle. */
            status &= (uint8_t)~(DTC_STATUS_TEST_FAILED |
                                 DTC_STATUS_TEST_FAILED_THIS_CYCLE |
                                 DTC_STATUS_PENDING);
            (void)DtcManager_RestoreRecord((DtcId_t)index, status,
                                           occurrence, freeze_valid, &freeze);
        }
        offset += 19u;
    }
}

static void build_slot(uint8_t slot[NVM_DTC_SLOT_SIZE], uint32_t sequence)
{
    uint32_t index;
    uint32_t offset = 12u;

    (void)memset(slot, 0xFF, NVM_DTC_SLOT_SIZE);
    write_u32(&slot[0], NVM_DTC_MAGIC);
    write_u16(&slot[4], NVM_DTC_VERSION);
    write_u16(&slot[6], NVM_DTC_PAYLOAD_SIZE);
    write_u32(&slot[8], sequence);

    for (index = 0u; index < (uint32_t)DTC_ID_COUNT; index++)
    {
        DtcRecord_t record;
        (void)DtcManager_GetRecord((DtcId_t)index, &record);
        slot[offset] = (uint8_t)(record.code >> 16u);
        slot[offset + 1u] = (uint8_t)(record.code >> 8u);
        slot[offset + 2u] = (uint8_t)record.code;
        slot[offset + 3u] = record.status;
        slot[offset + 4u] = record.occurrence_count;
        slot[offset + 5u] = record.freeze_frame_valid ? 1u : 0u;
        write_u32(&slot[offset + 6u], record.freeze_frame.timestamp_ms);
        slot[offset + 10u] = record.freeze_frame.vehicle_state;
        write_i16(&slot[offset + 11u], record.freeze_frame.command_vx_mmps);
        write_i16(&slot[offset + 13u], record.freeze_frame.command_wz_mradps);
        write_i16(&slot[offset + 15u], record.freeze_frame.left_wheel_mmps);
        write_i16(&slot[offset + 17u], record.freeze_frame.right_wheel_mmps);
        offset += 19u;
    }

    write_u32(&slot[NVM_DTC_CRC_OFFSET], crc32_ieee(slot, NVM_DTC_CRC_OFFSET));
    write_u32(&slot[NVM_DTC_COMMIT_OFFSET], NVM_DTC_COMMIT_WORD);
}

static bool program_slot(uint32_t address, const uint8_t slot[NVM_DTC_SLOT_SIZE])
{
    uint32_t offset;
    HAL_StatusTypeDef result = HAL_OK;

    if (HAL_FLASH_Unlock() != HAL_OK) return false;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    /* Program the commit marker last so interrupted writes are rejected. */
    for (offset = 0u; offset < NVM_DTC_COMMIT_OFFSET; offset += 4u)
    {
        uint32_t word = read_u32(&slot[offset]);
        if ((word != 0xFFFFFFFFu) &&
            (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + offset,
                               (uint64_t)word) != HAL_OK))
        {
            result = HAL_ERROR;
            break;
        }
        (void)HAL_IWDG_Refresh(&hiwdg);
    }
    if (result == HAL_OK)
    {
        uint32_t commit = read_u32(&slot[NVM_DTC_COMMIT_OFFSET]);
        result = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                   address + NVM_DTC_COMMIT_OFFSET,
                                   (uint64_t)commit);
    }
    (void)HAL_FLASH_Lock();
    return result == HAL_OK;
}

void NvMDtc_Init(void)
{
    uint32_t address;
    const uint8_t *latest = NULL;

    (void)memset(&s_stats, 0, sizeof(s_stats));
    for (address = NVM_DTC_FLASH_BASE;
         address + NVM_DTC_SLOT_SIZE <= NVM_DTC_FLASH_END;
         address += NVM_DTC_SLOT_SIZE)
    {
        const uint8_t *slot = (const uint8_t *)address;
        if (read_u32(slot) == 0xFFFFFFFFu)
        {
            s_next_address = address;
            break;
        }
        if (slot_valid(slot))
        {
            latest = slot;
            s_stats.latest_sequence = read_u32(&slot[8]);
            s_stats.valid_records++;
        }
        else
        {
            s_stats.crc_errors++;
        }
        s_next_address = address + NVM_DTC_SLOT_SIZE;
    }

    if ((s_next_address + NVM_DTC_SLOT_SIZE) > NVM_DTC_FLASH_END)
    {
        s_stats.storage_full = true;
    }
    if (latest != NULL)
    {
        restore_slot(latest);
        s_stats.restored = true;
    }
    s_last_saved_generation = DtcManager_GetGeneration();
    s_last_save_ms = 0u;
    LOGINFO_T("NVM", "DTC init restored=%u seq=%lu confirmed=%u slots=%lu crc_err=%lu",
              s_stats.restored ? 1u : 0u,
              (unsigned long)s_stats.latest_sequence,
              (unsigned)DtcManager_GetConfirmedCount(),
              (unsigned long)s_stats.valid_records,
              (unsigned long)s_stats.crc_errors);
    if (s_stats.storage_full)
    {
        LOGWARN_T("NVM", "DTC storage full; erase is intentionally inhibited");
    }
}

void NvMDtc_Service(uint32_t now_ms, bool safe_to_write)
{
    uint32_t generation = DtcManager_GetGeneration();
    uint8_t slot[NVM_DTC_SLOT_SIZE];
    uint32_t sequence;

    if (!safe_to_write || s_stats.storage_full) return;
    if (generation == s_last_saved_generation) return;
    if ((now_ms - s_last_save_ms) < NVM_DTC_SAVE_INTERVAL_MS) return;

    sequence = s_stats.latest_sequence + 1u;
    build_slot(slot, sequence);
    if (program_slot(s_next_address, slot))
    {
        s_stats.latest_sequence = sequence;
        s_stats.valid_records++;
        s_next_address += NVM_DTC_SLOT_SIZE;
        s_last_saved_generation = generation;
        s_last_save_ms = now_ms;
        if ((s_next_address + NVM_DTC_SLOT_SIZE) > NVM_DTC_FLASH_END)
        {
            s_stats.storage_full = true;
        }
        LOGINFO_T("NVM", "DTC saved seq=%lu", (unsigned long)sequence);
    }
    else
    {
        s_stats.write_errors++;
        LOGERROR_T("NVM", "DTC write failed addr=0x%08lX",
                   (unsigned long)s_next_address);
    }
}

NvMDtcStats_t NvMDtc_GetStats(void)
{
    return s_stats;
}
