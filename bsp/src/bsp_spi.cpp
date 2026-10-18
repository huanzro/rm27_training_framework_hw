//
// bsp_spi.cpp —— SPI 总线封装。新建文件。
//

#include "bsp_spi.hpp"

//new: 只在本文件可见
namespace {

volatile uint32_t g_spi_fail = 0;   //new: 失败计数

}  // namespace

//new
HAL_StatusTypeDef SPI_Transfer(SPI_HandleTypeDef *bus, const uint8_t *tx,
                               uint8_t *rx, uint16_t length, uint32_t timeout_ms)
{
    //new: 参数检查。Instance 为空说明 MX_SPIx_Init 没跑过
    if (bus == nullptr || bus->Instance == nullptr ||
        tx == nullptr || rx == nullptr || length == 0) {
        g_spi_fail++;
        return HAL_ERROR;
    }

    //new: HAL 的签名本来就是 const uint8_t*，不需要 const_cast
    const HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(bus, tx, rx, length, timeout_ms);

    if (st != HAL_OK) {
        g_spi_fail++;
    }
    return st;
}

//new
HAL_StatusTypeDef SPI_Write(SPI_HandleTypeDef *bus, const uint8_t *tx,
                            uint16_t length, uint32_t timeout_ms)
{
    //new: 参数检查
    if (bus == nullptr || bus->Instance == nullptr || tx == nullptr || length == 0) {
        g_spi_fail++;
        return HAL_ERROR;
    }

    const HAL_StatusTypeDef st = HAL_SPI_Transmit(bus, tx, length, timeout_ms);

    if (st != HAL_OK) {
        g_spi_fail++;
    }
    return st;
}

//new
uint32_t SPI_GetFailCount(void)
{
    return g_spi_fail;
}
