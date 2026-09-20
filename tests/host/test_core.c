/* Runs unmodified production translation units; only hardware/RTOS are stubs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vehicle_can.h"
#include "vehicle_state.h"
#include "dtc_manager.h"
#include "uds_server.h"
#include "pid.h"
#include "sensor_task.h"
#include "fault_manager.h"
#include "task.h"
#include "stm32f4xx.h"
#include "boot_layout.h"

static unsigned checks, critical_depth, resets;
static uint32_t ticks;
HostScb host_scb = {0x08020000u};
uint32_t host_boot_request;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
void Host_EnterCritical(void) { critical_depth++; }
void Host_ExitCritical(void) { CHECK(critical_depth > 0); critical_depth--; }
TickType_t xTaskGetTickCount(void) { return ticks; }
void Host_Log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
void NVIC_SystemReset(void) { resets++; }
void SensorSnap_Get(proto_status_t *out) { memset(out, 0, sizeof(*out)); out->battery_mv = 3300; }
uint8_t FaultManager_GetInjectionMask(void) { return 0x12; }

static void fix_crc(uint8_t f[8]) { f[7] = VehicleCan_Crc8SaeJ1850(f, 7); }
static void test_can(void)
{
    VehicleCanRxContext_t c = {0};
    VehicleCanCommand_t cmd = {0}, saved;
    uint8_t f[8] = {0x2c, 1, 0x88, 0xff, 1, 1, 15, 0};
    CHECK(VehicleCan_Crc8SaeJ1850((const uint8_t *)"123456789", 9) == 0x4b);
    CHECK(VehicleCan_Crc8SaeJ1850(NULL, 2) == 0);
    CHECK(VehicleCan_Crc8SaeJ1850(f, 0) == 0);
    fix_crc(f);
    CHECK(VehicleCan_DecodeCommand(NULL, f, 8, &cmd) == VEHICLE_CAN_DECODE_LENGTH);
    CHECK(VehicleCan_DecodeCommand(&c, NULL, 8, &cmd) == VEHICLE_CAN_DECODE_LENGTH);
    CHECK(VehicleCan_DecodeCommand(&c, f, 8, NULL) == VEHICLE_CAN_DECODE_LENGTH);
    CHECK(VehicleCan_DecodeCommand(&c, f, 7, &cmd) == VEHICLE_CAN_DECODE_LENGTH);
    CHECK(VehicleCan_DecodeCommand(&c, f, 8, &cmd) == VEHICLE_CAN_DECODE_OK);
    CHECK(cmd.target_vx_mmps == 300 && cmd.target_wz_mradps == -120);
    CHECK(cmd.ignition == 1 && cmd.gear == 1 && cmd.alive_counter == 15);
    f[6] = 0; fix_crc(f);
    CHECK(VehicleCan_DecodeCommand(&c, f, 8, &cmd) == VEHICLE_CAN_DECODE_OK);
    saved = cmd;
    CHECK(VehicleCan_DecodeCommand(&c, f, 8, &cmd) == VEHICLE_CAN_DECODE_COUNTER);
    CHECK(memcmp(&saved, &cmd, sizeof(cmd)) == 0);
    f[0] ^= 1;
    CHECK(VehicleCan_DecodeCommand(&c, f, 8, &cmd) == VEHICLE_CAN_DECODE_CRC);
    CHECK(memcmp(&saved, &cmd, sizeof(cmd)) == 0);
    f[6] = 0x10; fix_crc(f);
    CHECK(VehicleCan_DecodeCommand(&c, f, 8, &cmd) == VEHICLE_CAN_DECODE_VALUE);
    f[6] = 1; f[4] = 2; fix_crc(f);
    CHECK(VehicleCan_DecodeCommand(&c, f, 8, &cmd) == VEHICLE_CAN_DECODE_VALUE);
    f[6] = 2; f[4] = 1; f[5] = 3; fix_crc(f);
    CHECK(VehicleCan_DecodeCommand(&c, f, 8, &cmd) == VEHICLE_CAN_DECODE_VALUE);
    CHECK(memcmp(&saved, &cmd, sizeof(cmd)) == 0);
    f[6] = 3; f[5] = 2; fix_crc(f);
    CHECK(VehicleCan_DecodeCommand(&c, f, 8, &cmd) == VEHICLE_CAN_DECODE_OK);
    CHECK(c.accepted == 3 && c.length_errors == 3 && c.crc_errors == 1 && c.counter_errors == 1 && c.value_errors == 3);
    VehicleCan_EncodeMotionStatus(-32768, 32767, 3, 0x80, 0xff, f);
    CHECK(f[0] == 0 && f[1] == 0x80 && f[2] == 0xff && f[3] == 0x7f);
    CHECK(f[4] == 3 && f[5] == 0x80 && f[6] == 15 && f[7] == VehicleCan_Crc8SaeJ1850(f, 7));
    VehicleCan_EncodeMotionStatus(0, 0, 0, 0, 0, NULL);
    puts("CAN CRC / counter / malformed frames: PASS");
}

static void state_is(VehicleStateId_t state) { CHECK(VehicleState_GetSnapshot().state == state); }
static void test_state(void)
{
    VehicleState_Init(10); state_is(VCU_STATE_STANDBY);
    CHECK(!VehicleState_DriveAllowed());
    VehicleState_OnValidCommand(false, 11);
    CHECK(VehicleState_GetSnapshot().transition_count == 1);
    VehicleState_OnValidCommand(true, 20); state_is(VCU_STATE_DRIVE);
    CHECK(VehicleState_DriveAllowed());
    CHECK(VehicleState_GetSnapshot().last_transition_ms == 20);
    VehicleState_OnCommandTimeout(120); state_is(VCU_STATE_SAFE_STOP);
    CHECK(!VehicleState_DriveAllowed());
    VehicleState_OnValidCommand(true, 121); state_is(VCU_STATE_DRIVE);
    VehicleState_SetSafetyFault(true, 130); state_is(VCU_STATE_SAFE_STOP);
    VehicleState_OnValidCommand(true, 131); state_is(VCU_STATE_SAFE_STOP);
    VehicleState_SetFatalFault(true, 140); state_is(VCU_STATE_FAULT);
    VehicleState_OnValidCommand(true, 141); state_is(VCU_STATE_FAULT);
    VehicleState_OnCommandTimeout(142); state_is(VCU_STATE_FAULT);
    VehicleState_SetSafetyFault(true, 143); state_is(VCU_STATE_FAULT);
    VehicleState_SetSafetyFault(false, 144); state_is(VCU_STATE_FAULT);
    VehicleState_SetFatalFault(false, 145); state_is(VCU_STATE_SAFE_STOP);
    VehicleState_SetFatalFault(false, 146); state_is(VCU_STATE_SAFE_STOP);
    VehicleState_SetSafetyFault(true, 147);
    VehicleState_SetSafetyFault(false, 148); state_is(VCU_STATE_SAFE_STOP);
    VehicleState_OnValidCommand(false, 150); state_is(VCU_STATE_STANDBY);
    VehicleState_SetSafetyFault(false, 151); state_is(VCU_STATE_STANDBY);
    VehicleState_SetSafetyFault(true, 152);
    VehicleState_SetSafetyFault(false, 153); state_is(VCU_STATE_STANDBY);
    VehicleState_SetFatalFault(true, 154);
    VehicleState_SetFatalFault(false, 155); state_is(VCU_STATE_STANDBY);
    VehicleState_SetSafetyFault(true, 156);
    VehicleState_SetFatalFault(true, 157);
    VehicleState_SetFatalFault(false, 158); state_is(VCU_STATE_SAFE_STOP);
    puts("State transitions / fatal priority / recovery: PASS");
}

static void test_dtc(void)
{
    DtcRecord_t r;
    DtcFreezeFrame_t ff = {0};
    DtcManager_Init();
    CHECK(DtcManager_GetGeneration() == 0 && DtcManager_GetActiveCount() == 0);
    CHECK(!DtcManager_GetRecord(DTC_ID_COUNT, &r));
    CHECK(!DtcManager_GetRecord(DTC_ID_COMMAND_TIMEOUT, NULL));
    DtcManager_Report(DTC_ID_COUNT, true, 0);
    CHECK(DtcManager_GetGeneration() == 0);
    DtcManager_UpdateContext(300, -20, 250, 260, 2, 100);
    for (unsigned i = 1; i <= 2; i++) DtcManager_Report(DTC_ID_COMMAND_TIMEOUT, true, 100+i);
    CHECK(DtcManager_GetConfirmedCount() == 0);
    DtcManager_Report(DTC_ID_COMMAND_TIMEOUT, true, 103);
    CHECK(DtcManager_GetConfirmedCount() == 1 && DtcManager_GetActiveCount() == 1);
    CHECK(DtcManager_GetRecord(DTC_ID_COMMAND_TIMEOUT, &r));
    CHECK(r.freeze_frame_valid && r.freeze_frame.timestamp_ms == 103 && r.freeze_frame.command_vx_mmps == 300);
    CHECK(r.occurrence_count == 1 && r.status == 0x2f);
    uint32_t generation = DtcManager_GetGeneration();
    DtcManager_Report(DTC_ID_COMMAND_TIMEOUT, true, 104);
    CHECK(DtcManager_GetGeneration() == generation);
    for (unsigned i = 0; i < 9; i++) DtcManager_Report(DTC_ID_COMMAND_TIMEOUT, false, 200+i);
    CHECK(DtcManager_GetActiveCount() == 1);
    DtcManager_Report(DTC_ID_COMMAND_TIMEOUT, false, 209);
    CHECK(DtcManager_GetActiveCount() == 0 && DtcManager_GetConfirmedCount() == 1);
    DtcManager_Report(DTC_ID_COMMAND_TIMEOUT, false, 210);
    CHECK(!DtcManager_ClearAll(false) && DtcManager_GetConfirmedCount() == 1);
    CHECK(DtcManager_ClearAll(true) && DtcManager_GetConfirmedCount() == 0);
    CHECK(DtcManager_GetRecord(DTC_ID_COMMAND_TIMEOUT, &r) && r.code == DTC_CODE_COMMAND_TIMEOUT);
    CHECK(!DtcManager_RestoreRecord(DTC_ID_COUNT, 0, 0, false, NULL));
    CHECK(!DtcManager_RestoreRecord(DTC_ID_COMMAND_TIMEOUT, 0, 0, true, NULL));
    ff.timestamp_ms = 321;
    CHECK(DtcManager_RestoreRecord(DTC_ID_COMMAND_TIMEOUT, 0x28, 7, true, &ff));
    CHECK(DtcManager_GetRecord(DTC_ID_COMMAND_TIMEOUT, &r) && r.freeze_frame.timestamp_ms == 321 && r.occurrence_count == 7);
    CHECK(DtcManager_RestoreRecord(DTC_ID_COMMAND_TIMEOUT, 0, 255, false, NULL));
    for (unsigned i = 0; i < 3; i++) DtcManager_Report(DTC_ID_COMMAND_TIMEOUT, true, 500+i);
    CHECK(DtcManager_GetRecord(DTC_ID_COMMAND_TIMEOUT, &r) && r.occurrence_count == 255);
    puts("DTC 3-fail / 10-pass thresholds / freeze / restore / clear: PASS");
}

static void test_pid(void)
{
    PID_t p;
    PID_Init(NULL, 0, 0, 0, 0, 0); PID_Reset(NULL);
    CHECK(PID_Update(NULL, 1, 0, 1) == 0);
    PID_Init(&p, 2, 1, 0, -10, -100);
    CHECK(p.i_limit == 10 && p.out_limit == 100);
    CHECK(PID_Update(&p, 10, 0, 1) == 30);
    CHECK(PID_Update(&p, 1000, 0, 1) == 100 && p.i_acc == 10);
    CHECK(PID_Update(&p, -1000, 0, 1) == -100 && p.i_acc == -10);
    PID_Reset(&p); CHECK(p.i_acc == 0 && p.prev_err == 0);
    PID_Init(&p, 0, 0, 2, 10, 100);
    CHECK(PID_Update(&p, 10, 0, 2) == 10);
    CHECK(PID_Update(&p, 20, 0, 0) == 0);
    PID_Init(&p, 1, 0, 0, 10, 100);
    CHECK(PID_Update(&p, 1.5f, 0, 1) == 2);
    CHECK(PID_Update(&p, -1.5f, 0, 1) == -2);
    puts("PID limits / anti-windup / derivative / rounding: PASS");
}

static uint8_t response[8];
static void request(const uint8_t *p, unsigned n, uint32_t now)
{
    uint8_t f[8] = {0}; CHECK(n <= 7); f[0] = (uint8_t)n;
    memcpy(f+1, p, n); UdsServer_ProcessCanFrame(f, 8, now);
}
#define REQ(now, ...) do { const uint8_t p[] = {__VA_ARGS__}; request(p, sizeof(p), now); } while (0)
static void pop(uint32_t now)
{
    CHECK(UdsServer_PeekResponse(response)); UdsServer_ConfirmResponseSent(now);
}
static void nrc(uint8_t sid, uint8_t code)
{
    pop(0); CHECK(response[0] == 3 && response[1] == 0x7f && response[2] == sid && response[3] == code);
}
static void empty(void) { CHECK(!UdsServer_PeekResponse(response)); }
static void fresh(void)
{
    UdsServer_Init(); VehicleState_Init(0); DtcManager_Init();
    resets = 0; host_boot_request = 0;
}
static void test_uds(void)
{
    uint8_t f[8] = {0x30};
    fresh(); empty(); CHECK(!UdsServer_PeekResponse(NULL)); UdsServer_ConfirmResponseSent(0);
    UdsServer_ProcessCanFrame(NULL, 8, 0); UdsServer_ProcessCanFrame(f, 7, 0);
    UdsServer_ProcessCanFrame(f, 8, 0); f[0]=0x10; UdsServer_ProcessCanFrame(f,8,0);
    f[0]=0; UdsServer_ProcessCanFrame(f,8,0); f[0]=8; UdsServer_ProcessCanFrame(f,8,0); empty();
    REQ(0, 0x99); nrc(0x99,0x11);
    const uint8_t sids[] = {0x10,0x11,0x14,0x19,0x22,0x31,0x3e};
    for (unsigned i=0;i<sizeof(sids);i++) { request(&sids[i],1,0); nrc(sids[i],0x13); }
    REQ(0,0x10,4); nrc(0x10,0x12);
    REQ(0,0x11,2); nrc(0x11,0x12);
    REQ(0,0x11,1); nrc(0x11,0x7e);
    REQ(0,0x3e,1); nrc(0x3e,0x12);
    REQ(0,0x3e,0); pop(0); CHECK(response[1]==0x7e);
    REQ(0,0x3e,0x80); empty();
    REQ(0,0x10,1); pop(0); CHECK(response[1]==0x50 && response[2]==1);
    REQ(0,0x19,1,0xff); nrc(0x19,0x12);
    REQ(0,0x19,2,0xff); pop(0); CHECK(response[0]==3 && response[1]==0x59);
    REQ(0,0x22,0,0); nrc(0x22,0x31);
    REQ(0,0x14,0,0xff,0xff); nrc(0x14,0x31);
    REQ(0,0x14,0xff,0,0xff); nrc(0x14,0x31);
    REQ(0,0x14,0xff,0xff,0); nrc(0x14,0x31);
    VehicleState_OnValidCommand(true,0);
    REQ(0,0x14,0xff,0xff,0xff); nrc(0x14,0x22);
    REQ(0,0x10,2); nrc(0x10,0x22);
    REQ(0,0x10,3); pop(0); CHECK(response[1]==0x50 && response[2]==3);
    REQ(0,0x11,1); nrc(0x11,0x22);
    VehicleState_OnValidCommand(false,0);
    REQ(0,0x14,0xff,0xff,0xff); pop(0); CHECK(response[0]==1 && response[1]==0x54);
    REQ(0,0x10,0x83); empty(); CHECK(UdsServer_GetSession()==3);
    UdsServer_Service(4999); CHECK(UdsServer_GetSession()==3);
    REQ(4999,0x3e,0x80); UdsServer_Service(5000); CHECK(UdsServer_GetSession()==3);
    UdsServer_Service(9999); CHECK(UdsServer_GetSession()==1);

    /* Absolute deadlines also hold across uint32 tick wrap. */
    REQ(0xfffffff0u,0x10,0x83); UdsServer_Service(0x1377); CHECK(UdsServer_GetSession()==3);
    UdsServer_Service(0x1378); CHECK(UdsServer_GetSession()==1);
    ticks=0x12345678;
    REQ(0,0x22,0xf1,1); pop(0); CHECK(response[0]==7 && response[4]==0x12 && response[7]==0x78);
    REQ(0,0x22,0xf1,2); pop(0); CHECK(response[4]==8 && response[6]==0 && response[5]==2);
    /* VIN is reassembled independently and checked byte-for-byte. */
    REQ(0,0x22,0xf1,0x90); pop(0); CHECK(response[0]==0x10 && response[1]==20);
    uint8_t assembled[40]; memcpy(assembled,response+2,6);
    f[0]=0x31; UdsServer_ProcessCanFrame(f,8,1); empty();
    f[0]=0x30; UdsServer_ProcessCanFrame(f,8,2);
    pop(2); CHECK(response[0]==0x21); memcpy(assembled+6,response+1,7);
    pop(2); CHECK(response[0]==0x22); memcpy(assembled+13,response+1,7); empty();
    CHECK(memcmp(assembled+3,"KAOYA-F407-ECU001",17)==0);
    REQ(0,0x22,0xf1,0x95); pop(0); CHECK(response[1]==12);
    UdsServer_ProcessCanFrame(f,8,1); pop(1); CHECK(response[0]==0x21); empty();
    REQ(0,0x22,0xf1,0); pop(0); CHECK(response[1]==11);
    UdsServer_ProcessCanFrame(f,8,1); pop(1); CHECK(response[0]==0x21 && response[4]==0x12); empty();
    REQ(0,0x22,0xf1,0x90); pop(0); UdsServer_Service(999); empty();
    UdsServer_Service(1000); UdsServer_ProcessCanFrame(f,8,1001); empty();
    for (unsigned id=0;id<DTC_ID_COUNT;id++) for(unsigned j=0;j<3;j++) DtcManager_Report((DtcId_t)id,true,j);
    REQ(0,0x19,2,0xff); pop(0); CHECK(response[1]==39);
    UdsServer_ProcessCanFrame(f,8,1);
    for(unsigned sn=1;sn<=5;sn++) { pop(1); CHECK(response[0]==(0x20|sn)); } empty();

    fresh(); REQ(0,0x31,2,0xff,0); nrc(0x31,0x12);
    REQ(0,0x31,1,0xff,0); nrc(0x31,0x7e);
    REQ(0,0x10,3); pop(0);
    REQ(0,0x31,1,0,0); nrc(0x31,0x31);
    VehicleState_OnValidCommand(true,0); REQ(0,0x31,1,0xff,0); nrc(0x31,0x22);
    VehicleState_OnValidCommand(false,0); REQ(100,0x31,1,0xff,0); nrc(0x31,0x78);
    REQ(101,0x31,1,0xff,0); nrc(0x31,0x21);
    UdsServer_Service(349); empty(); UdsServer_Service(350); pop(350);
    CHECK(response[0]==5 && response[1]==0x71 && response[5]==0); empty();

    /* Reset must wait for transport confirmation, then 50 ms. */
    REQ(1000,0x11,1); UdsServer_Service(1100); CHECK(resets==0);
    pop(1100); CHECK(response[1]==0x51);
    UdsServer_Service(1149); CHECK(resets==0); UdsServer_Service(1150); CHECK(resets==1 && host_boot_request==0);
    fresh(); REQ(0,0x10,2); pop(0); REQ(0,0x11,1); pop(1);
    UdsServer_Service(51); CHECK(resets==1 && host_boot_request==BOOT_REQUEST_MAGIC);
    fresh(); REQ(0,0x10,3); pop(0); REQ(0,0x11,0x81); empty();
    UdsServer_Service(49); CHECK(resets==0); UdsServer_Service(50); CHECK(resets==1);
    fresh(); REQ(0,0x10,2); pop(0); REQ(0,0x11,0x81); UdsServer_Service(50);
    CHECK(host_boot_request==BOOT_REQUEST_MAGIC);
    /* Queue saturation: no ninth response and no reset on failed enqueue. */
    fresh(); REQ(0,0x10,3); pop(0);
    for(unsigned i=0;i<9;i++) { REQ(0,0x3e,0); }
    REQ(0,0x11,1); UdsServer_Service(100); CHECK(resets==0);
    for(unsigned i=0;i<8;i++) { pop(0); CHECK(response[1]==0x7e); } empty();
    UdsServer_Service(100); CHECK(resets==0);
    /* Full queue rejects a new multi-frame response and routine start. */
    for(unsigned i=0;i<8;i++) { REQ(0,0x3e,0); }
    REQ(0,0x22,0xf1,0x90); REQ(0,0x31,1,0xff,0);
    for(unsigned i=0;i<8;i++) { pop(0); CHECK(response[1]==0x7e); }
    UdsServer_Service(250); empty();
    /* A running routine retains its final response until space is available. */
    REQ(300,0x31,1,0xff,0); nrc(0x31,0x78);
    for(unsigned i=0;i<8;i++) { REQ(301,0x3e,0); }
    UdsServer_Service(550);
    for(unsigned i=0;i<8;i++) { pop(550); CHECK(response[1]==0x7e); }
    UdsServer_Service(551); pop(551); CHECK(response[1]==0x71); empty();
    /* CF enqueue failure keeps its offset; another CTS retries that frame. */
    REQ(600,0x22,0xf1,0x90); pop(600);
    for(unsigned i=0;i<8;i++) { REQ(601,0x3e,0); }
    UdsServer_ProcessCanFrame(f,8,602);
    for(unsigned i=0;i<8;i++) { pop(602); CHECK(response[1]==0x7e); }
    empty(); UdsServer_ProcessCanFrame(f,8,603);
    pop(603); CHECK(response[0]==0x21);
    pop(603); CHECK(response[0]==0x22); empty();
    puts("UDS NRC / ISO-TP / sessions / routine / reset ordering: PASS");
}

int main(void)
{
    test_can(); test_state(); test_dtc(); test_pid(); test_uds();
    CHECK(critical_depth == 0);
    printf("HOST CORE REGRESSION PASS: %u checks (including critical-section balance)\n", checks);
    return 0;
}
