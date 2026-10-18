#ifndef BSP_CAN_H
#define BSP_CAN_H

#include "main.h"
#include "can.h"

/**
 * @brief 初始化CAN滤波器配置。
 * 设置CAN硬件的滤波器，用于优化接收数据的处理。
 * 更多信息，请参考原文，链接：https://blog.csdn.net/weixin_54448108/article/details/128570593
 */
void CAN_Init(void);

/**
 * @brief 发送CAN数据。
 * @param hcan 指向CAN句柄的指针，用于配置CAN传输。
 * @param StdId CAN消息的标准标识符
 * @param msg 发送的数据
 * @param len 数据长度
 * @note 该函数用于发送CAN数据，目前只支持标准帧
 */
void CAN_Transmit(CAN_HandleTypeDef *hcan, uint32_t Id, uint8_t *msg, uint16_t len);

//new: 接收回调类型。bsp 不直接依赖 modules，由上层注册自己的处理函数。
//new: dlc 固定为 8（电机反馈帧），data 指向解析好的 8 字节。
typedef void (*CAN_RxCallback)(CAN_HandleTypeDef *hcan, uint32_t stdId,
                               const uint8_t *data, uint8_t dlc);

//new: 注册接收回调。传 nullptr 表示取消。
void CAN_SetRxCallback(CAN_RxCallback cb);

#endif // BSP_CAN_HPP
