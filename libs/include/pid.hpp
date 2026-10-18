//
// Created by cosmosmount on 2025/8/29.
//

#ifndef RM26_PID_HPP
#define RM26_PID_HPP

#include "math.hpp"
#include <cstdint>

/**
 * @brief PID模式
 */
enum PidModeType
{
    PID_POSITION = 0x01, // 位置式 PID
    PID_DELTA = 0x02,    // 增量式 PID
};

/**
 * @brief PID类
 */
class PID
{
public:
    uint8_t mode;

    float kp;
    float ki;
    float kd;

    float ref;
    float fdb;
    float err[3]{};

    float pResult;
    float iResult;
    float dResult;
    float result;

    float maxOut;
    float maxIOut;

    // new
    float dt;          // 采样周期,单位【秒】
    float last_fdb;    // 上一次反馈值 —— 【仅位置式的微分先行使用】
    bool  fdbValid;    // last_fdb 是否已有效 —— 【仅位置式的微分先行使用】
                      // 避免首拍用无意义的 last_fdb 算出巨大的假微分值

    PID(float kp, float ki, float kd, float maxOut, float maxIOut, int mode = PID_POSITION);
    void Tuning(float tuning_kp, float tuning_ki, float tuning_kd);
    void UpdateResult();
    void SetDt(float period_s);//new
    void Clear();
};

#endif //RM26_PID_HPP
