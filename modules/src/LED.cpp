//
// LED.cpp —— C 板板载 RGB LED。新建文件。
//

#include "LED.hpp"
#include "main.h"

//new: LED 极性未实测。AGENTS.md 记录的隐患：参考资料里"高电平亮"和"低电平亮"两说并存。
//new: 这里用宏开关，拿到板子后实测一次就能确定，不用改逻辑。
//new: LED_ACTIVE_LOW = 1 表示低电平点亮
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 1
#endif

namespace {

//new: 把"亮/灭"翻译成实际要写的引脚电平
inline GPIO_PinState level(bool on)
{
#if LED_ACTIVE_LOW
    return on ? GPIO_PIN_RESET : GPIO_PIN_SET;
#else
    return on ? GPIO_PIN_SET : GPIO_PIN_RESET;
#endif
}

}  // namespace

namespace LED {

//new
void Set(bool red, bool green, bool blue)
{
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, level(red));
    HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, level(green));
    HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, level(blue));
}

//new
void Off()
{
    Set(false, false, false);
}

}  // namespace LED
