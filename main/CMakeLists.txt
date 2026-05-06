idf_component_register(
    SRCS
        main.c
        lvgl_adapter_init.c
        lv_font_utf_24.c
        myicon_font.c
        ui/ui.c
        ui/screen_menu.c
        ui/screen_weather.c
        ui/screen_face.c
        ui/screen_ai.c
        function_weather/ntp_time.c
        function_weather/weather_api.c
        function_weather/weather_task.c
        function_weather/wifi_manager.c
    INCLUDE_DIRS
        .
        ui
        function_weather
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)