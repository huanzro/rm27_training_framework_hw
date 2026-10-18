//
// Created by cosmosmount on 2025/9/2.
//

#ifndef RM26_H7_BSP_PWM_HPP
#define RM26_H7_BSP_PWM_HPP

#include "tim.h"
#include <cstdint>              // ★ 新增:为了 uint32_t

/**
 * @brief  PWM初始化
 */
void PWM_Init(void);

/**
 * @brief  PWM启动，只是对HAL库函数的形式上的封装
 */
void PWM_Start(TIM_HandleTypeDef *htim, uint32_t Channel);

/**
 * @brief  PWM停止，只是对HAL库函数的形式上的封装
 */
void PWM_Stop(TIM_HandleTypeDef *htim, uint32_t Channel);

/**
 * @brief  设置PWM周期
 * @param  htim: 定时器句柄
 * @param  period: 周期，单位为秒
 * 只是对HAL库函数的形式上的封装
 * @attention 只改 ARR，不重算 CCR。舵机的 Period 不能改，别对它调这个函数。
 */
void PWM_SetPeriod(TIM_HandleTypeDef *htim, float period);

/**
 * @brief  设置PWM占空比
 * @param  htim: 定时器句柄
 * @param  dutyratio: 占空比，0~1
 * 只是对HAL库函数的形式上的封装
 */
void PWM_SetDutyRatio(TIM_HandleTypeDef *htim, float dutyratio, uint32_t channel);

//new: 按脉宽(微秒)设置。舵机按时长解读，用这个而不是占空比
void PWM_SetPulseUs(TIM_HandleTypeDef *htim, uint32_t channel, float pulse_us);

//new: 读失败次数，调试用
uint32_t PWM_GetFailCount(void);

#endif //RM26_H7_BSP_PWM_HPP
