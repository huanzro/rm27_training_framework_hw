// 主机端 PID 逻辑测试。不需要 C 板。
//
// 编译:
//   g++ -std=c++17 -I libs/include libs/src/pid.cpp libs/src/math.cpp tests/test_pid.cpp -o tests/test_pid.exe
// 运行:
//   .\tests\test_pid.exe
//
// 本文件只读取 libs/ 的实现,不修改任何现有代码。

#include "pid.hpp"
#include <cstdio>
#include <cmath>

static int g_fail = 0;
static int g_total = 0;

static void expect_near(const char* name, float got, float want, float tol = 1e-3f)
{
    g_total++;
    float diff = got - want;
    if (diff < 0.0f) diff = -diff;
    if (diff <= tol) {
        std::printf("  [PASS] %-34s %.6f\n", name, got);
    } else {
        g_fail++;
        std::printf("  [FAIL] %-34s got %.6f  want %.6f\n", name, got, want);
    }
}

int main()
{
    std::printf("===== PID 主机端测试 =====\n");

    // 用例 1:P 项单独作用
    {
        std::printf("\n[用例 1] 仅 P,kp=2, e=4 -> pResult=8, result=8\n");
        PID pid(2.0f, 0.0f, 0.0f, 100.0f, 0.0f, PID_POSITION);
        pid.ref = 10.0f;  pid.fdb = 6.0f;
        pid.UpdateResult();
        expect_near("pResult", pid.pResult, 8.0f);
        expect_near("result",  pid.result,  8.0f);
    }

    // 用例 2:输出限幅
    {
        std::printf("\n[用例 2] 同上但 maxOut=5 -> result 被钳到 5\n");
        PID pid(2.0f, 0.0f, 0.0f, 5.0f, 0.0f, PID_POSITION);
        pid.ref = 10.0f;  pid.fdb = 6.0f;
        pid.UpdateResult();
        expect_near("result", pid.result, 5.0f);
    }

    // 用例 3:积分限幅
    {
        std::printf("\n[用例 3] ki=1, e=1 持续 100 拍,maxIOut=3 -> iResult 停在 3\n");
        PID pid(0.0f, 1.0f, 0.0f, 1000.0f, 3.0f, PID_POSITION);
        pid.ref = 1.0f;  pid.fdb = 0.0f;
        for (int i = 0; i < 100; i++) pid.UpdateResult();
        expect_near("iResult", pid.iResult, 3.0f);
    }

    // 用例 4:退饱和(限幅不"粘住")
    {
        std::printf("\n[用例 4] 顶到 +3 后误差反号 5 拍 -> iResult 应为 -2\n");
        PID pid(0.0f, 1.0f, 0.0f, 1000.0f, 3.0f, PID_POSITION);
        pid.ref = 1.0f;  pid.fdb = 0.0f;
        for (int i = 0; i < 100; i++) pid.UpdateResult();
        pid.ref = 0.0f;  pid.fdb = 1.0f;
        for (int i = 0; i < 5; i++) pid.UpdateResult();
        expect_near("iResult", pid.iResult, -2.0f);
    }

    // 用例 5a:两模式等价性 —— kd = 0,ref 恒定
    //   手算依据:e 恒为 4,iResult 每拍累加 ki*e = 1.2
    //            第 N 拍 result = kp*e + ki*e*N = 6 + 1.2N
    //            第 50 拍 = 6 + 60 = 66
    {
        std::printf("\n[用例 5a] kd=0,ref 恒定 50 拍 -> result 应为 66,且两模式一致\n");
        PID pos(1.5f, 0.3f, 0.0f, 1e6f, 1e6f, PID_POSITION);
        PID inc(1.5f, 0.3f, 0.0f, 1e6f, 1e6f, PID_DELTA);
        pos.ref = 5.0f;  pos.fdb = 1.0f;
        inc.ref = 5.0f;  inc.fdb = 1.0f;
        for (int i = 0; i < 50; i++) { pos.UpdateResult(); inc.UpdateResult(); }
        expect_near("位置式 result", pos.result, 66.0f, 1e-2f);
        expect_near("增量式 result", inc.result, 66.0f, 1e-2f);
    }

    // 用例 5b:两模式等价性 —— kd != 0,给定值【恒定】
    //   位置式用微分先行(对反馈求导),增量式用误差二阶差分。
    //   ref 恒定时两者数学等价,这是检验增量式二阶差分实现是否正确的关键。
    //
    // 【首拍为什么不同】位置式在首拍因 fdbValid=false 而把 dv 置 0(D 项为 0);
    //   增量式首拍用 err[1]=err[2]=0 算出 fd2 = 4/0.01 = 400,得到虚假的 D 冲击。
    //   两者都是各自设计下的预期行为,因此本用例从【第 2 拍起】比较。
    {
        std::printf("\n[用例 5b] kd=0.8,ref 恒定 5,反馈沿固定轨迹变化\n");
        std::printf("          首拍两者必然不同(位置式屏蔽首拍微分),从第 2 拍起应完全一致\n");
        PID pos(1.5f, 0.3f, 0.8f, 1e6f, 1e6f, PID_POSITION);
        PID inc(1.5f, 0.3f, 0.8f, 1e6f, 1e6f, PID_DELTA);
        pos.ref = 5.0f;
        inc.ref = 5.0f;
        const float traj[6] = {1.0f, 1.2f, 1.5f, 1.9f, 2.4f, 3.0f};
        for (int i = 0; i < 6; i++) {
            pos.fdb = traj[i];
            inc.fdb = traj[i];
            pos.UpdateResult();
            inc.UpdateResult();
            if (i >= 1) {   // 从第 2 拍起逐拍比较
                char buf[64];
                std::snprintf(buf, sizeof(buf), "第 %d 拍 两模式之差", i + 1);
                expect_near(buf, inc.result - pos.result, 0.0f, 1e-3f);
            }
        }
        // 末拍绝对值(参考值:6 拍后为 -39.3,已用逐拍打印核对)
        expect_near("位置式末拍 result", pos.result, -39.3f, 1e-1f);
    }

    // 用例 6:Clear() 语义
    {
        std::printf("\n[用例 6] Clear() 后内部状态归零,ref 是否保持取决于你的选择\n");
        PID pid(1.0f, 1.0f, 0.0f, 100.0f, 10.0f, PID_POSITION);
        pid.ref = 10.0f;  pid.fdb = 0.0f;
        for (int i = 0; i < 10; i++) pid.UpdateResult();
        pid.Clear();
        expect_near("result",   pid.result,  0.0f);
        expect_near("iResult",  pid.iResult, 0.0f);
        expect_near("err[0]",   pid.err[0],  0.0f);
        // 你的 Clear() 目前会把 ref 也清零(pid.cpp 第 88 行),
        // 所以这里期望 0。若你以后改成"不清输入",把这条改成 10.0f。
        expect_near("ref(按当前实现应为 0)", pid.ref, 0.0f);
    }

    // 用例 7:dt 只影响 D 项
    {
        std::printf("\n[用例 7] dt 变化 10 倍 -> dResult 变化 10 倍,P/I 不变\n");
        PID a(3.0f, 0.5f, 1.0f, 1e6f, 0.0f, PID_POSITION);
        PID b(3.0f, 0.5f, 1.0f, 1e6f, 0.0f, PID_POSITION);
        a.SetDt(0.01f);   b.SetDt(0.001f);
        a.ref = 10.0f; a.fdb = 0.0f; a.UpdateResult();   // 建立 last_fdb / err[1]
        b.ref = 10.0f; b.fdb = 0.0f; b.UpdateResult();
        a.fdb = 2.0f; a.UpdateResult();                   // dv = 2/dt
        b.fdb = 2.0f; b.UpdateResult();
        expect_near("a.dResult(dt=0.01)",  a.dResult, -200.0f,  0.5f);
        expect_near("b.dResult(dt=0.001)", b.dResult, -2000.0f, 5.0f);
        expect_near("P 项不受 dt 影响", a.pResult - b.pResult, 0.0f, 1e-4f);
        expect_near("I 项不受 dt 影响", a.iResult - b.iResult, 0.0f, 1e-4f);
    }

    // 用例 8:NaN 输入被防御
    {
        std::printf("\n[用例 8] ref=NaN -> result 必须是有限值\n");
        PID pid(1.0f, 0.0f, 0.0f, 100.0f, 0.0f, PID_POSITION);
        pid.ref = std::nanf("");
        pid.fdb = 0.0f;
        pid.UpdateResult();
        g_total++;
        if (std::isfinite(pid.result)) {
            std::printf("  [PASS] %-34s %.6f\n", "result 有限", pid.result);
        } else {
            g_fail++;
            std::printf("  [FAIL] %-34s result 仍是 NaN\n", "result 有限");
        }
    }

    // 用例 9:非法 mode 被拦截
    {
        std::printf("\n[用例 9] mode=0(非法) -> result 必须归零,而非保持旧值\n");
        PID pid(1.0f, 0.0f, 0.0f, 100.0f, 0.0f, PID_POSITION);
        pid.ref = 10.0f;  pid.fdb = 0.0f;
        pid.UpdateResult();
        float before = pid.result;
        pid.mode = 0;
        pid.UpdateResult();
        std::printf("        (破坏前 result = %.3f)\n", before);
        expect_near("非法 mode 后 result 归零", pid.result, 0.0f);
    }

    // 用例 10:SetDt 拒绝非法值
    {
        std::printf("\n[用例 10] SetDt(0) / SetDt(-1) / SetDt(NaN) 应被拒绝,dt 不变\n");
        PID pid(1.0f, 0.0f, 0.0f, 100.0f, 0.0f, PID_POSITION);
        pid.SetDt(0.02f);
        expect_near("设置成功", pid.dt, 0.02f, 1e-6f);
        pid.SetDt(0.0f);
        expect_near("拒绝 0,dt 不变", pid.dt, 0.02f, 1e-6f);
        pid.SetDt(-1.0f);
        expect_near("拒绝负数,dt 不变", pid.dt, 0.02f, 1e-6f);
        pid.SetDt(std::nanf(""));
        expect_near("拒绝 NaN,dt 不变", pid.dt, 0.02f, 1e-6f);
    }

    std::printf("\n===== 结果:%d / %d 通过 =====\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
