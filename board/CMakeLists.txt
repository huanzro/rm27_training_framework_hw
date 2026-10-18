cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# C++ 设置:bsp/libs/modules/threads 全部是 .cpp,必须显式指定标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME board)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME} LANGUAGES C CXX ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM, C and C++ languages
enable_language(C CXX ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
#
# 注意:此处只列出当前已存在且依赖已满足的源文件。
#
# 以下文件【暂未纳入】,原因见 docs/logs.md:
#   (无) —— bsp_i2c / LED / alivethread 均已创建并纳入。   //new
#
# 历史记录(依赖已满足,已恢复):
#   ../bsp/src/bsp_pwm.cpp —— 曾因缺少 tim.h 而排除;CubeMX 已配置 TIM1(PE9, 50Hz)
#       与 TIM10(PF6, 1kHz)并生成 tim.c/tim.h,故恢复。
#   ../modules/src/BMI088.cpp —— 曾因缺少 tx_api.h 而排除;CubeMX 已安装
#       X-CUBE-AZRTOS-F4 的 ThreadX 并生成 tx_api.h,故恢复。
#   ../bsp/src/bsp_spi.cpp —— 新建,依赖 spi.h(已生成),故纳入。   //new
#   ../bsp/src/bsp_i2c.cpp —— 新建,依赖 i2c.h(已生成),故纳入。   //new
#   ../modules/src/LED.cpp —— 新建,依赖 main.h 的 LED 引脚宏,故纳入。   //new
#   ../threads/src/alivethread.cpp —— 已编写,依赖 tx_api.h,故纳入。   //new
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ../bsp/src/bsp.cpp
    ../bsp/src/bsp_can.cpp
    ../bsp/src/bsp_usart.cpp
    ../bsp/src/bsp_pwm.cpp
    ../bsp/src/bsp_spi.cpp      #new
    ../bsp/src/bsp_i2c.cpp      #new
    ../libs/src/pid.cpp
    ../libs/src/math.cpp
    ../libs/src/crc.cpp
    ../modules/src/BMI088.cpp
    ../modules/src/M2006.cpp
    ../modules/src/DJIMotorHandler.cpp
    ../modules/src/LED.cpp      #new
    ../threads/src/alivethread.cpp  #new
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ../bsp/include
    ../libs/include
    ../modules/include
    ../threads/include
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
