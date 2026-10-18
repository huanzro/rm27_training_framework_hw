#include "pid.hpp"
#include <cmath>  //new

PID::PID(float kp, float ki, float kd, float maxOut, float maxIOut, int mode)
    : mode(mode), kp(kp), ki(ki), kd(kd), maxOut(maxOut), maxIOut(maxIOut), dt(0.01f), last_fdb(0.0f), fdbValid(false)
{
    Clear();
}

void PID::Tuning(float tuning_kp, float tuning_ki, float tuning_kd)
{
    kp = tuning_kp;
    ki = tuning_ki;
    kd = tuning_kd;
}

//new
void PID::SetDt(float period_s)
{
    if (std::isfinite(period_s) && period_s > 0.0f) {
        dt = period_s;
    }
}

void PID::UpdateResult()
{
if (!std::isfinite(ref) || !std::isfinite(fdb) || maxOut <= 0.0f) {
        Clear();
        return;
    }

    if (mode != PID_POSITION && mode != PID_DELTA) {
        Clear();
        return;
    }

   
    err[2] = err[1];
    err[1] = err[0];
    err[0] = ref - fdb;

    float dv = 0.0f;
    if (fdbValid) {
        dv = (fdb - last_fdb) / dt;
    }
    last_fdb = fdb;
    fdbValid = true;

    if (mode == PID_POSITION) {
        pResult = kp * err[0];
        iResult += ki * err[0];
        if (maxIOut > 0.0f) {
            iResult = Numeric::LimitABS(iResult, maxIOut);
        }
        dResult = -kd * dv;
        result = pResult + iResult + dResult;   
        result = Numeric::LimitABS(result, maxOut);

    } else { 
        float fd2 = (err[0] - 2.0f * err[1] + err[2]) / dt;

        float delta = kp * (err[0] - err[1])
                    + ki * err[0]
                    + kd * fd2;

        result = result + delta;

        // 限幅 
        result = Numeric::LimitABS(result, maxOut);

        pResult = kp * (err[0] - err[1]);
        iResult = ki * err[0];
        dResult = kd * fd2;

    }
}


void PID::Clear()
{
    ref = fdb = 0.0f;
    err[0] = err[1] = err[2] = 0.0f;
    pResult = iResult = dResult = result = 0.0f;
    last_fdb = 0.0f;//new
    fdbValid = false;//new
}
