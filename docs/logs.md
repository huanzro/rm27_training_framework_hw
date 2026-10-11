# 开发与验证记录

> 记录格式:每次改动写清 **问题 / 计算 / 修改 / 验证 / 等级 / 待办**。
> 等级严格区分:**文件检查通过 / 编译通过 / 逻辑测试通过 / 硬件实测通过**。
> 没有 C 板期间,一切结论最高只能到"编译通过",不得写成"通信/控制功能正确"。

---

## 2026-10-11 阶段 0:打通构建基线

### 一、确认构建入口

仓库内存在两份逐字节相同的构建文件(顶层与 `board/` 下 SHA-256 前 10 位一致):

| 文件 | 顶层 | `board/` | 相同 |
| --- | --- | --- | --- |
| `CMakeLists.txt` | `2F5D2D62BD` | `2F5D2D62BD` | 是 |
| `CMakePresets.json` | `4D0B8FB936` | `4D0B8FB936` | 是 |
| `STM32F407XX_FLASH.ld` | `89C032E7AE` | `89C032E7AE` | 是 |
| `startup_stm32f407xx.s` | `599D14C39A69` | `599D14C39A69` | 是 |

**在仓库顶层执行 `cmake --preset Debug` 会失败**,报:

```
Could not find toolchain file:
  .../111rm27_training_framework_hw/cmake/gcc-arm-none-eabi.cmake
```

原因:顶层 `CMakePresets.json` 的 `toolchainFile` 为
`${sourceDir}/cmake/gcc-arm-none-eabi.cmake`,而 `cmake/` 目录**只存在于 `board/` 下**。

**结论:构建入口是 `board\`,不是仓库顶层。** 顶层两份为纯冗余副本。
(处理方式待定:删除顶层副本,或把顶层改为转发到 `board/`。)

### 二、配置与构建

在 `board\` 下执行:

```
cmake --preset Debug
  -- The C compiler identification is GNU 13.3.1
  -- The CXX compiler identification is GNU 13.3.1
  -- Configuring done (2.9s)
  -- Generating done (0.1s)
  -- Build files have been written to: .../board/build/Debug

cmake --build --preset Debug
  [29/29] Linking C executable board.elf
```

工具链:`D:/arm-gnu-toolchain/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin/`

### 三、内存基线

`arm-none-eabi-size board.elf`:

```
   text    data     bss     dec     hex
  15240      12    1980   17232    4350
```

| 区 | 用量 | 容量 | 占比 |
| --- | --- | --- | --- |
| FLASH | 15252 B | 1 MB | 1.45% |
| RAM | 1992 B | 128 KB | 1.52% |
| CCMRAM | 0 B | 64 KB | 0.00% |

对照参考实现(含 C++、ThreadX、BMI088、M2006 全套):
FLASH 41356 B / RAM 16720 B。差额主要来自 ThreadX 内核与三个线程静态栈
(3072 + 2048 + 2048 = 7168 B)。

### 四、编译单元现状

`compile_commands.json` 中共 **28 个编译单元,全部由 `arm-none-eabi-gcc` 编译,
无任何 C++ 源文件**。说明 `board/CMakeLists.txt` 仍是原始骨架:

- 第 32 行 `enable_language(C ASM)` —— 未启用 CXX
- 第 46-48 行 `target_sources` 为空
- 第 51-53 行 `target_include_directories` 为空

因此 `bsp/`、`libs/`、`modules/`、`threads/` 的源码一个都没参与编译。

### 五、踩坑记录:CMake 工作目录

**现象**:`cmake --preset Debug` 报 `no such file or directory` 与
`CMake Error: Generator: execution of make failed`;随后
`cmake --build --preset Debug` 报同样的错。

**根因链**:

1. 在**仓库根目录**执行 → 读到顶层 `CMakePresets.json`
2. `${sourceDir}` 解析为仓库根 → 工具链路径指向不存在的 `顶层\cmake\`
3. 配置在早期失败 → `board\build\Debug\` 里**有 `CMakeFiles\` 却没有 `CMakeCache.txt` 和 `build.ninja`**
4. `--build` 找不到 `build.ninja` → `no such file or directory`
5. CMake 退而调用 `make` → Windows 上未安装 make → `execution of make failed`

**处理**:删除 `build\` 与 `board\build\`(含半成品 `CMakeFiles`),
在 `board\` 下重新配置,一次通过。

**教训**:

1. 构建入口是 `board\`;执行 `cmake`/`git` 时用
   `Set-Location <绝对路径>; <命令>` 绑在同一段里,避免工作目录漂移
2. **首次配置慢是正常的**(CMake 要实际编译链接测试程序来探测编译器 ABI),
   不可中断。判断"慢"与"卡死":屏幕有新行在滚就等;超过 2 分钟一行不动才中断
3. 判据:`CMakeCache.txt` 存在 = 配置成功过,之后配置都很快;
   只有 `CMakeFiles\` 而无 `CMakeCache.txt` = 半成品,删掉整个 build 目录重来

### 六、CubeMX 重新生成代码的影响

在 CubeMX 中改完 SPI1 并点 `GENERATE CODE` 后,`board/Drivers/` 下有
**288 个文件被删除**(78 个 HAL Src、70 个 HAL Inc、22 个 CMSIS Device Include、
46 个 IAR linker 脚本、23+23+23 个 arm/gcc/iar startup 等)。

**判定:这是 CubeMX 的正常行为,不是故障。** 依据:

- `.ioc` 中 `ProjectManager.LibraryCopy=0`,对应 CubeMX 界面上的
  `Copy only the necessary library files`。该选项会让生成器只保留当前 MCU
  (STM32F407)与当前工具链(CMake/GCC)真正需要的文件,删掉其他型号与其他工具链的文件
- 被删的都是**其他 MCU 型号的头文件**(`stm32f401xc.h`、`stm32f410cx.h` …)
  和**其他 IDE/工具链的启动文件**(IAR `.icf`、各型号 `startup_*.s`)
- 我们实际需要的文件全部健在,已逐个确认:
  `stm32f4xx_hal_can.c`、`stm32f4xx_hal_spi.c`、`stm32f4xx_hal_uart.c`、
  `stm32f4xx_hal_rcc.c`、`stm32f4xx_hal_gpio.c`、
  `stm32f4xx_hal.h`、`stm32f4xx_hal_can.h`、`stm32f4xx_hal_spi.h`、
  `stm32f4xx_hal_uart.h`、`stm32f407xx.h`

**验证**(用直接调用工具链的方式,避免完整重建在沙箱下过慢):

```
arm-none-eabi-gcc ... -c Core/Src/spi.c   → 退出码 0
arm-none-eabi-gcc ... -c Core/Src/can.c   → 退出码 0
arm-none-eabi-gcc ... -c Core/Src/main.c  → 退出码 0
```

**处理**:这些删除应当提交,使仓库与当前 `.ioc` 配置保持一致
(别人克隆后得到同样的干净状态)。

**待办**:若后续需要其他 MCU 型号头文件或改用其他工具链,
必须回到 CubeMX 打开 `Copy only the necessary library files` 再生成。

### 七、`.gitattributes` 配置错误与修正

首次编写的 `.gitattributes` 把厂商规则写在了最前面:

```
board/Drivers/**      -text      ← 写在前面
...
*.c      text eol=lf             ← 写在后面,把上面的覆盖了
*.h      text eol=lf
```

**`.gitattributes` 的匹配规则是"后面的覆盖前面的"**,因此 `-text` 完全没生效。
用 `git check-attr` 验证,厂商文件显示 `text: set`、`eol: lf`,证明规则被覆盖。

**修正**:调整顺序 —— 通用规则写在前面,`board/Drivers/**` 与
`board/Middlewares/**` 的 `-text` 写在**最后**。修正后
`git check-attr` 显示厂商文件为 `text: unset`(即 `-text` 生效)。

**为什么需要这个文件**:本仓库 `core.autocrlf=true`,若不显式规定行尾,
`git status` 会报告大量"内容无差异却标记为已修改"的假修改
(实测 1441 个 `board/Drivers/**` 文件),淹没真实改动。

### 八、本阶段等级

- 构建基线:**编译通过** ✅
- 外设功能:**未上板验证** ⏳(无 C 板)
- 待办清单:
  - [ ] `board/cmake/gcc-arm-none-eabi.cmake:39` 引用 `STM32F407XX_FLASH.ld`(XX 大写),
        磁盘实际文件为 `STM32F407xx_FLASH.ld`(xx 小写)。
        Windows 不区分大小写故当前可用,**Linux/WSL 下会链接失败**。
        需先实验确认 CubeMX 重新生成时写入哪个大小写,再决定改文件还是改工具链脚本
  - [ ] 顶层两份冗余 `CMakeLists.txt` / `CMakePresets.json` 的处理方式待定
  - [x] 启用 C++ 并把 `bsp/libs/modules/threads` 的源码纳入构建(阶段 2,见下)

---

## 2026-10-11 阶段 2:C++ 源文件纳入构建

### 一、`board/CMakeLists.txt` 的改动

原始骨架有三处"空位",必须一起填:

| 位置 | 改动前 | 改动后 |
| --- | --- | --- |
| 编译器设置 | 只有 `CMAKE_C_STANDARD 11` | 增加 `CMAKE_CXX_STANDARD 17` 与 `CMAKE_CXX_STANDARD_REQUIRED ON` |
| 工程定义 | `project(${CMAKE_PROJECT_NAME})` | `project(${CMAKE_PROJECT_NAME} LANGUAGES C CXX ASM)` |
| 语言启用 | `enable_language(C ASM)` | `enable_language(C CXX ASM)` |
| 源文件 | `target_sources` 内只有注释 | 加入 8 个 `.cpp` |
| 头文件路径 | `target_include_directories` 内只有注释 | 加入 `../bsp/include`、`../libs/include`、`../modules/include`、`../threads/include` |

**为什么三处必须同时改**:`enable_language` 决定"CMake 是否知道如何调用 C++ 编译器";
`target_sources` 决定"编译哪些文件";`target_include_directories` 决定"能否找到头文件"。
缺第一处 → 报无法确定链接语言;缺第二处 → 没有任何 `.cpp` 参与编译;
缺第三处 → 报 `fatal error: pid.hpp: No such file or directory`。

### 二、验证:C++ 确实参与构建

`cmake --preset Debug` 输出:

```
-- The CXX compiler identification is GNU 13.3.1
-- Check for working CXX compiler: .../arm-none-eabi-g++.exe - skipped
-- Configuring done (0.4s)
```

`build/Debug/compile_commands.json`:

- **9 个 `.cpp` 编译单元**:`bsp.cpp`、`bsp_can.cpp`、`bsp_usart.cpp`、`pid.cpp`、
  `math.cpp`、`crc.cpp`、`BMI088.cpp`、`M2006.cpp`、`DJIMotorHandler.cpp`
- 编译命令为 **`arm-none-eabi-g++.exe -std=gnu++17`**

构建树中已产出 8 个 C++ 目标文件(`BMI088.cpp.obj` 除外,原因见下):
`bsp.cpp.obj`、`bsp_can.cpp.obj`、`bsp_usart.cpp.obj`、`crc.cpp.obj`、`math.cpp.obj`、
`pid.cpp.obj`、`DJIMotorHandler.cpp.obj`、`M2006.cpp.obj`

### 三、依赖障碍(逐个记录)

把 11 个 `.cpp` 的 `#include` 全部扫过,得出依赖表:

| 源文件 | 依赖 | 状态 |
| --- | --- | --- |
| `bsp.cpp` / `bsp_can.cpp` / `bsp_usart.cpp` | 各自的 `.hpp`(→`main.h`、`can.h`、`stm32f4xx_hal_uart.h`) | ✅ |
| `libs/` 三个 | 仅自己的 `.hpp` 与标准库 | ✅ |
| `DJIMotorHandler.cpp` / `M2006.cpp` | `DJIMotor.hpp`(→`pid.hpp`、`can.h`) | ✅ |
| `bsp_pwm.cpp` | `bsp_pwm.hpp` 内 `#include "tim.h"` | ❌ **`tim.h` 不存在**(TIM1/TIM10 未配置) |
| `BMI088.cpp` | 第 2 行 `#include "tx_api.h"` | ❌ **ThreadX 未安装** |
| `alivethread.cpp` | —— | 0 字节,尚未编写 |

**`BMI088.cpp` 的确切报错**(从 `compile_commands.json` 提取命令单独编译得到):

```
modules/src/BMI088.cpp:2:10: fatal error: tx_api.h: No such file or directory
    2 | #include "tx_api.h"
      |          ^~~~~~~~~~
compilation terminated.
```

**处理**:在 `target_sources` 中**注释掉** `BMI088.cpp`,注释里写明原因与恢复条件。
原则:**仓库必须始终停留在"能编译"的状态**,一次只引入一个变化,便于定位问题。

### 四、环境问题:完整 ninja 构建在本机极慢

**现象**:`cmake --build --preset Debug` 多次超过 10 分钟未完成,而
`cmake` 与 `ninja` 进程的 CPU 时间仅 **0.078 秒 / 0.156 秒** —— 处于 I/O 阻塞而非计算。

**推断**:疑似实时防病毒扫描每个构建产物(`.o`/`.obj`)。非代码问题。

**应对**:验证单个源文件时不必等完整构建。`build/Debug/compile_commands.json`
记录了每个文件**完整的**编译命令(编译器、全部 `-I`、全部 `-D`),取出对应条目直接执行,
**1~2 秒**即可得到与 ninja 一致的结果:

```powershell
$cc = Get-Content board\build\Debug\compile_commands.json -Raw | ConvertFrom-Json
$e  = $cc | Where-Object { (Split-Path $_.file -Leaf) -eq 'BMI088.cpp' }
Set-Location $e.directory
cmd /c $e.command
```

**注意**:本机 PowerShell 执行策略禁止运行 `.ps1`(报
`UnauthorizedAccess ... about_Execution_Policies`),脚本须用
`powershell -ExecutionPolicy Bypass -File <脚本>` 调用。

### 五、本阶段等级与后续依赖链

- 状态:**8 个 C++ 源文件编译通过**(有 `.obj` 产物为证)✅
- 未完成:`board.elf` 未用新源文件重新链接(完整构建受环境 I/O 阻塞影响)
- **未上板验证** ⏳

---

## 2026-10-11 阶段 1 完成:外设与 ThreadX 配置全部落地

### 一、逐项核对结果

| 项 | 结果 | 证据 |
| --- | --- | --- |
| **TIM1**(舵机 PWM) | ✅ | `.ioc`: `Prescaler=167`、`Period=19999`、`Channel-PWM Generation1 CH1`;`tim.c:46,48` 确认;`PE9.Signal=S_TIM1_CH1`。168 MHz/168/20000 = **50 Hz** |
| **TIM10**(加热 PWM) | ✅(曾出错,已修) | `.ioc`: `Prescaler=167`、`Period=999`;`tim.c:104,106`;`tim.c:117,121` 为 `TIM_OCMODE_PWM1` + `HAL_TIM_PWM_ConfigChannel`。1 MHz/1000 = **1 kHz** |
| **I2C2 / I2C3** | ✅ | `i2c.c:42`、`:71`:`ClockSpeed = 100000`、`AddressingMode = I2C_ADDRESSINGMODE_7BIT`;引脚 PF0/PF1、PA8/PC9 正确 |
| **HAL 时基 SysTick → TIM6** | ✅ | `.ioc`: `NVIC.TimeBase=TIM6_DAC_IRQn`;CubeMX 生成 `stm32f4xx_hal_timebase_tim.c`,含完整 `HAL_InitTick`/`HAL_SuspendTick`/`HAL_ResumeTick`,`Period=(1000000/1000)-1` → 1 kHz |
| **ThreadX** | ✅ | `board/Middlewares/ST/threadx/`(common/src 156 个 .c)、`tx_api.h`、`tx_user.h`(`TX_TIMER_TICKS_PER_SECOND 1000`)、`tx_initialize_low_level.s`(`SYSTEM_CLOCK=168000000`,`SYSTICK_CYCLES=(SYSTEM_CLOCK/1000)-1`)、`app_threadx.c`、`app_azure_rtos.c`、`RTE_Components.h` |
| **三个冲突中断函数已删** | ✅ | `stm32f4xx_it.c` 中 `SVC_Handler`/`PendSV_Handler`/`SysTick_Handler` 均已不存在,由 `tx_initialize_low_level.s:220,630` 提供 |
| **HAL 模块开关** | ✅ | `HAL_TIM_MODULE_ENABLED`、`HAL_I2C_MODULE_ENABLED` 均已启用 |
| **CMake 自动接入** | ✅ | `stm32cubemx/CMakeLists.txt` 已含 `tim.c`、`i2c.c`、`stm32f4xx_hal_timebase_tim.c`、`app_threadx.c`、`app_azure_rtos.c`、`tx_initialize_low_level.s` 及 `ThreadX_Src`(156 个源文件)与两个 ThreadX 头文件目录 |
| **main.c 初始化顺序** | ✅ | `MX_I2C2_Init` → `MX_I2C3_Init` → `MX_TIM1_Init` → `MX_TIM10_Init` → `MX_ThreadX_Init()`(第 110 行) |

### 二、期间发现并修复的错误

**TIM10 曾配错两处**(已修):

1. `Period` 未填 → 生成成寄存器最大值 **65535**,实际频率 1 MHz/65536 = **15.26 Hz**
   (应为 999,即 1 kHz)。判据:`.ioc` 的 `TIM10.IPParameters` 当时不含 `Period` —— 
   **清单里没有 = 从未填过 = 用默认值**。
2. 通道模式未选 → 生成的通道配置缺少 PWM 模式(现已为 `TIM_OCMODE_PWM1`)。

**教训**:CubeMX 面板上**没填的框不会报错,而是静默取默认值**。
TIM10 是 16 位定时器,不填 Period 就取 65535 —— 频率差 65 倍,却零警告。
改完必须回到生成的 `.c` 里逐行核对数字。

### 三、阶段 2 收尾:恢复两个源文件

`tim.h` 与 `tx_api.h` 已生成,依赖满足,故恢复编译:

- `../bsp/src/bsp_pwm.cpp`
- `../modules/src/BMI088.cpp`

**验证**(直接调用工具链编译,各 1.6 秒):

```
bsp_pwm.cpp   编译通过   目标文件 1123176 字节
BMI088.cpp    编译通过   目标对象 1233276 字节
```

**符号级验证**(`arm-none-eabi-nm`):

- `BMI088.o` 中**唯一的未定义符号**是 `U _tx_thread_sleep` —— 
  由 ThreadX 库在链接时提供,这正是正确的中间状态(源码只引用,不需自己实现)
- `BMI088.o` 定义了 12 个 `cBMI088::` 成员函数(`Config`、`Calibrate`、`ReadAccData`、
  `ReadGyroData`、`ReadAccTemperature`、`VerifyAccChipID`、`VerifyGyroChipID`、
  `TemperatureControl`、`ReadReg`、`WriteReg` 等)
- 编译命令的 `-I` 已含 `Middlewares/ST/threadx/common/inc` 与
  `Middlewares/ST/threadx/ports/cortex_m4/gnu/inc`

**构建图规模**:总编译单元由 36 增至 **211**(ThreadX 的 156 个源文件进入);
C++ 编译单元由 8 个增至 **10 个**。

- 状态:**10 个 C++ 源文件编译通过** ✅
- `board.elf` **仍是旧产物**(16:37,仅含 SPI 修正),尚未用新配置重新链接 ⏳

### 四、仍待处理

- [ ] **中断优先级**:`usart.c`、`can.c`、`dma.c` 中
      `CAN1_RX0/CAN2_RX0/DMA2_Stream2/USART1/USART6` 仍为优先级 **0**,
      高于 ThreadX 的 SysTick(=1)。
      后果:① 抢占内核的节拍中断,影响线程计时;② 这些 ISR 的回调中**不允许**
      调用 `tx_*` API,否则破坏内核就绪链表。
      处理:CubeMX → System Core → NVIC,改为 Preemption Priority = **5**
- [ ] **`threads/src/alivethread.cpp` 仍为 0 字节**,三个线程一个都没创建:
      `main.c:110 MX_ThreadX_Init()` → `tx_kernel_enter()` →
      `app_azure_rtos.c:66 tx_application_define()` →
      `app_threadx.c:59 App_ThreadX_Init()`(内部为空)。
      **内核会正常启动,但什么也不做,且不会报任何错。**
      实现应放在 `App_ThreadX_Init()` 的 `USER CODE` 区内,或由
      `threads/src/alivethread.cpp` 提供该函数
- [ ] 完整构建验证 `board.elf` 能链接通过(受环境 I/O 阻塞影响,需耐心)
- [ ] `bsp_spi.cpp`、`bsp_i2c.cpp`、`modules/src/LED.cpp` 尚未创建

**阶段 2 待办清单(已完成项)**:

- [x] CubeMX 配置 TIM1
- [x] CubeMX 配置 TIM10(修正 Period=999 与 PWM 模式)
- [x] CubeMX 配置 I2C2 与 I2C3
- [x] CubeMX 将 `SYS → Timebase Source` 改为 TIM6
- [x] CubeMX 安装 `X-CUBE-AZRTOS-F4` 的 ThreadX,tick = 1000
- [x] 删除 `stm32f4xx_it.c` 中的 `SVC_Handler`/`PendSV_Handler`/`SysTick_Handler`
- [x] `tx_user.h` 已生成,tick = 1000
- [x] `HAL_I2C_MODULE_ENABLED`、`HAL_TIM_MODULE_ENABLED` 已启用
- [x] 恢复 `CMakeLists.txt` 中 `BMI088.cpp` 与 `bsp_pwm.cpp` 的编译
