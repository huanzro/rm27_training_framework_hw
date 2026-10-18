#include "M2006.hpp"
#include <algorithm>
#include <cmath>

M2006::M2006()
{
    //new: M2006 配 C610 电调，减速比 36:1
    gearBox = GearBox_M2006;

    //new: maxCurrent 是【CAN 原始单位】，不是安培。
    //new: C610 协议里 ±10000 对应 ±10A，所以 3000 = 3A。
    //new: 这里取保守值；真机必须按 M2006 额定电流和实测温度重新确认，不要直接给 10000。
    maxCurrent = 3000;

    //new: 以下增益全是占位值，只能在轮子架空、限流、可急停的条件下按实车标定。
    //new: 内环速度：输出是电流原始单位，所以 maxOut 取 maxCurrent
    speedPid = PID(100.0f, 2.0f, 0.0f, static_cast<float>(maxCurrent),
                   static_cast<float>(maxCurrent) * 0.2f, PID_POSITION);
    //new: 外环位置：输出是速度设定值，单位 rad/s，限 10 rad/s（约 95 rpm）
    positionPid = PID(20.0f, 0.0f, 0.0f, 10.0f, 0.0f, PID_POSITION);

    //new: 控制线程周期 10ms，dt 用秒
    speedPid.SetDt(0.01f);
    positionPid.SetDt(0.01f);

    //new: 位置环输出给速度环当给定，所以速度环的 ref 来自 positionPid.result
    speedSet = 0.0f;
    positionSet = 0.0f;
    currentSet = 0;
}

void M2006::setOutput()
{
    //new: 松开模式或电机离线时，清掉两个环的状态并给零电流。
    //new: 清 PID 是为了下次重新进入控制时残积分不会带进来。
    if (controlMode == RELAX_MODE || MotorState != MOTOR_ONLINE) {
        speedPid.Clear();
        positionPid.Clear();
        currentSet = 0;
        return;
    }

    //new: 位置模式：先跑外环，把角度误差换成速度设定
    if (controlMode == POS_MODE) {
        positionPid.ref = positionSet;
        positionPid.fdb = motorFeedback.positionFdb;
        positionPid.UpdateResult();
        speedSet = positionPid.result;
    }

    //new: 速度环（位置模式和速度模式都会走这里）
    speedPid.ref = speedSet;
    speedPid.fdb = motorFeedback.speedFdb;
    speedPid.UpdateResult();

    //new: 再夹一次限幅。PID 内部已按 maxOut 限过，这里防 maxCurrent 被改小后越界
    currentSet = static_cast<int16_t>(std::clamp(speedPid.result,
                                                 -static_cast<float>(maxCurrent),
                                                 static_cast<float>(maxCurrent)));
}

M2006::MotorStateTypedef M2006::AliveCheck()
{
    //new: 本周期内 AliveFlag 有没有变化。变化说明收到了新的反馈帧。
    MotorState = (AliveFlag != Pre_AliveFlag) ? MOTOR_ONLINE : MOTOR_OFFLINE;
    Pre_AliveFlag = AliveFlag;

    //new: 掉线立刻清零输出，避免继续按上一次的电流驱动
    if (MotorState == MOTOR_OFFLINE) {
        currentSet = 0;
    }
    return MotorState;
}
