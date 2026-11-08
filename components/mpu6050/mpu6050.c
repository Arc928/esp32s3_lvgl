#include "mpu6050.h"
#include "inv_mpu_dmp_motion_driver.h"
#include "inv_mpu.h"
#include <math.h>

#define q30  1073741824.0f //用于归一化四元数
#define DEFAULT_MPU_HZ  (200) //DMP采样频率200Hz

short gyro[3], accel[3], sensors; //用于读取DMP_FIFO

static signed char gyro_orientation[9] = { 1, 0, 0,  //方向矩阵
                                           0, 1, 0,
                                           0, 0, 1};

static  unsigned short inv_row_2_scale(const signed char *row) //用于初始化DMP
{
    unsigned short b;

    if (row[0] > 0)
        b = 0;
    else if (row[0] < 0)
        b = 4;
    else if (row[1] > 0)
        b = 1;
    else if (row[1] < 0)
        b = 5;
    else if (row[2] > 0)
        b = 2;
    else if (row[2] < 0)
        b = 6;
    else
        b = 7;      // error
    return b;

}

//i2c设备句柄
static i2c_master_dev_handle_t dev_handle;

static  unsigned short inv_orientation_matrix_to_scalar(
    const signed char *mtx) //用于初始化DMP
{
    unsigned short scalar;
    scalar = inv_row_2_scale(mtx);
    scalar |= inv_row_2_scale(mtx + 3) << 3;
    scalar |= inv_row_2_scale(mtx + 6) << 6;

    return scalar;

}

static void run_self_test(void) //用于初始化DMP
{
    int result;
    long gyro[3], accel[3];

    result = mpu_run_self_test(gyro, accel);
    if (result == 0x7) {
        /* Test passed. We can trust the gyro data here, so let's push it down
         * to the DMP.
         */
        float sens;
        unsigned short accel_sens;
        mpu_get_gyro_sens(&sens);
        gyro[0] = (long)(gyro[0] * sens);
        gyro[1] = (long)(gyro[1] * sens);
        gyro[2] = (long)(gyro[2] * sens);
        dmp_set_gyro_bias(gyro);
        mpu_get_accel_sens(&accel_sens);
        accel[0] *= accel_sens;
        accel[1] *= accel_sens;
        accel[2] *= accel_sens;
        dmp_set_accel_bias(accel);
        printf("setting bias succesfully ......\r\n");
    }

}

void delay_ms(uint32_t ms) 
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * @brief Read a sequence of bytes from a MPU6050 sensor registers
 */
esp_err_t mpu6050_register_read(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, len, MPU6050_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief Write a byte to a MPU6050 sensor register
 */
esp_err_t mpu6050_register_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), MPU6050_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief 设置MPU6050陀螺仪传感器满量程范围
 * @param fsr:0,±250dps;1,±500dps;2,±1000dps;3,±2000dps
 * @return ESP_OK,设置成功  其他,设置失败
 */
static esp_err_t MPU_Set_Gyro_Fsr(uint8_t fsr)
{
    return mpu6050_register_write_byte(dev_handle, MPU_GYRO_CFG_REG, fsr << 3);
}

/**
 * @brief 设置MPU6050加速度传感器满量程范围
 * @param fsr:0,±2g;1,±4g;2,±8g;3,±16g
 * @return ESP_OK,设置成功  其他,设置失败
 */
static esp_err_t MPU_Set_Accel_Fsr(uint8_t fsr)
{
    return mpu6050_register_write_byte(dev_handle, MPU_ACCEL_CFG_REG, fsr << 3);
}

/**
 * @brief 设置MPU6050的数字低通滤波器
 * @param lpf:数字低通滤波频率(Hz)
 * @return ESP_OK,设置成功  其他,设置失败
 */
static esp_err_t MPU_Set_LPF(uint16_t lpf)
{
    uint8_t data = 0;

    if (lpf >= 188) data = 1;
    else if (lpf >= 98) data = 2;
    else if (lpf >= 42) data = 3;
    else if (lpf >= 20) data = 4;
    else if (lpf >= 10) data = 5;
    else data = 6;
    return mpu6050_register_write_byte(dev_handle, MPU_CFG_REG, data);
}

/**
 * @brief 设置MPU6050的采样率(假定Fs=1KHz)
 * @param rate:4~1000(Hz)
 * @return ESP_OK,设置成功  其他,设置失败
 */
static esp_err_t MPU_Set_Rate(uint16_t rate)
{
    uint8_t data;
    if (rate > 1000) rate = 1000;
    if (rate < 4) rate = 4;
    data = 1000 / rate - 1;
    esp_err_t res = mpu6050_register_write_byte(dev_handle, MPU_SAMPLE_RATE_REG, data);
    if (res != ESP_OK) return res;
    return MPU_Set_LPF(rate); 
}

esp_err_t MPU6050ReadID(void) 
{
    uint8_t Re[2] = {0};
    mpu6050_register_read(dev_handle, MPU6050_WHO_AM_I, Re, 1);

    if (Re[0] != 0x68)
    {
        printf("检测不到 MPU6050 模块");
        return ESP_FAIL;
    }
    else
    {
        printf("MPU6050 ID = %x\r\n",Re[0]);
        return ESP_OK;

    }
}

esp_err_t MPU6050_Init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 先探测设备是否在总线上；不在就直接返回，避免后面一连串 I2C 报错
    if (i2c_master_probe(bus, MPU6050_SENSOR_ADDR, MPU6050_I2C_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    // 设备句柄只添加一次，掉线后重新初始化时复用
    if (dev_handle == NULL) {
        i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = MPU6050_SENSOR_ADDR,
            .scl_speed_hz = MPU6050_I2C_FREQ_HZ,
        };
        esp_err_t ret = i2c_master_bus_add_device(bus, &dev_config, &dev_handle);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    //复位6050
    mpu6050_register_write_byte(dev_handle, MPU6050_RA_PWR_MGMT_1, 0x80);
    vTaskDelay(pdMS_TO_TICKS(100));
    //电源管理寄存器
    //选择X轴陀螺作为参考PLL的时钟源，设置CLKSEL=001
    mpu6050_register_write_byte(dev_handle,MPU6050_RA_PWR_MGMT_1, 0x00);

    MPU_Set_Gyro_Fsr(3);    //陀螺仪传感器,±2000dps
    MPU_Set_Accel_Fsr(0);   //加速度传感器,±2g
    MPU_Set_Rate(50);

    mpu6050_register_write_byte(dev_handle, MPU_INT_EN_REG, 0x00);
    mpu6050_register_write_byte(dev_handle, MPU_USER_CTRL_REG, 0x00);
    mpu6050_register_write_byte(dev_handle, MPU_FIFO_EN_REG, 0x00);
    mpu6050_register_write_byte(dev_handle, MPU_INTBP_CFG_REG, 0X80);

    if (MPU6050ReadID() == ESP_OK)
    {
        mpu6050_register_write_byte(dev_handle, MPU6050_RA_PWR_MGMT_1, 0x01);
        mpu6050_register_write_byte(dev_handle, MPU_PWR_MGMT2_REG, 0x00);
        MPU_Set_Rate(50);
        return ESP_OK;
    }
    return ESP_FAIL;
}

/**
 * @brief 符合 DMP/通用驱动格式的 I2C 写函数
 * 
 * @param slave_addr 从机地址 (在 IDF v5 中，地址已绑定在 handle 里，此处忽略)
 * @param reg_addr   寄存器地址
 * @param length     写入数据的长度
 * @param data       数据指针
 * @return 0 表示成功 (ESP_OK), 非 0 表示失败
 */
int i2c_write(unsigned char slave_addr, unsigned char reg_addr,
              unsigned char length, unsigned char const *data)
{
    if (dev_handle == NULL) {
        return -1;
    }
    
    (void)slave_addr;

    uint8_t write_buf[32]; 
    
    if (length >= sizeof(write_buf)) {
        return -1;  // 长度检查
    }
    
    write_buf[0] = reg_addr;
    memcpy(&write_buf[1], data, length);
    
    esp_err_t ret = i2c_master_transmit(dev_handle, write_buf, length + 1, 
                                         MPU6050_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    
    return (ret == ESP_OK) ? 0 : -1;
}

/**
 * @brief 符合 DMP/通用驱动格式的 I2C 读函数
 * 
 * @param slave_addr 从机地址 (此处忽略)
 * @param reg_addr   寄存器地址
 * @param length     读取数据的长度
 * @param data       接收数据的 buffer
 * @return 0 表示成功 (ESP_OK), 非 0 表示失败
 */
int i2c_read(unsigned char slave_addr, unsigned char reg_addr,
             unsigned char length, unsigned char *data)
{
    if (dev_handle == NULL) {
        return -1;
    }

    (void)slave_addr;

    // ESP-IDF v5 直接支持 "写寄存器地址 -> Restart -> 读数据" 的操作
    esp_err_t ret = i2c_master_transmit_receive(dev_handle, 
                                                &reg_addr, 1, 
                                                data, length, 
                                                MPU6050_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);

    return (ret == ESP_OK) ? 0 : -1;
}

// DMP（数字运动处理器）初始化函数
// 芯片初始化、固件加载、开启 DMP 这三步失败就无法输出姿态，返回错误；
// 其余配置步骤失败只打警告（与原先行为一致，不中断初始化）
esp_err_t DMP_Init(void)
{
    static const char *TAG = "DMP";

    // 主初始化流程（成功返回0）
    if (mpu_init()) {
        ESP_LOGE(TAG, "mpu_init failed");
        return ESP_FAIL;
    }

    // 配置传感器使能状态：启用陀螺仪和加速度计的三轴数据
    if (mpu_set_sensors(INV_XYZ_GYRO | INV_XYZ_ACCEL))
        ESP_LOGW(TAG, "mpu_set_sensors failed");

    // FIFO配置：存储陀螺仪和加速度计数据
    if (mpu_configure_fifo(INV_XYZ_GYRO | INV_XYZ_ACCEL))
        ESP_LOGW(TAG, "mpu_configure_fifo failed");

    // 采样率设置
    if (mpu_set_sample_rate(DEFAULT_MPU_HZ))
        ESP_LOGW(TAG, "mpu_set_sample_rate failed");

    // DMP固件加载
    if (dmp_load_motion_driver_firmware()) {
        ESP_LOGE(TAG, "failed to load DMP firmware");
        return ESP_FAIL;
    }

    // 方向矩阵配置
    if (dmp_set_orientation(inv_orientation_matrix_to_scalar(gyro_orientation)))
        ESP_LOGW(TAG, "dmp_set_orientation failed");

    // 启用DMP特性
    if (dmp_enable_feature(DMP_FEATURE_6X_LP_QUAT | DMP_FEATURE_TAP |  // 6轴低功耗四元数 | 敲击检测
            DMP_FEATURE_ANDROID_ORIENT | DMP_FEATURE_SEND_RAW_ACCEL |  // Android方向识别 | 原始加速度数据
            DMP_FEATURE_SEND_CAL_GYRO | DMP_FEATURE_GYRO_CAL))         // 校准后的陀螺仪数据 | 陀螺仪校准
        ESP_LOGW(TAG, "dmp_enable_feature failed");

    // FIFO速率设置：DMP输出速率与采样率同步
    if (dmp_set_fifo_rate(DEFAULT_MPU_HZ))
        ESP_LOGW(TAG, "dmp_set_fifo_rate failed");

    run_self_test(); // 运行自检程序（校准传感器）

    // 启用DMP
    if (mpu_set_dmp_state(1)) {
        ESP_LOGE(TAG, "failed to enable DMP");
        return ESP_FAIL;
    }

    return ESP_OK;
}

uint8_t MPU6050_DMP_Get_Data(float *pitch, float *roll, float *yaw)
{
    unsigned long sensor_timestamp;
    unsigned char more = 0;
    long quat[4];
    long latest_quat[4];
    int has_quat = 0;
    float q0, q1, q2, q3;

    // DMP 以 200Hz 往 FIFO 写数据，调用方读得更慢。
    // 这里一次把 FIFO 读空，只用最新一包，避免数据滞后和 FIFO 溢出。
    // FIFO 最多 1024 字节，按每包 32 字节算不超过 32 包，循环上限留了余量。
    for (int i = 0; i < 40; i++) {
        if (dmp_read_fifo(gyro, accel, quat, &sensor_timestamp, &sensors, &more))
            break;
        if (sensors & INV_WXYZ_QUAT) {
            latest_quat[0] = quat[0];
            latest_quat[1] = quat[1];
            latest_quat[2] = quat[2];
            latest_quat[3] = quat[3];
            has_quat = 1;
        }
        if (!more)
            break;
    }

    // 四元数数据解析（q30格式转浮点）
    if (has_quat)
    {
        q0 = latest_quat[0] / q30;
        q1 = latest_quat[1] / q30;
        q2 = latest_quat[2] / q30;
        q3 = latest_quat[3] / q30;

        // 欧拉角计算（单位：度）
        *pitch = asin(-2 * q1 * q3 + 2 * q0 * q2) * 57.3;  // 俯仰角
        *roll  = atan2(2 * q2 * q3 + 2 * q0 * q1, 
                      -2 * q1 * q1 - 2 * q2 * q2 + 1) * 57.3;  // 横滚角
        *yaw   = atan2(2 * (q1 * q2 + q0 * q3),
                       q0 * q0 + q1 * q1 - q2 * q2 - q3 * q3) * 57.3;  // 偏航角
    }
    else {
        return 2;
    }
    
    return 0;
}