#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 单小时天气预报数据结构
 */
typedef struct {
    int32_t timestamp;    // UNIX 时间戳
    float   temp;         // 温度（摄氏度）
    float   humidity;     // 湿度（%）
    char    icon[16];     // 天气图标代码（如 "04d"）
    char    desc[64];     // 天气描述（如 "多云"）
} weather_hourly_t;

/**
 * @brief 获取指定经纬度的天气预报（包含当前 + 24小时逐时）。
 *
 * @param lat       纬度
 * @param lon       经度
 * @param hourly_out 输出逐时预报数组（由调用方预分配，建议最多 24 条）
 * @param max_count 数组容量
 * @param out_count 实际返回条数
 * @return 0 成功；非 0 失败。
 */
int weather_fetch_forecast(float lat, float lon,
                           weather_hourly_t *hourly_out,
                           int max_count,
                           int *out_count);

/**
 * @brief 释放 weather_fetch_forecast 内部分配的内存
 *        （当前实现中在函数内完成清理，此函数仅做扩展预留）。
 */
void weather_cleanup(void);

#ifdef __cplusplus
}
#endif