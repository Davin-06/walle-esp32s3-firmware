#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <FS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SD.h>
#include <SD_MMC.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "driver/i2s.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/lwip_napt.h"
#include "esp32-hal-rgb-led.h"
#include "local_llm.h"
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WALL_E_DEEPSEEK_API_KEY
#define WALL_E_DEEPSEEK_API_KEY ""
#endif

static const char *AP_SSID = "WALL-E-Setup";
static const char *AP_PASS = "12345678";
static const char *DEEPSEEK_API_KEY = WALL_E_DEEPSEEK_API_KEY;
static const char *SD_MANIFEST_PATH = "/walle/manifest.json";
static const char *SD_TINY_MODEL_PATH = "/walle/models/tiny1m/model.bin";
static const char *SD_TINY_TOKENIZER_PATH = "/walle/models/tiny1m/tokenizer.bin";
static const char *SD_FALLBACK_MODEL_PATH = "/walle/models/stories260k/model.bin";
static const char *SD_FALLBACK_TOKENIZER_PATH = "/walle/models/stories260k/tokenizer.bin";
static const char *SD_OFFLINE_RESPONSES_PATH = "/walle/config/offline_responses.json";
static const uint8_t DNS_PORT = 53;
static const uint8_t LED_RGB_PIN = 48;
static const uint8_t LED_BACKUP_PIN = 2;
static const int I2S_BCLK_PIN = 4;
static const int I2S_WS_PIN = 5;
static const int I2S_MIC_DOUT_PIN = 6;
static const int I2S_SPK_DIN_PIN = 7;
static const uint32_t I2S_AUDIO_RATE = 16000;
static const int SD_XIAO_CS = 21;
static const int SD_XIAO_SCK = 7;
static const int SD_XIAO_MISO = 8;
static const int SD_XIAO_MOSI = 9;
static const int SD_CAM_CS = 38;
static const int SD_CAM_SCK = 39;
static const int SD_CAM_MISO = 40;
static const int SD_CAM_MOSI = 41;
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

String savedSsid;
String savedPass;
String savedWeatherName = "南昌";
double savedWeatherLat = 28.6820;
double savedWeatherLon = 115.8580;
bool wifiReady = false;
bool natEnabled = false;
bool apDnsReal = false;
bool speakerReady = false;
uint8_t audioVolume = 45;
bool replyToneEnabled = true;
enum AudioI2sMode { AUDIO_I2S_NONE, AUDIO_I2S_SPEAKER, AUDIO_I2S_MIC };
AudioI2sMode audioI2sMode = AUDIO_I2S_NONE;
bool micReady = false;
bool micWakeTestEnabled = true;
uint16_t micThreshold = 1800;
uint32_t micLevel = 0;
uint32_t micPeakLevel = 0;
uint32_t micTriggerCount = 0;
uint32_t micLastPoll = 0;
uint32_t micLastTriggerMs = 0;
uint32_t lastWifiCheck = 0;
static const uint8_t CHAT_MEMORY_TURNS = 6;
String memoryUser[CHAT_MEMORY_TURNS];
String memoryAssistant[CHAT_MEMORY_TURNS];
uint8_t memoryTurnCount = 0;
fs::FS *sdFs = nullptr;
bool sdReady = false;
bool sdPrimaryModelReady = false;
bool sdFallbackModelReady = false;
String sdMode = "未挂载";
String sdLastError = "尚未检测";
uint64_t sdTotalBytes = 0;
uint64_t sdUsedBytes = 0;
String offlineNoNetwork = "哔卟，网络断开了。我先用本地小脑袋陪你，聪明程度打折，但态度不打折。";
String offlineCantAnswer = "这个离线问题我现在答不稳，等联网后再问我一次。";
String speakerLastError = "尚未初始化";

String micLastError = "Not initialized";

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WALL-E 控制台</title>
<style>
:root{color-scheme:dark;--bg:#101418;--panel:#171f25;--line:#2b3740;--text:#eef5f7;--muted:#9fb0b8;--blue:#36a8ff;--green:#50df89;--warn:#ffd38b}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,sans-serif}
main{max-width:820px;margin:0 auto;padding:18px}h1{font-size:24px;margin:8px 0 4px}h2{font-size:18px;margin:0 0 12px}.sub,.hint{color:var(--muted);font-size:14px;line-height:1.45}
.top{display:flex;justify-content:space-between;gap:12px;align-items:flex-start}.badge{border:1px solid var(--line);border-radius:999px;padding:6px 10px;color:var(--muted);font-size:13px;white-space:nowrap}
section{border-top:1px solid var(--line);padding:18px 0}.panel{border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:12px;margin:10px 0}
.kv{display:grid;grid-template-columns:96px 1fr;gap:8px;font-size:14px}.kv b{color:var(--muted);font-weight:500}.ok{color:var(--green)}.bad{color:#ff9a9a}.warn{color:var(--warn)}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}button,input,select,textarea{font:inherit;border-radius:8px;border:1px solid var(--line);background:#0d1115;color:var(--text);padding:11px 12px}
button{background:#1d2a33;cursor:pointer}button.primary{background:var(--blue);border-color:var(--blue);color:#00131f;font-weight:700}button.danger{background:#3a2222}
input,select,textarea{width:100%;margin:6px 0 10px}textarea{min-height:74px;resize:vertical}.grid{display:grid;grid-template-columns:1fr auto;gap:8px}.log{height:260px;overflow:auto;border:1px solid var(--line);border-radius:8px;padding:10px;background:#0b0f12;white-space:pre-wrap}.msg{margin:0 0 10px}.me{color:#9ed2ff}.bot{color:#d4ffc8}
input[type=checkbox]{width:auto;margin:0 6px 0 0;vertical-align:middle}
.files{white-space:pre-wrap;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:12px;color:#cdd8dd;background:#0b0f12;border:1px solid var(--line);border-radius:8px;padding:8px;margin-top:8px;overflow:auto}
</style>
</head>
<body>
<main>
  <div class="top"><div><h1>WALL-E 控制台</h1><p class="sub">先联网，再用下面的文字框测试对话。手机麦克风/扬声器只是网页功能，不需要开发板外接硬件。</p></div><span id="statusBadge" class="badge">检查中</span></div>

  <section>
    <h2>当前网络</h2>
    <div class="panel kv">
      <b>状态</b><span id="wifiState">-</span>
      <b>连接 Wi-Fi</b><span id="wifiName">-</span>
      <b>开发板 IP</b><span id="staIp">-</span>
      <b>配置页 IP</b><span id="apIp">192.168.4.1</span>
      <b>NAT 转发</b><span id="natState">-</span>
      <b>天气位置</b><span id="weatherPlace">-</span>
      <b>短期记忆</b><span id="memoryState">-</span>
      <b>SD 卡</b><span id="sdState">-</span>
      <b>硬件</b><span id="hwInfo">-</span>
      <b>存储/PSRAM</b><span id="memInfo">-</span>
      <b>提示</b><span id="netHint" class="hint">-</span>
    </div>
    <div class="row"><button onclick="refresh()">刷新状态</button><button onclick="sdCheck()">检测背面 SD 卡槽</button><button class="danger" onclick="forgetWifi()">断开并忘记 Wi-Fi</button><button onclick="diagnose()">网络诊断</button></div>
    <div id="diag" class="hint"></div>
    <div id="sdBox" class="panel hint"></div>
  </section>

  <section>
    <h2>连接或更换 Wi-Fi</h2>
    <div class="row"><button onclick="scan()">搜索附近 Wi-Fi</button><button onclick="portalAssist()">校园网认证助手</button><button onclick="diagnose()">网络诊断</button></div>
    <select id="ssid" onchange="ssidManual.value=this.value"></select>
    <input id="ssidManual" placeholder="Wi-Fi 名称，可手动输入">
    <input id="pass" type="password" placeholder="Wi-Fi 密码，开放校园网可留空">
    <button class="primary" onclick="connectWifi()">连接 / 更换 Wi-Fi</button>
    <p class="hint warn">校园网认证建议：连接校园 Wi-Fi 成功后，手机先断开 WALL-E-Setup 再重新连接一次，让手机拿到新的 DNS；然后点“校园网认证助手”。不要使用 https://1.1.1.1 触发认证，HTTPS 通常无法被校园网正常劫持。</p>
    <div id="portalBox" class="panel hint"></div>
  </section>

  <section>
    <h2>文字对话测试</h2>
    <textarea id="textQ" placeholder="例如：瓦力，你现在联网了吗？ 或：瓦力，福建明天天气怎么样？"></textarea>
    <div class="row"><button class="primary" onclick="askText()">发送给瓦力</button><button onclick="clearLog()">清空聊天框</button><button onclick="clearMemory()">清空短期记忆</button></div>
    <div id="log" class="log"></div>
  </section>

  <section>
    <h2>语音入口</h2>
    <div class="row"><button id="talk" onclick="toggleListen()">开始听</button><button onclick="speakerTest()">播放测试音</button><span id="heard" class="badge">可选：手机浏览器支持才可用</span></div>
    <div class="panel">
      <label class="hint">硬件音量：<span id="volLabel">-</span>%</label>
      <input id="volume" type="range" min="0" max="100" value="45" oninput="volLabel.textContent=this.value" onchange="saveAudio()">
      <label class="hint"><input id="replyTone" type="checkbox" onchange="saveAudio()"> 回复时扬声器同步提示音</label>
      <label class="hint"><input id="micWake" type="checkbox" onchange="saveMic()"> 开发板麦克风唤醒测试</label>
      <label class="hint">麦克风阈值：<span id="micThrLabel">-</span></label>
      <input id="micThreshold" type="range" min="200" max="12000" step="100" value="1800" oninput="micThrLabel.textContent=this.value" onchange="saveMic()">
      <p id="micState" class="hint">-</p>
      <p id="audioState" class="hint">-</p>
    </div>
  </section>
</main>
<script>
const $=id=>document.getElementById(id);
let rec,listening=false,awake=false;
function esc(s){return String(s||'').replace(/[<>&]/g,m=>({'<':'&lt;','>':'&gt;','&':'&amp;'}[m]))}
function log(cls,who,text){$('log').innerHTML+=`<div class="msg ${cls}">${who}: ${esc(text)}</div>`;$('log').scrollTop=$('log').scrollHeight}
function say(t){try{const u=new SpeechSynthesisUtterance(t);u.lang='zh-CN';u.rate=.92;speechSynthesis.speak(u)}catch(e){}}
async function refresh(){const r=await fetch('/api/status');const j=await r.json();$('wifiState').innerHTML=j.wifi?'<span class="ok">已连接</span>':'<span class="bad">未连接</span>';$('wifiName').textContent=j.ssid||'未连接';$('staIp').textContent=j.ip||'-';$('apIp').textContent=j.ap||'192.168.4.1';$('natState').textContent=j.nat?'已开启':'未开启';$('weatherPlace').textContent=j.weatherName||'南昌';$('memoryState').textContent=(j.memoryTurns||0)+'轮';$('sdState').innerHTML=j.sdReady?'<span class="ok">'+(j.sdMode||'已挂载')+'</span>':'<span class="bad">未挂载</span>';$('hwInfo').textContent=(j.chip||'ESP32-S3')+' / '+(j.board||'N16R8');$('memInfo').textContent=`Flash ${j.flashMB||0}MB / PSRAM ${j.psramMB||0}MB，可用 ${j.freePsramKB||0}KB`;if($('volume')){$('volume').value=j.audioVolume??45;$('volLabel').textContent=$('volume').value;$('replyTone').checked=!!j.replyToneEnabled;$('audioState').textContent=(j.speakerReady?'扬声器已就绪':'扬声器未就绪')+' / '+(j.speakerError||'');}$('statusBadge').textContent=j.wifi?('已联网：'+(j.ssid||j.ip)):'未联网';$('netHint').textContent=j.nat?'手机重连 WALL-E-Setup 后点校园网认证助手。':'先连接一个 Wi-Fi。'}
async function scan(){$('ssid').innerHTML='<option>扫描中...</option>';const r=await fetch('/api/scan');const j=await r.json();$('ssid').innerHTML=j.networks.map(n=>`<option value="${esc(n.ssid)}">${esc(n.ssid)} (${n.rssi} dBm) ${n.open?'开放':'加密'}</option>`).join('');if(j.networks[0])$('ssidManual').value=j.networks[0].ssid}
async function connectWifi(){const ssid=$('ssidManual').value||$('ssid').value;const pass=$('pass').value;const r=await fetch('/api/connect',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({ssid,pass})});alert(await r.text());setTimeout(refresh,1800)}
async function forgetWifi(){if(!confirm('确定断开并忘记当前 Wi-Fi？'))return;const r=await fetch('/api/disconnect',{method:'POST'});alert(await r.text());setTimeout(refresh,800)}
async function portalAssist(){const box=$('portalBox');box.textContent='正在探测校园网认证入口...';const r=await fetch('/api/portal_probe');const j=await r.json();let html='<b>请依次尝试下面的 HTTP 入口：</b><br>';for(const u of j.links){html+=`<p><a href="${esc(u.url)}" target="_blank">${esc(u.name)}</a><br><span>${esc(u.result||'')}</span></p>`}if(j.redirect){html=`<b class="ok">探测到疑似认证页：</b><p><a href="${esc(j.redirect)}" target="_blank">${esc(j.redirect)}</a></p>`+html}html+='<p class="warn">如果手机把链接自动改成 https，返回本页换另一个入口；校园网认证必须靠 HTTP 触发。</p>';box.innerHTML=html}
async function diagnose(){$('diag').textContent='诊断中...';const r=await fetch('/api/diagnose');const j=await r.json();$('diag').textContent=`Wi-Fi: ${j.wifi?'已连接':'未连接'} / NAT: ${j.nat?'开':'关'} / HTTP入口1: ${j.msftProbe} / HTTP入口2: ${j.appleProbe} / DeepSeek: ${j.deepseekProbe}`;}
async function sdCheck(){const box=$('sdBox');box.textContent='正在检测背面 SD 卡槽...';const r=await fetch('/api/sd_check',{method:'POST'});const j=await r.json();$('sdState').innerHTML=j.ready?'<span class="ok">'+esc(j.mode||'已挂载')+'</span>':'<span class="bad">未挂载</span>';let paths='';for(const p of (j.paths||[])){paths+=`${p.ok?'OK':'缺失'}  ${p.path}${p.alias?' -> '+p.alias:''}  ${p.bytes||0} B\n`}box.innerHTML=`<b>SD 状态：</b>${j.ready?'已挂载':'未挂载'}<br><b>方式：</b>${esc(j.mode||'-')}<br><b>容量：</b>${esc(j.totalMB||0)} MB，已用 ${esc(j.usedMB||0)} MB<br><b>tiny1m：</b>${j.primaryModel?'存在':'缺失'}<br><b>备用模型：</b>${j.fallbackModel?'存在':'缺失'}<br><b>manifest：</b>${j.manifest?'存在':'缺失'}<br><b>提示：</b>${esc(j.error||'')}<div class="files">${esc(paths||'没有路径检查结果')}</div><div class="files">${esc(j.listing||'没有目录列表')}</div>`;}
async function askText(){const text=$('textQ').value.trim();if(!text)return;log('me','我',text);$('textQ').value='';const r=await fetch('/api/chat',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({message:text})});const j=await r.json();const reply=j.reply||j.error||'没有收到回复';log('bot','瓦力',reply);say(reply)}
function clearLog(){$('log').innerHTML=''}
async function clearMemory(){const r=await fetch('/api/memory_clear',{method:'POST'});const j=await r.json();$('memoryState').textContent='0轮';log('bot','瓦力',j.reply||'短期记忆已清空')}
async function ask(text){$('heard').textContent='思考中';await askTextFromSpeech(text);awake=false;$('heard').textContent='等待唤醒词：瓦力'}
async function askTextFromSpeech(text){log('me','我',text);const r=await fetch('/api/chat',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({message:text})});const j=await r.json();const reply=j.reply||j.error;log('bot','瓦力',reply);say(reply)}
async function speakerTest(){$('heard').textContent='正在播放测试音';const r=await fetch('/api/speaker_test',{method:'POST'});const j=await r.json();$('heard').textContent=j.ok?'测试音已发送':'测试音失败：'+(j.error||'未知错误')}
async function saveAudio(){const volume=Number($('volume').value||0);const replyTone=$('replyTone').checked;const r=await fetch('/api/audio_config',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({volume,replyTone})});const j=await r.json();$('audioState').textContent=j.ok?'已保存 / 音量 '+j.volume+'%':'保存失败：'+(j.error||'未知错误')}
function updateMicUi(j){if(!$('micWake'))return;$('micWake').checked=!!j.micWakeTestEnabled;$('micThreshold').value=j.micThreshold||1800;$('micThrLabel').textContent=$('micThreshold').value;const mode=j.audioMode||'-';$('micState').textContent=`麦克风：${j.micReady?'已监听':'未监听'} / 电平 ${j.micLevel||0} / 峰值 ${j.micPeakLevel||0} / 触发 ${j.micTriggerCount||0} 次 / ${mode} / ${j.micError||''}`;}
async function refreshMic(){try{const r=await fetch('/api/status');const j=await r.json();updateMicUi(j)}catch(e){}}
async function saveMic(){const enabled=$('micWake').checked;const threshold=Number($('micThreshold').value||1800);$('micThrLabel').textContent=threshold;const r=await fetch('/api/mic_config',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({enabled,threshold})});const j=await r.json();if(j.ok)updateMicUi(j);else $('micState').textContent='保存失败：'+(j.error||'未知错误')}
function setupSpeech(){const SR=window.SpeechRecognition||window.webkitSpeechRecognition;if(!SR){$('heard').textContent='此浏览器不支持语音识别';return}rec=new SR();rec.lang='zh-CN';rec.continuous=true;rec.interimResults=false;rec.onresult=e=>{const t=e.results[e.results.length-1][0].transcript.trim();$('heard').textContent=t;if(!awake&&/瓦力|哇力|wall\s*e/i.test(t)){awake=true;say('嗨，我在。');return}if(awake)ask(t.replace(/^(瓦力|哇力)/,''))};rec.onend=()=>{if(listening)rec.start()}}
function toggleListen(){if(!rec)setupSpeech();if(!rec)return;listening=!listening;$('talk').textContent=listening?'停止听':'开始听';if(listening)rec.start();else rec.stop()}
refresh();scan();refreshMic();setInterval(refresh,5000);setInterval(refreshMic,1000);
</script>
</body>
</html>
)HTML";

void logBoth(const String &message) {
  Serial.println(message);
  Serial0.println(message);
}

bool initMicI2s();

const char *audioModeName() {
  if (audioI2sMode == AUDIO_I2S_SPEAKER) return "speaker";
  if (audioI2sMode == AUDIO_I2S_MIC) return "mic";
  return "none";
}

void releaseAudioI2s() {
  if (audioI2sMode != AUDIO_I2S_NONE || speakerReady || micReady) {
    i2s_driver_uninstall(I2S_NUM_0);
  }
  audioI2sMode = AUDIO_I2S_NONE;
  speakerReady = false;
  micReady = false;
}

void resumeMicWakeTest() {
  if (micWakeTestEnabled) initMicI2s();
}

void setLed(bool connected) {
  digitalWrite(LED_BACKUP_PIN, connected ? HIGH : LOW);
  if (connected) {
    neopixelWrite(LED_RGB_PIN, 0, 0, 32);
  } else {
    neopixelWrite(LED_RGB_PIN, 0, 0, 0);
  }
}

bool initSpeakerI2s() {
  if (audioI2sMode == AUDIO_I2S_SPEAKER && speakerReady) return true;
  if (audioI2sMode != AUDIO_I2S_NONE) releaseAudioI2s();

  i2s_config_t config;
  memset(&config, 0, sizeof(config));
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = I2S_AUDIO_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = 0;
  config.dma_buf_count = 6;
  config.dma_buf_len = 128;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;
  config.mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT;
  config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &config, 0, nullptr);
  if (err == ESP_ERR_INVALID_STATE) {
    i2s_driver_uninstall(I2S_NUM_0);
    err = i2s_driver_install(I2S_NUM_0, &config, 0, nullptr);
  }
  if (err != ESP_OK) {
    speakerLastError = "I2S driver install failed: " + String(esp_err_to_name(err));
    logBoth(speakerLastError);
    releaseAudioI2s();
    return false;
  }

  i2s_pin_config_t pins;
  memset(&pins, 0, sizeof(pins));
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_BCLK_PIN;
  pins.ws_io_num = I2S_WS_PIN;
  pins.data_out_num = I2S_SPK_DIN_PIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) {
    speakerLastError = "I2S set pin failed: " + String(esp_err_to_name(err));
    logBoth(speakerLastError);
    releaseAudioI2s();
    return false;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  speakerReady = true;
  micReady = false;
  audioI2sMode = AUDIO_I2S_SPEAKER;
  speakerLastError = "I2S speaker ready";
  logBoth("I2S speaker ready: BCLK GPIO" + String(I2S_BCLK_PIN) + ", WS GPIO" + String(I2S_WS_PIN) +
          ", DIN GPIO" + String(I2S_SPK_DIN_PIN));
  return true;
}

int16_t speakerAmplitude(uint16_t base = 90) {
  uint16_t vol = audioVolume > 100 ? 100 : audioVolume;
  return static_cast<int16_t>(vol * base);
}

void speakerWriteTone(float freq, uint16_t durationMs, int16_t amplitude) {
  if (!initSpeakerI2s()) return;
  const uint16_t chunkFrames = 128;
  int16_t samples[chunkFrames * 2];
  uint32_t totalFrames = (I2S_AUDIO_RATE * durationMs) / 1000;
  float phase = 0.0f;
  float step = freq > 0.0f ? (6.283185307f * freq / I2S_AUDIO_RATE) : 0.0f;

  while (totalFrames > 0) {
    uint16_t frames = totalFrames > chunkFrames ? chunkFrames : totalFrames;
    for (uint16_t i = 0; i < frames; i++) {
      int16_t value = freq > 0.0f ? static_cast<int16_t>(sinf(phase) * amplitude) : 0;
      phase += step;
      if (phase > 6.283185307f) phase -= 6.283185307f;
      samples[i * 2] = value;
      samples[i * 2 + 1] = value;
    }
    size_t bytesWritten = 0;
    i2s_write(I2S_NUM_0, samples, frames * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    totalFrames -= frames;
  }
}

bool playSpeakerTestTone() {
  if (!initSpeakerI2s()) {
    resumeMicWakeTest();
    return false;
  }
  int16_t amp = speakerAmplitude();
  if (amp <= 0) {
    resumeMicWakeTest();
    return true;
  }
  speakerWriteTone(660.0f, 180, amp);
  speakerWriteTone(0.0f, 70, 0);
  speakerWriteTone(880.0f, 180, amp);
  speakerWriteTone(0.0f, 90, 0);
  i2s_zero_dma_buffer(I2S_NUM_0);
  resumeMicWakeTest();
  return true;
}

bool playReplyTone() {
  if (!replyToneEnabled || audioVolume == 0) return true;
  if (!initSpeakerI2s()) {
    resumeMicWakeTest();
    return false;
  }
  int16_t amp = speakerAmplitude(70);
  speakerWriteTone(740.0f, 70, amp);
  speakerWriteTone(0.0f, 35, 0);
  speakerWriteTone(980.0f, 85, amp);
  i2s_zero_dma_buffer(I2S_NUM_0);
  resumeMicWakeTest();
  return true;
}

bool initMicI2s() {
  if (audioI2sMode == AUDIO_I2S_MIC && micReady) return true;
  if (audioI2sMode != AUDIO_I2S_NONE) releaseAudioI2s();

  i2s_config_t config;
  memset(&config, 0, sizeof(config));
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = I2S_AUDIO_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = 0;
  config.dma_buf_count = 6;
  config.dma_buf_len = 128;
  config.use_apll = false;
  config.tx_desc_auto_clear = false;
  config.fixed_mclk = 0;
  config.mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT;
  config.bits_per_chan = I2S_BITS_PER_CHAN_32BIT;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &config, 0, nullptr);
  if (err == ESP_ERR_INVALID_STATE) {
    i2s_driver_uninstall(I2S_NUM_0);
    err = i2s_driver_install(I2S_NUM_0, &config, 0, nullptr);
  }
  if (err != ESP_OK) {
    micLastError = "I2S mic install failed: " + String(esp_err_to_name(err));
    logBoth(micLastError);
    releaseAudioI2s();
    return false;
  }

  i2s_pin_config_t pins;
  memset(&pins, 0, sizeof(pins));
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_BCLK_PIN;
  pins.ws_io_num = I2S_WS_PIN;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = I2S_MIC_DOUT_PIN;
  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) {
    micLastError = "I2S mic set pin failed: " + String(esp_err_to_name(err));
    logBoth(micLastError);
    releaseAudioI2s();
    return false;
  }

  i2s_start(I2S_NUM_0);
  micReady = true;
  speakerReady = false;
  audioI2sMode = AUDIO_I2S_MIC;
  micLastError = "I2S mic ready";
  logBoth("I2S mic ready: BCLK GPIO" + String(I2S_BCLK_PIN) + ", WS GPIO" + String(I2S_WS_PIN) +
          ", DOUT GPIO" + String(I2S_MIC_DOUT_PIN));
  return true;
}

uint32_t readMicLevelFrame() {
  if (!micWakeTestEnabled) return micLevel;
  if (!initMicI2s()) return micLevel;

  int32_t samples[128];
  size_t bytesRead = 0;
  esp_err_t err = i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytesRead, pdMS_TO_TICKS(4));
  if (err != ESP_OK || bytesRead == 0) {
    if (err != ESP_ERR_TIMEOUT) micLastError = "I2S mic read failed: " + String(esp_err_to_name(err));
    return micLevel;
  }

  uint32_t count = bytesRead / sizeof(samples[0]);
  if (count == 0) return micLevel;

  uint64_t sum = 0;
  uint32_t peak = 0;
  for (uint32_t i = 0; i < count; i++) {
    int32_t v = samples[i] >> 14;
    uint32_t a = static_cast<uint32_t>(v < 0 ? -v : v);
    sum += a;
    if (a > peak) peak = a;
  }

  micLevel = static_cast<uint32_t>(sum / count);
  if (peak > micPeakLevel) micPeakLevel = peak;
  else micPeakLevel = (micPeakLevel * 15) / 16;
  return micLevel;
}

bool playMicWakeResponseTone() {
  if (!initSpeakerI2s()) {
    resumeMicWakeTest();
    return false;
  }
  int16_t amp = speakerAmplitude(82);
  if (amp > 0) {
    speakerWriteTone(520.0f, 70, amp);
    speakerWriteTone(0.0f, 35, 0);
    speakerWriteTone(780.0f, 90, amp);
    speakerWriteTone(0.0f, 30, 0);
    speakerWriteTone(1040.0f, 70, amp);
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  resumeMicWakeTest();
  return true;
}

void pollMicWakeTest() {
  if (!micWakeTestEnabled) return;
  uint32_t now = millis();
  if (now - micLastPoll < 60) return;
  micLastPoll = now;

  uint32_t level = readMicLevelFrame();
  static uint8_t loudFrames = 0;
  if (level >= micThreshold) {
    if (loudFrames < 4) loudFrames++;
  } else if (loudFrames > 0) {
    loudFrames--;
  }

  if (loudFrames >= 2 && now - micLastTriggerMs > 1800) {
    micLastTriggerMs = now;
    micTriggerCount++;
    loudFrames = 0;
    micLastError = "Wake sound detected";
    playMicWakeResponseTone();
  }
}

void setApDnsForClients(const char *dnsIp, bool realDns) {
  esp_netif_t *apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!apNetif) {
    logBoth("AP netif not found, cannot set DHCP DNS");
    return;
  }

  esp_netif_dns_info_t dns;
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = ipaddr_addr(dnsIp);
  esp_netif_dhcps_stop(apNetif);
  esp_err_t err = esp_netif_set_dns_info(apNetif, ESP_NETIF_DNS_MAIN, &dns);
  esp_netif_dhcps_start(apNetif);
  apDnsReal = realDns && err == ESP_OK;
  logBoth(String("AP DHCP DNS set to ") + dnsIp + (err == ESP_OK ? "" : " failed"));
}

void disableNatAndRestoreCaptiveDns() {
  if (natEnabled) {
    ip_napt_enable(WiFi.softAPIP(), 0);
    natEnabled = false;
    logBoth("NAPT disabled");
  }
  setApDnsForClients("192.168.4.1", false);
  dnsServer.stop();
  dnsServer.start(DNS_PORT, "*", AP_IP);
}

void startAp() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(AP_SSID, AP_PASS);
  setApDnsForClients("192.168.4.1", false);
  dnsServer.start(DNS_PORT, "*", AP_IP);
  logBoth("Setup AP: " + String(AP_SSID) + " / " + WiFi.softAPIP().toString());
}

void enableNatIfPossible() {
  if (natEnabled || WiFi.status() != WL_CONNECTED) return;
  ip_napt_enable(WiFi.softAPIP(), 1);
  natEnabled = true;
  dnsServer.stop();
  setApDnsForClients("223.5.5.5", true);
  logBoth("NAPT enabled on AP IP: " + WiFi.softAPIP().toString());
}

bool connectSta(const String &ssid, const String &pass, uint32_t timeoutMs = 18000) {
  if (ssid.isEmpty()) return false;
  if (natEnabled) {
    ip_napt_enable(WiFi.softAPIP(), 0);
    natEnabled = false;
  }
  WiFi.disconnect(false, false);
  delay(300);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
  }
  wifiReady = WiFi.status() == WL_CONNECTED;
  setLed(wifiReady);
  if (wifiReady) {
    enableNatIfPossible();
    logBoth("Connected Wi-Fi: " + ssid + " / " + WiFi.localIP().toString());
  } else {
    disableNatAndRestoreCaptiveDns();
    logBoth("Wi-Fi connect failed: " + ssid);
  }
  return wifiReady;
}

String httpProbe(const char *url) {
  if (WiFi.status() != WL_CONNECTED) return "未联网";
  HTTPClient http;
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  const char *headers[] = {"Location", "Content-Type"};
  http.collectHeaders(headers, 2);
  if (!http.begin(url)) return "无法开始请求";
  int code = http.GET();
  String location = http.header("Location");
  String contentType = http.header("Content-Type");
  http.end();
  String result = String(code);
  if (location.length()) result += " -> " + location;
  else if (contentType.length()) result += " / " + contentType;
  return result;
}

String firstRedirectFromProbes() {
  const char *urls[] = {
      "http://connectivitycheck.gstatic.com/generate_204",
      "http://www.msftconnecttest.com/connecttest.txt",
      "http://captive.apple.com/hotspot-detect.html",
      "http://neverssl.com/",
      "http://example.com/"};
  for (const char *url : urls) {
    HTTPClient http;
    http.setTimeout(6000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.useHTTP10(true);
    const char *headers[] = {"Location"};
    http.collectHeaders(headers, 1);
    if (!http.begin(url)) continue;
    int code = http.GET();
    String location = http.header("Location");
    http.end();
    if (code >= 300 && code < 400 && location.length()) return location;
  }
  return "";
}

String previewText(String text, size_t limit = 180) {
  text.replace("\r", " ");
  text.replace("\n", " ");
  text.trim();
  if (text.length() > limit) text = text.substring(0, limit) + "...";
  return text;
}

const char *sdShortAliasFor(const char *path) {
  if (!path) return nullptr;
  if (strcmp(path, SD_MANIFEST_PATH) == 0) return "/WALLE/MANIFE~1.JSO";
  if (strcmp(path, SD_TINY_MODEL_PATH) == 0) return "/WALLE/MODELS/TINY1M/MODEL.BIN";
  if (strcmp(path, SD_TINY_TOKENIZER_PATH) == 0) return "/WALLE/MODELS/TINY1M/TOKENI~1.BIN";
  if (strcmp(path, SD_FALLBACK_MODEL_PATH) == 0) return "/WALLE/MODELS/STORIE~1/MODEL.BIN";
  if (strcmp(path, SD_FALLBACK_TOKENIZER_PATH) == 0) return "/WALLE/MODELS/STORIE~1/TOKENI~1.BIN";
  if (strcmp(path, SD_OFFLINE_RESPONSES_PATH) == 0) return "/WALLE/CONFIG/OFFLIN~1.JSO";
  if (strcmp(path, "/walle/models/stories260k") == 0) return "/WALLE/MODELS/STORIE~1";
  return nullptr;
}

File sdOpenCompat(const char *path, const char *mode = FILE_READ) {
  if (!sdReady || !sdFs || !path) return File();
  File file = sdFs->open(path, mode);
  if (file) return file;
  if (path[0] == '/') {
    file = sdFs->open(path + 1, mode);
    if (file) return file;
  }
  const char *alias = sdShortAliasFor(path);
  if (alias) {
    file = sdFs->open(alias, mode);
    if (file) return file;
    if (alias[0] == '/') {
      file = sdFs->open(alias + 1, mode);
      if (file) return file;
    }
  }
  return File();
}

bool sdExists(const char *path) {
  if (!sdReady || !sdFs || !path) return false;
  if (sdFs->exists(path)) return true;
  if (path[0] == '/' && sdFs->exists(path + 1)) return true;
  const char *alias = sdShortAliasFor(path);
  if (alias && sdFs->exists(alias)) return true;
  if (alias && alias[0] == '/' && sdFs->exists(alias + 1)) return true;
  File file = sdOpenCompat(path);
  if (!file) return false;
  file.close();
  return true;
}

uint32_t sdFileSize(const char *path) {
  if (!sdReady || !sdFs || !path) return 0;
  File file = sdOpenCompat(path);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    return 0;
  }
  uint32_t size = static_cast<uint32_t>(file.size());
  file.close();
  return size;
}

bool sdFileReadable(const char *path, uint32_t minBytes = 1) {
  return sdFileSize(path) >= minBytes;
}

String sdListDir(const char *path, uint8_t maxItems = 18) {
  if (!sdReady || !sdFs) return "SD not ready";
  File root = sdOpenCompat(path);
  if (!root) return String(path) + " : cannot open";
  if (!root.isDirectory()) {
    String line = String(path) + " : file, " + String((uint32_t)root.size()) + " B";
    root.close();
    return line;
  }

  String out = String(path) + "\n";
  uint8_t count = 0;
  File file = root.openNextFile();
  while (file && count < maxItems) {
    out += file.isDirectory() ? "[DIR]  " : "       ";
    out += file.name();
    if (!file.isDirectory()) {
      out += " (";
      out += String((uint32_t)file.size());
      out += " B)";
    }
    out += "\n";
    file.close();
    file = root.openNextFile();
    count++;
  }
  if (file) {
    out += "...";
    file.close();
  }
  root.close();
  return out;
}

String sdDiagnosticListing() {
  if (!sdReady || !sdFs) return "SD card is not mounted";
  String out;
  out.reserve(1200);
  out += sdListDir("/");
  out += "\n";
  out += sdListDir("/walle");
  out += "\n";
  out += sdListDir("/walle/models");
  out += "\n";
  out += sdListDir("/walle/config");
  out += "\n";
  out += sdListDir("/walle/models/tiny1m", 8);
  out += "\n";
  out += sdListDir("/walle/models/stories260k", 8);
  return out;
}

void addSdPathCheck(JsonArray &paths, const char *path) {
  uint32_t bytes = sdFileSize(path);
  JsonObject item = paths.add<JsonObject>();
  item["path"] = path;
  item["ok"] = bytes > 0;
  item["bytes"] = bytes;
  const char *alias = sdShortAliasFor(path);
  if (alias) item["alias"] = alias;
}

String readSdText(const char *path, size_t limit = 2048) {
  if (!sdReady || !sdFs) return "";
  File file = sdOpenCompat(path);
  if (!file) return "";
  String out;
  while (file.available() && out.length() < limit) {
    out += static_cast<char>(file.read());
  }
  file.close();
  return out;
}

String firstJsonArrayItem(JsonDocument &doc, const char *key, const String &fallback) {
  JsonArray arr = doc[key].as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) return fallback;
  const char *value = arr[0] | "";
  return String(value && strlen(value) ? value : fallback.c_str());
}

void updateSdModelState() {
  sdPrimaryModelReady = sdFileReadable(SD_TINY_MODEL_PATH, 1024 * 1024) &&
                        sdFileReadable(SD_TINY_TOKENIZER_PATH, 1024);
  sdFallbackModelReady = sdFileReadable(SD_FALLBACK_MODEL_PATH, 128 * 1024) &&
                         sdFileReadable(SD_FALLBACK_TOKENIZER_PATH, 1024);
}

void loadOfflineConfig() {
  String payload = readSdText(SD_OFFLINE_RESPONSES_PATH, 2048);
  if (payload.isEmpty()) return;
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return;
  offlineNoNetwork = firstJsonArrayItem(doc, "no_network", offlineNoNetwork);
  offlineCantAnswer = firstJsonArrayItem(doc, "cant_answer", offlineCantAnswer);
}

void finishSdMount(const String &mode, uint64_t totalBytes, uint64_t usedBytes) {
  sdReady = true;
  sdMode = mode;
  sdLastError = "SD 卡已挂载";
  sdTotalBytes = totalBytes;
  sdUsedBytes = usedBytes;
  updateSdModelState();
  loadOfflineConfig();
  logBoth("SD mounted: " + sdMode + " total " + String(sdTotalBytes / (1024 * 1024)) + " MB");
}

bool trySpiSd(const String &label, int sck, int miso, int mosi, int cs) {
  SD.end();
  SPI.end();
  SPI.begin(sck, miso, mosi, cs);
  if (!SD.begin(cs, SPI, 10000000, "/sd", 8, false)) {
    sdLastError = label + " 挂载失败";
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    SD.end();
    sdLastError = label + " 未检测到卡";
    return false;
  }
  sdFs = &SD;
  finishSdMount(label, SD.totalBytes(), SD.usedBytes());
  return true;
}

bool trySdMmc(const String &label, int clk, int cmd, int d0) {
  SD_MMC.end();
  if (!SD_MMC.setPins(clk, cmd, d0)) {
    sdLastError = label + " 设置引脚失败";
    return false;
  }
  if (!SD_MMC.begin("/sdmmc", true, false, SDMMC_FREQ_DEFAULT, 8)) {
    sdLastError = label + " 挂载失败";
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    sdLastError = label + " 未检测到卡";
    return false;
  }
  sdFs = &SD_MMC;
  finishSdMount(label, SD_MMC.totalBytes(), SD_MMC.usedBytes());
  return true;
}

bool mountSdCard(bool force = false) {
  if (sdReady && !force) return true;
  if (force) {
    SD.end();
    SD_MMC.end();
    sdReady = false;
    sdFs = nullptr;
  }

  sdLastError = "正在检测 SD 卡";
  sdTotalBytes = 0;
  sdUsedBytes = 0;
  sdPrimaryModelReady = false;
  sdFallbackModelReady = false;

  if (trySdMmc("SD_MMC 背面卡槽/Freenove GPIO39 CLK, GPIO38 CMD, GPIO40 D0", 39, 38, 40)) return true;
  if (trySpiSd("SPI XIAO Sense GPIO7/8/9 CS21", SD_XIAO_SCK, SD_XIAO_MISO, SD_XIAO_MOSI, SD_XIAO_CS)) return true;
  if (trySpiSd("SPI S3-CAM GPIO39/40/41 CS38", SD_CAM_SCK, SD_CAM_MISO, SD_CAM_MOSI, SD_CAM_CS)) return true;

  sdReady = false;
  sdFs = nullptr;
  sdMode = "未挂载";
  sdLastError += "；请确认 SD 卡已插入、FAT32 格式。已尝试背面 SD_MMC 卡槽和常见 SPI 卡槽。";
  logBoth("SD mount failed: " + sdLastError);
  return false;
}

bool hasAny(const String &text, const char *const words[], size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (text.indexOf(words[i]) >= 0) return true;
  }
  return false;
}

String offlineModelState() {
  if (sdReady && sdPrimaryModelReady) return "SD 小脑袋在，我能读到 tiny1m。";
  if (sdReady && sdFallbackModelReady) return "SD 卡能读到，备用小模型在。";
  if (sdReady) return "SD 卡能读，但模型文件还不完整。";
  return "我还没检测到 SD 卡。";
}

String offlineSnippet(String text, size_t limit) {
  text.replace("\r", " ");
  text.replace("\n", " ");
  text.trim();
  if (text.length() > limit) text = text.substring(0, limit) + "...";
  return text;
}

String sdPosixPath(const char *logicalPath) {
  String prefix = sdMode.indexOf("SD_MMC") >= 0 ? "/sdmmc" : "/sd";
  const char *alias = sdShortAliasFor(logicalPath);
  if (alias && strlen(alias)) return prefix + String(alias);
  return prefix + String(logicalPath);
}

String cleanLocalModelOutput(String text) {
  text.replace("\r", " ");
  text.replace("\n", " ");
  text.replace("\t", " ");
  text.trim();
  while (text.indexOf("  ") >= 0) text.replace("  ", " ");
  if (text.length() > 220) text = text.substring(0, 220) + "...";
  return text;
}

String runLocalModelText(const String &message, String &usedModel, String &error) {
  if (!sdReady) mountSdCard(false);
  if (!sdReady) {
    error = "SD 卡还没挂载";
    return "";
  }

  struct LocalChoice {
    const char *name;
    const char *model;
    const char *tokenizer;
    bool ready;
    int steps;
  };
  LocalChoice choices[] = {
      {"stories260k", SD_FALLBACK_MODEL_PATH, SD_FALLBACK_TOKENIZER_PATH, sdFallbackModelReady, 24},
      {"tiny1m", SD_TINY_MODEL_PATH, SD_TINY_TOKENIZER_PATH, sdPrimaryModelReady, 24},
  };

  String prompt = "A tiny robot named Walle replies kindly and briefly: ";
  prompt += offlineSnippet(message, 90);
  prompt += "\nWalle:";

  char output[384];
  for (auto &choice : choices) {
    if (!choice.ready) continue;
    String modelPath = sdPosixPath(choice.model);
    String tokenizerPath = sdPosixPath(choice.tokenizer);
    output[0] = '\0';
    uint32_t started = millis();
    bool ok = local_llm_generate(modelPath.c_str(), tokenizerPath.c_str(), prompt.c_str(), choice.steps, 0.7f, 0.9f,
                                 output, sizeof(output));
    if (ok && strlen(output) > 0) {
      usedModel = choice.name;
      String cleaned = cleanLocalModelOutput(String(output));
      error = "耗时 " + String(millis() - started) + "ms";
      if (cleaned.length()) return cleaned;
    }
    error = String(local_llm_last_error());
  }
  if (error.isEmpty()) error = "没有可用的本地模型";
  return "";
}

String offlineChat(const String &message) {
  String text = message;
  text.trim();
  String lower = text;
  lower.toLowerCase();

  if (text.isEmpty()) return "哔卟，我在。你刚才像是没说完。";

  const char *weatherNews[] = {"天气", "气温", "下雨", "降雨", "新闻", "热搜", "今天发生", "明天", "后天"};
  if (hasAny(text, weatherNews, sizeof(weatherNews) / sizeof(weatherNews[0]))) {
    return "唔，这个要联网查实时数据。我现在断网，不能瞎编，编了会显得我螺丝松。";
  }

  const char *netWords[] = {"联网", "网络", "wifi", "Wi-Fi", "热点", "deepseek", "DeepSeek", "api", "API"};
  if (hasAny(text, netWords, sizeof(netWords) / sizeof(netWords[0])) || lower.indexOf("wifi") >= 0 ||
      lower.indexOf("deepseek") >= 0 || lower.indexOf("api") >= 0) {
    return offlineNoNetwork + " " + offlineModelState() + " 要恢复云端回答，就在网页里重新连接 Wi-Fi。";
  }

  const char *modelWords[] = {"模型", "sd", "SD", "内存卡", "tf卡", "TF卡", "本地", "离线", "tiny1m"};
  if (hasAny(text, modelWords, sizeof(modelWords) / sizeof(modelWords[0])) || lower.indexOf("tiny1m") >= 0) {
    return "我现在是离线模式。" + offlineModelState() + " 普通闲聊我能顶一下，复杂知识题还是要等联网。";
  }

  const char *nameWords[] = {"你是谁", "叫什么", "名字", "瓦力是谁"};
  if (hasAny(text, nameWords, sizeof(nameWords) / sizeof(nameWords[0]))) {
    return "我是瓦力，一个小机器人伙伴。现在离线，但嘴还在，哔卟。";
  }

  const char *greetWords[] = {"你好", "嗨", "哈喽", "hello", "Hello", "在吗", "瓦力"};
  if (hasAny(text, greetWords, sizeof(greetWords) / sizeof(greetWords[0])) || lower.indexOf("hello") >= 0) {
    return "我在。离线也在，别慌。你说，我听着呢。";
  }

  const char *thanksWords[] = {"谢谢", "谢了", "感谢", "辛苦"};
  if (hasAny(text, thanksWords, sizeof(thanksWords) / sizeof(thanksWords[0]))) {
    return "不客气。哔卟，夸我可以，别让我拖地就行。";
  }

  const char *sadWords[] = {"难过", "伤心", "烦", "焦虑", "压力", "累", "不开心"};
  if (hasAny(text, sadWords, sizeof(sadWords) / sizeof(sadWords[0]))) {
    return "唔，先慢一点。你已经撑到现在了，不算差。要不要先把最烦的一件事说给我听？";
  }

  const char *jokeWords[] = {"笑话", "逗我", "讲个段子", "搞笑"};
  if (hasAny(text, jokeWords, sizeof(jokeWords) / sizeof(jokeWords[0]))) {
    return "讲个冷的：我断网了还在陪聊，说明我不是智能音箱，我是意志力音箱。哔卟。";
  }

  const char *canWords[] = {"能做什么", "会什么", "帮我", "功能"};
  if (hasAny(text, canWords, sizeof(canWords) / sizeof(canWords[0]))) {
    return "我现在离线，能正常中文短句陪聊、记最近几轮、检查 SD 卡。要测试 SD 卡里的小模型，可以说“本地模型测试：给我讲个英文小故事”。天气新闻和复杂知识要联网。";
  }

  const char *llmTestWords[] = {"本地模型测试", "测试模型", "跑模型", "英文故事", "english story"};
  if (hasAny(text, llmTestWords, sizeof(llmTestWords) / sizeof(llmTestWords[0])) || lower.indexOf("english story") >= 0) {
    String usedModel;
    String localError;
    String localText = runLocalModelText(text, usedModel, localError);
    if (localText.length()) {
      return "本地模型输出（" + usedModel + "，" + localError + "）：\n" + localText +
             "\n\n哔卟，说明模型确实能跑；但它是英文 TinyStories 小模型，所以正常中文聊天我会用瓦力离线回复来包装。";
    }
    return "本地模型这次没跑稳：" + localError + "。我还在，先用中文离线回复陪你。";
  }

  if (memoryTurnCount > 0) {
    return "我听到了。你说“" + offlineSnippet(text, 36) + "”。我先用离线瓦力模式理解：你可以继续说细一点，我陪你捋。";
  }
  return "我听到了：“" + offlineSnippet(text, 42) + "”。离线模式下我能短句陪聊；复杂知识题等联网后我再认真开脑。";
}

String urlEncode(const String &value) {
  const char *hex = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(value.length() * 3);
  for (size_t i = 0; i < value.length(); i++) {
    uint8_t c = static_cast<uint8_t>(value[i]);
    bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      encoded += static_cast<char>(c);
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String compactSpaces(const String &value) {
  String out;
  bool lastSpace = false;
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    bool isSpace = c == ' ' || c == '\t' || c == '\r' || c == '\n';
    if (isSpace) {
      if (!lastSpace) out += ' ';
      lastSpace = true;
    } else {
      out += c;
      lastSpace = false;
    }
  }
  out.trim();
  return out;
}

bool textHasWeatherSignal(const String &text) {
  const char *keys[] = {"天气", "气温", "温度", "预报", "下雨", "降雨", "有雨", "冷不冷",
                        "热不热", "冷吗", "热吗", "风大", "湿度", "雨伞", "带伞", "穿什么", "℃"};
  for (const char *key : keys) {
    if (text.indexOf(key) >= 0) return true;
  }
  return false;
}

bool recentWeatherContext() {
  for (uint8_t i = 0; i < memoryTurnCount; i++) {
    if (textHasWeatherSignal(memoryUser[i]) || textHasWeatherSignal(memoryAssistant[i])) return true;
  }
  return false;
}

String memoryText(const String &text, size_t limit) {
  return previewText(text, limit);
}

void clearChatMemory() {
  for (uint8_t i = 0; i < CHAT_MEMORY_TURNS; i++) {
    memoryUser[i] = "";
    memoryAssistant[i] = "";
  }
  memoryTurnCount = 0;
}

bool shouldRememberReply(const String &reply) {
  if (reply.isEmpty()) return false;
  const char *skipPrefixes[] = {
      "还没连上网络", "连接 DeepSeek 失败", "DeepSeek 请求失败", "DeepSeek 返回 HTTP",
      "我听到了，但解析 DeepSeek 回复失败"};
  for (const char *prefix : skipPrefixes) {
    if (reply.startsWith(prefix)) return false;
  }
  return true;
}

void rememberChatTurn(const String &user, const String &assistant) {
  if (user.isEmpty() || assistant.isEmpty()) return;
  if (memoryTurnCount < CHAT_MEMORY_TURNS) {
    memoryUser[memoryTurnCount] = memoryText(user, 260);
    memoryAssistant[memoryTurnCount] = memoryText(assistant, 320);
    memoryTurnCount++;
    return;
  }

  for (uint8_t i = 1; i < CHAT_MEMORY_TURNS; i++) {
    memoryUser[i - 1] = memoryUser[i];
    memoryAssistant[i - 1] = memoryAssistant[i];
  }
  memoryUser[CHAT_MEMORY_TURNS - 1] = memoryText(user, 260);
  memoryAssistant[CHAT_MEMORY_TURNS - 1] = memoryText(assistant, 320);
}

void addShortTermMemory(JsonArray messages) {
  for (uint8_t i = 0; i < memoryTurnCount; i++) {
    if (memoryUser[i].isEmpty() || memoryAssistant[i].isEmpty()) continue;
    JsonObject oldUser = messages.add<JsonObject>();
    oldUser["role"] = "user";
    oldUser["content"] = memoryUser[i];
    JsonObject oldAssistant = messages.add<JsonObject>();
    oldAssistant["role"] = "assistant";
    oldAssistant["content"] = memoryAssistant[i];
  }
}

bool isWeatherQuestion(const String &message) {
  if (textHasWeatherSignal(message)) return true;

  const char *followUps[] = {"明天", "后天", "大后天", "今天", "现在", "那里", "那边",
                             "那个地方", "当地", "还会", "还要", "继续", "要不要"};
  if (recentWeatherContext()) {
    for (const char *key : followUps) {
      if (message.indexOf(key) >= 0) return true;
    }
  }
  return false;
}

int weatherDayOffset(const String &message) {
  if (message.indexOf("大后天") >= 0) return 3;
  if (message.indexOf("后天") >= 0) return 2;
  if (message.indexOf("明天") >= 0 || message.indexOf("明日") >= 0) return 1;
  return 0;
}

String weatherDayLabel(int offset) {
  switch (offset) {
    case 0: return "今天";
    case 1: return "明天";
    case 2: return "后天";
    case 3: return "大后天";
    default: return String(offset) + "天后";
  }
}

String extractWeatherPlace(const String &message) {
  String place = message;
  const char *puncts[] = {"，", "。", "？", "！", "、", "；", "：", "“", "”", "（", "）",
                          ",", ".", "?", "!", ";", ":", "\"", "'", "(", ")", "-", "_"};
  for (const char *punct : puncts) place.replace(punct, " ");

  const char *tokens[] = {
      "WALL-E", "wall-e", "wall e", "瓦力", "哇力", "大后天", "后天", "明天", "明日", "今天",
      "现在", "当前", "当地", "这里", "这边", "天气", "气温", "温度", "预报", "下雨", "降雨",
      "有雨", "雨伞", "冷不冷", "热不热", "风大不大", "风大", "湿度", "怎么样", "怎样", "如何",
      "冷吗", "热吗", "冷", "热", "带伞", "穿什么", "适合穿", "多少", "请", "帮我", "帮", "查一下", "查", "看看", "告诉我", "想知道", "问一下", "一下",
      "会不会", "有没有", "会", "有", "吗", "嘛", "呢", "呀", "啊", "吧", "的", "在", "我",
      "到", "了", "是", "给"};
  for (const char *token : tokens) place.replace(token, " ");

  place = compactSpaces(place);
  int lastSpace = place.lastIndexOf(' ');
  if (lastSpace >= 0 && lastSpace < static_cast<int>(place.length()) - 1) {
    place = place.substring(lastSpace + 1);
  }
  place.trim();
  if (place.length() > 42) return "";
  return place;
}

const char *weatherCodeText(int code) {
  switch (code) {
    case 0: return "晴";
    case 1: return "大部晴朗";
    case 2: return "局部多云";
    case 3: return "阴";
    case 45:
    case 48: return "有雾";
    case 51: return "小毛毛雨";
    case 53: return "毛毛雨";
    case 55: return "较强毛毛雨";
    case 56:
    case 57: return "冻毛毛雨";
    case 61: return "小雨";
    case 63: return "中雨";
    case 65: return "大雨";
    case 66:
    case 67: return "冻雨";
    case 71: return "小雪";
    case 73: return "中雪";
    case 75: return "大雪";
    case 77: return "雪粒";
    case 80: return "小阵雨";
    case 81: return "阵雨";
    case 82: return "强阵雨";
    case 85:
    case 86: return "阵雪";
    case 95: return "雷暴";
    case 96:
    case 99: return "雷暴伴冰雹";
    default: return "未知天气";
  }
}

struct WeatherLocation {
  String query;
  String name;
  double lat;
  double lon;
};

struct WeatherSnapshot {
  String askedPlace;
  String locationName;
  String date;
  int dayOffset = 0;
  int code = -1;
  double currentTemp = -999.0;
  double apparentTemp = -999.0;
  double currentWind = -999.0;
  int humidity = -1;
  double maxTemp = -999.0;
  double minTemp = -999.0;
  double rainProb = -999.0;
  double windMax = -999.0;
};

struct KnownPlace {
  const char *alias;
  const char *name;
  double lat;
  double lon;
};

static const KnownPlace KNOWN_PLACES[] = {
    {"南昌", "南昌", 28.6820, 115.8580},     {"厦门", "厦门", 24.4798, 118.0894},
    {"福州", "福州", 26.0745, 119.2965},     {"泉州", "泉州", 24.8741, 118.6757},
    {"漳州", "漳州", 24.5133, 117.6471},     {"莆田", "莆田", 25.4540, 119.0070},
    {"三明", "三明", 26.2638, 117.6392},     {"龙岩", "龙岩", 25.0750, 117.0170},
    {"宁德", "宁德", 26.6657, 119.5482},     {"赣州", "赣州", 25.8310, 114.9350},
    {"九江", "九江", 29.7050, 116.0010},     {"上饶", "上饶", 28.4540, 117.9430},
    {"宜春", "宜春", 27.8150, 114.4160},     {"吉安", "吉安", 27.1130, 114.9930},
    {"抚州", "抚州", 27.9480, 116.3580},     {"景德镇", "景德镇", 29.2680, 117.1780},
    {"萍乡", "萍乡", 27.6220, 113.8520},     {"新余", "新余", 27.8170, 114.9170},
    {"鹰潭", "鹰潭", 28.2380, 117.0690},     {"北京", "北京", 39.9042, 116.4074},
    {"上海", "上海", 31.2304, 121.4737},     {"广州", "广州", 23.1291, 113.2644},
    {"深圳", "深圳", 22.5431, 114.0579},     {"杭州", "杭州", 30.2741, 120.1551},
    {"南京", "南京", 32.0603, 118.7969},     {"武汉", "武汉", 30.5928, 114.3055},
    {"长沙", "长沙", 28.2282, 112.9388},     {"成都", "成都", 30.5728, 104.0668},
    {"重庆", "重庆", 29.5630, 106.5516},     {"西安", "西安", 34.3416, 108.9398},
    {"天津", "天津", 39.3434, 117.3616},     {"郑州", "郑州", 34.7466, 113.6254},
    {"合肥", "合肥", 31.8206, 117.2272},     {"济南", "济南", 36.6512, 117.1201},
    {"青岛", "青岛", 36.0671, 120.3826},     {"沈阳", "沈阳", 41.8057, 123.4315},
    {"大连", "大连", 38.9140, 121.6147},     {"哈尔滨", "哈尔滨", 45.8038, 126.5349},
    {"长春", "长春", 43.8171, 125.3235},     {"太原", "太原", 37.8706, 112.5489},
    {"石家庄", "石家庄", 38.0428, 114.5149}, {"呼和浩特", "呼和浩特", 40.8426, 111.7492},
    {"银川", "银川", 38.4872, 106.2309},     {"兰州", "兰州", 36.0611, 103.8343},
    {"西宁", "西宁", 36.6171, 101.7782},     {"乌鲁木齐", "乌鲁木齐", 43.8256, 87.6168},
    {"拉萨", "拉萨", 29.6520, 91.1721},      {"昆明", "昆明", 24.8801, 102.8329},
    {"贵阳", "贵阳", 26.6470, 106.6302},     {"南宁", "南宁", 22.8170, 108.3669},
    {"海口", "海口", 20.0442, 110.1999},     {"三亚", "三亚", 18.2528, 109.5119},
    {"香港", "香港", 22.3193, 114.1694},     {"澳门", "澳门", 22.1987, 113.5439},
    {"台北", "台北", 25.0330, 121.5654},     {"江西", "南昌", 28.6820, 115.8580},
    {"福建", "福州", 26.0745, 119.2965},     {"广东", "广州", 23.1291, 113.2644},
    {"浙江", "杭州", 30.2741, 120.1551},     {"江苏", "南京", 32.0603, 118.7969},
    {"湖北", "武汉", 30.5928, 114.3055},     {"湖南", "长沙", 28.2282, 112.9388},
    {"四川", "成都", 30.5728, 104.0668},     {"陕西", "西安", 34.3416, 108.9398},
    {"河南", "郑州", 34.7466, 113.6254},     {"安徽", "合肥", 31.8206, 117.2272},
    {"山东", "济南", 36.6512, 117.1201},     {"辽宁", "沈阳", 41.8057, 123.4315},
    {"吉林", "长春", 43.8171, 125.3235},     {"黑龙江", "哈尔滨", 45.8038, 126.5349},
    {"山西", "太原", 37.8706, 112.5489},     {"河北", "石家庄", 38.0428, 114.5149},
    {"内蒙古", "呼和浩特", 40.8426, 111.7492}, {"宁夏", "银川", 38.4872, 106.2309},
    {"甘肃", "兰州", 36.0611, 103.8343},     {"青海", "西宁", 36.6171, 101.7782},
    {"新疆", "乌鲁木齐", 43.8256, 87.6168}, {"西藏", "拉萨", 29.6520, 91.1721},
    {"云南", "昆明", 24.8801, 102.8329},     {"贵州", "贵阳", 26.6470, 106.6302},
    {"广西", "南宁", 22.8170, 108.3669},     {"海南", "海口", 20.0442, 110.1999},
    {"台湾", "台北", 25.0330, 121.5654}};

bool findKnownPlace(const String &query, WeatherLocation &location) {
  String q = query;
  q.trim();
  if (q.length() < 4) return false;
  for (const KnownPlace &place : KNOWN_PLACES) {
    String alias = place.alias;
    if (q.indexOf(alias) >= 0 || alias.indexOf(q) >= 0) {
      location.query = query;
      location.name = place.name;
      location.lat = place.lat;
      location.lon = place.lon;
      return true;
    }
  }
  return false;
}

bool secureGet(const String &url, String &payload, String &error, uint32_t timeoutMs = 12000) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "还没连上网络";
    return false;
  }
  if (strlen(DEEPSEEK_API_KEY) == 0) {
    return "DeepSeek API key is not configured. Create src/secrets.h from src/secrets.example.h before building.";
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(timeoutMs);
  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  const char *headers[] = {"Content-Type", "Location"};
  http.collectHeaders(headers, 2);
  if (!http.begin(client, url)) {
    error = "HTTP 客户端初始化失败";
    return false;
  }
  int code = http.GET();
  payload = http.getString();
  String contentType = http.header("Content-Type");
  String location = http.header("Location");
  http.end();
  if (code <= 0) {
    error = "HTTPClient 错误码 " + String(code);
    return false;
  }
  if (code < 200 || code >= 300) {
    error = "HTTP " + String(code);
    if (contentType.length()) error += "，类型：" + contentType;
    if (location.length()) error += "，跳转：" + location;
    if (payload.length()) error += "，内容：" + previewText(payload);
    return false;
  }
  return true;
}

bool geocodePlace(const String &place, WeatherLocation &location, String &error) {
  if (findKnownPlace(place, location)) return true;

  String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(place) +
               "&count=1&language=zh&format=json";
  String payload;
  if (!secureGet(url, payload, error)) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    error = String("地理编码解析失败：") + err.c_str() + "，内容片段：" + previewText(payload);
    return false;
  }
  JsonArray results = doc["results"].as<JsonArray>();
  if (results.isNull() || results.size() == 0) {
    error = "没有找到这个地点：" + place;
    return false;
  }

  JsonObject first = results[0];
  String name = first["name"] | place;
  String admin1 = first["admin1"] | "";
  if (admin1.length() && name.indexOf(admin1) < 0) name += " " + admin1;
  location.query = place;
  location.name = name;
  location.lat = first["latitude"] | 0.0;
  location.lon = first["longitude"] | 0.0;
  return true;
}

bool resolveWeatherLocation(const String &place, WeatherLocation &location, String &error) {
  if (place.isEmpty()) {
    location.query = savedWeatherName;
    location.name = savedWeatherName;
    location.lat = savedWeatherLat;
    location.lon = savedWeatherLon;
    return true;
  }
  return geocodePlace(place, location, error);
}

void saveWeatherLocation(const WeatherLocation &location) {
  savedWeatherName = location.name;
  savedWeatherLat = location.lat;
  savedWeatherLon = location.lon;
  prefs.putString("weatherName", savedWeatherName);
  prefs.putDouble("weatherLat", savedWeatherLat);
  prefs.putDouble("weatherLon", savedWeatherLon);
}

bool fetchWeather(const WeatherLocation &location, int requestedOffset, WeatherSnapshot &weather, String &error) {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(location.lat, 6) +
               "&longitude=" + String(location.lon, 6) +
               "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,wind_speed_10m_max"
               "&timezone=Asia%2FShanghai&forecast_days=7";
  String payload;
  if (!secureGet(url, payload, error, 15000)) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    error = String("天气数据解析失败：") + err.c_str() + "，内容片段：" + previewText(payload);
    return false;
  }

  JsonObject daily = doc["daily"].as<JsonObject>();
  JsonArray dates = daily["time"].as<JsonArray>();
  if (dates.isNull() || dates.size() == 0) {
    error = "天气接口没有返回 daily.time";
    return false;
  }
  int offset = requestedOffset;
  if (offset < 0) offset = 0;
  if (offset >= static_cast<int>(dates.size())) offset = dates.size() - 1;

  weather.askedPlace = location.query;
  weather.locationName = location.name;
  weather.date = dates[offset].as<String>();
  weather.dayOffset = offset;
  weather.code = daily["weather_code"][offset] | -1;
  weather.maxTemp = daily["temperature_2m_max"][offset] | -999.0;
  weather.minTemp = daily["temperature_2m_min"][offset] | -999.0;
  weather.rainProb = daily["precipitation_probability_max"][offset] | -999.0;
  weather.windMax = daily["wind_speed_10m_max"][offset] | -999.0;

  JsonObject current = doc["current"].as<JsonObject>();
  weather.currentTemp = current["temperature_2m"] | -999.0;
  weather.apparentTemp = current["apparent_temperature"] | -999.0;
  weather.currentWind = current["wind_speed_10m"] | -999.0;
  weather.humidity = current["relative_humidity_2m"] | -1;
  return true;
}

String formatWeatherValue(double value, uint8_t digits, const char *unit) {
  if (value < -900.0) return "-";
  return String(value, static_cast<unsigned int>(digits)) + unit;
}

String weatherFacts(const WeatherSnapshot &weather) {
  String facts = "地点：" + weather.locationName;
  if (weather.askedPlace.length() && weather.askedPlace != weather.locationName) {
    facts += "（用户说的是“" + weather.askedPlace + "”，当前按“" + weather.locationName + "”查询）";
  }
  facts += "\n日期：" + weather.date + "，" + weatherDayLabel(weather.dayOffset);
  facts += "\n天气：" + String(weatherCodeText(weather.code));
  facts += "\n最高/最低：" + formatWeatherValue(weather.maxTemp, 1, "℃") + " / " +
           formatWeatherValue(weather.minTemp, 1, "℃");
  facts += "\n最大降雨概率：" + formatWeatherValue(weather.rainProb, 0, "%");
  facts += "\n最大风速：" + formatWeatherValue(weather.windMax, 1, "km/h");
  if (weather.dayOffset == 0) {
    facts += "\n当前气温：" + formatWeatherValue(weather.currentTemp, 1, "℃");
    facts += "\n体感温度：" + formatWeatherValue(weather.apparentTemp, 1, "℃");
    if (weather.humidity >= 0) facts += "\n当前湿度：" + String(weather.humidity) + "%";
    facts += "\n当前风速：" + formatWeatherValue(weather.currentWind, 1, "km/h");
  }
  return facts;
}

String fallbackWeatherReply(const WeatherSnapshot &weather) {
  String reply = "哔卟，" + weather.locationName + " " + weatherDayLabel(weather.dayOffset) + "是" +
                 weatherCodeText(weather.code) + "。";
  reply += "气温大概 " + formatWeatherValue(weather.minTemp, 1, "℃") + " 到 " +
           formatWeatherValue(weather.maxTemp, 1, "℃") + "。";
  if (weather.rainProb >= 0) reply += "降雨概率 " + formatWeatherValue(weather.rainProb, 0, "%") + "。";
  if (weather.windMax >= 0) reply += "最大风速 " + formatWeatherValue(weather.windMax, 1, "km/h") + "。";
  if (weather.askedPlace.length() && weather.askedPlace != weather.locationName) {
    reply += "你说的是" + weather.askedPlace + "，我先按" + weather.locationName + "查，别怪我，省太大啦。";
  }
  return reply;
}

String deepSeekChat(const String &message);

String weatherChat(const String &message) {
  if (WiFi.status() != WL_CONNECTED) {
    return "还没连上网络。先在网页里连接 Wi-Fi，蓝灯亮起来我才能查天气。";
  }

  String place = extractWeatherPlace(message);
  int offset = weatherDayOffset(message);
  WeatherLocation location;
  String error;
  if (!resolveWeatherLocation(place, location, error)) {
    return "我没定位到这个地方，唔。你可以换个城市名试试，比如“厦门天气”。错误：" + error;
  }

  WeatherSnapshot weather;
  if (!fetchWeather(location, offset, weather, error)) {
    return "天气接口没接稳，哔卟。错误：" + error;
  }

  if (!place.isEmpty()) saveWeatherLocation(location);

  String facts = weatherFacts(weather);
  String prompt = "用户原话：" + message +
                  "\n\n下面是 Open-Meteo 返回的真实天气数据：\n" + facts +
                  "\n\n请只根据这些天气数据回答。用中文短句，瓦力式俏皮可爱；如果地点是省份默认城市，顺口说明一下。";
  String reply = deepSeekChat(prompt);
  if (reply.startsWith("DeepSeek") || reply.indexOf("解析 DeepSeek") >= 0 ||
      reply.indexOf("连接 DeepSeek") >= 0) {
    return fallbackWeatherReply(weather) + "\n\n（DeepSeek 这会儿没接稳，原始错误：" + reply + "）";
  }
  return reply;
}

String chatWithTools(const String &message) {
  if (WiFi.status() != WL_CONNECTED) {
    String reply = offlineChat(message);
    if (shouldRememberReply(reply)) rememberChatTurn(message, reply);
    return reply;
  }
  String reply = isWeatherQuestion(message) ? weatherChat(message) : deepSeekChat(message);
  if (reply.startsWith("DeepSeek 请求失败") || reply.startsWith("连接 DeepSeek 失败")) {
    reply = offlineChat(message) + "\n\n联网请求失败，原始错误：" + reply;
  }
  if (shouldRememberReply(reply)) rememberChatTurn(message, reply);
  return reply;
}

String deepSeekChat(const String &message) {
  if (WiFi.status() != WL_CONNECTED) {
    return "还没连上网络。先在网页里连接 Wi-Fi，蓝灯亮起来我就能说话。";
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);
  HTTPClient http;
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  const char *headers[] = {"Content-Type", "Location"};
  http.collectHeaders(headers, 2);
  if (!http.begin(client, "https://api.deepseek.com/chat/completions")) {
    return "连接 DeepSeek 失败：HTTP 客户端初始化失败。";
  }

  JsonDocument req;
  req["model"] = "deepseek-chat";
  req["temperature"] = 0.8;
  req["max_tokens"] = 180;
  JsonArray messages = req["messages"].to<JsonArray>();
  JsonObject sys = messages.add<JsonObject>();
  sys["role"] = "system";
  sys["content"] =
      "你是一个机器人，名字叫瓦力。用中文短句回答，回答俏皮可爱，有的时候有点毒舌搞笑，但要分场景，不要在严肃、危险或需要安慰的时候毒舌。"
      "你可以参考最近几轮短期对话记忆来保持上下文，但这不是长期记忆；如果用户问你是否记得重启前的内容，要如实说明重启后会忘。";
  addShortTermMemory(messages);
  JsonObject user = messages.add<JsonObject>();
  user["role"] = "user";
  user["content"] = message;

  String body;
  serializeJson(req, body);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(DEEPSEEK_API_KEY));
  int code = http.POST(body);
  String payload = http.getString();
  String contentType = http.header("Content-Type");
  String location = http.header("Location");
  http.end();

  if (code <= 0) {
    return "DeepSeek 请求失败，HTTPClient 错误码：" + String(code) +
           "。这通常是网络不稳、DNS 不通、TLS 握手失败或校园网未认证。";
  }
  if (code < 200 || code >= 300) {
    String detail = "DeepSeek 返回 HTTP " + String(code);
    if (contentType.length()) detail += "，类型：" + contentType;
    if (location.length()) detail += "，跳转：" + location;
    if (payload.length()) detail += "，内容：" + previewText(payload);
    return detail;
  }

  JsonDocument resp;
  DeserializationError err = deserializeJson(resp, payload);
  if (err) {
    String detail = "我听到了，但解析 DeepSeek 回复失败：";
    detail += err.c_str();
    if (contentType.length()) detail += "，类型：" + contentType;
    detail += "，HTTP " + String(code);
    detail += "，内容片段：" + previewText(payload);
    return detail;
  }
  const char *reply = resp["choices"][0]["message"]["content"];
  return reply ? String(reply) : "DeepSeek 返回 JSON 里没有 choices[0].message.content。内容片段：" + previewText(payload);
}

void setupRoutes() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  auto captiveRedirect = []() {
    server.sendHeader("Location", String("http://") + AP_IP.toString() + "/", true);
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.send(302, "text/plain; charset=utf-8", "Open WALL-E setup page");
  };
  server.on("/generate_204", HTTP_GET, captiveRedirect);
  server.on("/gen_204", HTTP_GET, captiveRedirect);
  server.on("/hotspot-detect.html", HTTP_GET, captiveRedirect);
  server.on("/library/test/success.html", HTTP_GET, captiveRedirect);
  server.on("/success.txt", HTTP_GET, captiveRedirect);
  server.on("/connecttest.txt", HTTP_GET, captiveRedirect);
  server.on("/redirect", HTTP_GET, captiveRedirect);
  server.on("/ncsi.txt", HTTP_GET, captiveRedirect);
  server.on("/fwlink", HTTP_GET, captiveRedirect);
  server.on("/api/status", HTTP_GET, []() {
    JsonDocument doc;
    doc["wifi"] = WiFi.status() == WL_CONNECTED;
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["ap"] = WiFi.softAPIP().toString();
    doc["nat"] = natEnabled;
    doc["apDnsReal"] = apDnsReal;
    doc["weatherName"] = savedWeatherName;
    doc["memoryTurns"] = memoryTurnCount;
    doc["sdReady"] = sdReady;
    doc["sdMode"] = sdMode;
    doc["sdPrimaryModel"] = sdPrimaryModelReady;
    doc["sdFallbackModel"] = sdFallbackModelReady;
    doc["speakerReady"] = speakerReady;
    doc["speakerError"] = speakerLastError;
    doc["audioVolume"] = audioVolume;
    doc["replyToneEnabled"] = replyToneEnabled;
    doc["audioMode"] = audioModeName();
    doc["micReady"] = micReady;
    doc["micWakeTestEnabled"] = micWakeTestEnabled;
    doc["micThreshold"] = micThreshold;
    doc["micLevel"] = micLevel;
    doc["micPeakLevel"] = micPeakLevel;
    doc["micTriggerCount"] = micTriggerCount;
    doc["micError"] = micLastError;
    doc["chip"] = ESP.getChipModel();
    doc["board"] = "ESP32-S3-WROOM-1 N16R8";
    doc["flashMB"] = static_cast<uint32_t>(ESP.getFlashChipSize() / (1024 * 1024));
    doc["psramMB"] = static_cast<uint32_t>(ESP.getPsramSize() / (1024 * 1024));
    doc["freePsramKB"] = static_cast<uint32_t>(ESP.getFreePsram() / 1024);
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });
  server.on("/api/scan", HTTP_GET, []() {
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject item = networks.add<JsonObject>();
      item["ssid"] = WiFi.SSID(i);
      item["rssi"] = WiFi.RSSI(i);
      item["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });
  server.on("/api/connect", HTTP_POST, []() {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String ssid = doc["ssid"] | "";
    String pass = doc["pass"] | "";
    if (ssid.isEmpty()) {
      server.send(400, "text/plain; charset=utf-8", "请先选择或输入 Wi-Fi 名称。");
      return;
    }
    bool ok = connectSta(ssid, pass);
    if (ok) {
      savedSsid = ssid;
      savedPass = pass;
      prefs.putString("ssid", ssid);
      prefs.putString("pass", pass);
      server.send(200, "text/plain; charset=utf-8", "已连接到 " + ssid + "，开发板 IP：" + WiFi.localIP().toString() + "。如果是校园网，请让手机断开 WALL-E-Setup 后重新连接一次，再点“校园网认证助手”。");
    } else {
      server.send(500, "text/plain; charset=utf-8", "连接失败，请检查密码或信号。开放校园网可留空密码再试。");
    }
  });
  server.on("/api/disconnect", HTTP_POST, []() {
    prefs.remove("ssid");
    prefs.remove("pass");
    savedSsid = "";
    savedPass = "";
    WiFi.disconnect(false, true);
    wifiReady = false;
    setLed(false);
    disableNatAndRestoreCaptiveDns();
    server.send(200, "text/plain; charset=utf-8", "已断开并忘记保存的 Wi-Fi。你可以重新搜索并连接。");
  });
  server.on("/api/diagnose", HTTP_GET, []() {
    JsonDocument doc;
    doc["wifi"] = WiFi.status() == WL_CONNECTED;
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["nat"] = natEnabled;
    doc["msftProbe"] = httpProbe("http://www.msftconnecttest.com/connecttest.txt");
    doc["appleProbe"] = httpProbe("http://captive.apple.com/hotspot-detect.html");
    doc["deepseekProbe"] = httpProbe("http://api.deepseek.com/");
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json; charset=utf-8", out);
  });
  server.on("/api/sd_check", HTTP_POST, []() {
    mountSdCard(true);
    JsonDocument doc;
    doc["ready"] = sdReady;
    doc["mode"] = sdMode;
    doc["error"] = sdLastError;
    doc["totalMB"] = static_cast<uint32_t>(sdTotalBytes / (1024 * 1024));
    doc["usedMB"] = static_cast<uint32_t>(sdUsedBytes / (1024 * 1024));
    doc["primaryModel"] = sdPrimaryModelReady;
    doc["fallbackModel"] = sdFallbackModelReady;
    doc["manifest"] = sdFileReadable(SD_MANIFEST_PATH);
    JsonArray paths = doc["paths"].to<JsonArray>();
    addSdPathCheck(paths, SD_MANIFEST_PATH);
    addSdPathCheck(paths, SD_TINY_MODEL_PATH);
    addSdPathCheck(paths, SD_TINY_TOKENIZER_PATH);
    addSdPathCheck(paths, SD_FALLBACK_MODEL_PATH);
    addSdPathCheck(paths, SD_FALLBACK_TOKENIZER_PATH);
    doc["listing"] = sdDiagnosticListing();
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json; charset=utf-8", out);
  });
  server.on("/api/portal_probe", HTTP_GET, []() {
    JsonDocument doc;
    doc["wifi"] = WiFi.status() == WL_CONNECTED;
    doc["nat"] = natEnabled;
    String redirect = firstRedirectFromProbes();
    doc["redirect"] = redirect;
    JsonArray links = doc["links"].to<JsonArray>();
    struct ProbeLink {
      const char *name;
      const char *url;
    };
    ProbeLink probeLinks[] = {
        {"Android HTTP 检测入口", "http://connectivitycheck.gstatic.com/generate_204"},
        {"Windows HTTP 检测入口", "http://www.msftconnecttest.com/connecttest.txt"},
        {"iPhone HTTP 检测入口", "http://captive.apple.com/hotspot-detect.html"},
        {"NeverSSL 普通 HTTP", "http://neverssl.com/"},
        {"Example 普通 HTTP", "http://example.com/"}};
    for (auto &probe : probeLinks) {
      JsonObject item = links.add<JsonObject>();
      item["name"] = probe.name;
      item["url"] = probe.url;
      item["result"] = httpProbe(probe.url);
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json; charset=utf-8", out);
  });
  server.on("/api/memory_clear", HTTP_POST, []() {
    clearChatMemory();
    JsonDocument doc;
    doc["ok"] = true;
    doc["memoryTurns"] = memoryTurnCount;
    doc["reply"] = "短期记忆已清空。哔卟，脑袋擦干净了。";
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json; charset=utf-8", out);
  });
  server.on("/api/speaker_test", HTTP_POST, []() {
    bool ok = playSpeakerTestTone();
    JsonDocument doc;
    doc["ok"] = ok;
    doc["speakerReady"] = speakerReady;
    doc["error"] = speakerLastError;
    String out;
    serializeJson(doc, out);
    server.send(ok ? 200 : 500, "application/json; charset=utf-8", out);
  });
  server.on("/api/audio_config", HTTP_POST, []() {
    JsonDocument req;
    DeserializationError err = deserializeJson(req, server.arg("plain"));
    if (err) {
      server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"JSON 解析失败\"}");
      return;
    }
    int volume = req["volume"] | audioVolume;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    audioVolume = static_cast<uint8_t>(volume);
    replyToneEnabled = req["replyTone"] | replyToneEnabled;
    prefs.putUChar("audioVol", audioVolume);
    prefs.putBool("replyTone", replyToneEnabled);
    JsonDocument doc;
    doc["ok"] = true;
    doc["volume"] = audioVolume;
    doc["replyTone"] = replyToneEnabled;
    doc["speakerReady"] = speakerReady;
    doc["error"] = speakerLastError;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json; charset=utf-8", out);
  });
  server.on("/api/mic_config", HTTP_POST, []() {
    JsonDocument req;
    DeserializationError err = deserializeJson(req, server.arg("plain"));
    if (err) {
      server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"JSON parse failed\"}");
      return;
    }
    micWakeTestEnabled = req["enabled"] | micWakeTestEnabled;
    int threshold = req["threshold"] | micThreshold;
    if (threshold < 200) threshold = 200;
    if (threshold > 12000) threshold = 12000;
    micThreshold = static_cast<uint16_t>(threshold);
    prefs.putBool("micWake", micWakeTestEnabled);
    prefs.putUShort("micThr", micThreshold);
    if (micWakeTestEnabled) {
      initMicI2s();
    } else if (audioI2sMode == AUDIO_I2S_MIC) {
      releaseAudioI2s();
      micLastError = "Mic wake test disabled";
    }

    JsonDocument doc;
    doc["ok"] = true;
    doc["audioMode"] = audioModeName();
    doc["micReady"] = micReady;
    doc["micWakeTestEnabled"] = micWakeTestEnabled;
    doc["micThreshold"] = micThreshold;
    doc["micLevel"] = micLevel;
    doc["micPeakLevel"] = micPeakLevel;
    doc["micTriggerCount"] = micTriggerCount;
    doc["micError"] = micLastError;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json; charset=utf-8", out);
  });
  server.on("/api/chat", HTTP_POST, []() {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String msg = doc["message"] | "";
    JsonDocument outDoc;
    String reply = chatWithTools(msg);
    playReplyTone();
    outDoc["reply"] = reply;
    String out;
    serializeJson(outDoc, out);
    server.send(200, "application/json; charset=utf-8", out);
  });
  server.onNotFound(captiveRedirect);
  server.begin();
}

void setup() {
  Serial.begin(115200);
  Serial0.begin(115200);
  delay(1000);
  pinMode(LED_BACKUP_PIN, OUTPUT);
  setLed(false);
  logBoth("\nESP32-S3 voice companion booting");

  prefs.begin("walle", false);
  savedSsid = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  savedWeatherName = prefs.getString("weatherName", "南昌");
  savedWeatherLat = prefs.getDouble("weatherLat", 28.6820);
  savedWeatherLon = prefs.getDouble("weatherLon", 115.8580);
  audioVolume = prefs.getUChar("audioVol", 45);
  if (audioVolume > 100) audioVolume = 45;
  replyToneEnabled = prefs.getBool("replyTone", true);
  micWakeTestEnabled = prefs.getBool("micWake", true);
  micThreshold = prefs.getUShort("micThr", 1800);
  if (micThreshold < 200 || micThreshold > 12000) micThreshold = 1800;

  startAp();
  setupRoutes();
  initSpeakerI2s();
  playSpeakerTestTone();
  resumeMicWakeTest();
  if (!savedSsid.isEmpty()) {
    connectSta(savedSsid, savedPass, 12000);
  }
}

void loop() {
  if (!natEnabled) dnsServer.processNextRequest();
  server.handleClient();
  pollMicWakeTest();
  if (millis() - lastWifiCheck > 2000) {
    lastWifiCheck = millis();
    bool nowReady = WiFi.status() == WL_CONNECTED;
    if (nowReady != wifiReady) {
      wifiReady = nowReady;
      setLed(wifiReady);
      if (wifiReady) {
        enableNatIfPossible();
      } else {
        disableNatAndRestoreCaptiveDns();
      }
    }
  }
}
