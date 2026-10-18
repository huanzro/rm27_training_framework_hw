#include "bsp_usart.hpp"
#include "usart.h"
#include <cstring>          // 为 std::memcpy

namespace {

// 两个串口的接收缓冲区长度。
// 取值依据:
//   USART6 接裁判系统,一帧最长约 128 字节(以实际协议为准),这里留足余量。
//   USART1 用于调试,一般不发给我们数据,长度够用即可。
constexpr uint16_t USART1_RX_BUF_SIZE = 128;
constexpr uint16_t USART6_RX_BUF_SIZE = 256;

// DMA 接收缓冲区。必须是【静态/全局】的,不能用局部数组:
// DMA 是异步的,函数返回后它仍在写这块内存。
uint8_t g_usart1_rx_buf[USART1_RX_BUF_SIZE];
uint8_t g_usart6_rx_buf[USART6_RX_BUF_SIZE];

// "当前活跃缓冲区"的记录。
// 存在的原因:USART_Receive() 允许调用者传入自己的缓冲区,
// 因此回调里重新启动接收时,必须知道【刚才用的是哪块、多长】。
// 若调用者传的是局部数组,这里记录的就是它 —— 但那种用法是【不安全】的,
// 见 bsp_usart.hpp 的接口约定。
uint8_t *g_usart1_active_buf = g_usart1_rx_buf;
uint16_t g_usart1_active_len = USART1_RX_BUF_SIZE;
uint8_t *g_usart6_active_buf = g_usart6_rx_buf;
uint16_t g_usart6_active_len = USART6_RX_BUF_SIZE;

// 接收错误/重启失败的计数,便于在调试时发现问题。
// 为什么不直接在回调里打印:中断上下文里做串口输出会阻塞很久。
volatile uint32_t g_usart1_restart_fail = 0;
volatile uint32_t g_usart6_restart_fail = 0;

// 启动一次"到空闲为止"的 DMA 接收。
// 返回 true 表示成功,false 表示失败(调用者应记录)。
bool start_idle_rx(UART_HandleTypeDef *huart, uint8_t *buf, uint16_t len)
{
    if (huart == nullptr || buf == nullptr || len == 0) {
        return false;
    }
    // 【关键】重启前先中止当前接收。
    // 原因:HAL_UARTEx_ReceiveToIdle_DMA 要求 RxState == HAL_UART_STATE_READY,
    //       否则直接返回 HAL_BUSY 且【什么都不做】。
    //       若不先中止,重启用会静默失败 -> IDLEIE 不再使能 -> 该串口永久静默。
    (void)HAL_UART_AbortReceive(huart);

    // DMA 接收必须已经链接好句柄(由 CubeMX 生成的 MX_USARTx_UART_Init 完成)
    if (huart->hdmarx == nullptr) {
        return false;
    }

    return HAL_UARTEx_ReceiveToIdle_DMA(huart, buf, len) == HAL_OK;
}

}  // namespace

void USART_Init(void)
{
  USART1_Init();
  USART6_Init();
}


// 配置 USART6 的 DMA 接收、发送及空闲中断，并启动接收。
void USART6_Init()
{
   g_usart6_active_buf = g_usart6_rx_buf;
    g_usart6_active_len = USART6_RX_BUF_SIZE;

    if (!start_idle_rx(&huart6, g_usart6_active_buf, g_usart6_active_len)) {
        g_usart6_restart_fail++;
    }
    
}

//  配置 USART1 的 DMA 接收、发送及空闲中断，并启动接收。
void USART1_Init()
{
 g_usart1_active_buf = g_usart1_rx_buf;
    g_usart1_active_len = USART1_RX_BUF_SIZE;

    if (!start_idle_rx(&huart1, g_usart1_active_buf, g_usart1_active_len)) {
        g_usart1_restart_fail++;
    } 
};

void USART_Transmit(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, enum USART_Mode mode)
{
  // 检查参数，并按 mode 选择阻塞、DMA 或中断发送。
   if (huart == nullptr || pData == nullptr || Size == 0) {
        return;
    }

    // ---- 按 mode 选择发送方式 ----
    HAL_StatusTypeDef st = HAL_ERROR;

    switch (mode) {
        case USART_MODE_BLOCK:
            // 阻塞发送,超时 100 ms。
            // 注意:在 ThreadX 线程里调用会阻塞该线程;
            //       在中断里【绝不能】用这个模式。
            st = HAL_UART_Transmit(huart, pData, Size, 100);
            break;

        case USART_MODE_DMA:
            // DMA 发送,不阻塞。
            // 【接口契约】pData 必须在发送完成前保持有效(不能是局部数组)。
            // 【易漏点】上一次 DMA 未完成时会返回 HAL_BUSY 且不发任何数据。
            st = HAL_UART_Transmit_DMA(huart, pData, Size);
            break;

        case USART_MODE_IT:
            st = HAL_UART_Transmit_IT(huart, pData, Size);
            break;

        default:
            // 非法 mode:显式返回,不静默什么都不做。
            // (静默会让人以为"发出去了",实际没有)
            return;
    }

    // ---- 检查返回值 ----
    // HAL_BUSY 是最需要关注的:它意味着数据被丢弃。
    if (st != HAL_OK) {
        // 这里只做最小动作。不要在中断上下文里打印日志。
        // 可在此累加计数器,由线程周期性输出。
    }
}

void USART_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size)
{
  // 选择接收方式并启动 HAL UART 接收。
  if (huart == nullptr || pData == nullptr || Size == 0) {
        return;
    }

    // 记录活跃缓冲区,供回调重新启动时使用
    if (huart->Instance == USART1) {
        g_usart1_active_buf = pData;
        g_usart1_active_len = Size;
    } else if (huart->Instance == USART6) {
        g_usart6_active_buf = pData;
        g_usart6_active_len = Size;
    } else {
        return;   // 不是我们管理的串口
    }

    // start_idle_rx 内部会先 AbortReceive 再启动
    if (!start_idle_rx(huart, pData, Size)) {
        if (huart->Instance == USART1) {
            g_usart1_restart_fail++;
        } else {
            g_usart6_restart_fail++;
        }
    }
}

// 回调 new
extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart == nullptr) {
        return;
    }

    if (huart->Instance == USART1) {
        // ---- ① 处理本次收到的数据 ----
        // Size 是本次实际字节数。
        // 建议:只做"搬运 + 置标志",把解析留给线程。
        //
        // 例如交给上层解析:
        //   USART_OnReceive(huart, g_usart1_active_buf, Size);
        //
        // 本实现暂不解析,仅重新启动接收(保证链路不断)。

        if (!start_idle_rx(huart, g_usart1_active_buf, g_usart1_active_len)) {
            g_usart1_restart_fail++;
        }

    } else if (huart->Instance == USART6) {
        if (!start_idle_rx(huart, g_usart6_active_buf, g_usart6_active_len)) {
            g_usart6_restart_fail++;
        }
    }
}