#include "bsp_can.hpp"

extern CAN_HandleTypeDef hcan1;
extern CAN_HandleTypeDef hcan2;

//new: 上层注册的接收回调
static CAN_RxCallback s_rx_cb = nullptr;

/**
 * @brief 初始化CAN滤波器配置。
 * 设置CAN硬件的滤波器，用于优化接收数据的处理。
 * 更多信息，请参考原文，链接：https://blog.csdn.net/weixin_54448108/article/details/128570593
 */
void CAN_Init(void)
{
    CAN_FilterTypeDef can_filter_st;                   ///< 定义过滤器结构体
    can_filter_st.FilterActivation = ENABLE;           ///< ENABLE使能过滤器
    can_filter_st.FilterMode = CAN_FILTERMODE_IDMASK;  ///< 设置过滤器模式--标识符屏蔽位模式
    can_filter_st.FilterScale = CAN_FILTERSCALE_32BIT; ///< 过滤器的位宽 32 位
    can_filter_st.FilterIdHigh = 0x0000;               ///< ID高位
    can_filter_st.FilterIdLow = 0x0000;                ///< ID低位
    can_filter_st.FilterMaskIdHigh = 0x0000;           ///< 过滤器掩码高位
    can_filter_st.FilterMaskIdLow = 0x0000;            ///< 过滤器掩码低位

    can_filter_st.FilterBank = 0;                                      ///< 过滤器组-双CAN可指定0~27
    can_filter_st.FilterFIFOAssignment = CAN_RX_FIFO0;                 ///< 与过滤器组管理的 FIFO
    HAL_CAN_ConfigFilter(&hcan1, &can_filter_st);                      ///< HAL库配置过滤器函数
    HAL_CAN_Start(&hcan1);                                             ///< 使能CAN1控制器
    HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING); ///< 使能CAN的各种中断

    can_filter_st.SlaveStartFilterBank = 14;                           ///< 双CAN模式下规定CAN的主从模式的过滤器分配，从过滤器为14
    can_filter_st.FilterBank = 14;                                     ///< 过滤器组-双CAN可指定0~27
    HAL_CAN_ConfigFilter(&hcan2, &can_filter_st);                      ///< HAL库配置过滤器函数
    HAL_CAN_Start(&hcan2);                                             ///< 使能CAN2控制器
    HAL_CAN_ActivateNotification(&hcan2, CAN_IT_RX_FIFO0_MSG_PENDING); ///< 使能CAN的各种中断
}

void CAN_Transmit(CAN_HandleTypeDef *hcan, uint32_t Id, uint8_t *msg, uint16_t len)
{
    //new: 参数与状态检查。扩展帧的 ID 上限是 0x1FFFFFFF，这里只发标准帧故限 0x7FF
    if (hcan == nullptr || msg == nullptr || len == 0 || len > 8 || Id > 0x7FF) {
        return;
    }
    if (hcan->State != HAL_CAN_STATE_LISTENING) {
        return;
    }

    if (HAL_CAN_GetTxMailboxesFreeLevel(hcan) == 0)
    {
        // 如果邮箱已满，可以选择等待或者采取其他操作
        // 例如，可以添加一个超时机制或者直接返回错误状态
        // Monitor::instance()->Log_Messages(Monitor::WARNING, (uint8_t *)"CAN Tx Mailbox is full\r\n");
        return;
    }

    //new: 构造标准数据帧头并发送。DLC 就是字节数，0~8
    CAN_TxHeaderTypeDef header = {};
    header.StdId = Id;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = len;

    uint32_t mailbox = 0;
    (void)HAL_CAN_AddTxMessage(hcan, &header, msg, &mailbox);
}

//new: 注册接收回调
void CAN_SetRxCallback(CAN_RxCallback cb)
{
    s_rx_cb = cb;
}

//new: CAN 接收回调。HAL 的弱函数，这里给出强定义。
//new: 只转发"标准帧 + 数据帧 + 8 字节"的报文，其余丢弃。
//new: 放在 bsp 层是为了统一校验报文格式；具体的电机解析交给上层注册的函数。
extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    if (hcan == nullptr || s_rx_cb == nullptr) {
        return;
    }

    CAN_RxHeaderTypeDef header = {};
    uint8_t bytes[8] = {};
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &header, bytes) != HAL_OK) {
        return;
    }

    if (header.IDE != CAN_ID_STD || header.RTR != CAN_RTR_DATA || header.DLC != 8) {
        return;
    }

    s_rx_cb(hcan, header.StdId, bytes, static_cast<uint8_t>(header.DLC));
}
