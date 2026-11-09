# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# 每个目标芯片一个依赖锁文件（dependencies.lock.esp32s3 / dependencies.lock.esp32p4），
# 不然切换芯片编译时同一个锁文件会被来回改写
idf_build_set_property(DEPENDENCIES_LOCK dependencies.lock.${IDF_TARGET})
project(lvgl_test)