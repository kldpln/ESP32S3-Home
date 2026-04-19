# ESP32S3-Home

## 中文简介

基于 ESP32-S3 的家庭环境监控固件，面向“设备自建热点 + 局域网接入”双场景设计。

当前版本是完整的端侧小系统，主要能力包括：

- AP+STA 双模联网
- 网页端配网下发并写入 NVS
- NTP 与浏览器时间双通道同步
- WebSocket 实时推送 + HTTP 轮询保底
- 温度报警阈值在线配置并持久化
- 今日极值统计 + 过去 7 天历史数据
- mDNS 服务发现（esp.local）

## English Overview

ESP32S3-Home is a home environment monitoring firmware based on ESP32-S3, designed for both standalone hotspot mode and LAN mode.

This version is a complete edge-side system with:

- AP+STA dual-mode networking
- Web-based Wi-Fi provisioning with NVS persistence
- Dual time sync paths (NTP and browser fallback)
- Real-time WebSocket channel with HTTP polling fallback
- Online alarm threshold configuration with persistence
- Daily min/max stats plus 7-day history
- mDNS discovery (esp.local)

## 1. 核心能力 / Core Features

### 1.1 传感采集 / Sensor Sampling

- 中文：使用 DHT11，默认 GPIO7；底层采用 RMT 解码单总线时序，降低传统 bit-bang 抖动影响。
- English: Uses DHT11 on GPIO7 by default; RMT-based single-wire decoding improves timing stability over bit-banging.

- 中文：包含突发异常值过滤逻辑，避免图表和报警被毛刺数据污染。
- English: Includes spike filtering to prevent charts and alerts from being polluted by outlier readings.

### 1.2 网络与时间 / Networking and Time

- 中文：设备启动即开启 SoftAP（默认 SSID: ESP32_WEB），同时启用 STA，可通过网页下发路由器信息。
- English: SoftAP (default SSID: ESP32_WEB) starts at boot, while STA is also enabled for router provisioning via web UI.

- 中文：STA 连网后使用 SNTP（ntp.aliyun.com / pool.ntp.org）同步时间；未联网时由浏览器时间接口兜底。
- English: SNTP is used after STA connection (ntp.aliyun.com / pool.ntp.org); browser time sync is used as fallback when offline.

### 1.3 可视化与控制 / Visualization and Control

- 中文：内置单页前端，展示实时温湿度、今日极值、昨日回顾与周趋势。
- English: Built-in single-page UI shows real-time temperature/humidity, today's extremes, yesterday summary, and weekly trend.

- 中文：WebSocket 每 2 秒获取实时数据，HTTP /data 作为降级保底。
- English: WebSocket requests data every 2 seconds, with HTTP /data as fallback.

- 中文：支持报警阈值在线设置，写入 NVS 并在重启后恢复。
- English: Alarm threshold can be configured online, stored in NVS, and restored after reboot.

### 1.4 数据持久化 / Data Persistence

- 中文：NVS 命名空间 history 保存 7 天历史；storage 保存 Wi-Fi 配置和报警阈值。
- English: NVS namespace history stores 7-day history; storage keeps Wi-Fi credentials and alarm threshold.

- 中文：跨天自动结算“昨日”极值并滚动历史数组。
- English: At day rollover, yesterday's extremes are settled and history is shifted.

## 2. 系统架构 / Architecture

### 2.1 启动流程 / Boot Sequence

1. 初始化 NVS / Initialize NVS
2. 启动 Wi-Fi（AP+STA）/ Start Wi-Fi (AP+STA)
3. 启动 Web Server / Start Web Server
4. 启动 mDNS 服务 / Start mDNS service
5. 初始化数据处理模块 / Initialize data processing
6. 启动采集任务 / Start sensor task

### 2.2 组件划分 / Components

- components/AP: AP+STA, Wi-Fi event handling, SNTP state
- components/RMT: DHT11 RMT driver and waveform decoding
- components/DataProcess: sampling, filtering, daily stats, history management
- components/Webserver: static page, REST API, WebSocket, provisioning, alarm config
- components/mDNS: local service discovery

## 3. 硬件与接线 / Hardware and Wiring

### 3.1 硬件需求 / Requirements

- ESP32-S3 开发板（16MB Flash）/ ESP32-S3 development board (16MB Flash)
- DHT11 温湿度传感器 / DHT11 temperature and humidity sensor

### 3.2 接线 / Wiring

- DHT11 VCC -> 3.3V
- DHT11 GND -> GND
- DHT11 DATA -> GPIO7

中文：若改动引脚，请同步修改 DataProcess 中的 DHT11_GPIO 宏。

English: If you change the pin, update DHT11_GPIO in DataProcess accordingly.

## 4. 软件环境 / Software Environment

- ESP-IDF 5.x（当前配置基于 5.4.3）/ ESP-IDF 5.x (current config based on 5.4.3)
- Python 3.8+（仅压测脚本可选）/ Python 3.8+ (optional, for stress test only)

关键配置 / Key project settings:

- Target: esp32s3
- Flash size: 16MB
- PSRAM enabled
- HTTP WebSocket support enabled
- Custom partition table enabled

## 5. 快速开始 / Quick Start

### 5.1 编译烧录 / Build and Flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <YOUR_PORT> flash monitor
```

### 5.2 首次访问 / First Access

1. 连接热点 / Connect to SoftAP
- SSID: ESP32_WEB
- Password: 12345678

2. 打开页面 / Open in browser
- http://192.168.4.1

3. 在页面右上角配置家中 Wi-Fi / Configure router Wi-Fi in the top-right settings panel.

4. STA 成功后可尝试 / After STA connects, try:
- http://esp.local

中文说明：Windows 上 mDNS 可能不可用，请改用串口日志中的 STA IP。

English note: mDNS may not work on some Windows setups; use the STA IP from serial logs.

## 6. 接口说明 / API Reference

### 6.1 页面与静态资源 / Page and Static Resources

- GET /
- GET /chart.js

### 6.2 数据接口 / Data Endpoints

- GET /data
  - 中文：返回实时温湿度、今日极值、报警阈值和 7 天历史。
  - English: Returns real-time values, today's extremes, alarm threshold, and 7-day history.

- GET /ws (WebSocket)
  - 中文：握手后发送文本 get，返回与 /data 等价 JSON。
  - English: Send text get after handshake; server returns JSON equivalent to /data.

### 6.3 控制接口 / Control Endpoints

- POST /wifi_config

```json
{"ssid":"your_ssid","password":"your_password"}
```

```json
{"status":"ok","ip":"192.168.1.100"}
```

- POST /set_alarm

```json
{"threshold":30.0}
```

```json
{"status":"ok"}
```

- POST /sync_time
  - 中文：请求体为 Unix 时间戳字符串（秒），仅在 NTP 未同步时兜底。
  - English: Body is Unix timestamp string (seconds), used only when NTP is not synced.

## 7. 数据策略 / Data Strategy

1. 实时采样 / Real-time sampling
- 中文：采样周期约 2 秒；突变过大时使用上次有效值兜底。
- English: Sampling period is about 2 seconds; abnormal jumps fall back to previous valid values.

2. 今日统计 / Daily stats
- 中文：持续更新当日温湿度最大值/最小值。
- English: Maintains daily max/min for temperature and humidity.

3. 跨天结算 / Day rollover
- 中文：日期变化时写入“昨日”记录并保存 NVS。
- English: On date change, yesterday record is settled and persisted to NVS.

## 8. 分区与存储 / Partition and Storage

当前分区 / Current partitions:

- nvs: 24K
- phy_init: 4K
- factory app: 10M
- storage (spiffs subtype): 4M

中文：参数持久化主要使用 NVS API，不依赖 SPIFFS 挂载流程。

English: Runtime parameter persistence is mainly based on NVS API, not SPIFFS mounting.

## 9. 压测脚本 / Stress Test

根目录 stress_test.py 用于验证：

- WebSocket rapid reconnect stability
- concurrent connection cleanup behavior
- HTTP high-frequency request resistance

```bash
pip install websockets
python stress_test.py
```

中文：若 esp.local 无法解析，修改脚本中的 HOST 为设备实际 IP。

English: If esp.local cannot be resolved, change HOST in the script to the real device IP.

## 10. 项目结构 / Project Structure

```text
ESP32S3-Home-main/
├─ main/
│  └─ main.c
├─ components/
│  ├─ AP/
│  ├─ DataProcess/
│  ├─ RMT/
│  ├─ Webserver/
│  └─ mDNS/
├─ partitions.csv
├─ sdkconfig.defaults
├─ stress_test.py
└─ CMakeLists.txt
```

## 11. 常见问题 / FAQ

1. 页面无法访问 / Page not reachable
- 中文：确认已连接设备热点，并检查访问地址是否为 192.168.4.1 或 STA IP。
- English: Ensure you are connected to device AP and visit 192.168.4.1 or STA IP.

2. esp.local 无法访问 / esp.local not reachable
- 中文：Windows 常见问题，优先使用 STA IP。
- English: Common on Windows; use STA IP directly.

3. 历史数据为空 / Empty history
- 中文：需先完成时间同步并经历至少一次跨天。
- English: Time sync and at least one day rollover are required.

4. 温度偶发跳变 / Occasional temp spikes
- 中文：已做过滤，但 DHT11 本身精度有限，建议缩短线长并稳定供电。
- English: Filtering is enabled, but DHT11 has limited precision; keep wiring short and power stable.

## 12. 后续扩展 / Roadmap

- OTA firmware upgrade
- more sensors (for example SHT3x and BME280)
- relay/fan linkage for over-temperature protection
- authentication and API security hardening
