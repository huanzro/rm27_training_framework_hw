#include "bsp.hpp"

#include "bsp_can.hpp"
#include "bsp_usart.hpp"
#include "bsp_pwm.hpp"      //new

void bsp_Init() {
    USART_Init();
    CAN_Init();
    PWM_Init();             //new
}
