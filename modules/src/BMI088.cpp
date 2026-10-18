#include "BMI088.hpp"
#include "tx_api.h"
#include "bsp_pwm.hpp"
#include "bsp_spi.hpp"      //new: SPI_Transfer / SPI_Write
#include <cmath>
#include <cstring>

namespace BMI088
{
    //new: 小端拼成有符号 16 位（BMI088 的数据寄存器是低字节在前）
    namespace {
    inline int16_t le16(const uint8_t *p)
    {
        return static_cast<int16_t>(static_cast<uint16_t>(p[0]) |
                                    (static_cast<uint16_t>(p[1]) << 8));
    }

    //new: 按片选选对应的 GPIO 端口和引脚
    inline GPIO_TypeDef *csPort(enum BMI088_SENSOR cs)
    {
        return (cs == BMI088_CS_ACC) ? BMI088_CS_ACC_PORT : BMI088_CS_GYRO_PORT;
    }
    inline uint16_t csPin(enum BMI088_SENSOR cs)
    {
        return (cs == BMI088_CS_ACC) ? BMI088_CS_ACC_PIN : BMI088_CS_GYRO_PIN;
    }
    }  // namespace

    /**
     * @brief BMI088 acc gyro 标定
     * @note 标定后的陀螺仪零偏存储在 Gyro_offset 中。
     * @attention 不管工作模式是blocking还是IT,标定时都是blocking模式,所以不用担心中断关闭后无法标定(RobotInit关闭了全局中断)
     * @attention 标定精度和等待时间有关。
     * @todo 将标定次数(等待时间)变为参数供设定
     * @section 整体流程为1.累加加速度数据计算gNrom()
     *                   2.累加陀螺仪数据计算零飘
     *                   3. 如果标定过程运动幅度过大,重新标定
     *                   4.保存标定参数
     */
    void cBMI088::Calibrate()
    {
        const int calib_samples = 4000; // 采样次数
        float gyro_sum[3] = {0.0f, 0.0f, 0.0f};
        gyro_data_t temp_gyro;

        // 1. 清除旧的 Offset，防止叠加
        Gyro_offset[0] = 0.0f;
        Gyro_offset[1] = 0.0f;
        Gyro_offset[2] = 0.0f;

        // 2. 循环采样
        for (int i = 0; i < calib_samples; i++)
        {
            ReadGyroData(&temp_gyro); // 这里读取的是原始值（因为Offset已清零）
            gyro_sum[0] += temp_gyro.x;
            gyro_sum[1] += temp_gyro.y;
            gyro_sum[2] += temp_gyro.z;
            
            tx_thread_sleep(1); // 等待一个 ThreadX tick；实际耗时由 tick 频率决定。
        }

        // 3. 计算平均值作为零偏
        Gyro_offset[0] = gyro_sum[0] / calib_samples;
        Gyro_offset[1] = gyro_sum[1] / calib_samples;
        Gyro_offset[2] = gyro_sum[2] / calib_samples;
        
        // 如果零偏过大（例如超过 0.1 rad/s），可能是在运动中标定的，应报错或丢弃
        if (fabs(Gyro_offset[0]) > 0.1f || fabs(Gyro_offset[1]) > 0.1f || fabs(Gyro_offset[2]) > 0.1f)
        {
            self_test.CALIBRATE_ERR = true;
            // 恢复默认值或保留上次值
            Gyro_offset[0] = BMI088_GYRO_PRE_CALI_OFFSET_X; 
            Gyro_offset[1] = BMI088_GYRO_PRE_CALI_OFFSET_Y;
            Gyro_offset[2] = BMI088_GYRO_PRE_CALI_OFFSET_Z;
        }
        else
        {
            self_test.CALIBRATE_ERR = false;
        }
    }

    void cBMI088::TemperatureControl(float target_temp)
    {
        //new: 先读温度
        float current = 0.0f;
        ReadAccTemperature(&current);

        //new: 初始化失败、温度非有限值或明显越界时，直接关加热。
        //new: 加热是"只增不减"的物理过程，读错温度还加热会把传感器烤坏。
        if (self_test.INIT_ERR || !std::isfinite(current) ||
            current < -40.0f || current > 100.0f) {
            self_test.TEMP_CTRL_ERR = true;
            PWM_SetDutyRatio(&HEATING_RESISTANCE_TIM, 0.0f, HEATING_RESISTANCE_CHANNEL);
            return;
        }

        self_test.TEMP_CTRL_ERR = false;

        //new: 温度 PID：目标 - 当前 = 误差，输出直接当占空比
        TempPid.ref = target_temp;
        TempPid.fdb = current;
        TempPid.UpdateResult();

        //new: 再夹一次到 [0,1]，防止 PID 参数异常时写出奇怪占空比
        float duty = TempPid.result;
        if (duty < 0.0f) duty = 0.0f;
        if (duty > 1.0f) duty = 1.0f;

        PWM_SetDutyRatio(&HEATING_RESISTANCE_TIM, duty, HEATING_RESISTANCE_CHANNEL);
    }

    void cBMI088::VerifyAccChipID()
    {
        uint8_t pRxData[2]; //< 读取两个字节，第一个是需要丢弃的无效字节，第二个是芯片 ID

        ReadReg(BMI088_CS_ACC, ACC_CHIP_ID_ADDR, pRxData, 2); //< 读取加速度计chip id
        tx_thread_sleep(1);
        //< 如果chip id不等于预设值,则加速度计ID错误,初始化错误
        if (pRxData[1] != ACC_CHIP_ID_VAL)
        {
            self_test.ACC_CHIP_ID_ERR = true;
            self_test.INIT_ERR = true;
        }
        else if (pRxData[1] == ACC_CHIP_ID_VAL)
        {
            self_test.ACC_CHIP_ID_ERR = false;
        }
    }

    void cBMI088::VerifyGyroChipID()
    {
        uint8_t pRxData;                                                 //< 读取一个字节,chip id
        ReadReg(BMI088_CS_GYRO, GYRO_CHIP_ID_ADDR, &pRxData, 1); //< 读取陀螺仪chip id
        tx_thread_sleep(1);
        //< 如果chip id不等于预设值,则陀螺仪ID错误,初始化错误
        if (pRxData != GYRO_CHIP_ID_VAL)
        {
            self_test.GYRO_CHIP_ID_ERR = true;
            self_test.INIT_ERR = true;
        }
        else if (pRxData == GYRO_CHIP_ID_VAL)
        {
            self_test.GYRO_CHIP_ID_ERR = false;
        }
    }

    void cBMI088::VerifyAccData()
    {
        //new: 检查最近一次 ReadAccData 的结果（acc_data 是基类成员）
        const float x = acc_data.x;
        const float y = acc_data.y;
        const float z = acc_data.z;

        //new: 非有限值 / 三轴全零 / 幅值超过满量程 24g，都判为数据异常
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            self_test.ACC_DATA_ERR = true;
            return;
        }
        if (x == 0.0f && y == 0.0f && z == 0.0f) {
            //new: 三轴恒为零说明数据线没通，或寄存器还没输出
            self_test.ACC_DATA_ERR = true;
            return;
        }
        const float norm = std::sqrt(x * x + y * y + z * z);
        self_test.ACC_DATA_ERR = (norm > 24.0f * 9.80665f);
    }

    void cBMI088::VerifyGyroData()
    {
        //new: 检查最近一次 ReadGyroData 的结果
        const float x = gyro_data.x;
        const float y = gyro_data.y;
        const float z = gyro_data.z;

        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            self_test.GYRO_DATA_ERR = true;
            return;
        }
        //new: 静止时三轴全零是可疑的（正常会有微小噪声），但也可能是真的静止，
        //new: 所以这里只判"超过满量程"这一个硬条件，全零留给上层的零偏判断。
        constexpr float MAX_RAD_S = 2000.0f * 3.14159265358979f / 180.0f;  // 2000 dps
        const float norm = std::sqrt(x * x + y * y + z * z);
        self_test.GYRO_DATA_ERR = (norm > MAX_RAD_S);
    }

    void cBMI088::WriteReg(enum BMI088_SENSOR cs, uint8_t addr, uint8_t *data, uint8_t len)
    {
        //new: 参数检查。tx 缓冲区固定 9 字节 = 1 地址 + 最多 8 数据
        if (data == nullptr || len == 0 || len > 8) {
            self_test.INIT_ERR = true;
            return;
        }

        uint8_t tx[9] = {};
        tx[0] = static_cast<uint8_t>(addr & BMI088_SPI_WRITE_CODE);   //new: 写 = 地址 & 0x7F
        std::memcpy(tx + 1, data, len);

        //new: 片选拉低 → 传输 → 拉高。顺序不能反，整个传输期间 CS 必须保持低
        HAL_GPIO_WritePin(csPort(cs), csPin(cs), GPIO_PIN_RESET);
        const HAL_StatusTypeDef st = SPI_Write(&BMI088_SPI_HANDLE, tx, len + 1, 10);
        HAL_GPIO_WritePin(csPort(cs), csPin(cs), GPIO_PIN_SET);

        if (st != HAL_OK) {
            self_test.INIT_ERR = true;
        }
    }

    void cBMI088::ReadReg(enum BMI088_SENSOR cs, uint8_t addr, uint8_t *data, uint8_t len)
    {
        //new: 参数检查
        if (data == nullptr || len == 0 || len > 8) {
            self_test.INIT_ERR = true;
            return;
        }

        uint8_t tx[9] = {};
        uint8_t rx[9] = {};
        tx[0] = static_cast<uint8_t>(addr | BMI088_SPI_READ_CODE);    //new: 读 = 地址 | 0x80

        //new: SPI 没有"单纯读"，必须同时发字节才有时钟，所以多传一个字节
        HAL_GPIO_WritePin(csPort(cs), csPin(cs), GPIO_PIN_RESET);
        const HAL_StatusTypeDef st = SPI_Transfer(&BMI088_SPI_HANDLE, tx, rx, len + 1, 10);
        HAL_GPIO_WritePin(csPort(cs), csPin(cs), GPIO_PIN_SET);

        if (st != HAL_OK) {
            for (uint8_t i = 0; i < len; ++i) data[i] = 0;
            self_test.INIT_ERR = true;
            return;
        }

        //new: rx[0] 对应"发地址那一拍"，是无效回显，丢弃；数据从 rx[1] 开始。
        //new: 注意加速度计比陀螺仪多一个无效字节，那一字节由调用者按需再丢。
        for (uint8_t i = 0; i < len; ++i) data[i] = rx[i + 1];
    }

    void cBMI088::Config()
    {
        //new: 每次重新配置都先把错误标志和零偏复位，避免上一次的失败状态残留
        self_test.INIT_ERR = false;
        Gyro_offset[0] = BMI088_GYRO_PRE_CALI_OFFSET_X;
        Gyro_offset[1] = BMI088_GYRO_PRE_CALI_OFFSET_Y;
        Gyro_offset[2] = BMI088_GYRO_PRE_CALI_OFFSET_Z;

        tx_thread_sleep(10); //< 等待系统稳定

        //new: 加速度计上电默认在 I2C 模式，需要一次 SPI 读写才会切到 SPI 模式。
        //new: 这里读一次 chip id 当虚读，读到的内容不用。
        {
            uint8_t dummy[2] = {};
            ReadReg(BMI088_CS_ACC, ACC_CHIP_ID_ADDR, dummy, 2);
        }

        /*-------------------------------------加速度计初始化-------------------------------------*/
        
        //< 先软重启，清空所有寄存器
        uint8_t pTxData;
        pTxData = ACC_SOFTRESET_VAL;
        WriteReg(BMI088_CS_ACC, ACC_SOFTRESET_ADDR, &pTxData, 1);
        tx_thread_sleep(100); //< 延时100ms,重启需要时间

        //< 打开加速度计电源
        pTxData = ACC_PWR_CTRL_ON;
        WriteReg(BMI088_CS_ACC, ACC_PWR_CTRL_ADDR, &pTxData, 1);
        tx_thread_sleep(150);

        //< 加速度计变成正常模式
        pTxData = ACC_PWR_CONF_ACT;
        WriteReg(BMI088_CS_ACC, ACC_PWR_CONF_ADDR, &pTxData, 1);
        tx_thread_sleep(10); //

        //< 测量范围
        pTxData = ACC_RANGE_6G;
        WriteReg(BMI088_CS_ACC, ACC_RANGE_ADDR, &pTxData, 1);
        tx_thread_sleep(5); //< 延时5ms

        pTxData = 0xAB;
        WriteReg(BMI088_CS_ACC, ACC_CONF_ADDR, &pTxData, 1);
        tx_thread_sleep(5); //< 延时5ms

        pTxData = 0x08;
        WriteReg(BMI088_CS_ACC, INT1_IO_CTRL_ADDR, &pTxData, 1);
        tx_thread_sleep(5); //< 延时5ms

        pTxData = 0x04;
        WriteReg(BMI088_CS_ACC, INT_MAP_DATA_ADDR, &pTxData, 1);
        tx_thread_sleep(5); //< 延时5ms

        /*-------------------------------------陀螺仪初始化-------------------------------------*/
        //< 先软重启，清空所有寄存器
        pTxData = GYRO_SOFTRESET_VAL;
        WriteReg(BMI088_CS_GYRO, GYRO_SOFTRESET_ADDR, &pTxData, 1);
        tx_thread_sleep(100); //< 延时100ms,重启需要时间

        pTxData = GYRO_RANGE_2000_DEG_S;
        WriteReg(BMI088_CS_GYRO, GYRO_RANGE_ADDR, &pTxData, 1);
        tx_thread_sleep(5); //< 延时5ms

        pTxData = GYRO_ODR_2000Hz_BANDWIDTH_230Hz | GYRO_LPM1_SUS;
        WriteReg(BMI088_CS_GYRO, GYRO_BANDWIDTH_ADDR, &pTxData, 1);
        tx_thread_sleep(5); //< 延时5ms

        pTxData = GYRO_LPM1_NOR;
        WriteReg(BMI088_CS_GYRO, GYRO_LPM1_ADDR, &pTxData, 1);
        tx_thread_sleep(5); //< 延时5ms

        pTxData = GYRO_DRDY_ON;
        WriteReg(BMI088_CS_GYRO, GYRO_INT_CTRL_ADDR, &pTxData, 1);
        tx_thread_sleep(5); //< 延时5ms

        pTxData = 0x00;
        WriteReg(BMI088_CS_GYRO, GYRO_INT3_INT4_IO_CONF_ADDR, &pTxData, 1);
        tx_thread_sleep(5); //< 延时5ms

        pTxData = 0x01;
        WriteReg(BMI088_CS_GYRO, GYRO_INT3_INT4_IO_MAP_ADDR, &pTxData, 1);
        tx_thread_sleep(5); //< 延时5ms
    }


    void cBMI088::ReadAccData(acc_data_t *data)
    {
        if (data == nullptr) return;

        //new: 0x12~0x17 共 6 字节。ReadReg 已丢掉地址回显，
        //new: 但加速度计还多一个无效字节，所以多读一个，用 buf[1..6]
        uint8_t buf[7] = {};
        ReadReg(BMI088_CS_ACC, ACC_X_LSB_ADDR, buf, 7);

        data->x = static_cast<float>(le16(&buf[1])) * IMU_ACCEL_6G_SEN;
        data->y = static_cast<float>(le16(&buf[3])) * IMU_ACCEL_6G_SEN;
        data->z = static_cast<float>(le16(&buf[5])) * IMU_ACCEL_6G_SEN;
        //new: 温度是独立寄存器，由 ReadAccTemperature 负责，这里不动它
    }

    void cBMI088::ReadGyroData(gyro_data_t *data)
    {
        if (data == nullptr) return;

        //new: 0x02~0x07 共 6 字节，陀螺仪没有多余的无效字节
        uint8_t buf[6] = {};
        ReadReg(BMI088_CS_GYRO, GYRO_RATE_X_LSB_ADDR, buf, 6);

        //new: 换算成 rad/s 后减零偏
        data->x = static_cast<float>(le16(&buf[0])) * IMU_GYRO_2000_SEN - Gyro_offset[0];
        data->y = static_cast<float>(le16(&buf[2])) * IMU_GYRO_2000_SEN - Gyro_offset[1];
        data->z = static_cast<float>(le16(&buf[4])) * IMU_GYRO_2000_SEN - Gyro_offset[2];
    }

    void cBMI088::ReadAccTemperature(float *temp)
    {
        if (temp == nullptr) return;

        //new: 0x22(MSB) / 0x23(LSB)。同样要多丢一个加速度计无效字节
        uint8_t buf[3] = {};
        ReadReg(BMI088_CS_ACC, TEMP_MSB_ADDR, buf, 3);

        //new: 温度是 11 位有符号：MSB 全部 8 位 + LSB 的高 3 位
        int16_t raw = static_cast<int16_t>(
            (static_cast<uint16_t>(buf[1]) << 3) | (static_cast<uint16_t>(buf[2]) >> 5));

        //new: 11 位补码：大于 1023 视为负数
        if (raw > 1023) raw = static_cast<int16_t>(raw - 2048);

        //new: 每 LSB 0.125℃，零点 23℃
        *temp = static_cast<float>(raw) * TEMP_UNIT + TEMP_BIAS;
    }

}
