//
// alivethread.cpp —— 三个线程（imu / control / alive）的创建与实现。新建内容。
//

#include "threads.hpp"
#include "tx_api.h"
#include "BMI088.hpp"
#include "DJIMotorHandler.hpp"
#include "LED.hpp"
#include "M2006.hpp"
#include "can.h"
#include "main.h"

//new: 功能开关。默认全部关闭，只跑"读传感器 + 心跳"，不驱动电机、不加热。
//new: 拿到 C 板、轮子架空、限流、备好急停之后再逐个打开。
#ifndef ENABLE_MOTOR_DRIVE
#define ENABLE_MOTOR_DRIVE 0
#endif
#ifndef ENABLE_TEMP_CONTROL
#define ENABLE_TEMP_CONTROL 0
#endif
#ifndef ENABLE_GYRO_CALIBRATE
#define ENABLE_GYRO_CALIBRATE 0
#endif

namespace {

TX_THREAD alive_thread;
TX_THREAD imu_thread;
TX_THREAD control_thread;

//new: 两个心跳信号量。生产者 put，消费者在 alive 线程里取空。
//new: 必须"取空"而不是只取一次：信号量计数只增不减会在约 477 天后溢出，
//new: 溢出后 tx_semaphore_put 恒失败，但 alive 仍能取到正值 —— LED 正常、看门狗失效。
TX_SEMAPHORE imu_heartbeat;
TX_SEMAPHORE motor_heartbeat;

//new: ThreadX 要求栈按 8 字节对齐，栈大小也要求 8 的倍数
alignas(8) UCHAR alive_stack[2048];
alignas(8) UCHAR imu_stack[3072];
alignas(8) UCHAR control_stack[2048];

BMI088::cBMI088 imu;
M2006 motor;

//new: 初始化是否成功。alive 线程靠它决定红灯还是绿灯
volatile bool g_init_ok = false;

//new: 温控目标温度（℃）。BMI088 常用 40℃ 左右
constexpr float IMU_TARGET_TEMP_C = 40.0f;

/**
 * @brief alive 线程：灯效 + 心跳监视。优先级最低，100ms 一次。
 */
void alive_entry(ULONG)
{
    for (;;) {
        //new: 把信号量取空，只要取到过一次就认为该线程这一轮有在跑
        bool imu_alive = false;
        bool motor_alive = false;
        while (tx_semaphore_get(&imu_heartbeat, TX_NO_WAIT) == TX_SUCCESS) {
            imu_alive = true;
        }
        while (tx_semaphore_get(&motor_heartbeat, TX_NO_WAIT) == TX_SUCCESS) {
            motor_alive = true;
        }

        //new: 三色独立显示：红=初始化未完成，绿=imu 有心跳，蓝=control 有心跳
        LED::Set(!g_init_ok, imu_alive, motor_alive);

        tx_thread_sleep(100);
    }
}

/**
 * @brief imu 线程：配置传感器 → 周期读数据。优先级 6，10ms 一次。
 */
void imu_entry(ULONG)
{
    //new: 配置失败就等一会重试，不把错误当成"已就绪"
    for (;;) {
        imu.Config();
        imu.VerifyAccChipID();
        imu.VerifyGyroChipID();
        if (!imu.self_test.INIT_ERR) {
            break;
        }
        tx_thread_sleep(1000);
    }

    //new: 陀螺零偏标定。默认关闭，用头文件里的预标定值。
    //new: 打开时需约 4 秒（4000 次采样 × 1 tick），期间机器人必须静止。
    if (ENABLE_GYRO_CALIBRATE) {
        imu.Calibrate();
    }

    g_init_ok = true;

    for (;;) {
        imu.ReadAccData(&imu.acc_data);
        imu.ReadGyroData(&imu.gyro_data);
        imu.ReadAccTemperature(&imu.acc_data.temperature);

        imu.VerifyAccData();
        imu.VerifyGyroData();

        if (ENABLE_TEMP_CONTROL) {
            imu.TemperatureControl(IMU_TARGET_TEMP_C);
        }

        //new: 三项都正常才给心跳，异常时让 alive 线程看到灯色变化
        if (!imu.self_test.INIT_ERR && !imu.self_test.ACC_DATA_ERR &&
            !imu.self_test.GYRO_DATA_ERR) {
            (void)tx_semaphore_put(&imu_heartbeat);
        }

        tx_thread_sleep(10);
    }
}

/**
 * @brief control 线程：电机双环 + CAN 下发。优先级 8，10ms 一次。
 */
void control_entry(ULONG)
{
    DJIMotorHandler *handler = DJIMotorHandler::Instance();

    //new: 注册电机。M2006 配 C610 默认 ID 是 0x201，接在 CAN1 上。
    handler->registerMotor(&motor, &hcan1, 0x201);

    unsigned ticks = 0;

    for (;;) {
        //new: 每 10 拍（约 100ms）做一次在线检测。通信超时判据靠反馈帧计数是否在涨。
        if (++ticks >= 10) {
            handler->AllMotorAliveCheck();
            ticks = 0;
        }

        //new: 默认松开模式。只有开关打开且电机在线才进位置环。
        motor.controlMode = (ENABLE_MOTOR_DRIVE && motor.MotorState == DJIMotor::MOTOR_ONLINE)
                                ? DJIMotor::POS_MODE
                                : DJIMotor::RELAX_MODE;
        motor.positionSet = 0.0f;

        motor.setOutput();
        handler->sendControlData();

        (void)tx_semaphore_put(&motor_heartbeat);

        tx_thread_sleep(10);
    }
}

}  // namespace

//new: 由 App_ThreadX_Init() 调用。此时内核已初始化、调度器尚未启动。
UINT Threads_Init(void)
{
    UINT ret = TX_SUCCESS;

    //new: 信号量初始计数 0，表示"还没有任何一次心跳"
    ret = tx_semaphore_create(&imu_heartbeat, const_cast<CHAR *>("imu_hb"), 0);
    if (ret != TX_SUCCESS) return ret;

    ret = tx_semaphore_create(&motor_heartbeat, const_cast<CHAR *>("motor_hb"), 0);
    if (ret != TX_SUCCESS) return ret;

    //new: 优先级数字越小优先级越高。imu(6) > control(8) > alive(12)。
    //new: preempt_threshold 与 priority 相同表示不额外允许抢占降级。
    ret = tx_thread_create(&imu_thread, const_cast<CHAR *>("imu"), imu_entry, 0,
                           imu_stack, sizeof(imu_stack),
                           6, 6, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (ret != TX_SUCCESS) return ret;

    ret = tx_thread_create(&control_thread, const_cast<CHAR *>("control"), control_entry, 0,
                           control_stack, sizeof(control_stack),
                           8, 8, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (ret != TX_SUCCESS) return ret;

    ret = tx_thread_create(&alive_thread, const_cast<CHAR *>("alive"), alive_entry, 0,
                           alive_stack, sizeof(alive_stack),
                           12, 12, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (ret != TX_SUCCESS) return ret;

    return TX_SUCCESS;
}
