/**
 ****************************************************************************************************
 * @file        capture.h
 * @author      Kaoya (烤鸭)
 * @version     V1.0
 * @date        2026-02-04
 * @brief       编码器输入捕获测速模块接口
 * @license     Copyright (c) 2025-2035, Kaoya Project
 ****************************************************************************************************
 */
#ifndef __CAPTURE_H
#define __CAPTURE_H

#include <stdint.h>

#ifndef ENCODER_KAOYATEACH_ENABLE
#define ENCODER_KAOYATEACH_ENABLE 0
#endif

/**
 * @brief   初始化编码器输入捕获模块
 */
void EncoderCap_Init(void);

/**
 * @brief 设置单通道编码器的方向提示
 * @param left_sign  左轮方向：正数正转、负数反转、0 保持原方向
 * @param right_sign 右轮方向：正数正转、负数反转、0 保持原方向
 * @note 单通道脉冲本身无法识别方向，因此使用电机指令作为估计方向。
 */
void EncoderCap_SetDirectionHint(int8_t left_sign, int8_t right_sign);

/**
 * @brief   获取左轮编码器频率（Hz）
 */
float EncoderCap_GetLeftHz(void);

/**
 * @brief   获取右轮编码器频率（Hz）
 */
float EncoderCap_GetRightHz(void);

/**
 * @brief   获取左轮线速度（mm/s）
 */
float EncoderCap_GetLeftMmps(void);

/**
 * @brief   获取右轮线速度（mm/s）
 */
float EncoderCap_GetRightMmps(void);

/**
 * @brief   获取差速车体速度（vx, vy, wz）
 *
 * @param   vx_mmps   前向速度（mm/s）
 * @param   vy_mmps   横向速度（mm/s）
 * @param   wz_mradps 偏航角速度（mrad/s）
 */
void EncoderCap_GetDiff3Axis(float *vx_mmps,
                             float *vy_mmps,
                             float *wz_mradps);

#if ENCODER_KAOYATEACH_ENABLE
void Capture_KaoYaTeaching(void);
#endif
							 
#endif /* __CAPTURE_H */
