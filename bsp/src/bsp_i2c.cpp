//
// bsp_i2c.cpp —— I2C 总线封装。新建文件。
//

#include "bsp_i2c.hpp"

//new: 只在本文件可见
namespace {

volatile uint32_t g_i2c_fail = 0;   //new: 失败计数

//new: 7 位地址左移一位。HAL 收的是"已移位"的 8 位地址
inline uint16_t dev8(uint8_t dev7)
{
    return static_cast<uint16_t>(dev7) << 1;
}

}  // namespace

//new
HAL_StatusTypeDef I2C_WriteReg(I2C_HandleTypeDef *bus, uint8_t dev7, uint8_t reg,
                               const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    //new: 参数检查。Instance 为空说明 MX_I2Cx_Init 没跑过
    if (bus == nullptr || bus->Instance == nullptr || data == nullptr || len == 0) {
        g_i2c_fail++;
        return HAL_ERROR;
    }

    const HAL_StatusTypeDef st =
        HAL_I2C_Mem_Write(bus, dev8(dev7), reg, I2C_MEMADD_SIZE_8BIT,
                          const_cast<uint8_t *>(data), len, timeout_ms);

    if (st != HAL_OK) {
        g_i2c_fail++;
    }
    return st;
}

//new
HAL_StatusTypeDef I2C_ReadReg(I2C_HandleTypeDef *bus, uint8_t dev7, uint8_t reg,
                              uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    //new: 参数检查
    if (bus == nullptr || bus->Instance == nullptr || data == nullptr || len == 0) {
        g_i2c_fail++;
        return HAL_ERROR;
    }

    const HAL_StatusTypeDef st =
        HAL_I2C_Mem_Read(bus, dev8(dev7), reg, I2C_MEMADD_SIZE_8BIT,
                         data, len, timeout_ms);

    if (st != HAL_OK) {
        //new: 读失败时清零，避免上层拿到未初始化数据
        for (uint16_t i = 0; i < len; ++i) {
            data[i] = 0;
        }
        g_i2c_fail++;
    }
    return st;
}

//new
uint32_t I2C_GetFailCount(void)
{
    return g_i2c_fail;
}
