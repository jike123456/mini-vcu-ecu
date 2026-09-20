#include "can_task.h"

#include "can_bus.h"
#include "cmsis_os.h"
#include "log_task.h"
#include "monitor_task.h"
#include "fault_manager.h"

#include <string.h>

#define CAN_TEST_REQUEST_ID       0x5A0U
#define CAN_TEST_RESPONSE_ID      0x5A1U
#define CAN_SELF_TEST_PERIOD_MS   1000U
#define CAN_SELF_TEST_TIMEOUT_MS  500U

static CanSelfTestStats_t s_self_test;
static uint8_t s_consecutive_failures;

static bool prvFrameMatches(const CanBusFrame_t *frame, const uint8_t *expected)
{
#if CAN_BUS_INTERNAL_LOOPBACK
    const uint16_t expected_id = CAN_TEST_REQUEST_ID;
#else
    const uint16_t expected_id = CAN_TEST_RESPONSE_ID;
#endif

    return (frame->std_id == expected_id) &&
           (frame->dlc == 8U) &&
           (memcmp(frame->data, expected, 8U) == 0);
}

bool KaoYaApp_CanInit(void)
{
    CanBusStats_t bus_stats;

    memset(&s_self_test, 0, sizeof(s_self_test));
    s_consecutive_failures = 0u;
    s_self_test.initialized = CanBus_Init();
    CanBus_GetStats(&bus_stats);

    if (s_self_test.initialized) {
#if CAN_BUS_INTERNAL_LOOPBACK
        LOGINFO_T("CAN", "CAN1 500k loopback ready TSR=0x%08X",
                  (unsigned int)bus_stats.tsr);
#else
        LOGINFO_T("CAN", "CAN1 500k physical ready req=0x5A0 rsp=0x5A1");
#endif
    } else {
        LOGERROR_T("CAN", "init failed step=%u MCR=0x%08X MSR=0x%08X TSR=0x%08X",
                   (unsigned int)bus_stats.init_status,
                   (unsigned int)bus_stats.mcr,
                   (unsigned int)bus_stats.msr,
                   (unsigned int)bus_stats.tsr);
    }
    return s_self_test.initialized;
}

void KaoYaApp_CanTask(void *argument)
{
    uint8_t sequence = 0U;
    (void)argument;

    osDelay(500U);

    for (;;) {
        uint8_t tx_data[8];
        CanBusFrame_t rx_frame;
        bool matched = false;

        MON_HB_CAN();

        /* Retry a failed peripheral initialization every five seconds. */
        if (!s_self_test.initialized && ((sequence % 5U) == 0U)) {
            (void)KaoYaApp_CanInit();
        }

        tx_data[0] = 0xECU;
        tx_data[1] = 0x55U;
        tx_data[2] = sequence;
        tx_data[3] = (uint8_t)(~sequence);
        tx_data[4] = 0x50U;
        tx_data[5] = 0x00U;
        tx_data[6] = 0x07U;
        tx_data[7] = 0xE1U;

        if (s_self_test.initialized && CanBus_SendStd(CAN_TEST_REQUEST_ID, tx_data, 8U)) {
            uint32_t start_tick = osKernelGetTickCount();
            do {
                if (!CanBus_Receive(&rx_frame, CAN_SELF_TEST_TIMEOUT_MS)) {
                    break;
                }
                matched = prvFrameMatches(&rx_frame, tx_data);
            } while (!matched &&
                     ((osKernelGetTickCount() - start_tick) < CAN_SELF_TEST_TIMEOUT_MS));
        }

        if (matched) {
            s_consecutive_failures = 0u;
            s_self_test.pass_count++;
            s_self_test.last_sequence = sequence;
            if ((s_self_test.pass_count % 10U) == 1U) {
#if CAN_BUS_INTERNAL_LOOPBACK
                LOGINFO_T("CAN", "loopback PASS seq=%u total=%u",
                          (unsigned int)sequence,
                          (unsigned int)s_self_test.pass_count);
#else
                LOGINFO_T("CAN", "peer PASS seq=%u total=%u",
                          (unsigned int)sequence,
                          (unsigned int)s_self_test.pass_count);
#endif
            }
        } else if ((s_self_test.fail_count % 10U) == 0U) {
            CanBusStats_t bus_stats;
            CanBus_GetStats(&bus_stats);
#if CAN_BUS_INTERNAL_LOOPBACK
            LOGERROR_T("CAN", "loopback FAIL seq=%u init=%u tx=%u/%u rx=%u err=%u ESR=%08X",
#else
            LOGERROR_T("CAN", "peer TIMEOUT seq=%u init=%u tx=%u/%u rx=%u err=%u ESR=%08X",
#endif
                       (unsigned int)sequence,
                       (unsigned int)bus_stats.init_status,
                       (unsigned int)bus_stats.tx_ok,
                       (unsigned int)bus_stats.tx_fail,
                       (unsigned int)bus_stats.rx_ok,
                       (unsigned int)bus_stats.error_irq,
                       (unsigned int)bus_stats.last_esr);
        }

        if (!matched) {
            s_self_test.fail_count++;
            if (s_consecutive_failures < 0xFFu) s_consecutive_failures++;
        }
        FaultManager_SetObserved(FAULT_INJECT_CAN,
                                 (!s_self_test.initialized ||
                                  (s_consecutive_failures >= 3u)) ? 1u : 0u);

        sequence++;
        osDelay(CAN_SELF_TEST_PERIOD_MS);
    }
}

void CanTask_GetSelfTestStats(CanSelfTestStats_t *stats)
{
    if (stats != NULL) {
        CanBusStats_t bus_stats;

        CanBus_GetStats(&bus_stats);
        *stats = s_self_test;
        stats->init_status = bus_stats.init_status;
        stats->bus_tx_ok = bus_stats.tx_ok;
        stats->bus_tx_fail = bus_stats.tx_fail;
        stats->bus_rx_ok = bus_stats.rx_ok;
        stats->bus_rx_drop = bus_stats.rx_drop;
        stats->mcr = bus_stats.mcr;
        stats->msr = bus_stats.msr;
        stats->tsr = bus_stats.tsr;
        stats->esr = bus_stats.last_esr;
    }
}
