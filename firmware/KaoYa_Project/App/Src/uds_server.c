#include "uds_server.h"

#include <string.h>

#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "dtc_manager.h"
#include "vehicle_state.h"
#include "log_task.h"
#include "sensor_task.h"
#include "fault_manager.h"
#include "boot_layout.h"

#define UDS_SID_DIAGNOSTIC_SESSION_CONTROL     (0x10u)
#define UDS_SID_ECU_RESET                      (0x11u)
#define UDS_SID_CLEAR_DIAGNOSTIC_INFORMATION  (0x14u)
#define UDS_SID_READ_DTC_INFORMATION          (0x19u)
#define UDS_SID_READ_DATA_BY_IDENTIFIER        (0x22u)
#define UDS_SID_ROUTINE_CONTROL                (0x31u)
#define UDS_SID_TESTER_PRESENT                 (0x3Eu)
#define UDS_POSITIVE_RESPONSE_OFFSET          (0x40u)

#define UDS_SUB_REPORT_DTC_BY_STATUS_MASK     (0x02u)
#define UDS_DTC_STATUS_AVAILABILITY_MASK      (0x2Fu)

#define UDS_NRC_INCORRECT_LENGTH              (0x13u)
#define UDS_NRC_CONDITIONS_NOT_CORRECT        (0x22u)
#define UDS_NRC_BUSY_REPEAT_REQUEST            (0x21u)
#define UDS_NRC_REQUEST_OUT_OF_RANGE          (0x31u)
#define UDS_NRC_SERVICE_NOT_SUPPORTED         (0x11u)
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED     (0x12u)
#define UDS_NRC_SUBFUNCTION_NOT_IN_SESSION     (0x7Eu)
#define UDS_NRC_RESPONSE_PENDING               (0x78u)

#define UDS_TX_QUEUE_DEPTH                    (8u)
#define UDS_RESPONSE_MAX_LEN                  (3u + 4u * DTC_ID_COUNT)
#define UDS_FLOW_CONTROL_TIMEOUT_MS           (1000u)
#define UDS_SESSION_TIMEOUT_MS                (5000u)
#define UDS_P2_SERVER_MAX_MS                  (50u)
#define UDS_P2_STAR_SERVER_MAX_10MS           (500u)

#define UDS_DID_RUNTIME_DATA                  (0xF100u)
#define UDS_DID_UPTIME_MS                     (0xF101u)
#define UDS_DID_VECTOR_TABLE                  (0xF102u)
#define UDS_DID_VIN                           (0xF190u)
#define UDS_DID_SOFTWARE_VERSION              (0xF195u)

#define UDS_ROUTINE_ASYNC_SELF_TEST           (0xFF00u)
#define UDS_ROUTINE_DURATION_MS               (250u)
#define UDS_RESET_AFTER_TX_DELAY_MS           (50u)

#define UDS_TX_ACTION_NONE                    (0u)
#define UDS_TX_ACTION_HARD_RESET              (1u)
#define UDS_TX_ACTION_PROGRAMMING_RESET       (2u)

static uint8_t s_tx_queue[UDS_TX_QUEUE_DEPTH][8];
static uint8_t s_tx_action[UDS_TX_QUEUE_DEPTH];
static uint8_t s_tx_head;
static uint8_t s_tx_tail;
static uint8_t s_tx_count;

static uint8_t s_multiframe[UDS_RESPONSE_MAX_LEN];
static uint8_t s_multiframe_len;
static uint8_t s_multiframe_offset;
static uint8_t s_multiframe_sn;
static bool s_waiting_flow_control;
static uint32_t s_flow_control_deadline_ms;
static uint8_t s_session;
static uint32_t s_session_deadline_ms;
static bool s_reset_pending;
static uint32_t s_reset_due_ms;
static bool s_reset_to_bootloader;
static bool s_routine_pending;
static uint32_t s_routine_due_ms;

static void touch_session(uint32_t now_ms)
{
    if (s_session != UDS_SESSION_DEFAULT)
    {
        s_session_deadline_ms = now_ms + UDS_SESSION_TIMEOUT_MS;
    }
}

static bool enqueue_frame_action(const uint8_t frame[8], uint8_t action)
{
    bool queued = false;

    taskENTER_CRITICAL();
    if (s_tx_count < UDS_TX_QUEUE_DEPTH)
    {
        (void)memcpy(s_tx_queue[s_tx_tail], frame, 8u);
        s_tx_action[s_tx_tail] = action;
        s_tx_tail = (uint8_t)((s_tx_tail + 1u) % UDS_TX_QUEUE_DEPTH);
        s_tx_count++;
        queued = true;
    }
    taskEXIT_CRITICAL();
    return queued;
}

static bool enqueue_frame(const uint8_t frame[8])
{
    return enqueue_frame_action(frame, UDS_TX_ACTION_NONE);
}

static bool send_single_frame_action(const uint8_t *payload, uint8_t length,
                                     uint8_t action)
{
    uint8_t frame[8] = {0};

    if ((payload == NULL) || (length > 7u)) return false;
    frame[0] = length;
    (void)memcpy(&frame[1], payload, length);
    return enqueue_frame_action(frame, action);
}

static bool send_single_frame(const uint8_t *payload, uint8_t length)
{
    return send_single_frame_action(payload, length, UDS_TX_ACTION_NONE);
}

static void send_negative_response(uint8_t request_sid, uint8_t nrc)
{
    uint8_t response[3] = {0x7Fu, request_sid, nrc};
    send_single_frame(response, sizeof(response));
}

static void start_response(const uint8_t *payload, uint8_t length,
                           uint32_t now_ms)
{
    uint8_t frame[8] = {0};

    if ((payload == NULL) || (length == 0u) ||
        (length > UDS_RESPONSE_MAX_LEN)) return;

    if (length <= 7u)
    {
        send_single_frame(payload, length);
        return;
    }

    frame[0] = (uint8_t)(0x10u | ((length >> 8u) & 0x0Fu));
    frame[1] = length;
    (void)memcpy(&frame[2], payload, 6u);
    if (enqueue_frame(frame))
    {
        (void)memcpy(s_multiframe, payload, length);
        s_multiframe_len = length;
        s_multiframe_offset = 6u;
        s_multiframe_sn = 1u;
        s_waiting_flow_control = true;
        s_flow_control_deadline_ms = now_ms + UDS_FLOW_CONTROL_TIMEOUT_MS;
    }
}

static void continue_multiframe_after_flow_control(void)
{
    while (s_multiframe_offset < s_multiframe_len)
    {
        uint8_t frame[8] = {0};
        uint8_t remaining = (uint8_t)(s_multiframe_len - s_multiframe_offset);
        uint8_t chunk = (remaining > 7u) ? 7u : remaining;

        frame[0] = (uint8_t)(0x20u | (s_multiframe_sn & 0x0Fu));
        (void)memcpy(&frame[1], &s_multiframe[s_multiframe_offset], chunk);
        if (!enqueue_frame(frame)) return;
        s_multiframe_offset = (uint8_t)(s_multiframe_offset + chunk);
        s_multiframe_sn = (uint8_t)((s_multiframe_sn + 1u) & 0x0Fu);
    }
    s_waiting_flow_control = false;
}

static void handle_read_dtc(const uint8_t *request, uint8_t length,
                            uint32_t now_ms)
{
    uint8_t response[UDS_RESPONSE_MAX_LEN];
    uint8_t response_len = 3u;
    uint8_t status_mask;
    uint32_t index;

    if (length != 3u)
    {
        send_negative_response(UDS_SID_READ_DTC_INFORMATION,
                               UDS_NRC_INCORRECT_LENGTH);
        return;
    }
    if (request[1] != UDS_SUB_REPORT_DTC_BY_STATUS_MASK)
    {
        send_negative_response(UDS_SID_READ_DTC_INFORMATION,
                               UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    status_mask = (uint8_t)(request[2] & UDS_DTC_STATUS_AVAILABILITY_MASK);
    response[0] = UDS_SID_READ_DTC_INFORMATION + UDS_POSITIVE_RESPONSE_OFFSET;
    response[1] = UDS_SUB_REPORT_DTC_BY_STATUS_MASK;
    response[2] = UDS_DTC_STATUS_AVAILABILITY_MASK;

    for (index = 0u; index < (uint32_t)DTC_ID_COUNT; index++)
    {
        DtcRecord_t record;
        if (DtcManager_GetRecord((DtcId_t)index, &record) &&
            ((record.status & status_mask) != 0u))
        {
            response[response_len++] = (uint8_t)(record.code >> 16u);
            response[response_len++] = (uint8_t)(record.code >> 8u);
            response[response_len++] = (uint8_t)record.code;
            response[response_len++] = record.status;
        }
    }
    start_response(response, response_len, now_ms);
}

static void handle_clear_dtc(const uint8_t *request, uint8_t length)
{
    VehicleStateSnapshot_t vehicle;
    uint8_t response = UDS_SID_CLEAR_DIAGNOSTIC_INFORMATION +
                       UDS_POSITIVE_RESPONSE_OFFSET;

    if (length != 4u)
    {
        send_negative_response(UDS_SID_CLEAR_DIAGNOSTIC_INFORMATION,
                               UDS_NRC_INCORRECT_LENGTH);
        return;
    }
    if ((request[1] != 0xFFu) || (request[2] != 0xFFu) ||
        (request[3] != 0xFFu))
    {
        send_negative_response(UDS_SID_CLEAR_DIAGNOSTIC_INFORMATION,
                               UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    vehicle = VehicleState_GetSnapshot();
    if (!DtcManager_ClearAll(vehicle.state != VCU_STATE_DRIVE))
    {
        send_negative_response(UDS_SID_CLEAR_DIAGNOSTIC_INFORMATION,
                               UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }
    send_single_frame(&response, 1u);
}

static void handle_session_control(const uint8_t *request, uint8_t length,
                                   uint32_t now_ms)
{
    uint8_t subfunction;
    uint8_t requested_session;
    bool suppress_response;
    uint8_t response[6];

    if (length != 2u)
    {
        send_negative_response(UDS_SID_DIAGNOSTIC_SESSION_CONTROL,
                               UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    subfunction = request[1];
    suppress_response = (subfunction & 0x80u) != 0u;
    requested_session = (uint8_t)(subfunction & 0x7Fu);
    if ((requested_session != UDS_SESSION_DEFAULT) &&
        (requested_session != UDS_SESSION_PROGRAMMING) &&
        (requested_session != UDS_SESSION_EXTENDED))
    {
        send_negative_response(UDS_SID_DIAGNOSTIC_SESSION_CONTROL,
                               UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    if ((requested_session == UDS_SESSION_PROGRAMMING) &&
        (VehicleState_GetSnapshot().state == VCU_STATE_DRIVE))
    {
        send_negative_response(UDS_SID_DIAGNOSTIC_SESSION_CONTROL,
                               UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    s_session = requested_session;
    s_session_deadline_ms = now_ms + UDS_SESSION_TIMEOUT_MS;
    if (suppress_response) return;

    response[0] = UDS_SID_DIAGNOSTIC_SESSION_CONTROL +
                  UDS_POSITIVE_RESPONSE_OFFSET;
    response[1] = requested_session;
    response[2] = (uint8_t)(UDS_P2_SERVER_MAX_MS >> 8u);
    response[3] = (uint8_t)UDS_P2_SERVER_MAX_MS;
    response[4] = (uint8_t)(UDS_P2_STAR_SERVER_MAX_10MS >> 8u);
    response[5] = (uint8_t)UDS_P2_STAR_SERVER_MAX_10MS;
    send_single_frame(response, sizeof(response));
}

static void handle_tester_present(const uint8_t *request, uint8_t length,
                                  uint32_t now_ms)
{
    uint8_t response[2] = {UDS_SID_TESTER_PRESENT + UDS_POSITIVE_RESPONSE_OFFSET,
                           0x00u};

    if (length != 2u)
    {
        send_negative_response(UDS_SID_TESTER_PRESENT,
                               UDS_NRC_INCORRECT_LENGTH);
        return;
    }
    if ((request[1] & 0x7Fu) != 0u)
    {
        send_negative_response(UDS_SID_TESTER_PRESENT,
                               UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    touch_session(now_ms);
    if ((request[1] & 0x80u) == 0u)
    {
        send_single_frame(response, sizeof(response));
    }
}

static void handle_read_data_by_identifier(const uint8_t *request,
                                           uint8_t length,
                                           uint32_t now_ms)
{
    static const uint8_t vin[17] = "KAOYA-F407-ECU001";
    static const uint8_t sw_version[] = "VCU-0.6.0";
    uint8_t response[UDS_RESPONSE_MAX_LEN];
    uint8_t response_len = 3u;
    uint16_t did;

    if (length != 3u)
    {
        send_negative_response(UDS_SID_READ_DATA_BY_IDENTIFIER,
                               UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    did = (uint16_t)(((uint16_t)request[1] << 8u) | request[2]);
    response[0] = UDS_SID_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_OFFSET;
    response[1] = request[1];
    response[2] = request[2];

    if (did == UDS_DID_VIN)
    {
        (void)memcpy(&response[response_len], vin, sizeof(vin));
        response_len = (uint8_t)(response_len + sizeof(vin));
    }
    else if (did == UDS_DID_SOFTWARE_VERSION)
    {
        (void)memcpy(&response[response_len], sw_version, sizeof(sw_version) - 1u);
        response_len = (uint8_t)(response_len + sizeof(sw_version) - 1u);
    }
    else if (did == UDS_DID_RUNTIME_DATA)
    {
        proto_status_t sensor;
        VehicleStateSnapshot_t vehicle = VehicleState_GetSnapshot();
        SensorSnap_Get(&sensor);
        response[response_len++] = (uint8_t)vehicle.state;
        response[response_len++] = vehicle.fault_flags;
        response[response_len++] = (uint8_t)(sensor.battery_mv >> 8u);
        response[response_len++] = (uint8_t)sensor.battery_mv;
        response[response_len++] = DtcManager_GetActiveCount();
        response[response_len++] = DtcManager_GetConfirmedCount();
        response[response_len++] = FaultManager_GetInjectionMask();
        response[response_len++] = s_session;
    }
    else if (did == UDS_DID_UPTIME_MS)
    {
        uint32_t uptime_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        response[response_len++] = (uint8_t)(uptime_ms >> 24u);
        response[response_len++] = (uint8_t)(uptime_ms >> 16u);
        response[response_len++] = (uint8_t)(uptime_ms >> 8u);
        response[response_len++] = (uint8_t)uptime_ms;
    }
    else if (did == UDS_DID_VECTOR_TABLE)
    {
        uint32_t vector_table = SCB->VTOR;
        response[response_len++] = (uint8_t)(vector_table >> 24u);
        response[response_len++] = (uint8_t)(vector_table >> 16u);
        response[response_len++] = (uint8_t)(vector_table >> 8u);
        response[response_len++] = (uint8_t)vector_table;
    }
    else
    {
        send_negative_response(UDS_SID_READ_DATA_BY_IDENTIFIER,
                               UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }
    start_response(response, response_len, now_ms);
}

static void handle_ecu_reset(const uint8_t *request, uint8_t length,
                             uint32_t now_ms)
{
    uint8_t subfunction;
    bool suppress_response;
    VehicleStateSnapshot_t vehicle;
    uint8_t response[2];

    if (length != 2u)
    {
        send_negative_response(UDS_SID_ECU_RESET, UDS_NRC_INCORRECT_LENGTH);
        return;
    }
    subfunction = (uint8_t)(request[1] & 0x7Fu);
    suppress_response = (request[1] & 0x80u) != 0u;
    if (subfunction != 0x01u)
    {
        send_negative_response(UDS_SID_ECU_RESET,
                               UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }
    if ((s_session != UDS_SESSION_EXTENDED) &&
        (s_session != UDS_SESSION_PROGRAMMING))
    {
        send_negative_response(UDS_SID_ECU_RESET,
                               UDS_NRC_SUBFUNCTION_NOT_IN_SESSION);
        return;
    }
    vehicle = VehicleState_GetSnapshot();
    if (vehicle.state == VCU_STATE_DRIVE)
    {
        send_negative_response(UDS_SID_ECU_RESET,
                               UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    if (suppress_response)
    {
        s_reset_to_bootloader = (s_session == UDS_SESSION_PROGRAMMING);
        s_reset_pending = true;
        s_reset_due_ms = now_ms + UDS_RESET_AFTER_TX_DELAY_MS;
        return;
    }
    response[0] = UDS_SID_ECU_RESET + UDS_POSITIVE_RESPONSE_OFFSET;
    response[1] = subfunction;
    (void)send_single_frame_action(response, sizeof(response),
                                   (s_session == UDS_SESSION_PROGRAMMING) ?
                                   UDS_TX_ACTION_PROGRAMMING_RESET :
                                   UDS_TX_ACTION_HARD_RESET);
}

static void handle_routine_control(const uint8_t *request, uint8_t length,
                                   uint32_t now_ms)
{
    uint16_t routine_id;
    uint8_t pending[3] = {0x7Fu, UDS_SID_ROUTINE_CONTROL,
                          UDS_NRC_RESPONSE_PENDING};

    if (length != 4u)
    {
        send_negative_response(UDS_SID_ROUTINE_CONTROL,
                               UDS_NRC_INCORRECT_LENGTH);
        return;
    }
    if (request[1] != 0x01u)
    {
        send_negative_response(UDS_SID_ROUTINE_CONTROL,
                               UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }
    if (s_session != UDS_SESSION_EXTENDED)
    {
        send_negative_response(UDS_SID_ROUTINE_CONTROL,
                               UDS_NRC_SUBFUNCTION_NOT_IN_SESSION);
        return;
    }
    if (s_routine_pending)
    {
        send_negative_response(UDS_SID_ROUTINE_CONTROL,
                               UDS_NRC_BUSY_REPEAT_REQUEST);
        return;
    }
    routine_id = (uint16_t)(((uint16_t)request[2] << 8u) | request[3]);
    if (routine_id != UDS_ROUTINE_ASYNC_SELF_TEST)
    {
        send_negative_response(UDS_SID_ROUTINE_CONTROL,
                               UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }
    if (VehicleState_GetSnapshot().state == VCU_STATE_DRIVE)
    {
        send_negative_response(UDS_SID_ROUTINE_CONTROL,
                               UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    if (send_single_frame(pending, sizeof(pending)))
    {
        s_routine_pending = true;
        s_routine_due_ms = now_ms + UDS_ROUTINE_DURATION_MS;
    }
}

void UdsServer_Init(void)
{
    taskENTER_CRITICAL();
    (void)memset(s_tx_queue, 0, sizeof(s_tx_queue));
    (void)memset(s_tx_action, 0, sizeof(s_tx_action));
    s_tx_head = 0u;
    s_tx_tail = 0u;
    s_tx_count = 0u;
    s_multiframe_len = 0u;
    s_multiframe_offset = 0u;
    s_multiframe_sn = 0u;
    s_waiting_flow_control = false;
    s_flow_control_deadline_ms = 0u;
    s_session = UDS_SESSION_DEFAULT;
    s_session_deadline_ms = 0u;
    s_reset_pending = false;
    s_reset_due_ms = 0u;
    s_reset_to_bootloader = false;
    s_routine_pending = false;
    s_routine_due_ms = 0u;
    taskEXIT_CRITICAL();
}

void UdsServer_ProcessCanFrame(const uint8_t data[8], uint8_t dlc,
                               uint32_t now_ms)
{
    uint8_t pci_type;
    uint8_t length;
    const uint8_t *request;

    if ((data == NULL) || (dlc != 8u)) return;
    pci_type = (uint8_t)(data[0] & 0xF0u);

    if (pci_type == 0x30u)
    {
        if (s_waiting_flow_control && ((data[0] & 0x0Fu) == 0u))
        {
            continue_multiframe_after_flow_control();
        }
        return;
    }
    if (pci_type != 0x00u) return;

    length = (uint8_t)(data[0] & 0x0Fu);
    if ((length == 0u) || (length > 7u)) return;
    request = &data[1];
    touch_session(now_ms);

    if (request[0] == UDS_SID_DIAGNOSTIC_SESSION_CONTROL)
    {
        handle_session_control(request, length, now_ms);
    }
    else if (request[0] == UDS_SID_ECU_RESET)
    {
        handle_ecu_reset(request, length, now_ms);
    }
    else if (request[0] == UDS_SID_READ_DTC_INFORMATION)
    {
        handle_read_dtc(request, length, now_ms);
    }
    else if (request[0] == UDS_SID_CLEAR_DIAGNOSTIC_INFORMATION)
    {
        handle_clear_dtc(request, length);
    }
    else if (request[0] == UDS_SID_READ_DATA_BY_IDENTIFIER)
    {
        handle_read_data_by_identifier(request, length, now_ms);
    }
    else if (request[0] == UDS_SID_ROUTINE_CONTROL)
    {
        handle_routine_control(request, length, now_ms);
    }
    else if (request[0] == UDS_SID_TESTER_PRESENT)
    {
        handle_tester_present(request, length, now_ms);
    }
    else
    {
        send_negative_response(request[0], UDS_NRC_SERVICE_NOT_SUPPORTED);
    }
}

bool UdsServer_PeekResponse(uint8_t data[8])
{
    bool available = false;
    if (data == NULL) return false;

    taskENTER_CRITICAL();
    if (s_tx_count > 0u)
    {
        (void)memcpy(data, s_tx_queue[s_tx_head], 8u);
        available = true;
    }
    taskEXIT_CRITICAL();
    return available;
}

void UdsServer_ConfirmResponseSent(uint32_t now_ms)
{
    uint8_t action = UDS_TX_ACTION_NONE;
    taskENTER_CRITICAL();
    if (s_tx_count > 0u)
    {
        action = s_tx_action[s_tx_head];
        s_tx_action[s_tx_head] = UDS_TX_ACTION_NONE;
        s_tx_head = (uint8_t)((s_tx_head + 1u) % UDS_TX_QUEUE_DEPTH);
        s_tx_count--;
    }
    taskEXIT_CRITICAL();
    if ((action == UDS_TX_ACTION_HARD_RESET) ||
        (action == UDS_TX_ACTION_PROGRAMMING_RESET))
    {
        s_reset_to_bootloader =
            (action == UDS_TX_ACTION_PROGRAMMING_RESET);
        s_reset_pending = true;
        s_reset_due_ms = now_ms + UDS_RESET_AFTER_TX_DELAY_MS;
    }
}

void UdsServer_Service(uint32_t now_ms)
{
    if (s_waiting_flow_control &&
        ((int32_t)(now_ms - s_flow_control_deadline_ms) >= 0))
    {
        s_waiting_flow_control = false;
        LOGWARN_T("UDS", "ISO-TP flow-control timeout");
    }
    if ((s_session != UDS_SESSION_DEFAULT) &&
        ((int32_t)(now_ms - s_session_deadline_ms) >= 0))
    {
        s_session = UDS_SESSION_DEFAULT;
        LOGINFO_T("UDS", "session timeout -> default");
    }
    if (s_routine_pending &&
        ((int32_t)(now_ms - s_routine_due_ms) >= 0))
    {
        uint8_t response[5] = {UDS_SID_ROUTINE_CONTROL + UDS_POSITIVE_RESPONSE_OFFSET,
                               0x01u, 0xFFu, 0x00u, 0x00u};
        if (send_single_frame(response, sizeof(response)))
        {
            s_routine_pending = false;
        }
    }
    if (s_reset_pending && ((int32_t)(now_ms - s_reset_due_ms) >= 0))
    {
        if (s_reset_to_bootloader)
        {
            *((volatile uint32_t *)BOOT_REQUEST_ADDRESS) = BOOT_REQUEST_MAGIC;
            __DSB();
        }
        __disable_irq();
        NVIC_SystemReset();
    }
}

uint8_t UdsServer_GetSession(void)
{
    return s_session;
}
