set(INCLUDES "./" "./App" "./UI" "./UI/custom" "./UI/generated" "./UI/generated/guider_customer_fonts")
file(GLOB_RECURSE SOURCES "./*.c")

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS ${INCLUDES})

# 界面数据由 UI/dashboard_data_board.c 从各服务读取，不用模拟器的假数据
target_compile_definitions(${COMPONENT_LIB} PRIVATE DASHBOARD_DATA_MOCK=0)
