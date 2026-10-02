# ESP32-P4 数字班牌工程阅读指南

> 分支：`study-comments-20261002`
>
> 目标：不是只看懂“每一行做什么”，而是按 **系统入口 → 任务/事件 → 数据流 → 外设 → 网络 → AI → UI** 的顺序，把整个工程串起来。

---

## 1. 先建立全局认识

这个工程可以分成 5 条主链路：

### 1.1 显示/UI 链路

```text
LVGL Object
  -> LVGL draw buffer
  -> esp_lv_adapter
  -> LCD panel driver
  -> MIPI-DSI Host / D-PHY
  -> LCD
```

关键文件：

- `main/lvgl_adapter_init.c`
- `main/ui/ui.c`
- `main/ui/screen_weather.c`
- `main/ui/screen_face.c`
- `main/ui/screen_ai.c`

### 1.2 天气/网络链路

```text
ESP32-P4
  -> esp_wifi_remote / ESP-Hosted
  -> Wi-Fi
  -> DHCP 获得 IP
  -> NTP(UDP) 校时
  -> HTTPS(TLS/TCP) GET 天气
  -> JSON
  -> cJSON
  -> Weather UI
```

关键文件：

- `main/function_weather/wifi_manager.c`
- `main/function_weather/ntp_time.c`
- `main/function_weather/weather_api.c`
- `main/function_weather/weather_task.c`

### 1.3 AI 语音链路

```text
I2S Mic
  -> I2S RX
  -> PCM
  -> WAV / SD Card
  -> HTTPS PUT OSS
  -> Paraformer-v2 ASR
  -> Text
  -> Qwen LLM
  -> Reply Text
  -> LVGL
```

关键文件：

- `main/function_aichat/audio_recorder.c`
- `main/function_aichat/ai_chat_api.c`
- `main/ui/screen_ai.c`

### 1.4 摄像头 / 人脸链路

```text
Camera Sensor
  -> MIPI-CSI
  -> esp_video / V4L2
  -> Capture Buffer
  -> PPA
  -> 640x360 RGB565
      -> LVGL Canvas
      -> Face Inference Task
          -> HumanFaceDetect
          -> HumanFaceRecognizer
          -> ID + Similarity
          -> Attendance Manager
```

关键文件：

- `main/function_face/camera_preview.c`
- `main/function_face/face_detector.cpp`
- `main/function_face/attendance_manager.c`
- `main/ui/screen_face.c`

### 1.5 存储链路

```text
SD Card
  |- /sdcard/ai_record.wav   语音录音
  |- /sdcard/face.db         人脸特征数据库
  |- /sdcard/users.csv       ID -> 姓名
  '- /sdcard/attend.csv      考勤记录
```

---

# 2. 推荐阅读顺序

## 第 0 步：先看构建关系，不要急着读业务代码

先读：

1. `main/idf_component.yml`
2. `main/CMakeLists.txt`

要弄懂：

- ESP-IDF Component Manager 如何引入 LVGL、cJSON、人脸检测/识别、远程 Wi-Fi；
- `idf_component_register(SRCS ...)` 决定哪些源文件真正被编译进 main component；
- 目前 `screen_menu.c` 没有列在 `main/CMakeLists.txt` 的 SRCS 中，而且 `ui_init()` 也没有创建菜单页，因此它当前不是主运行链路的一部分。

读完后回答：

> “我的第三方组件是从哪里来的？我自己的哪些 .c/.cpp 文件被编译？”

---

## 第 1 步：main.c —— 看系统启动顺序

读：

`main/main.c`

只抓主干，不要陷入 API 参数：

```text
app_main
 -> NVS
 -> SD 电源
 -> EventGroup
 -> Display/LVGL
 -> UI
 -> SD mount
 -> Audio init
 -> Wi-Fi Task
 -> Weather Task
```

重点理解：

- 为什么大部分功能不是在 `app_main()` 的 while(1) 中运行；
- ESP-IDF 本身已经运行 FreeRTOS，应用通过创建 Task 承载长期逻辑；
- 为什么 LVGL 不应该在多个任务里无锁访问；
- 为什么 PSRAM 对摄像头/显示/AI 很重要。

---

## 第 2 步：lvgl_adapter_init.c —— 搞清楚“屏幕为什么能亮”

读：

`main/lvgl_adapter_init.c`

建议顺着下面几个 API 看：

```text
bsp_display_new_with_handles()
 -> esp_lv_adapter_init()
 -> esp_lv_adapter_register_display()
 -> bsp_touch_new()
 -> esp_lv_adapter_register_touch()
 -> esp_lv_adapter_start()
```

先做到能口述：

> “LVGL 并不直接控制 MIPI-DSI 寄存器，它通过 Espressif 的 LVGL adapter 和 BSP/Panel driver 把绘制结果送到 LCD。”

然后再看文件后半段 SD 文件系统：

```text
LVGL "S:/xxx"
 -> LVGL fs callback
 -> fopen("/sdcard/xxx")
```

---

## 第 3 步：ui.c —— 搞清楚页面生命周期

读：

`main/ui/ui.c`

关注：

- 三个 screen 是什么时候创建的；
- `ui_show_face()` 为什么除了切屏还启动 camera；
- 离开 Face 页面为什么要 stop camera；
- Weather Task 为什么不能直接无锁改 LVGL。

然后快速浏览：

- `screen_weather.c`
- `screen_ai.c`
- `screen_face.c`

此时先只看“页面由哪些对象组成”，不要深挖业务。

---

# 3. 第二轮：网络和天气

## 第 4 步：wifi_manager.c

按事件流看：

```text
esp_wifi_start()
 -> WIFI_EVENT_STA_START
 -> esp_wifi_connect()

连接断开
 -> WIFI_EVENT_STA_DISCONNECTED
 -> 重连

DHCP 成功
 -> IP_EVENT_STA_GOT_IP
 -> WIFI_CONNECTED_BIT = 1
```

重点复习：

- Station Mode；
- Wi-Fi 关联和 DHCP/GOT_IP 的区别；
- EventGroup 为什么比 while 忙等好；
- Blocked -> Ready 后 FreeRTOS 如何调度。

建议自己回答：

> “Weather Task 如何知道 Wi-Fi 已经联网？”

答案应该能说到 `xEventGroupWaitBits()`。

---

## 第 5 步：ntp_time.c

这个文件很短，适合完整读完。

重点：

```text
SNTP -> UDP -> IP
```

注意：

- 这里直接使用 NTP Server IP，所以没有 DNS；
- NTP 响应不是广播；
- SNTP 组件内部帮你完成 socket 和协议处理；
- `setenv("TZ", "CST-8")` 是时区设置，不是修改 RTC 的“+8”。

---

## 第 6 步：weather_api.c

建议按这条线读：

```text
_build_url()
 -> esp_http_client_init()
 -> esp_http_client_perform()
 -> _http_event_handler()
 -> g_http_buf
 -> cJSON_Parse()
 -> weather_hourly_t[]
```

一定弄懂两个 Buffer：

- HTTP Client 内部 4096 B Buffer；
- 应用层 16 KB `g_http_buf`。

为什么需要拼包：

> TCP 是字节流，HTTP body 可以通过多次 `HTTP_EVENT_ON_DATA` 到达。

---

## 第 7 步：weather_task.c

把它当成“状态机/业务编排”看，不要当 HTTP 文件看。

```text
等 Wi-Fi
 -> 校时
 -> 拉天气
 -> 整理今天/明天
 -> 更新 UI
 -> 周期等待
```

关注：

- `vTaskDelay()`；
- `xTaskGetTickCount()`；
- 30 分钟天气周期和 1 秒时间刷新周期；
- 网络失败后的重试；
- 为什么等待过程中任务不会一直占 CPU。

---

# 4. 第三轮：音频和 AI 对话

## 第 8 步：audio_recorder.c

先理解数据：

```text
I2S 32-bit slots
 -> rec_task
 -> 取有效 16 bit
 -> mono PCM
 -> WAV
```

一定弄懂：

- 16 kHz = 每秒 16000 个采样点；
- 16-bit mono PCM 约 32000 Byte/s；
- WAV = Header + PCM；
- 为什么先写 dummy header，停止录音后再回填；
- I2S Driver 底层会使用 DMA，但应用层主要调用 `i2s_channel_read()`，不需要自己逐样本搬运。

---

## 第 9 步：ai_chat_api.c

不要从第一行硬啃，先跳到：

`ai_chat_process()`

从最外层往里追：

```text
ai_chat_process
 |- read_whole_file
 |- call_asr
 |   |- upload_wav_to_oss
 |   |- submit_asr_task
 |   |- poll_asr_result
 |   '- fetch_asr_text
 '- call_llm
```

先搞懂整体，再研究：

- HTTP PUT 和 POST；
- Authorization Header；
- JSON 请求/响应；
- OSS HMAC-SHA1 签名；
- 为什么签名依赖正确时间；
- 为什么 ASR 是异步任务，需要 task_id 轮询。

最后回到：

`screen_ai.c`

看为什么要新建 `ai_process_task`，而不是在 LVGL callback 中直接进行网络请求。

---

# 5. 第四轮：摄像头与人脸识别

## 第 10 步：camera_preview.c

这是整个工程最值得慢读的文件之一。

第一遍只跟 V4L2：

```text
open(video device)
 -> QUERYCAP
 -> G_FMT / S_FMT
 -> REQBUFS
 -> QUERYBUF
 -> mmap
 -> QBUF
 -> STREAMON
 -> DQBUF
 -> QBUF
```

需要能解释：

- V4L2 是“应用访问视频设备的统一 API”；
- `mmap()` 不是复制摄像头图像，而是把驱动 Buffer 映射到应用地址空间；
- `DQBUF` 是“从驱动拿一块已经装好一帧数据的 Buffer”；
- 处理完后必须 `QBUF` 归还，否则驱动没有 Buffer 可继续采集。

第二遍看 PPA：

```text
capture frame
 -> ppa_do_scale_rotate_mirror()
 -> 640x360 RGB565
```

PPA 是 ESP32-P4 芯片内部的专用图像硬件，不是独立芯片。

第三遍看双缓冲和 LVGL：

- `CAPTURE_BUF_COUNT=2`
- `s_preview_buf[2]`
- `lv_canvas_set_buffer()`
- `lv_obj_invalidate()`

第四遍看 AI 提交：

- 每 3 帧提交一帧；
- AI 忙则允许丢帧；
- 摄像头预览不能被 AI 推理阻塞。

---

## 第 11 步：face_detector.cpp

先找：

`detector_task()`

主链：

```text
xQueueReceive
 -> 构造 dl::image::img_t
 -> HumanFaceDetect::run()
 -> 输出 box/score
 -> UI callback
 -> process_identity()
```

再读 `process_identity()`：

分三种模式：

1. Clear database；
2. Enroll；
3. Recognize。

理解：

```text
图片
 -> 人脸检测
 -> 人脸特征模型
 -> feature vector
 -> 与 face.db 比较
 -> similarity
 -> ID
```

不要把 similarity 说成“概率”，它是特征匹配分数。

再看 `face_detector_submit_rgb565()`：

这是很好的 FreeRTOS 任务通信例子：

```text
Camera Task
 -> Binary Semaphore 尝试占用推理 Buffer
 -> memcpy 大图像到共享 Buffer
 -> Queue 只发送 width/height
 -> Inference Task 被唤醒
```

为什么 Queue 不直接存整张图：

> Queue 会复制 item，几百 KB 图像放 Queue 成本太高。

---

## 第 12 步：attendance_manager.c

人脸识别完成不代表签到完成。

继续：

```text
Recognizer
 -> attendance_manager_submit_recognition()
 -> Queue
 -> attendance_task
 -> 时间检查
 -> 姓名映射
 -> 60 s 去重
 -> CSV
 -> UI callback
```

这里重点复习：

- Queue 为什么能把推理和慢速 SD I/O 解耦；
- `xQueueReceive(..., portMAX_DELAY)` 时 Task 为什么进入 Blocked；
- 收到消息后为什么又变成 Ready；
- 为什么文件写入集中在一个 Task 更安全。

---

## 第 13 步：screen_face.c —— 最后再看“融合”

前面都懂后，这个文件会很简单：

```text
camera_canvas
+ transparent face_overlay
+ bounding-box objects
+ side panel
```

检测框没有写进 RGB565 图像，而是 LVGL Overlay。

回调关系：

```text
face_detector
 |- detect callback ------> 人脸框
 '- identity callback ----> ID/相似度

attendance_manager
 '- attendance callback --> 签到文本
```

所有后台 callback 修改 LVGL 前都需要 adapter lock。

---

# 6. 当前主要 FreeRTOS Task 地图

这些优先级来自当前工程源码：

| Task | 来源 | 优先级 | 主要工作 |
|---|---|---:|---|
| wifi_task | wifi_manager.c | 5 | 初始化/维护 Wi-Fi |
| weather_task | weather_task.c | 4 | NTP、天气、时间 UI |
| camera_preview | camera_preview.c | 6 | V4L2 取帧、PPA、预览 |
| face_inference | face_detector.cpp | 5 | 人脸检测/识别 |
| attendance | attendance_manager.c | 4 | SD 考勤文件 I/O |
| rec_task | audio_recorder.c | 5 | I2S PCM -> WAV |
| ai_task | screen_ai.c | 5 | ASR + LLM 网络流程 |

另外还有 `esp_lv_adapter` 自己创建的 LVGL 执行任务；它的具体优先级由组件配置决定，不要从本工程源码凭空推断。

---

# 7. 用这个工程复习 FreeRTOS 的最佳位置

### Queue

看：

- `face_detector.cpp`
- `attendance_manager.c`

理解：

```text
xQueueSend
 -> 拷贝一个固定大小 item
 -> 接收 Task Blocked -> Ready
```

### Binary Semaphore

看：

`face_detector.cpp`

`s_buffer_free` 不是在传人脸数据，而是在表达：

> “推理 Buffer 当前能不能被 Camera Task 使用”。

### Event Group

看：

`wifi_manager.c`

`WIFI_CONNECTED_BIT` 表示：

> 网络是否拿到 IP、可以给其他任务使用。

### Task + Block

看：

`weather_task.c`

大量 `vTaskDelay()` / Wait API 都是很好的任务状态切换例子。

---

# 8. 每读一个模块都问自己 6 个问题

不要逐行背代码，每个文件回答下面六个问题：

1. **这个模块的输入是什么？**
2. **输出是什么？**
3. **运行在哪个 Task/回调上下文？**
4. **会不会阻塞？阻塞时 CPU 去哪里？**
5. **数据放在哪里？栈、堆、PSRAM、SD 还是共享 Buffer？**
6. **它如何把结果通知下一个模块？函数调用、Queue、Semaphore、EventGroup 还是 callback？**

能回答这六个问题，基本就不是“看懂语法”，而是真正理解工程。

---

# 9. 面试前建议至少能手画的 5 张图

1. **系统启动图**：`app_main -> display/UI/SD/audio/Wi-Fi/weather`
2. **网络协议栈图**：`HTTP/TLS/TCP/IP/Wi-Fi` 与 `NTP/UDP/IP/Wi-Fi`
3. **语音 AI 图**：`I2S -> WAV -> OSS -> ASR -> LLM -> LVGL`
4. **摄像头图**：`MIPI-CSI -> V4L2 -> PPA -> RGB565 -> LVGL/ESP-DL`
5. **人脸考勤图**：`detect -> feature -> similarity -> ID -> Queue -> SD CSV`

如果这五张图都能不看代码讲清楚，再回到具体 API，项目面试会稳很多。

---

# 10. 推荐实际阅读节奏

不要一天一次性读完。

### 第一轮：只看框架

`main.c -> lvgl_adapter_init.c -> ui.c -> 每个模块的 start/public API`

目标：知道谁调用谁。

### 第二轮：只看数据流

重点追踪：

- Weather JSON 最终怎么到 Label；
- PCM 怎么变 WAV；
- Camera Buffer 怎么到 Canvas；
- RGB565 怎么到 face detector；
- ID 怎么到 attend.csv。

### 第三轮：只看 RTOS/并发

圈出所有：

- `xTaskCreate*`
- `xQueue*`
- `xSemaphore*`
- `xEventGroup*`
- `vTaskDelay`
- LVGL lock

目标：知道每个 Task 什么时候 Running / Blocked / Ready。

### 第四轮：只看异常和资源

关注：

- malloc/PSRAM 失败；
- Wi-Fi 断线；
- HTTP 失败；
- SD 卡失败；
- 摄像头 Buffer 生命周期；
- AI 忙时为什么丢帧；
- UI 为什么必须加锁。

这一轮最接近真正嵌入式面试和工程调试。
