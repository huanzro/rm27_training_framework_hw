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
#   ../modules/src/BMI088.cpp —— 第 2 行 #include "tx_api.h",ThreadX 尚未通过
#       CubeMX 安装,编译报 "fatal error: tx_api.h: No such file or directory"。
#       待 CubeMX 配置 ThreadX 后取消下一行的注释。
#   ../bsp/src/bsp_pwm.cpp   —— bsp_pwm.hpp 需要 "tim.h",而 TIM1/TIM10 尚未配置。
#       待 CubeMX 配置 TIM1/TIM10 后加入。
#   ../threads/src/alivethread.cpp —— 当前为 0 字节,尚未编写。
#   ../bsp/src/bsp_spi.cpp / bsp_i2c.cpp / ../modules/src/LED.cpp —— 文件尚未创建。
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ../bsp/src/bsp.cpp
    ../bsp/src/bsp_can.cpp
    ../bsp/src/bsp_usart.cpp
    ../libs/src/pid.cpp
    ../libs/src/math.cpp
    ../libs/src/crc.cpp
    # ../modules/src/BMI088.cpp   # 待 ThreadX 装好后启用
    ../modules/src/M2006.cpp
    ../modules/src/DJIMotorHandler.cpp
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
