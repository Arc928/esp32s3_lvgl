#include "mpu6050.h"
#include "inv_mpu_dmp_motion_driver.h"
#include "inv_mpu.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG                 "mpu6050"
#define q30                 1073741824.0f   /* 用于归一化四元数 */
#define DEFAULT_MPU_HZ      200             /* DMP 内部解算采样频率 200Hz */
/* DMP 写入 FIFO 的输出频率：20Hz（50ms/包），与 imu_service 100ms 采集周期匹配，
 * 相比 200Hz 降低约 10 倍 I2C 流量，减少与同总线 GT911 触摸 / CH422G 的竞争 */
#define DMP_FIFO_RATE_HZ    20

static const signed char k_gyro_orientation[9] = {
    1, 0, 0,
    0, 1, 0,
    0, 0, 1
};

static i2c_master_dev_handle_t s_dev_handle;

static unsigned short inv_row_2_scale(const signed char *row)
{
    unsigned short b;

    if (row[0] > 0) {
        b = 0;
    } else if (row[0] < 0) {
        b = 4;
    } else if (row[1] > 0) {
        b = 1;
    } else if (row[1] < 0) {
        b = 5;
    } else if (row[2] > 0) {
        b = 2;
    } else if (row[2] < 0) {
        b = 6;
    } else {
        b = 7; /* error */
    }
    return b;
}

static unsigned short inv_orientation_matrix_to_scalar(const signed char *mtx)
{
    unsigned short scalar = inv_row_2_scale(mtx);
    scalar |= inv_row_2_scale(mtx + 3) << 3;
    scalar |= inv_row_2_scale(mtx + 6) << 6;
    return scalar;
}

static void run_self_test(void)
{
    long gyro_bias[3], accel_bias[3];
    int result = mpu_run_self_test(gyro_bias, accel_bias);

    if (result == 0x7) {
        float sens;
        unsigned short accel_sens;
        mpu_get_gyro_sens(&sens);
        gyro_bias[0] = (long)(gyro_bias[0] * sens);
        gyro_bias[1] = (long)(gyro_bias[1] * sens);
        gyro_bias[2] = (long)(gyro_bias[2] * sens);
        dmp_set_gyro_bias(gyro_bias);
        mpu_get_accel_sens(&accel_sens);
        accel_bias[0] *= accel_sens;
        accel_bias[1] *= accel_sens;
        accel_bias[2] *= accel_sens;
        dmp_set_accel_bias(accel_bias);
        ESP_LOGI(TAG, "DMP self-test bias applied");
    }
}

void mpu6050_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void mpu6050_get_ms(unsigned long *count)
{
    if (count != NULL) {
        *count = (unsigned long)(esp_timer_get_time() / 1000LL);
    }
}

static esp_err_t register_read(i2c_master_dev_handle_t handle, uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(handle, &reg_addr, 1, data, len, MPU6050_I2C_TIMEOUT_MS);
}

static esp_err_t register_write_byte(i2c_master_dev_handle_t handle, uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = { reg_addr, data };
    return i2c_master_transmit(handle, write_buf, sizeof(write_buf), MPU6050_I2C_TIMEOUT_MS);
}

static esp_err_t set_gyro_fsr(uint8_t fsr)
{
    return register_write_byte(s_dev_handle, MPU_GYRO_CFG_REG, (uint8_t)(fsr << 3));
}

static esp_err_t set_accel_fsr(uint8_t fsr)
{
    return register_write_byte(s_dev_handle, MPU_ACCEL_CFG_REG, (uint8_t)(fsr << 3));
}

static esp_err_t set_lpf(uint16_t lpf)
{
    uint8_t data;

    if (lpf >= 188) {
        data = 1;
    } else if (lpf >= 98) {
        data = 2;
    } else if (lpf >= 42) {
        data = 3;
    } else if (lpf >= 20) {
        data = 4;
    } else if (lpf >= 10) {
        data = 5;
    } else {
        data = 6;
    }
    return register_write_byte(s_dev_handle, MPU_CFG_REG, data);
}

static esp_err_t set_rate(uint16_t rate)
{
    if (rate > 1000) {
        rate = 1000;
    }
    if (rate < 4) {
        rate = 4;
    }
    uint8_t data = (uint8_t)(1000 / rate - 1);
    esp_err_t res = register_write_byte(s_dev_handle, MPU_SAMPLE_RATE_REG, data);
    if (res != ESP_OK) {
        return res;
    }
    return set_lpf(rate);
}

static esp_err_t read_chip_id(void)
{
    uint8_t who_am_i = 0;
    ESP_RETURN_ON_ERROR(register_read(s_dev_handle, MPU6050_WHO_AM_I, &who_am_i, 1),
                        TAG, "failed to read WHO_AM_I");

    if (who_am_i != MPU6050_SENSOR_ADDR) {
        ESP_LOGW(TAG, "unexpected WHO_AM_I=0x%02X (expected 0x%02X)",
                 (unsigned)who_am_i, (unsigned)MPU6050_SENSOR_ADDR);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "MPU6050 ID = 0x%02X", (unsigned)who_am_i);
    return ESP_OK;
}

esp_err_t MPU6050_Init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 先探测设备是否在总线上；不在就直接返回，避免后面一连串 I2C 报错 */
    if (i2c_master_probe(bus, MPU6050_SENSOR_ADDR, MPU6050_I2C_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    /* 设备句柄只添加一次，掉线后重新初始化时复用 */
    if (s_dev_handle == NULL) {
        const i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = MPU6050_SENSOR_ADDR,
            .scl_speed_hz = MPU6050_I2C_FREQ_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_config, &s_dev_handle),
                            TAG, "failed to add MPU6050 to I2C bus");
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    /* 复位芯片并唤醒 */
    ESP_RETURN_ON_ERROR(register_write_byte(s_dev_handle, MPU6050_RA_PWR_MGMT_1, 0x80), TAG, "reset failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(register_write_byte(s_dev_handle, MPU6050_RA_PWR_MGMT_1, 0x00), TAG, "wake failed");

    ESP_RETURN_ON_ERROR(set_gyro_fsr(3), TAG, "set_gyro_fsr failed");   /* ±2000 dps */
    ESP_RETURN_ON_ERROR(set_accel_fsr(0), TAG, "set_accel_fsr failed"); /* ±2 g */
    ESP_RETURN_ON_ERROR(set_rate(50), TAG, "set_rate failed");

    ESP_RETURN_ON_ERROR(register_write_byte(s_dev_handle, MPU_INT_EN_REG, 0x00), TAG, "cfg INT_EN failed");
    ESP_RETURN_ON_ERROR(register_write_byte(s_dev_handle, MPU_USER_CTRL_REG, 0x00), TAG, "cfg USER_CTRL failed");
    ESP_RETURN_ON_ERROR(register_write_byte(s_dev_handle, MPU_FIFO_EN_REG, 0x00), TAG, "cfg FIFO_EN failed");
    ESP_RETURN_ON_ERROR(register_write_byte(s_dev_handle, MPU_INTBP_CFG_REG, 0x80), TAG, "cfg INTBP failed");

    ESP_RETURN_ON_ERROR(read_chip_id(), TAG, "WHO_AM_I check failed");

    /* 选择 X 轴陀螺作为参考 PLL 时钟源 */
    ESP_RETURN_ON_ERROR(register_write_byte(s_dev_handle, MPU6050_RA_PWR_MGMT_1, 0x01), TAG, "set PLL failed");
    ESP_RETURN_ON_ERROR(register_write_byte(s_dev_handle, MPU_PWR_MGMT2_REG, 0x00), TAG, "enable axes failed");
    ESP_RETURN_ON_ERROR(set_rate(50), TAG, "set_rate failed");

    return ESP_OK;
}

int mpu6050_i2c_write(unsigned char slave_addr, unsigned char reg_addr,
                      unsigned char length, unsigned char const *data)
{
    if (s_dev_handle == NULL || (length > 0 && data == NULL)) {
        return -1;
    }
    (void)slave_addr;

    uint8_t write_buf[32];
    if (length >= sizeof(write_buf)) {
        return -1;
    }

    write_buf[0] = reg_addr;
    if (length > 0) {
        memcpy(&write_buf[1], data, length);
    }

    esp_err_t ret = i2c_master_transmit(s_dev_handle, write_buf, (size_t)length + 1U,
                                        MPU6050_I2C_TIMEOUT_MS);
    return (ret == ESP_OK) ? 0 : -1;
}

int mpu6050_i2c_read(unsigned char slave_addr, unsigned char reg_addr,
                     unsigned char length, unsigned char *data)
{
    if (s_dev_handle == NULL || data == NULL || length == 0) {
        return -1;
    }
    (void)slave_addr;

    esp_err_t ret = i2c_master_transmit_receive(s_dev_handle,
                                                &reg_addr, 1,
                                                data, length,
                                                MPU6050_I2C_TIMEOUT_MS);
    return (ret == ESP_OK) ? 0 : -1;
}

/* DMP 初始化：芯片初始化、固件加载、开启 DMP 三步失败则无法输出姿态，返回错误；
 * 其余配置步骤失败只打警告。 */
esp_err_t DMP_Init(void)
{
    if (mpu_init()) {
        ESP_LOGE(TAG, "mpu_init failed");
        return ESP_FAIL;
    }

    if (mpu_set_sensors(INV_XYZ_GYRO | INV_XYZ_ACCEL)) {
        ESP_LOGW(TAG, "mpu_set_sensors failed");
    }
    if (mpu_configure_fifo(INV_XYZ_GYRO | INV_XYZ_ACCEL)) {
        ESP_LOGW(TAG, "mpu_configure_fifo failed");
    }
    if (mpu_set_sample_rate(DEFAULT_MPU_HZ)) {
        ESP_LOGW(TAG, "mpu_set_sample_rate failed");
    }
    if (dmp_load_motion_driver_firmware()) {
        ESP_LOGE(TAG, "failed to load DMP firmware");
        return ESP_FAIL;
    }
    if (dmp_set_orientation(inv_orientation_matrix_to_scalar(k_gyro_orientation))) {
        ESP_LOGW(TAG, "dmp_set_orientation failed");
    }
    if (dmp_enable_feature(DMP_FEATURE_6X_LP_QUAT | DMP_FEATURE_TAP |
                           DMP_FEATURE_ANDROID_ORIENT | DMP_FEATURE_SEND_RAW_ACCEL |
                           DMP_FEATURE_SEND_CAL_GYRO | DMP_FEATURE_GYRO_CAL)) {
        ESP_LOGW(TAG, "dmp_enable_feature failed");
    }
    if (dmp_set_fifo_rate(DMP_FIFO_RATE_HZ)) {
        ESP_LOGW(TAG, "dmp_set_fifo_rate failed");
    }

    run_self_test();

    if (mpu_set_dmp_state(1)) {
        ESP_LOGE(TAG, "failed to enable DMP");
        return ESP_FAIL;
    }

    return ESP_OK;
}

uint8_t MPU6050_DMP_Get_Data(float *pitch, float *roll, float *yaw)
{
    short gyro[3], accel[3], sensors;
    unsigned long sensor_timestamp;
    unsigned char more = 0;
    long quat[4];
    long latest_quat[4];
    int has_quat = 0;

    /* 一次排空 FIFO 只取最新一包，避免滞后与溢出；20Hz FIFO 速率下 100ms 周期通常仅 1~2 包 */
    for (int i = 0; i < 40; i++) {
        if (dmp_read_fifo(gyro, accel, quat, &sensor_timestamp, &sensors, &more)) {
            break;
        }
        if (sensors & INV_WXYZ_QUAT) {
            latest_quat[0] = quat[0];
            latest_quat[1] = quat[1];
            latest_quat[2] = quat[2];
            latest_quat[3] = quat[3];
            has_quat = 1;
        }
        if (!more) {
            break;
        }
    }

    if (!has_quat) {
        return 2;
    }

    float q0 = latest_quat[0] / q30;
    float q1 = latest_quat[1] / q30;
    float q2 = latest_quat[2] / q30;
    float q3 = latest_quat[3] / q30;

    float sinp = -2.0f * q1 * q3 + 2.0f * q0 * q2;
    if (sinp > 1.0f) {
        sinp = 1.0f;
    } else if (sinp < -1.0f) {
        sinp = -1.0f;
    }

    *pitch = asinf(sinp) * 57.2957795f;
    *roll  = atan2f(2.0f * q2 * q3 + 2.0f * q0 * q1,
                    -2.0f * q1 * q1 - 2.0f * q2 * q2 + 1.0f) * 57.2957795f;
    *yaw   = atan2f(2.0f * (q1 * q2 + q0 * q3),
                    q0 * q0 + q1 * q1 - q2 * q2 - q3 * q3) * 57.2957795f;
    return 0;
}
