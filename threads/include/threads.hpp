//
// threads.hpp —— ThreadX 线程创建入口。新建文件。
//

#ifndef THREADS_HPP
#define THREADS_HPP

#include "tx_api.h"

//new: 必须加 extern "C"：本头文件会被 board/Core/Src/app_threadx.c(按 C 编译)包含，
//new: 而实现在 alivethread.cpp(按 C++ 编译)。不加的话 C 侧找的是 Threads_Init，
//new: C++ 侧生成的是 _Z12Threads_Initv，链接时报 undefined reference。
#ifdef __cplusplus
extern "C" {
#endif

//new: 创建信号量与三个线程。必须在 tx_application_define 阶段调用。
//new: 本工程由 App_ThreadX_Init() 调用，不能自己再定义 tx_application_define。
UINT Threads_Init(void);

#ifdef __cplusplus
}
#endif

#endif // THREADS_HPP
