# OpenUPS-ESP32S3

基于 ESP32-S3 的智能锂电池 UPS 控制系统，使用 BQ24780S 充电管理芯片与 BQ76920 电池监控芯片，支持 3-5 串锂离子/磷酸铁锂电池，12-19V 宽电压输入。

> **为什么不用铅酸 UPS？** 铅酸 UPS 待机功耗 10W+，体积笨重。本项目面向小功率主机、NAS、软路由、网络设备等场景，锂电池方案更小巧、更省电、更安静。

## 功能特性

### 核心功能

- **USB HID UPS** — ESP32-S3 原生 USB 接口，Windows/macOS/Linux 自动识别为标准 UPS 设备，支持低电量安全关机
- **充放电管理** — BQ24780S 充电控制，支持自适应充电电流、定时充电窗口（5 组周计划）、混合供电模式
- **电池管理** — BQ76920 电池监控，库仑计 SOC 计算、SOH 学习、自动均衡、多重保护（OV/UV/OCD/SCD/OT）
- **Web 仪表盘** — SPA 单页应用，macOS 风格 UI，实时 WebSocket 数据推送（3 秒间隔）
- **OTA 固件更新** — 网页拖拽上传固件，带签名校验，双分区安全升级
- **MQTT 集成** — Home Assistant 自动发现，支持 TLS 加密连接
- **Prometheus 监控** — `/metrics` 端点，可直接接入 Grafana

### 保护机制

| 保护类型 | 说明 |
|---|---|
| 过压保护 (OV) | 单体电压超阈值，切断充电 FET |
| 欠压保护 (UV) | 单体电压低于阈值，切断放电 FET |
| 过流保护 (OCD) | 放电电流超限 |
| 短路保护 (SCD) | 短路快速响应 |
| 过温保护 | 板载 + 环境温度双重检测 |
| 充电超时 | 防止异常长时间充电 |
| 系统状态机 | INIT → NORMAL → WARNING → CRITICAL 四级状态管理 |

## 硬件

### 关键参数

| 参数 | 值 |
|---|---|
| 输入电压 | 12-19V DC |
| 充电电流 | 最高 8128mA（BQ24780S） |
| 放电电流 | 最高 20A |
| 输入电流 | 最高 8064mA |
| 电池类型 | 3-5 串 Li-ion / LiFePO4 |
| 电流采样电阻 | 10mΩ（充电侧）/ 5mΩ（放电侧） |
| I2C 总线 | GPIO11(SDA) / GPIO12(SCL)，100kHz |

### ESP32 开发板

选择 **ESP32-S3 N16R8** 版本的开发板（16MB Flash / 8MB PSRAM）。插入时**注意核对引脚排列和插入方向**，反接可能损坏开发板或主板。

### 电池夹选择

电池组有条件的**尽量选择直接点焊成组**，接触电阻最小、最可靠。

如果使用电池夹，建议如下：

| 类型 | 材质 | 优缺点 |
|---|---|---|
| 插针电池夹 | 钢片 | 夹持力强，但**过紧**，拆装困难 |
| 贴片电池夹 | 铜材质 | 导电好，但**过松**，接触不可靠 |
| **贴片铜弹片 + 螺旋弹簧**（推荐） | 黄铜/紫铜 | 导电好，弹簧提供足够的回弹力，**推荐方案** |

推荐方案：[贴片铜弹片](https://item.taobao.com/item.htm?id=833701074625) + 线径 0.8mm × 外径 5mm × 长度 50mm 的螺旋弹簧配合使用。弹片负责导电，弹簧负责提供夹持力。

> **不建议使用钢片电池夹**，夹持力过大容易损伤电池外壳；纯铜弹片弹力不足，需要搭配螺旋弹簧。

### 硬件注意事项

- **R27、R66** — 正常情况下**不需要焊接**，这两个电阻用于选择 NTC 与隔离 I2C 芯片的供电源
- **SW_activate 按键** — 电池刚接入或 BQ76920 进入运输模式后，需要按下此按键一次激活芯片
- **SW_reset 按键** — 长按数秒进入 WiFi AP 模式（重置网络）；开机时持续按住则重新进入配置模式
- **H6、H7 跳线** — 使用 3S 电池需短接 H6，4S 电池需短接 H7（取决于电池串数），**请务必仔细确认后再上电**
- PCB 设计文件位于 `hardware/` 目录（EasyEDA Pro 格式）
- 3D 打印外壳基于贴片电路板设计，可直接打印使用（文件后续上传）

### 引脚定义

<details>
<summary>点击展开完整引脚表</summary>

| 功能 | GPIO | 说明 |
|---|---|---|
| **I2C** | | |
| I2C_SDA | 11 | I2C 数据线 |
| I2C_SCL | 12 | I2C 时钟线 |
| **BQ24780S** | | |
| ACOK | 13 | 电源适配器接入检测 |
| PROCHOT# | 14 | 芯片报警状态 |
| TB_STAT# | 15 | 混合供电状态 |
| IADP | 1 | 输入电流 ADC |
| IDCHG | 2 | 放电电流 ADC |
| PMON | 9 | 系统功率监控 ADC |
| **BQ76920** | | |
| ALERT | 16 | 电池管理报警中断 |
| I2C_VCC | 6 | I2C 隔离芯片供电控制 |
| **电压/温度** | | |
| INPUT_VOLTAGE | 4 | 输入电压（1:10 分压） |
| BATTERY_VOLTAGE | 5 | 电池电压（1:10 分压） |
| BOARD_TEMP | 7 | 板载温度（NTC 10K） |
| ENV_TEMP | 8 | 环境温度（NTC 10K） |
| TEMP_POWER | 21 | NTC 供电使能 |
| **LED 指示** | | |
| POWER_LED | 42 | 电源指示灯 |
| CHARGING_LED | 41 | 充电指示灯 |
| DISCHARGING_LED | 40 | 放电指示灯 |
| WIFI_FAIL_LED | 39 | WiFi 连接失败 |
| WIFI_OK_LED | 38 | WiFi 连接成功 |
| RGB_LED | 48 | WS2812B 可编程 RGB |
| **控制** | | |
| BUZZER | 18 | 蜂鸣器 |
| RESET_BTN | 17 | 重置按键（长按 2.5s+ 恢复出厂） |

</details>

## 快速开始

### 环境准备

1. 安装 [Arduino IDE](https://www.arduino.cc/en/software)
2. 安装 ESP32-S3 开发板支持包（Board Manager 搜索 `esp32`）
3. 安装所需库：

| 库名 | 用途 |
|---|---|
| ESPAsyncWebServer | 异步 Web 服务器 |
| AsyncTCP | 异步 TCP |
| ArduinoJson | JSON 序列化 |
| FastLED | WS2812B RGB LED |
| Preferences | NVS Flash 存储 |
| esp_task_wdt | 看门狗定时器 |
| **AsyncMQTT_ESP32** | MQTT 客户端（作者 khoih-prog，**请务必安装此版本**） |

> **注意**：MQTT 库请安装 **AsyncMQTT_ESP32**（作者 khoih-prog），不要安装其他 MQTT 库，API 不兼容。

### 编译烧录

1. 用 Arduino IDE 打开项目根目录的 `sketch_jan14a.ino`
2. 选择开发板：ESP32-S3 Dev Module，并按如下配置：

   | 选项 | 值 |
   |---|---|
   | USB CDC On Boot | **Disabled** |
   | Flash Mode | **QIO 80MHz** |
   | Flash Size | **16MB** |
   | Partition Scheme | **Custom**（使用项目自带的 `partitions.csv`） |
   | PSRAM | **OPI PSRAM** |
   | USB Mode | **USB-OTG** |

3. 编译并烧录

### OTA 更新

系统运行后，访问 `http://<设备IP>/update`，拖拽固件文件即可在线升级。固件需包含签名校验头。

## 首次使用

### 按键操作说明

| 按键 | 操作 | 效果 |
|---|---|---|
| **SW_activate** | 短按 | 激活 BQ76920（电池接入或退出运输模式后使用） |
| **SW_reset** | 长按数秒（运行中） | 重置网络配置，进入 WiFi AP 热点模式 |
| **SW_reset** | 开机时持续按住 | 重新进入初始配置模式（清除 NVS 配置） |

### 1. 硬件准备

1. 选择 **ESP32-S3 N16R8** 开发板，插入主板时注意引脚方向
2. 根据电池串数短接对应跳线：
   - 3S 电池 → 短接 **H6**
   - 4S 电池 → 短接 **H7**
   - 5S 电池 → 无需短接
3. 连接电池，按下 **SW_activate** 按键一次激活 BQ76920
4. 连接电源适配器（12-19V）

> **3S 电池用户注意**：3S 电池满电电压为 12.6V，如果使用 12V 电源适配器，由于充电芯片需要一定的压差才能工作，电池将无法充满。建议使用 **13V 左右**的电源适配器。当然，充不满也无大碍——锂电池保持 40%-80% 电量区间反而更有利于延长电池寿命。

### 2. 网络配置

刷机后首次启动（NVS 无配置时），系统自动进入配置模式：

1. 搜索并连接 WiFi 热点 **`OpenUPS-esp32`**（密码：`12345678`）
2. 浏览器访问 `http://192.168.4.1` 进入配置向导
3. 按向导依次配置：WiFi → 网络 → 电池参数 → 硬件 → MQTT（可选）
4. 配置完成后系统自动重启，连接到你配置的 WiFi 网络

> WiFi 热点密码可在代码中自行修改。如果需要重新进入配置模式，开机时**持续按住 SW_reset** 即可。

### 3. 电池保护参数配置

**系统启动后，务必在配置页面正确设置电池保护参数，所有单位均为 mV 和 mA：**

| 参数 | 说明 | 示例（3S NCM） |
|---|---|---|
| 电池串数 | 3 / 4 / 5 | 3 |
| 标称容量 | 电池标称容量 (mAh) | 2500 |
| 过压阈值 (OV) | 单体过压保护 (mV) | 4250 |
| 过压恢复 | 单体过压恢复 (mV) | 4150 |
| 欠压阈值 (UV) | 单体欠压保护 (mV) | 2800 |
| 欠压恢复 | 单体欠压恢复 (mV) | 3000 |
| 最大充电电流 | 充电电流上限 (mA) | 2000 |
| 最大放电电流 | 放电电流上限 (mA) | 5000 |
| 短路保护阈值 | 短路电流阈值 (mA) | 10000 |
| 过温阈值 | 过温保护温度 (°C) | 55 |

> **请务必根据你所使用的电池规格设置这些参数，错误的参数可能导致电池损坏或安全事故！**

## Web 界面

系统运行后，浏览器访问设备 IP 即可查看仪表盘：

- **状态总览** — SOC 进度条、电压/电流/温度、5 节电池电压（高亮最大/最小值）、均衡状态、充放电模式
- **BMS 状态** — 详细电池信息、电池电压统计、BQ76920 寄存器转储
- **电源状态** — 输入/输出功率、充电控制状态、BQ24780S 寄存器转储
- **系统配置** — WiFi、BMS 参数、充放电管理、定时充电窗口、硬件控制
- **ADC 校准** — 各 ADC 通道独立校准系数
- **固件升级** — OTA 在线更新

## API 接口

| 端点 | 方法 | 说明 |
|---|---|---|
| `/` | GET | Web 仪表盘 |
| `/api/status` | GET | 系统状态 JSON |
| `/api/bms` | GET | BMS 数据 JSON |
| `/api/power` | GET | 电源数据 JSON |
| `/metrics` | GET | Prometheus 文本格式指标 |
| `/api/calibration` | GET/POST | ADC 校准读写 |
| `/save` | POST | 保存配置 |
| `/firmware` | POST | OTA 固件上传 |
| `/bms/shipmode` | POST | 进入 BMS 运输模式 |
| `/ws` | WebSocket | 实时数据推送（3s 间隔） |

## 架构概览

```
┌─────────────────────────────────────────────────────────┐
│                    SystemManagement                      │
│              (FSM: INIT → NORMAL → WARNING → CRITICAL)  │
├──────────┬──────────┬──────────┬──────────┬─────────────┤
│   BMS    │  Power   │ Hardware │ WebServer│  WiFiManager│
│ (BQ76920)│(BQ24780S)│Interface │ + OTA    │   STA/AP    │
├──────────┴──────────┴──────────┴──────────┴─────────────┤
│                     EventBus (Pub/Sub)                   │
├─────────────────────────────────────────────────────────┤
│              Global State (Blackboard)                   │
│         System_Global_State (all telemetry)              │
├─────────────────────────────────────────────────────────┤
│            Driver Layer (I2C + CRC)                      │
│         BQ24780S Driver  |  BQ76920 Driver               │
└─────────────────────────────────────────────────────────┘
```

**额外服务：**
- **UPS HID Service** — USB HID 协议，主机识别为标准 UPS
- **MQTT Service** — Home Assistant 自动发现 + TLS 支持
- **ConfigManager** — NVS Flash 持久化所有配置

## 分区布局

| 分区 | 类型 | 大小 | 说明 |
|---|---|---|---|
| nvs | data | 20KB | 配置存储 |
| otadata | data | 8KB | OTA 数据 |
| app0 | app | 4.5MB | 固件槽 A |
| app1 | app | 4.5MB | 固件槽 B |
| coredump | data | 64KB | 崩溃转储 |
| spiffs | data | 6MB | 文件系统 |

## 目录结构

```
├── sketch_jan14a.ino          # 主程序入口
├── partitions.csv             # 分区表
├── src/
│   ├── bq24780s.h/.cpp        # BQ24780S 充电芯片驱动
│   ├── bq76920.h/.cpp         # BQ76920 电池监控驱动
│   ├── i2c_interface.h/.cpp   # I2C 通信（含 CRC）
│   ├── pins_config.h          # 引脚定义
│   ├── data_structures.h      # 数据结构定义
│   ├── hardware_interface.h/.cpp  # GPIO/ADC/LED/蜂鸣器/按键
│   ├── bms.h/.cpp             # 电池管理系统
│   ├── power_management.h/.cpp    # 充放电管理
│   ├── system_management.h/.cpp   # 系统状态机
│   ├── config_manager.h/.cpp  # NVS 配置管理
│   ├── web_server.h/.cpp      # HTTP/WebSocket/Prometheus/OTA
│   ├── WiFiManager.h/.cpp     # WiFi 管理
│   ├── event_bus.h            # 事件总线
│   ├── event_types.h          # 事件类型定义
│   ├── ups_hid_service.h/.cpp # USB HID UPS 服务
│   ├── mqtt_service.h/.cpp    # MQTT + HA 自动发现
│   ├── time_utils.h/.cpp      # NTP 时间管理
│   ├── utils.h/.cpp           # 工具函数
│   └── templates/             # Web UI 模板
│       ├── css_templates.h
│       ├── js_templates.h
│       └── page_templates.h
├── hardware/
│   └── ups.eprj2              # EasyEDA Pro 硬件设计文件
└── doc/                       # 芯片寄存器文档
```

## 已知限制

- 代码目前处于**测试版本**，部分功能尚未完善
- 硬件设计基于 EasyEDA Pro，暂无 KiCad 版本
- SOC 计算在电池首次使用时需要一次完整充放电循环进行校准

## 安全提示

> **本项目涉及锂电池充放电管理，操作不当可能存在安全风险。请确保你：**
> - 了解锂电池安全知识
> - 能够正确设置电池保护参数（OV/UV/OCD/SCD）
> - 有基本的电子电路调试能力
> - 能够承担实验风险
>
> 作者不对因使用本项目造成的任何损失负责。

## 参与贡献

欢迎提交 Issue 和 Pull Request！

AI 时代，人人都是高级程序员。无论是代码优化、Bug 修复、功能增强还是文档改进，都欢迎参与。

![webpage](.\hardware\webpage.png)

![HomeAssistant](.\hardware\HomeAssistant.png)

![PCB_正面](.\hardware\PCB_正面.png)

![PCB_背面](.\hardware\PCB_背面.png)

![实物](.\hardware\实物.png)

![实物1](.\hardware\实物1.png)

![历史版本](.\hardware\历史版本.png)

## 致谢

本项目从硬件设计到嵌入式软件开发，几乎全部由 AI 辅助完成。作者此前没有任何硬件开发和嵌入式编程经验，没有 AI 的帮助，这个项目不可能实现。

特别感谢：

- **阿里巴巴通义千问（Qwen）**
- **小米 MiMo 大模型**

排名不分先后，都是伟大的公司！

AI 时代，人人都是高级程序员。

## 许可证

本项目为开源项目，详见仓库许可信息。
