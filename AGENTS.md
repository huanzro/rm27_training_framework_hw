# 工程协作约定

本文件是作业要求的一部分(README 执行步骤第 4 条):把需求、理解、项目规范与执行计划写下来,
并在开发过程中不断完善。

## 一、我的需求与理解

目标是**由我亲自**完成 C 板框架大作业:配置、编写、编译、解释,而不是拿到一份能编译的代码。
我需要理解每一步为什么这样做。

现状约束:**暂无 C 板**。因此当前工作以「配置 / 编译 / 代码分析 / 可在电脑上执行的测试」为主,
硬件相关结论一律标注为待实测。

## 二、项目结构与依赖方向

| 路径 | 内容 | 依赖谁 |
| --- | --- | --- |
| `board/` | CubeMX 配置与生成的板级代码;**唯一构建入口** | — |
| `bsp/` | CAN、UART、PWM 封装(SPI、I²C 需自行补充) | `board/` 生成的头文件 |
| `libs/` | PID、数值工具、CRC | 仅标准库 |
| `modules/` | BMI088、DJI 电机、M2006 | `bsp/`、`libs/` |
| `threads/` | ThreadX 线程 | `modules/` |

**依赖是单向的**:`threads → modules → libs / bsp → HAL`。
所以编写顺序固定为 **`board → bsp → libs → modules → threads`**;
下层没确定就写上层,只会得到编译不过或行为不明的代码。

## 三、构建约定

```powershell
Set-Location <仓库>\board
cmake --preset Debug
cmake --build --preset Debug
```

- **构建入口是 `board/`,不是仓库顶层。** 顶层 `CMakeLists.txt` / `CMakePresets.json` 是仓库模板,
  其工具链路径指向不存在的 `顶层/cmake/`,按它配置必然失败。
- **首次配置慢是正常的**(CMake 要实际编译链接测试程序探测编译器 ABI),不可中断。
  判据:屏幕有新行在滚就等;超过 2 分钟一行不动才中断。
- `.vscode/settings.json` 已设 `cmake.sourceDirectory = ${workspaceFolder}/board`,
  并关闭 `cmake.autoDetectSourceDirectory`,避免扩展自动认领其他 `CMakeLists.txt`。
- 单文件快速验证(不必等完整构建):从 `board/build/Debug/compile_commands.json` 取出该文件的
  完整编译命令直接执行,1~2 秒出结果。

## 四、硬性规则

1. **一次只验证少量功能。** 每次只引入一个变化,便于定位问题;为各功能设开关,
   或通过"线程是否启动"区分。
2. **验证分级,不得混淆。** 任何结论必须标明属于哪一级:

   | 等级 | 含义 |
   | --- | --- |
   | ① 文件检查通过 | 文件存在、内容与手册一致 |
   | ② 编译通过 | `cmake --build` 无错 |
   | ③ 逻辑测试通过 | 主机端可执行测试(见第五节) |
   | ④ 外设单独验证 | 需硬件 |
   | ⑤ 硬件功能验证 | 需硬件 |

   **「编译通过」不等于「通信或控制功能正确」。**
3. **不得编造通过结果。** 未实测的外设行为、未整定的参数,一律标注「待实测」。
4. **板级参数先查手册。** C 板官方手册、芯片手册、电调协议;每个数字都要能说出出处与计算过程。
5. **改 CubeMX 后回读生成的 `.c` 逐行核对数字。** 面板上没填的框不会报错,而是静默取默认值
   (已踩过:TIM10 的 `Period` 未填 → 静默取 65535 → 15.26 Hz 而非 1 kHz)。
   判据:`.ioc` 里该外设的 `IPParameters=` 清单**没有的参数 = 从未填过 = 用默认值**。
6. **电机默认不驱动。** `ENABLE_MOTOR_DRIVE = 0`。只有在拿到板子、轮子架空、限流、
   并准备好急停手段时才考虑启用。
7. **每次改动记录到 `docs/logs.md`**:问题 / 计算 / 修改 / 验证 / 等级 / 待办。
8. **提交前先 `git status --short` 过目。** 用显式路径 `git add`,**不要用 `git add -A`**
   (`build/` 产物与厂商代码会被卷进来)。

## 五、主机端测试(无 C 板时唯一的逻辑验证手段)

`libs/` 只依赖标准库,可以用本机编译器直接跑:

- 把 `libs/src/*.cpp` 与测试文件一起用本机 `g++` 编译成主机可执行文件
- 必须覆盖:PID 输出限幅、积分限幅与退饱和、位置式与增量式在相同输入下的等价性、
  `Clear()` 行为、编码器 8192 回绕、CAN 大端打包、`RELAX_MODE` 输出为零
- 测试通过只能记为等级 ③

## 六、已知隐患(不要重复踩)

| 位置 | 问题 | 处理 |
| --- | --- | --- |
| `board/cmake/gcc-arm-none-eabi.cmake` | 引用 `STM32F407XX_FLASH.ld`(`XX` 大写),`board/` 下实际文件是 `STM32F407xx_FLASH.ld`(`xx` 小写)。Windows 不区分大小写故当前可用,**Linux/WSL 下会链接失败** | 先留着,需与助教确认后再改 |
| `board/Drivers/` 下 288 个文件被删 | 这是 CubeMX 的 `Copy only the necessary library files` 行为,删除的是其他 MCU 型号与其他工具链的文件,属正常 | 如需其他型号头文件,回 CubeMX 打开该选项重新生成 |
| 仓库 `core.autocrlf=true` | 会产生大量"内容无差异却标记为已修改"的假修改 | 已用 `.gitattributes` 处理(厂商目录 `-text`,该规则须写在通配规则**之后**才生效) |
| LED 极性 | 参考资料中「高电平点亮」与「低电平点亮」两说并存,**无实测可判定** | 拿到 C 板第一件事就是确认 |

## 七、执行计划(与 `docs/logs.md` 的进度同步)

- [x] `board/` 构建打通(C++17 + ThreadX 接入,`board.elf` 可编译)
- [x] CubeMX 配置:时钟、GPIO、UART×2、CAN×2、SPI1、I²C×2、TIM1/TIM10、ThreadX
- [x] 中断优先级统一为 5(低于内核节拍 SysTick=4)
- [ ] `bsp/`:补 `bsp_usart.cpp`、`bsp_pwm.cpp`;新建 `bsp_spi.cpp`、`bsp_i2c.cpp`
- [ ] `libs/`:位置式 + 增量式 PID;主机端测试
- [ ] `modules/`:BMI088(SPI / 片选 / ID 校验 / 数据解析)、LED、M2006 双环
- [ ] `threads/`:`alive` / `imu` / `control` 三个线程 + 信号量心跳
- [ ] `docs/`:`chassis.md`、`spi.md`、`iic.md`、`refereces.md`
- [ ] 硬件到位后:按 `docs/logs.md` 的验证清单逐项实测
