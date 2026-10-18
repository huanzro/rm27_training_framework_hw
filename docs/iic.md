

## 一、特点

I²C 两根线挂一串设备：SCL 是时钟，由主机产生，但从机可以**拉低 SCL 让主机等**（时钟拉伸）；SDA 是双向数据，主从轮流驱动。

两根线的输出级都是开漏 + 外部上拉，这一点决定了它的全部特性：只能拉低不能推高，所以上升沿是 RC 充电、速率上不去（标准模式 100 kHz、快速模式 400 kHz）；设备不发言时处于高阻态，所以能多主多从、需要线与仲裁；必须接上拉，否则线永远拉不高。

同一时刻只有一方驱动 SDA，所以是半双工。生成代码里保留了时钟拉伸：`NoStretchMode = I2C_NOSTRETCH_DISABLE`（`i2c.c:49`/`:78`），DISABLE 指"禁用 NoStretch"，也就是允许从机拉伸。

## 二、时序、地址与应答

一次读寄存器的事务：

```
START → [7位地址 + 0(写)] → ACK → 寄存器地址 → ACK
      → 重复START → [7位地址 + 1(读)] → ACK → 数据 → NACK → STOP
```

`HAL_I2C_Mem_Read(..., I2C_MEMADD_SIZE_8BIT, ...)`（`bsp_i2c.cpp:59`）做的就是这一串动作。

**应答**：每传完 8 位，接收方在第 9 个 SCL 上把 SDA 拉低表示 ACK，放开表示 NACK。

- 地址帧由被寻址的从机应答。**没人应答就是"这个地址上没有设备"**——`HAL_I2C_IsDeviceReady()` 就是只发地址看有没有 ACK，我把它包成了 `I2C_Probe()`。
- 写数据由从机应答；读数据由**主机**应答：每收一字节回 ACK 表示"继续给"，**最后一个字节必须回 NACK** 再发 STOP，从机靠这个 NACK 才知道该放开 SDA。漏掉它是"读寄存器时多出一个字节"的常见原因。

**两种失败要分清**：NACK 说明总线有设备但不应答这个地址，HAL 返回 `HAL_ERROR`，我的 BSP 会累加 `I2C_ErrorCount()`；总线被拉死（某个从机中途掉电，把 SDA 一直拉低）则连 START 都发不出去，HAL 返回 `HAL_TIMEOUT`，需要主机手动补 9 个时钟把从机顶出来——**本工程没做这一步**，标准做法是把 SCL/SDA 临时切成 GPIO 手动打拍再重新初始化外设。

**七位地址 vs 八位地址**是最容易错的地方：手册写的是七位（IST8310 是 `0x0E`），HAL 要的是八位（左移一位留出读写位，即 `0x1C`）。传错的表现是 `HAL_I2C_IsDeviceReady` 一直报错，像"设备不存在"。我把左移收进了 BSP（`bsp_i2c.cpp:60`），上层只写手册上的数字，调用方不可能少移一位。

## 三、配置

| 项 | 值 | 依据 |
| --- | --- | --- |
| 速度模式 | Standard，100 kHz | `i2c.c:42`（I2C2）/ `:71`（I2C3） |
| 寻址模式 | 7-bit | `i2c.c:45` / `:74` |
| 双地址 | Disable，`OwnAddress1=0`（只作主机） | `i2c.c:44-48` / `:73-77` |
| 时钟拉伸 | 允许（`NoStretchMode = DISABLE`） | `i2c.c:49` / `:78` |

`.ioc` 里**没有** `I2C2.*`/`I2C3.*` 任何参数，这一页在 CubeMX 上没留下记录，生效的值只能回读生成的 `i2c.c`。100 kHz 是默认值，也在 IST8310 的 400 kHz 上限内取最保守一档。两路都挂 APB1（`RCC.APB1Freq_Value=42000000`），HAL 用 PCLK1 折算 CCR/TRISE（`stm32f4xx_hal_i2c.c:508`）。

引脚：I2C3 = PA8(SCL)/PC9(SDA)，接板载 IST8310（七位地址 `0x0E`，`WHO_AM_I=0x10`）；I2C2 = PF1(SCL)/PF0(SDA)，引到外部接口。地址和 `WHO_AM_I` 是手册值，工程里没有代码可以对照。`stm32f4xx_hal_conf.h:56` 的 `HAL_I2C_MODULE_ENABLED` 是 CubeMX 自动打开的，只有手工改 `.ioc` 时才需要回来确认。

## 四、BSP 封装

```cpp
HAL_StatusTypeDef I2C_ReadRegister (I2C_HandleTypeDef *bus, uint8_t address7, uint8_t reg,
                                    uint8_t *data, uint16_t length, uint32_t timeout_ms = 50);
HAL_StatusTypeDef I2C_WriteRegister(I2C_HandleTypeDef *bus, uint8_t address7, uint8_t reg,
                                    const uint8_t *data, uint16_t length, uint32_t timeout_ms = 50);
HAL_StatusTypeDef I2C_Probe        (I2C_HandleTypeDef *bus, uint8_t address7,
                                    uint32_t trials = 3, uint32_t timeout_ms = 50);
uint32_t I2C_ErrorCount(void);
void     I2C_Init(void);        // 由 bsp_Init() 调用，登记 I2C2 / I2C3
```

三个设计考虑：地址左移在内部完成，传进来 `> 0x7F`（八位地址）会被 `address_ok()` 直接挡掉；`I2C_Init()` 记录两路句柄并检查 `State == READY`，未初始化时任何调用直接返回 `HAL_ERROR` 而不是往未配置的外设寄存器里写——注意它是**两路一起判**（`bsp_i2c.cpp:43-44`），I2C2 没起来的话 I2C3 也会被一起拒绝，这是刻意的"宁可不发"；`I2C_ErrorCount()` 让"偶尔读错一次"这种最常见故障变得可观察。
