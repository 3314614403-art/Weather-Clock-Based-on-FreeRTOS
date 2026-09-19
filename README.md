# STM32F4 FreeRTOS 天气时钟 + BLE

本仓库基于 [yuxingcao6/WeatherClock](https://github.com/yuxingcao6/WeatherClock) 开发。原项目是 STM32F4 FreeRTOS 天气时钟；此版本增加了通过 ESP32-C3 ESP-AT 提供的 BLE GATT 功能，修改版于 2026-09-19 整理。原项目及此修改版按仓库中的 GPL-3.0 许可证发布。原项目作者与第三方库的版权声明保留在源码中。

## 功能

- RTC 显示时间与日期，通过 ESP-AT 同步网络时间。
- 获取室外天气，在 ST7789 屏幕显示；用 AHT20 采集室内温湿度。
- 通过 BLE GATT 读取和订阅室内温湿度，并可写入 1～60 秒的采样周期。
- BLE 连接断开后重新广播，天气时钟主体可独立运行。

## 本版本增加的内容

- `app/ble/`：BLE 任务、GATT 数据编码、ESP-AT 命令与事件处理。
- `app/app.c`、`app/main.c`：将传感器更新接入 BLE，并初始化 BLE。
- `driver/esp_at/`：处理 BLE 主动上报事件。
- `app/tests/test_ble_protocol.c`：GATT 编码、采样周期和 AT 命令的主机端测试。

BLE 服务、特征、载荷格式和 ESP32-C3 固件准备步骤见 [BLE 说明](app/ble/README.md)。

## 编译配置

1. 复制 `app/local_config.example.h` 为 `app/local_config.h`，填入自己的 Wi-Fi 信息、心知天气 API 密钥与城市。`app/local_config.h` 已加入 `.gitignore`。
2. 使用 Keil µVision 打开 `mdk/stm32f407.uvprojx` 编译 STM32 工程。
3. 按 [BLE 说明](app/ble/README.md) 配置 ESP32-C3 的 ESP-AT GATT 表。仅烧录默认 ESP-AT 固件不足以提供本项目的 GATT 特征。

> 当前仓库只记录了源码及协议测试；实际硬件效果需在对应 STM32F4、ESP32-C3、ST7789 和 AHT20 设备上验证。

## 来源与许可

- 原项目：[yuxingcao6/WeatherClock](https://github.com/yuxingcao6/WeatherClock)。
- 许可证：[GPL-3.0](LICENSE)。使用、修改和再发布时请遵守许可证，并保留源码中的第三方版权声明。
