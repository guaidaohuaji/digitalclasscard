idf_component_register(
    SRCS
        main.c
        lvgl_adapter_init.c
        lv_font_utf_24.c
        myicon_font.c
        ui/ui.c
        ui/screen_weather.c
        ui/screen_face.c
        ui/screen_ai.c
        function_weather/ntp_time.c
        function_weather/weather_api.c
        function_weather/weather_task.c
        function_weather/wifi_manager.c
        function_aichat/audio_recorder.c
        function_aichat/ai_chat_api.c
    INCLUDE_DIRS
        .
        ui
        function_weather
        function_aichat
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)

# lv_ftsystem.c uses FT_THROW which depends on FT_ERR_PREFIX.
# fterrors.h undefines FT_ERR_PREFIX at end unless FT2_BUILD_LIBRARY is set
target_compile_options(${LVGL_LIB} PRIVATE -DFT2_BUILD_LIBRARY)

# LVGL needs access to freetype headers when LV_USE_FREETYPE is enabled
idf_component_get_property(FREETYPE_LIB espressif__freetype COMPONENT_LIB)
target_link_libraries(${LVGL_LIB} PUBLIC ${FREETYPE_LIB})
