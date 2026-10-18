//
// Created by cosmosmount on 2025/9/2.
//

#include "bsp_pwm.hpp"
#include <cmath>                //new: std::isfinite

//new: 以下放在匿名命名空间里，只在本文件可见，避免和其他 .cpp 的全局名字冲突
namespace {

volatile uint32_t g_pwm_fail = 0;   //new: 失败计数，替代 Error_Handler

constexpr float SERVO_MID_US = 1500.0f;   //new: 舵机中位脉宽

//new: 只接受单个通道。TIM_CHANNEL_ALL 也是合法值，但会一次动 4 个通道
bool ch_ok(uint32_t ch)
{
    return ch == TIM_CHANNEL_1 || ch == TIM_CHANNEL_2 ||
           ch == TIM_CHANNEL_3 || ch == TIM_CHANNEL_4;
}

//new: 定时器时钟。APB 分频 != 1 时，定时器时钟是 APB 的 2 倍
uint32_t tim_clk(TIM_HandleTypeDef *htim)
{
    const bool apb2 = (htim->Instance == TIM1)  || (htim->Instance == TIM8) ||
                      (htim->Instance == TIM9)  || (htim->Instance == TIM10) ||
                      (htim->Instance == TIM11);
    const uint32_t pclk = apb2 ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
    const uint32_t ppre = apb2 ? (RCC->CFGR & RCC_CFGR_PPRE2)
                               : (RCC->CFGR & RCC_CFGR_PPRE1);
    return pclk * ((ppre != 0U) ? 2U : 1U);
}

}  // namespace

void PWM_Init(void)
{
    //new: 先写初值再启动。CubeMX 生成的 Pulse 是 0，先 Start 会让舵机处于无信号状态
    PWM_SetPulseUs(&htim1, TIM_CHANNEL_1, SERVO_MID_US);
    __HAL_TIM_SET_COMPARE(&htim10, TIM_CHANNEL_1, 0U);

    PWM_Start(&htim1, TIM_CHANNEL_1);
    PWM_Start(&htim10, TIM_CHANNEL_1);
}

void PWM_Start(TIM_HandleTypeDef *htim, uint32_t Channel)
{
    //new: 多查了 Instance 和通道。Instance 为空说明 MX_TIMx_Init 没跑过
    if (htim == nullptr || htim->Instance == nullptr || !ch_ok(Channel)) {
        g_pwm_fail++;
        return;
    }

    //new: 失败只计数。原来调 Error_Handler 会关中断死循环，ThreadX 下会卡死整个系统
    if (HAL_TIM_PWM_Start(htim, Channel) != HAL_OK) {
        g_pwm_fail++;
    }
}

void PWM_Stop(TIM_HandleTypeDef *htim, uint32_t Channel)
{
    //new: 参数检查
    if (htim == nullptr || htim->Instance == nullptr || !ch_ok(Channel)) {
        g_pwm_fail++;
        return;
    }

    //new: 检查返回值
    if (HAL_TIM_PWM_Stop(htim, Channel) != HAL_OK) {
        g_pwm_fail++;
    }
}

void PWM_SetPeriod(TIM_HandleTypeDef *htim, float period_s)
{
    //new: 参数检查
    if (htim == nullptr || htim->Instance == nullptr) {
        g_pwm_fail++;
        return;
    }
    if (!std::isfinite(period_s) || period_s <= 0.0f) {
        g_pwm_fail++;
        return;
    }

    //new: ARR = 定时器时钟 / (PSC+1) * 周期 - 1
    const double ticks = static_cast<double>(tim_clk(htim)) * period_s /
                         static_cast<double>(htim->Init.Prescaler + 1U);

    //new: ARR 是 16 位寄存器，超范围就别写了
    if (ticks < 1.0 || ticks > 65536.0) {
        g_pwm_fail++;
        return;
    }

    __HAL_TIM_SET_AUTORELOAD(htim, static_cast<uint32_t>(ticks) - 1U);
    //new: 这里不重算 CCR，所以脉宽和占空比的含义都会跟着周期变。舵机不要调这个函数
}

void PWM_SetDutyRatio(TIM_HandleTypeDef *htim, float dutyratio, uint32_t channel)
{
    //new: 加了通道校验，防止传 TIM_CHANNEL_ALL
    if (htim == nullptr || htim->Instance == nullptr || !ch_ok(channel)) {
        g_pwm_fail++;
        return;
    }
    if (!std::isfinite(dutyratio)) {
        g_pwm_fail++;
        return;
    }

    if (dutyratio < 0.0f) dutyratio = 0.0f;
    if (dutyratio > 1.0f) dutyratio = 1.0f;

    //new: Pulse = (ARR+1) * 占空比。读当前 ARR，不要硬编码周期
    const uint32_t arr = __HAL_TIM_GET_AUTORELOAD(htim);
    __HAL_TIM_SET_COMPARE(htim, channel,
        static_cast<uint32_t>(static_cast<float>(arr + 1U) * dutyratio));
}

void PWM_SetPulseUs(TIM_HandleTypeDef *htim, uint32_t channel, float pulse_us)
{
    if (htim == nullptr || htim->Instance == nullptr || !ch_ok(channel)) {
        g_pwm_fail++;
        return;
    }
    if (!std::isfinite(pulse_us)) {
        g_pwm_fail++;
        return;
    }

    //new: 计数器是 1MHz，所以微秒数就是计数值。超过 ARR 会整个周期都是高电平
    const uint32_t arr = __HAL_TIM_GET_AUTORELOAD(htim);
    uint32_t pulse = (pulse_us > 0.0f) ? static_cast<uint32_t>(pulse_us) : 0U;
    if (pulse > arr) pulse = arr;

    __HAL_TIM_SET_COMPARE(htim, channel, pulse);
}

uint32_t PWM_GetFailCount(void)
{
    return g_pwm_fail;
}
