# WALL-E ESP32-S3 Voice Assistant Firmware

ESP32-S3-WROOM-1 N16R8 固件，用于一个中文语音/网页对话机器人助手。开发板会启动配置热点，手机连接后进入网页配置 Wi-Fi，并可通过 DeepSeek API 进行联网聊天。

> 安全说明：仓库不包含任何 DeepSeek API key。请勿把自己的密钥提交到 GitHub。

## 功能

- 开机创建 `WALL-E-Setup` 配置热点，默认密码 `12345678`
- 手机连接热点后打开 `192.168.4.1` 进入配置页
- 支持扫描和切换 Wi-Fi
- 支持校园网/公共网络的 HTTP 认证页探测
- 连接 Wi-Fi 后开启 NAT，手机可通过开发板热点辅助访问认证页
- 连接成功后板载灯显示蓝色
- 网页文字对话测试
- DeepSeek 联网中文聊天
- Open-Meteo 天气查询
- 短期对话记忆
- SD 卡检测和离线备用回复
- I2S 扬声器测试音、回复提示音、音量调节
- INMP441 麦克风电平检测和“喊话触发提示音”硬件测试

## 硬件

目标开发板：

- ESP32-S3-WROOM-1 N16R8
- 16 MB Flash / 8 MB PSRAM
- 板载 SD 卡座
- 板载摄像头接口
- 双 USB-C

已使用的主要引脚：

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

## 接线

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

## 配置 DeepSeek API Key

复制示例文件：

```powershell
Copy-Item src\secrets.example.h src\secrets.h
```

编辑 `src/secrets.h`：

```cpp
#define WALL_E_DEEPSEEK_API_KEY "YOUR_DEEPSEEK_API_KEY"
```

`src/secrets.h` 已被 `.gitignore` 忽略，不要提交它。

如果没有配置 key，固件仍可编译，但联网聊天会提示需要配置 key。

## 编译和烧录

安装 PlatformIO 后，在仓库根目录执行：

```powershell
pio run
pio run -t upload
```

默认串口在 `platformio.ini` 中设置为 `COM8`。如果你的开发板端口不同，修改：

```ini
upload_port = COM8
monitor_port = COM8
```

## 使用方式

1. 给 ESP32-S3 上电。
2. 手机连接热点 `WALL-E-Setup`，密码 `12345678`。
3. 打开 `http://192.168.4.1`。
4. 在网页里扫描并连接目标 Wi-Fi。
5. 如果是校园网，连接成功后让手机断开并重新连接 `WALL-E-Setup`，再使用“校园网认证助手”尝试打开认证入口。
6. 在“文字对话测试”中测试聊天。
7. 在“语音入口”中点击“播放测试音”测试扬声器。
8. 打开“开发板麦克风唤醒测试”，对着 INMP441 说话，电平超过阈值后扬声器会播放回应提示音。

## 关于麦克风唤醒测试

当前固件做的是麦克风硬件链路验证：它检测声音强度，不是真正识别“瓦力”两个字。

如果要做到只在听到“瓦力”时唤醒，需要接入唤醒词模型或语音识别，例如 ESP-SR WakeNet、自训练唤醒词、或云端 ASR。

## 关于中文朗读

MAX98357A 只是功放，不能自己把文字变成语音。当前固件可以播放提示音，但还没有内置中文 TTS。

要让开发板真正朗读中文回复，需要接入：

- 云端 TTS API，将文本转成音频后播放
- 或适合 ESP32-S3 的离线中文 TTS 模型

## 注意

- 不要把 API key、账号密码、校园网账号提交到 GitHub。
- 校园网认证依赖学校网关策略，不同学校的跳转方式可能不一样。
- SD 卡里的本地模型文件不随仓库发布，需自行准备。
