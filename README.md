# TH1.0 - ESP32-S3 温湿度监测系统

基于 ESP32-S3 的温湿度监测系统，通过 DHT11 传感器采集环境温湿度数据，并通过 Web 页面实时展示。

## ✨ 功能特性

- 📡 **Wi-Fi AP 模式**：设备自建热点，无需路由器即可连接
- 🌡️ **温湿度监测**：使用 DHT11 传感器实时采集温度和湿度
- 🌐 **Web 可视化**：内置 Web 服务器，浏览器访问即可查看数据
- 🔄 **自动刷新**：网页每 3 秒自动更新温湿度数据

## 📋 硬件需求

| 组件 | 说明 |
|------|------|
| ESP32-S3 开发板 | 主控芯片 |
| DHT11 | 温湿度传感器 |

## 🔌 接线说明

| DHT11 引脚 | ESP32-S3 引脚 |
|-----------|---------------|
| VCC | 3.3V |
| GND | GND |
| DATA | GPIO7（见代码配置） |

## 🚀 快速开始

### 环境要求

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v5.x
- ESP32-S3 开发板

### 编译与烧录

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 编译项目
idf.py build

# 烧录到设备
idf.py flash

# 监控串口输出
idf.py monitor
```

### 使用方法

1. 烧录完成后，ESP32-S3 会自动启动 Wi-Fi 热点
2. 使用手机或电脑连接 Wi-Fi：
   - **SSID**：`ESP32_WEB`
   - **密码**：`12345678`
3. 打开浏览器访问：`http://192.168.4.1`
4. 即可看到实时温湿度数据

## 📁 项目结构

```
TH1.0/
├── main/
│   ├── CMakeLists.txt
│   └── main.c                 # 主程序入口
├── components/
│   ├── AP/                    # Wi-Fi AP 模式组件
│   │   ├── ap.c
│   │   └── ap.h
│   ├── DHT11/                 # DHT11 传感器驱动
│   │   ├── dht11.c
│   │   └── dht11.h
│   └── Webserver/             # Web 服务器组件
│       ├── web.c
│       ├── web.h
│       └── back.png           # 网页背景图
├── CMakeLists.txt             # 项目构建配置
├── partitions.csv             # 分区表配置
└── README.md
```

## 🔧 组件说明

### AP 组件
配置 ESP32-S3 为 Wi-Fi AP（热点）模式，允许设备直连。

### DHT11 组件
DHT11 传感器驱动，提供以下接口：
- `dht11_init()` - 初始化传感器
- `dht11_start_task()` - 启动数据采集任务
- `get_temperature_int()` / `get_temperature_dec()` - 获取温度
- `get_humidity_int()` / `get_humidity_dec()` - 获取湿度

### Webserver 组件
基于 ESP-IDF HTTP Server 实现的 Web 服务器：
- `/` - 主页面，显示温湿度数据
- `/data` - JSON API，返回温湿度数据
- `/back.png` - 背景图片资源

## 📄 License

MIT License
