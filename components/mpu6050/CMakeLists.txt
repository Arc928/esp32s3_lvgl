# MPU6050 驱动 + InvenSense DMP 库，只依赖 I2C 驱动，总线句柄由调用方传入
idf_component_register(SRCS "mpu6050.c" "inv_mpu.c" "inv_mpu_dmp_motion_driver.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_i2c esp_driver_gpio)
