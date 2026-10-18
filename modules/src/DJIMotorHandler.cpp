#include "DJIMotorHandler.hpp"
#include "bsp_can.hpp"
#include <cstring>

namespace {

//new: 把 CAN 句柄翻译成 0/1 的数组下标。不认识就返回 -1
int busIndex(CAN_HandleTypeDef *bus)
{
    if (bus == &hcan1) return 0;
    if (bus == &hcan2) return 1;
    return -1;
}

//new: 反馈帧里数值都是大端（高字节在前），这里拼成有符号 16 位
int16_t be16(const uint8_t *p)
{
    return static_cast<int16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

//new: CAN 接收回调的转发函数。bsp 只负责校验帧格式，解析交给这里
void canRxTrampoline(CAN_HandleTypeDef *hcan, uint32_t stdId,
                     const uint8_t *data, uint8_t dlc)
{
    if (dlc != 8 || stdId < 0x201 || stdId > 0x208) {
        return;
    }
    DJIMotorHandler::Instance()->updateFeedback(hcan, data, static_cast<int>(stdId - 0x201));
}

}  // namespace

void DJIMotorHandler::registerMotor(DJIMotor *motor, CAN_HandleTypeDef *hcan, uint16_t canId)
{
    //new: 句柄、总线、ID 三重检查。电机 ID 只允许 0x201~0x208
    const int bus = busIndex(hcan);
    if (motor == nullptr || bus < 0 || canId < 0x201 || canId > 0x208) {
        return;
    }

    const int slot = static_cast<int>(canId - 0x201);

    //new: 同一槽位已被别的电机占用就不覆盖（防止重复注册）
    if (DJIMotorList[bus][slot] != nullptr && DJIMotorList[bus][slot] != motor) {
        return;
    }

    DJIMotorList[bus][slot] = motor;
    motor->hcan = hcan;
    motor->canId = canId;

    //new: 按 ID 决定归哪个控制报文：0x201~0x204 走 0x200，0x205~0x208 走 0x1FF
    if (bus == 0 && slot < 4) CAN1_0x200_Exist = true;
    if (bus == 0 && slot >= 4) CAN1_0x1FF_Exist = true;
    if (bus == 1 && slot < 4) CAN2_0x200_Exist = true;
    if (bus == 1 && slot >= 4) CAN2_0x1FF_Exist = true;

    //new: 注册 CAN 接收回调（重复调用只是覆盖同一个函数指针，幂等）
    CAN_SetRxCallback(canRxTrampoline);
}

void DJIMotorHandler::sendControlData()
{
    //new: 每帧 8 字节 = 4 个 int16，先全部清零。
    //new: 没注册电机的槽位保持 0，等于该槽位给零电流。
    std::memset(can1_send_data_0, 0, sizeof(can1_send_data_0));
    std::memset(can1_send_data_1, 0, sizeof(can1_send_data_1));
    std::memset(can2_send_data_0, 0, sizeof(can2_send_data_0));
    std::memset(can2_send_data_1, 0, sizeof(can2_send_data_1));

    for (int bus = 0; bus < 2; ++bus) {
        for (int slot = 0; slot < 8; ++slot) {
            DJIMotor *motor = DJIMotorList[bus][slot];
            if (motor == nullptr) {
                continue;
            }

            //new: 选对应的 8 字节帧
            uint8_t *frame = nullptr;
            if (bus == 0) {
                frame = (slot < 4) ? can1_send_data_0 : can1_send_data_1;
            } else {
                frame = (slot < 4) ? can2_send_data_0 : can2_send_data_1;
            }

            //new: 帧内偏移 = (slot % 4) * 2。大端写入：高字节在前
            const int offset = (slot % 4) * 2;
            const uint16_t raw = static_cast<uint16_t>(motor->currentSet);
            frame[offset] = static_cast<uint8_t>(raw >> 8);
            frame[offset + 1] = static_cast<uint8_t>(raw & 0xFF);
        }
    }

    //new: 只发确实有电机的报文，避免空帧占总线
    if (CAN1_0x200_Exist) CAN_Transmit(&hcan1, 0x200, can1_send_data_0, 8);
    if (CAN1_0x1FF_Exist) CAN_Transmit(&hcan1, 0x1FF, can1_send_data_1, 8);
    if (CAN2_0x200_Exist) CAN_Transmit(&hcan2, 0x200, can2_send_data_0, 8);
    if (CAN2_0x1FF_Exist) CAN_Transmit(&hcan2, 0x1FF, can2_send_data_1, 8);
}

void DJIMotorHandler::updateFeedback(CAN_HandleTypeDef *hcan, const uint8_t *rx_data, int index)
{
    //new: 校验总线、数据和下标，再找对应电机
    const int bus = busIndex(hcan);
    if (bus < 0 || rx_data == nullptr || index < 0 || index >= 8) {
        return;
    }

    DJIMotor *motor = DJIMotorList[bus][index];
    if (motor != nullptr) {
        UpdateSensorData(motor, rx_data);
    }
}

void DJIMotorHandler::UpdateSensorData(DJIMotor *motor, const uint8_t *can_data)
{
    if (motor == nullptr || can_data == nullptr) {
        return;
    }

    DJIMotor::MotorFeedBack &fb = motor->motorFeedback;   //new: 嵌套类型要带外类名
    const uint16_t previous = fb.ecd;

    //new: 反馈帧格式（C610/C620 一致，均为大端）：
    //new:   [0..1] 机械角度 0~8191   [2..3] 转速 rpm
    //new:   [4..5] 实际转矩电流      [6] 温度     [7] 保留
    //new:   注意：0x200 控制帧里没有温度，8 个字节全是 4 个电机的电流。
    fb.last_ecd = static_cast<int16_t>(previous);
    fb.ecd = static_cast<uint16_t>(be16(can_data)) & 0x1FFF;
    fb.speed_rpm = be16(can_data + 2);

    //new: 电流原始值单位是 0.01A，乘 0.01 换成安培方便观察
    fb.currentFdb = static_cast<float>(be16(can_data + 4)) * 0.01f;
    fb.temperatureFdb = static_cast<float>(can_data[6]);

    //new: 减速比。M2006 = 36:1，其余按 1 处理
    const float ratio = (motor->gearBox == GearBox_M2006) ? 36.0f : 1.0f;

    //new: 位置用"增量累加"而不是直接换算，这样编码器 8192 回绕不会产生跳变。
    //new: 第一帧没有基准，跳过累加（AliveFlag == 0 时）。
    fb.lastPositionFdb = fb.positionFdb;
    if (motor->AliveFlag != 0) {
        int delta = static_cast<int>(fb.ecd) - static_cast<int>(previous);
        if (delta > 4096) delta -= 8192;      //new: 从 8191 跳到 0
        if (delta < -4096) delta += 8192;     //new: 从 0 跳回 8191
        fb.positionFdb += static_cast<float>(delta) * (6.28318530718f / (8192.0f * ratio));
    }

    //new: 转速 rpm（转子）→ 输出轴角速度 rad/s
    fb.lastSpeedFdb = fb.speedFdb;
    fb.speedFdb = static_cast<float>(fb.speed_rpm) * (6.28318530718f / (60.0f * ratio));

    //new: 心跳计数，AliveCheck 靠它判断电机是否还在回帧
    ++motor->AliveFlag;
}

void DJIMotorHandler::AllMotorAliveCheck()
{
    //new: 遍历两条总线上的 8 个槽位
    for (int bus = 0; bus < 2; ++bus) {
        for (int slot = 0; slot < 8; ++slot) {
            DJIMotor *motor = DJIMotorList[bus][slot];
            if (motor != nullptr) {
                motor->AliveCheck();
            }
        }
    }
}
