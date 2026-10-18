//
// bsp_spi.hpp —— SPI 总线封装。新建文件。
//

#ifndef BSP_SPI_HPP
#define BSP_SPI_HPP

#include "spi.h"
#include <cstdint>

//new: SPI 全双工传输。一边发 tx，一边收 rx，两者字节数相同
//new: 片选不在这里管 —— 调用前自己拉低 CS，调用后拉高
//new: 本工程 SPI1 = 模式0、8位、MSB先行、5.25MHz
HAL_StatusTypeDef SPI_Transfer(SPI_HandleTypeDef *bus, const uint8_t *tx,
                               uint8_t *rx, uint16_t length, uint32_t timeout_ms);

//new: SPI 只写，读回来的字节丢弃。写寄存器用这个
HAL_StatusTypeDef SPI_Write(SPI_HandleTypeDef *bus, const uint8_t *tx,
                            uint16_t length, uint32_t timeout_ms);

//new: 失败次数，调试用
uint32_t SPI_GetFailCount(void);

#endif // BSP_SPI_HPP
