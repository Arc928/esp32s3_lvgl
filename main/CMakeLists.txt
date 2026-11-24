set(APP_SRCS
    "main.c"
    "App/app_event.c"
    "App/bms_service.c"
    "App/can_service.c"
    "App/imu_service.c"
    "App/mqtt_service.c"
    "App/net_service.c"
    "App/time_service.c"
)

set(UI_SRCS
    "UI/dashboard_data_board.c"
    "UI/custom/custom.c"
    "UI/custom/gallery_view.c"
    "UI/custom/machine_list.c"
    "UI/custom/machine_view.c"
    "UI/custom/ui_common.c"
    "UI/custom/wifi_view.c"
    "UI/custom/images/_wifi1_RGB565A8_30x30.c"
    "UI/custom/images/img_machine_01.c"
    "UI/custom/images/img_machine_02.c"
    "UI/custom/images/img_machine_03.c"
    "UI/custom/images/img_machine_04.c"
    "UI/custom/images/img_machine_05.c"
    "UI/custom/images/img_machine_06.c"
    "UI/custom/images/img_machine_07.c"
    "UI/custom/images/img_machine_08.c"
    "UI/custom/images/img_machine_09.c"
    "UI/custom/images/img_machine_lg_01.c"
    "UI/custom/images/img_machine_lg_02.c"
    "UI/custom/images/img_machine_lg_03.c"
    "UI/custom/images/img_machine_lg_04.c"
    "UI/custom/images/img_machine_lg_05.c"
    "UI/custom/images/img_machine_lg_06.c"
    "UI/custom/images/img_machine_lg_07.c"
    "UI/custom/images/img_machine_lg_08.c"
    "UI/custom/images/img_machine_lg_09.c"
)

# 按目标板分辨率只编译实际使用的一套字库（P4: 1024x600 用 1.25x，S3: 800x480 用 1x）
if(IDF_TARGET STREQUAL "esp32p4")
    set(UI_FONT_SRCS
        "UI/custom/fonts/dash_font_18.c"
        "UI/custom/fonts/dash_font_20.c"
        "UI/custom/fonts/dash_font_28.c"
        "UI/custom/fonts/dash_font_80.c"
    )
else()
    set(UI_FONT_SRCS
        "UI/custom/fonts/dash_font_14.c"
        "UI/custom/fonts/dash_font_16.c"
        "UI/custom/fonts/dash_font_22.c"
        "UI/custom/fonts/dash_font_64.c"
    )
endif()

set(INCLUDES "./" "./App" "./UI" "./UI/custom")

idf_component_register(SRCS ${APP_SRCS} ${UI_SRCS} ${UI_FONT_SRCS}
                       INCLUDE_DIRS ${INCLUDES})

# 界面数据由 UI/dashboard_data_board.c 从各服务读取；板级屏幕参数由 UI/ui_board_config.h 注入
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    DASHBOARD_DATA_MOCK=0
    UI_PLATFORM_CONFIG_H="ui_board_config.h"
)
