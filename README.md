# WALL-E ESP32-S3 Voice Assistant Firmware

![ESP32-S3](https://img.shields.io/badge/Board-ESP32--S3-000000)
![PlatformIO](https://img.shields.io/badge/Build-PlatformIO-F5822A)
![Arduino](https://img.shields.io/badge/Framework-Arduino-00979D)
![C++](https://img.shields.io/badge/Language-C%2B%2B-00599C)

ESP32-S3-WROOM-1 N16R8 固件项目，用于制作一个 WALL-E 风格的语音/网页交互助手。固件内置 Wi-Fi 配网热点、网页控制台、联网文字对话、天气查询、SD 卡检测、I2S 扬声器测试和 INMP441 麦克风电平检测。

English: ESP32-S3 firmware for a WALL-E style assistant with Wi-Fi setup portal, web console, chat API, weather, SD card support, I2S speaker and microphone test.

## 项目亮点

- 开机自动创建 `WALL-E-Setup` 配网热点。
- 手机连接热点后可进入网页控制台配置 Wi-Fi。
- 支持校园网/公共网络 HTTP 认证入口探测。
- 支持联网文字对话和短期上下文记忆。
- 支持 Open-Meteo 天气查询。
- 支持 SD 卡挂载检测和离线备用回复。
- 支持 MAX98357A I2S 扬声器测试音、回复提示音和音量调节。
- 支持 INMP441 麦克风音量检测和唤醒测试。
- 密钥放在本地 `src/secrets.h`，仓库只提供示例文件。

## 硬件要求

目标开发板：

- ESP32-S3-WROOM-1 N16R8
- 16 MB Flash / 8 MB PSRAM
- 板载 SD 卡座
- 板载摄像头接口
- 双 USB-C

外接模块：

- INMP441 I2S 数字麦克风
- MAX98357A I2S 功放
- 4Ω/8Ω 小扬声器
- microSD 卡，可选

## 引脚分配

| 功能 | 引脚 |
| --- | --- |
| I2S BCLK | GPIO4 |
| I2S LRCLK / WS | GPIO5 |
| INMP441 DOUT | GPIO6 |
| MAX98357A DIN | GPIO7 |
| RGB LED | GPIO48 |
| 备用 LED | GPIO2 |
| SD_MMC CLK | GPIO39 |
| SD_MMC CMD | GPIO38 |
| SD_MMC D0 | GPIO40 |

## 接线说明

INMP441 I2S 数字麦克风：

| INMP441 | ESP32-S3 |
| --- | --- |
| VDD | 3V3 |
| GND | GND |
| SCK | GPIO4 |
| WS | GPIO5 |
| SD | GPIO6 |
| L/R | GND |

MAX98357A I2S 功放：

| MAX98357A | ESP32-S3 |
| --- | --- |
| VIN | 5V |
| GND | GND |
| BCLK | GPIO4 |
| LRC / WS | GPIO5 |
| DIN | GPIO7 |
| SD / EN | 3V3 或悬空 |
| SPK+ / SPK- | 扬声器 |

## 快速开始

安装 PlatformIO 后克隆项目：

```bash
git clone https://github.com/Davin-06/walle-esp32s3-firmware.git
cd walle-esp32s3-firmware
```

编译固件：

```bash
pio run
```

烧录到开发板：

```bash
pio run -t upload
```

打开串口监视器：

```bash
pio device monitor
```

默认串口在 `platformio.ini` 中设置为 `COM8`。如果你的开发板端口不同，请修改：

```ini
upload_port = COM8
monitor_port = COM8
```

## 配置联网对话

复制示例密钥文件：

```powershell
Copy-Item src\secrets.example.h src\secrets.h
```

编辑 `src/secrets.h`：

```cpp
#define WALL_E_DEEPSEEK_API_KEY "YOUR_DEEPSEEK_API_KEY"
```

`src/secrets.h` 已被 `.gitignore` 忽略，不要提交到 GitHub。如果没有配置 key，固件仍可编译，联网对话功能会提示需要配置服务端密钥。

## 使用方式

1. 给 ESP32-S3 上电。
2. 手机连接热点 `WALL-E-Setup`，默认密码 `12345678`。
3. 打开 `http://192.168.4.1` 进入网页控制台。
4. 扫描并连接目标 Wi-Fi。
5. 如果是校园网或公共网络，可使用网页里的认证入口探测功能。
6. 在“文字对话测试”中测试联网对话。
7. 在“语音入口”中点击“播放测试音”测试扬声器。
8. 打开“开发板麦克风唤醒测试”，对着 INMP441 说话，观察电平和触发次数。

## SD 卡资源

固件会尝试读取以下路径：

```text
/walle/manifest.json
/walle/models/tiny1m/model.bin
/walle/models/tiny1m/tokenizer.bin
/walle/models/stories260k/model.bin
/walle/models/stories260k/tokenizer.bin
/walle/config/offline_responses.json
```

这些模型和配置文件不随仓库发布，需要根据自己的硬件容量和功能需求准备。

## 当前能力边界

- 麦克风测试检测的是声音强度，不是真正的固定唤醒词识别。
- MAX98357A 是功放模块，不能单独把文字转换成语音。
- 中文朗读需要额外接入云端 TTS 或适合 ESP32-S3 的离线 TTS 方案。
- 校园网认证入口依赖不同学校的网关策略，可能需要按实际环境调整。

## 安全说明

仓库不包含真实 API key、Wi-Fi 密码、校园网账号或其他私人配置。请继续保持以下文件不进入版本库：

- `src/secrets.h`
- `.pio/`
- `.vscode/`
- `*.bin`
- `*.elf`
- `*.log`

## 适合用于

- ESP32-S3 语音助手项目
- PlatformIO 固件开发练习
- Wi-Fi 配网门户实验
- I2S 麦克风和扬声器测试
- 桌面机器人或智能硬件原型