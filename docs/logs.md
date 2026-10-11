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
  - [ ] 启用 C++ 并把 `bsp/libs/modules/threads` 的源码纳入构建(阶段 2)
