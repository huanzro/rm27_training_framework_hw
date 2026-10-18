//
// bsp_i2c.hpp —— I2C 总线封装。新建文件。
//

#ifndef BSP_I2C_HPP
#define BSP_I2C_HPP

#include "i2c.h"
#include <cstdint>

//new: 写寄存器。dev7 是 7 位地址，函数内部左移一位再交给 HAL
//new: 本工程 I2C2(PF0 SDA / PF1 SCL)、I2C3(PA8 SCL / PC9 SDA)，均 100kHz、7 位地址
HAL_StatusTypeDef I2C_WriteReg(I2C_HandleTypeDef *bus, uint8_t dev7, uint8_t reg,
                               const uint8_t *data, uint16_t len, uint32_t timeout_ms);

//new: 读寄存器
HAL_StatusTypeDef I2C_ReadReg(I2C_HandleTypeDef *bus, uint8_t dev7, uint8_t reg,
                              uint8_t *data, uint16_t len, uint32_t timeout_ms);

//new: 失败次数，调试用
uint32_t I2C_GetFailCount(void);

#endif // BSP_I2C_HPP
