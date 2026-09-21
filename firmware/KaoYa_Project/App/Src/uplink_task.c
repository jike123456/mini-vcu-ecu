/**
 ****************************************************************************************************
 * @file        uplink_task.c
 * @author      Kaoya (烤鸭)
 * @version     V1.0
 * @date        2026-02-04
 * @brief       上行状态上报任务（Uplink Task）
 * @details
 * 本任务用于周期性向上位机发送系统状态信息，典型流程为：
 *
 *   SensorTask / ControlTask
 *           ↓
 *   SensorSnap_Get()（获取一次完整快照）
 *           ↓
 *   UplinkProto_SendStatus()
 *           ↓
 *   UART 发送（协议帧）
 *
 * @platform    STM32F407ZGT6 + FreeRTOS + HAL
 * @board       Kaoya Robot 下位机
 *
 * @copyright
 *              Copyright (c) 2025-2035 Kaoya. All rights reserved.
 *
 ****************************************************************************************************
 */ 
 
#include "uplink_task.h"

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#include "log_task.h"
#include "control_task.h"
#include "uplink_proto.h"
#include "monitor_task.h"
#include "vehicle_can.h"
#include "vehicle_state.h"
#include "dtc_manager.h"
#include "uds_server.h"

/* ============================================================================ */
/* 上行任务配置                                                                */
/* ============================================================================ */

/** 车辆 CAN 状态周期：20 ms（50 Hz） */
#define UPLINK_PERIOD_MS      (20u)

static int16_t clamp_float_to_i16(float value)
{
    if (value > 32767.0f) return 32767;
    if (value < -32768.0f) return -32768;
    return (int16_t)value;
}

/* ============================================================================ */
/* FreeRTOS 任务：UplinkTask                                                   */
/* ============================================================================ */

/**
 * @brief   上行状态上报任务入口
 *
 * @details
 *  - 周期性读取系统状态快照
 *  - 通过 UplinkProto 打包为协议帧
 *  - 通过串口发送给上位机
 */

void KaoYaApp_UplinkTask(void *argument)
{
    (void)argument;

	uint8_t serial_seq = 0;
    uint8_t can_alive = 0;
    uint32_t report_count = 0;

	/* 初始化上行协议模块（如序号、CRC、端口等） */
    UplinkProto_Init();

	/* 上行协议帧缓存 */
	proto_uplink_t tx;
	
	/* 周期调度基准 */
	TickType_t last_wake = xTaskGetTickCount();
    for (;;)
    {
		float left_mmps;
        float right_mmps;
        VehicleStateSnapshot_t vehicle;
		uint8_t can_data[VEHICLE_CAN_DLC];
		uint8_t uds_data[VEHICLE_CAN_DLC];

		/* 任务心跳：用于 MonitorTask 监控任务存活 */
		MON_HB_UPLINK();
		UdsServer_Service((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));

        /* 诊断应答优先于周期状态，发送失败时保留到下一周期重试。 */
        if (UdsServer_PeekResponse(uds_data))
        {
            if (UplinkProto_SendVirtualCan(&tx, serial_seq,
                                           UDS_CAN_ID_PHYSICAL_RESPONSE,
                                           VEHICLE_CAN_DLC, uds_data))
            {
                serial_seq++;
                UdsServer_ConfirmResponseSent(
                    (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
            }
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(UPLINK_PERIOD_MS));
            continue;
        }

        Control_GetWheelSpeedMmps(&left_mmps, &right_mmps);
        vehicle = VehicleState_GetSnapshot();
        if (DtcManager_GetActiveCount() > 0u)
        {
            vehicle.fault_flags |= VCU_FAULT_DTC_ACTIVE;
        }
        if (DtcManager_GetConfirmedCount() > 0u)
        {
            vehicle.fault_flags |= VCU_FAULT_DTC_STORED;
        }
        VehicleCan_EncodeMotionStatus(clamp_float_to_i16(left_mmps),
                                      clamp_float_to_i16(right_mmps),
                                      (uint8_t)vehicle.state,
                                      vehicle.fault_flags,
                                      can_alive,
                                      can_data);

        if (UplinkProto_SendVirtualCan(&tx, serial_seq,
                                       VEHICLE_CAN_ID_MOTION_STATUS,
                                       VEHICLE_CAN_DLC,
                                       can_data))
        {
            serial_seq++;
            can_alive = (uint8_t)((can_alive + 1u) & 0x0Fu);
            report_count++;
        }
#if UPLINK_KAOYATEACH_ENABLE
		if ((report_count != 0u) && ((report_count % 50u) == 1u))
        {
            LOGKAOYA_T("VCAN", "STATUS n=%lu L=%d R=%d state=%u fault=0x%02X",
                       (unsigned long)report_count,
                       (int)clamp_float_to_i16(left_mmps),
                       (int)clamp_float_to_i16(right_mmps),
                       (unsigned)vehicle.state,
                       (unsigned)vehicle.fault_flags);
        }
#endif	
		/* 固定 20 ms 节拍 */
		vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(UPLINK_PERIOD_MS));
    }
}
