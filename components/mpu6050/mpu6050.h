#ifndef _BSP_MPU6050_H_
#define _BSP_MPU6050_H_

#include <stdint.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/i2c_master.h"

// --- MPU6050 寄存器定义 (保持不变) ---
#define MPU6050_SENSOR_ADDR         0x68        /*!< Address of the MPU6050 sensor */
#define MPU6050_RA_SMPLRT_DIV       0x19        
#define MPU6050_RA_CONFIG           0x1A        
#define MPU6050_RA_GYRO_CONFIG      0x1B        
#define MPU6050_RA_ACCEL_CONFIG     0x1C        
#define MPU_INT_EN_REG              0X38        
#define MPU_USER_CTRL_REG           0X6A        
#define MPU_FIFO_EN_REG             0X23        
#define MPU_PWR_MGMT2_REG           0X6C        
#define MPU_GYRO_CFG_REG            0X1B        
#define MPU_ACCEL_CFG_REG           0X1C        
#define MPU_CFG_REG                 0X1A        
#define MPU_SAMPLE_RATE_REG         0X19        
#define MPU_INTBP_CFG_REG           0X37        

#define MPU6050_RA_PWR_MGMT_1       0x6B
#define MPU6050_RA_PWR_MGMT_2       0x6C

#define MPU6050_WHO_AM_I            0x75
#define MPU6050_SMPLRT_DIV          0          
#define MPU6050_DLPF_CFG            0        
#define MPU6050_GYRO_OUT            0x43     
#define MPU6050_ACC_OUT             0x3B     
        
#define MPU6050_RA_TEMP_OUT_H       0x41        
#define MPU6050_RA_TEMP_OUT_L       0x42        

#define MPU_ACCEL_XOUTH_REG         0X3B        
#define MPU_ACCEL_XOUTL_REG         0X3C        
#define MPU_ACCEL_YOUTH_REG         0X3D        
#define MPU_ACCEL_YOUTL_REG         0X3E        
#define MPU_ACCEL_ZOUTH_REG         0X3F        
#define MPU_ACCEL_ZOUTL_REG         0X40        

#define MPU_TEMP_OUTH_REG           0X41        
#define MPU_TEMP_OUTL_REG           0X42        

#define MPU_GYRO_XOUTH_REG          0X43        
#define MPU_GYRO_XOUTL_REG          0X44        
#define MPU_GYRO_YOUTH_REG          0X45        
#define MPU_GYRO_YOUTL_REG          0X46        
#define MPU_GYRO_ZOUTH_REG          0X47        
#define MPU_GYRO_ZOUTL_REG          0X48

#define MPU6050_I2C_FREQ_HZ         400000      /*!< MPU6050 的 I2C 通信速率 */
#define MPU6050_I2C_TIMEOUT_MS      1000        /*!< 单次 I2C 传输超时 */


/**
 * @brief 初始化 MPU6050（可重复调用，掉线后用于重新初始化）
 * @param bus 设备所在的 I2C 总线句柄，由板级代码创建后传入
 * @return ESP_OK；设备不在总线上返回 ESP_ERR_NOT_FOUND
 */
esp_err_t MPU6050_Init(i2c_master_bus_handle_t bus);
esp_err_t mpu6050_register_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t data);
esp_err_t mpu6050_register_read(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t *data, size_t len);
int i2c_read(unsigned char slave_addr, unsigned char reg_addr,
             unsigned char length, unsigned char *data);
int i2c_write(unsigned char slave_addr, unsigned char reg_addr,
              unsigned char length, unsigned char const *data);
void delay_ms(uint32_t ms); 
uint8_t MPU6050_DMP_Get_Data(float *pitch, float *roll, float *yaw);
esp_err_t DMP_Init(void);

#endif