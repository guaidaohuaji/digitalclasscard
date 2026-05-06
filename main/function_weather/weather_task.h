#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动天气任务。
 *
 * 内部会等待 Wi-Fi 连接 → 同步 NTP → 定时获取并更新天气 UI。
 * 在 wi-fi 连接断开时暂停更新，重连后自动恢复。
 */
void weather_task_start(void);

#ifdef __cplusplus
}
#endif