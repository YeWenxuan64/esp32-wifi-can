# esp32-WiFi-CAN-Bridge
# ESP32 无线 slCAN 总线桥接器

## 📖 概述
本项目提供了一套基于 **ESP32** 系列的 **WiFi-CAN 透明桥接**固件，支持 LAWICEL/SLCAN 协议。通过 UDP 将 CAN 总线无缝延伸至无线网络，使得上位机（如 Python `python-can`）可以像操作本地 CAN 适配器一样远程操控 CAN 设备。



> 项目包含两套固件，配合使用：
> 1. **AP 模式**（`esp32_wifi_can_ap/`）：ESP32 作为 WiFi 热点 + CAN 网关，负责实际的 CAN 总线收发
> 2. **STA 模式**（`esp32_wifi_sta/`）：ESP32 作为 WiFi 客户端，将 USB 串口数据无线桥接至 AP 端

> #### 备注
> - 硬件兼容 **ESP32** / **ESP32-C3** / **ESP32-C6** / **ESP32-S3**
> - `legacy/` 目录中保留了早期 BLE（低功耗蓝牙）版本的实现，目前已弃用
>
> 同时本项目也是本小姐🍃的项目 [Focus-Finder](https://github.com/YeWenxuan64/Focus-Finder) | [DJI-Ronin-gimbal-focus-controller](https://github.com/YeWenxuan64/DJI-Ronin-gimbal-focus-controller) 项目中的硬件 CAN 控制器喵~



## 🧩 关键设计

- **LAWICEL/SLCAN 协议**：使用业界标准的 ASCII 文本 CAN 帧格式，兼容 SocketCAN、`python-can` 等主流工具链
- **UDP 无线透传**：WiFi 通信采用 UDP 协议，低延迟、无连接，适合实时控制场景
- **非阻塞架构**：所有 LED 指示、WiFi 收发、CAN 收发均在 `loop()` 中轮询，无 `delay()` 阻塞
- **NeoPixel 状态指示**：单颗 RGB LED 用颜色与闪烁直观展示设备工作状态（空闲/收发/等待连接）
- **TWAI v2 驱动**：使用 ESP32 原生 TWAI（Two-Wire Automotive Interface）v2 驱动，稳定可靠；兼容 ESP32 / C3 / C6 / S3 全系列

### 🏗️ 架构
```
┌────────────────────────────────────────────────────────┐
│   Host (PC / SBC)                                      │
│  ┌──────────────────────────────────────────────────┐  │
│  │  python-can / cantact / ...                      │  │
│  │  SLCAN protocol: t<ID><DLC><DATA>...             │  │
│  └───────────────────────┬──────────────────────────┘  │
│                          │  USB Serial                 │
│                          │                             │
├──────────────────────────┼─────────────────────────────┤
│                          │                             │
│   ESP32 STA mode         │                             │
│  ┌───────────────────────▼──────────────────────────┐  │
│  │  USB Serial ←→ UDP WiFi Client                   │  │
│  │  - Reads SLCAN commands from USB serial          │  │
│  │  - Forwards to AP via UDP                        │  │
│  │  - Receives CAN frames from AP, sends to USB     │  │
│  │  - NeoPixel LED: Red(wait) / Green(RX) / Blue(TX)│  │
│  └───────────────────────┬──────────────────────────┘  │
│                          │  UDP :8266                  │
│                          │  WiFi 2.4GHz                │
├──────────────────────────┼─────────────────────────────┤
│                          │                             │
│   ESP32 AP mode          │                             │
│  ┌───────────────────────▼──────────────────────────┐  │
│  │  UDP WiFi AP ←→ TWAI CAN                         │  │
│  │  - WiFi hotspot: ESP32-CAN-WIFI                  │  │
│  │  - Parses SLCAN commands, executes CAN ops       │  │
│  │  - Receives CAN frames, encodes as SLCAN reply   │  │
│  │  - NeoPixel LED: Red(wait) / Green(RX) / Blue(TX)│  │
│  └───────────────────────┬──────────────────────────┘  │
│                          │  TWAI (GPIO 4,5)            │
│                          ▼                             │
│                  CAN bus (CAN-H, CAN-L)                │
│                          │                             │
│                          ▼                             │
│         target CAN device (e.g. DJI Ronin ...)         │
└────────────────────────────────────────────────────────┘
```

### 📂 项目结构

```
esp32_WiFi_CAN/
├── README.md                     # 总览
├── LICENSE                       # MIT 许可证
├── esp32_wifi_can_ap/            # AP 模式固件（CAN 网关）
│   ├── esp32_twai-can.ino        # 主程序：SLCAN 解析 + TWAI CAN 驱动
│   ├── esp32_wifi-ap.h           # UDP WiFi AP 服务端类
│   └── esp32_neopixel-led.h      # 非阻塞 NeoPixel LED 控制类
├── esp32_wifi_sta/               # STA 模式固件（无线串口桥）
│   ├── esp32_sta-wifi.ino        # 主程序：USB 串口 ↔ WiFi 桥接
│   ├── esp32_wifi-sta.h          # UDP WiFi STA 客户端类
│   └── esp32_neopixel-led.h      # 非阻塞 NeoPixel LED 控制类
└── legacy/                       # 旧版 BLE 实现（已弃用）
    ├── esp32_ble-ap/             # BLE AP 模式
    └── esp32_ble-cli/            # BLE 客户端模式
```


## 🔧 安装与烧录

### 📋 硬件要求
- **ESP32 系列开发板** ×2，支持以下任意型号（需内置 WiFi）：
  - **ESP32**（经典款，如 ESP32-DevKitC / NodeMCU-32S）
  - **ESP32-C3**（RISC-V 内核，如 ESP32-C3-DevKitM-1）
  - **ESP32-C6**（RISC-V 内核，原生支持 WiFi 6）
  - **ESP32-S3**（XTensa 双核，如 ESP32-S3-DevKitC-1）
- **CAN 收发器模块** ×1（如 SN65HVD230 / TJA1050，3.3V 兼容）
- **USB 转串口模块**（用于 STA 端连接 PC；C3/C6/S3 板载 USB 可直接使用）
- NeoPixel RGB LED ×1（可选，用于状态指示）

### 🔌 接线
> 以 ESP32-C3 为例

**AP 端（CAN 网关）**

| ESP32 引脚 | 连接目标          |
|-----------|-------------------|
| GPIO 4    | CAN 收发器 TX     |
| GPIO 5    | CAN 收发器 RX     |
| GPIO 10   | NeoPixel Data In  |
| 3.3V / 5V | CAN 收发器 VCC    |
| GND       | CAN 收发器 GND    |

**STA 端（无线串口桥）**

| ESP32 引脚 | 连接目标          |
|-----------|-------------------|
| GPIO 10   | NeoPixel Data In  |
| USB       | 上位机 USB 口     |

### 📦 软件依赖

在 Arduino IDE 中安装以下库：
- **Adafruit NeoPixel**（LED 控制）

### 🚀 烧录步骤

1. 使用 Arduino IDE 打开对应 `.ino` 文件
2. 安装 `Arduino ESP32 Boards`, 安装 `Adafruit NeoPixel` (可选)
2. 根据实际芯片选择开发板：
3. 配置 Flash 参数（默认即可）
4. 分别烧录 AP 固件和 STA 固件到两块 ESP32


## 🎮 使用方法

### 🔋 1. 上电启动

1. **AP 端**先上电，等待 NeoPixel 亮起红色（表示 WiFi 热点已就绪，等待客户端连接）
2. **STA 端**上电，自动连接 AP 热点 `ESP32-CAN-WIFI`（密码 `12345678`）
3. 连接成功后两端的红色 LED 均常亮（空闲状态）

### 📡 2. SLCAN 协议通信

通过 STA 端的 USB 串口（波特率 **115200**）发送 SLCAN 命令，每条命令必须以 `\r`（回车符，`0x0D`）结尾：

```
O\r          — 打开 CAN 总线
S7\r         — 设置波特率为 800 Kbps（0-8 对应不同波特率）
t5048AABBCCDDEEFF88\r  — 发送标准帧：ID=0x504, DLC=8, 数据=AABBCCDDEEFF88
C\r          — 关闭 CAN 总线
V\r          — 查询固件版本
```

#### 支持的波特率

| 编号 | 波特率   |
|------|---------|
| 0    | 10 Kbps |
| 1    | 20 Kbps |
| 2    | 50 Kbps |
| 3    | 100 Kbps|
| 4    | 125 Kbps|
| 5    | 250 Kbps|
| 6    | 500 Kbps|
| 7    | 800 Kbps|
| 8    | 1 Mbps  |

### 🐍 3. Python 示例
> 配合 python-can 使用

```python
import can

# 使用 SLCAN 接口（通过 STA 端串口）
bus = can.interface.Bus(bustype='slcan', channel='COM3', bitrate=1000000)

# 发送 CAN 报文
msg = can.Message(arbitration_id=0x504, data=[0xAA, 0xBB, 0xCC, 0xDD])
bus.send(msg)

# 接收 CAN 报文
recv_msg = bus.recv(timeout=1.0)
print(recv_msg)
```

### 💡 5. NeoPixel LED 状态说明

| LED 状态             | 含义                   |
|---------------------|------------------------|
| 🔴 红色闪烁          | 等待 WiFi 连接          |
| 🔴 红色常量          | 已连接，无数据活动       |
| 🟢 绿色闪烁          | 正在接收数据（USB/CAN）  |
| 🔵 蓝色闪烁          | 正在发送数据（CAN 报文） |



## ⚠️ 注意事项

- AP 热点名称固定为 `ESP32-CAN-WIFI`，密码 `12345678`，端口 `8266`
- CAN 总线两端必须正确端接 **120Ω 终端电阻**，否则通信不稳定
- 需要独立的 CAN 收发器模块（如 SN65HVD230 / TJA1050）
- UDP 通信**无重传机制**，在强干扰环境中可能出现**丢包**
- 目前 AP 端最多支持 **1 个 STA 客户端**同时连接
- STA 端内建心跳保活机制，断线后自动重连
- 默认 CAN 过滤仅放行 ID `0x222, 0x223, 0x2e1, 0x426, 0x503, 0x504`，如需全收请修改 `esp32_twai-can.ino` 中的过滤逻辑
- **引脚差异**：ESP32-C3/C6/S3 的 GPIO 编号与经典 ESP32 不同，更换芯片时请对照数据手册调整 `TWAI_TX_PIN` / `TWAI_RX_PIN` / `NEOPIXEL_PIN` 宏定义


## 📚 参考
- [normaldotcom/canable2-fw](https://github.com/normaldotcom/canable2-fw)
- [python-can](https://github.com/hardbyte/python-can)
- [Focus-Finder 双目视觉焦点感知测距仪](https://github.com/YeWenxuan64/Focus-Finder)
- [DJI-Ronin-gimbal-controller](https://github.com/YeWenxuan64/DJI-Roini-gimbal-controller)

## 📜 许可证
[MIT License](LICENSE) © 2026 叶文轩
