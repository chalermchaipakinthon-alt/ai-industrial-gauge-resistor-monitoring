#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "base64.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"

#include "Start.h"
#include "Resistormode.h"
#include "Gaugemode.h"
#include "Resistorcomplete.h"
#include "GaugeSafe.h"
#include "GaugeHigh.h"
#include "GaugeDanger.h"

// ================= USER CONFIG =================
const char* ssid = "  ";
const char* password = "  ";

String GEMINI_API_KEY = " ";
String TELEGRAM_BOT_TOKEN = " ";
String TELEGRAM_CHAT_ID = " ";

// ================= OLED =================
#define OLED_SDA 1
#define OLED_SCL 2
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= SPEAKER =================
#define I2S_DOUT 41
#define I2S_LRC  40
#define I2S_BCLK 39

// เพิ่ม/ลดความดังเสียง WAV และเสียงเตือน
// 1.0 = เท่าเดิม, 2.0 = ดังขึ้นประมาณ 2 เท่า
// ถ้าเสียงแตกให้ลดลง เช่น 1.8 หรือ 2.0
#define WAV_VOLUME_GAIN 2.5
#define BEEP_VOLUME 30000

// ================= WEB / STATE =================
WebServer server(80);
String deviceURL = "";
String gaugeResult = "Not analyzed yet";

bool gaugeAutoMode = false;
bool dangerAlarmActive = false;
unsigned long lastGaugeAnalyze = 0;
unsigned long lastDangerBeep = 0;
const unsigned long GAUGE_INTERVAL = 30000;

// ================= CAMERA PINS =================
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

// ================= SPEAKER =================
void setupSpeaker() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 24000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

int16_t amplifySample(int16_t sample, float gain) {
  int32_t amplified = (int32_t)(sample * gain);

  if (amplified > 32767) amplified = 32767;
  if (amplified < -32768) amplified = -32768;

  return (int16_t)amplified;
}

void playWav(const uint8_t *data, size_t len) {
  // WAV ที่ใช้เป็น 24 kHz 16-bit mono และข้าม header 44 bytes
  if (len <= 44) return;

  const uint8_t *audio = data + 44;
  size_t audioLen = len - 44;

  // ส่งเป็น block เล็ก ๆ เพื่อปรับ volume ก่อนส่งเข้า MAX98357A
  const int samplesPerBlock = 256;
  int16_t buffer[samplesPerBlock];

  size_t offset = 0;

  while (offset + 1 < audioLen) {
    int count = 0;

    while (count < samplesPerBlock && offset + 1 < audioLen) {
      int16_t sample = (int16_t)(audio[offset] | (audio[offset + 1] << 8));
      buffer[count] = amplifySample(sample, WAV_VOLUME_GAIN);
      count++;
      offset += 2;
    }

    size_t written = 0;
    i2s_write(I2S_NUM_0, buffer, count * sizeof(int16_t), &written, portMAX_DELAY);
  }
}

void playBeep(int freq, int ms, int volume) {
  int sampleRate = 24000;
  int samples = sampleRate * ms / 1000;

  for (int i = 0; i < samples; i++) {
    float t = (float)i / sampleRate;
    int16_t sample = sin(2.0 * PI * freq * t) * volume;
    size_t written;
    i2s_write(I2S_NUM_0, &sample, sizeof(sample), &written, portMAX_DELAY);
  }
}

void dangerBeepLoop() {
  if (!dangerAlarmActive) return;

  if (millis() - lastDangerBeep >= 900) {
    lastDangerBeep = millis();
    playBeep(2200, 250, BEEP_VOLUME);
    delay(80);
    playBeep(1800, 250, BEEP_VOLUME);
  }
}

// ================= OLED =================
void oledShow(String a, String b, String c = "", String d = "") {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("AI Meter Reader");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(0, 16); display.println(a);
  display.setCursor(0, 29); display.println(b);
  display.setCursor(0, 42); display.println(c);
  display.setCursor(0, 55); display.println(d);
  display.display();
}

// ================= UI =================
String css() {
  return R"rawliteral(
<style>
body{margin:0;font-family:Arial;background:#0f172a;color:white;text-align:center;}
h1{font-size:28px;margin-top:24px;}
.card{background:#1e293b;margin:16px;padding:18px;border-radius:22px;white-space:pre-line;}
select,button{width:88%;height:55px;margin:8px;border:0;border-radius:15px;font-size:18px;}
button{font-weight:bold;}
.green{background:#22c55e;}
.orange{background:#f97316;}
.yellow{background:#eab308;}
.blue{background:#3b82f6;color:white;}
.gray{background:#64748b;color:white;}
.red{background:#ef4444;color:white;}
img{width:94%;border-radius:18px;background:#020617;}
</style>
)rawliteral";
}

String homePage() {
  return "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + css() +
  "</head><body><h1>AI Meter Reader</h1>"
  "<div class='card'>Choose Mode</div>"
  "<button class='green' onclick=\"location.href='/resistor'\">Resistor Calculator</button>"
  "<button class='orange' onclick=\"location.href='/gauge'\">Gauge Analyzer</button>"
  "</body></html>";
}

// ================= RESISTOR =================
String digitOptions() {
  return "<option value='0'>Black 0</option><option value='1'>Brown 1</option><option value='2'>Red 2</option><option value='3'>Orange 3</option><option value='4'>Yellow 4</option><option value='5'>Green 5</option><option value='6'>Blue 6</option><option value='7'>Violet 7</option><option value='8'>Gray 8</option><option value='9'>White 9</option>";
}

String multiplierOptions() {
  return "<option value='1'>Black x1</option><option value='10'>Brown x10</option><option value='100'>Red x100</option><option value='1000'>Orange x1k</option><option value='10000'>Yellow x10k</option><option value='100000'>Green x100k</option><option value='1000000'>Blue x1M</option><option value='10000000'>Violet x10M</option><option value='0.1'>Gold x0.1</option><option value='0.01'>Silver x0.01</option>";
}

String toleranceOptions() {
  return "<option value='1'>Brown ±1%</option><option value='2'>Red ±2%</option><option value='0.5'>Green ±0.5%</option><option value='0.25'>Blue ±0.25%</option><option value='0.1'>Violet ±0.1%</option><option value='5'>Gold ±5%</option><option value='10'>Silver ±10%</option>";
}

String resistorPage() {
  return "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + css() +
  "</head><body><h1>Resistor Calculator</h1>"
  "<div class='card'><h3>4 Band</h3><form action='/calc4'>"
  "<select name='b1'>" + digitOptions() + "</select>"
  "<select name='b2'>" + digitOptions() + "</select>"
  "<select name='mul'>" + multiplierOptions() + "</select>"
  "<select name='tol'>" + toleranceOptions() + "</select>"
  "<button class='yellow' type='submit'>Calculate 4 Band</button></form></div>"
  "<div class='card'><h3>5 Band</h3><form action='/calc5'>"
  "<select name='b1'>" + digitOptions() + "</select>"
  "<select name='b2'>" + digitOptions() + "</select>"
  "<select name='b3'>" + digitOptions() + "</select>"
  "<select name='mul'>" + multiplierOptions() + "</select>"
  "<select name='tol'>" + toleranceOptions() + "</select>"
  "<button class='yellow' type='submit'>Calculate 5 Band</button></form></div>"
  "<button class='gray' onclick=\"location.href='/'\">Back Home</button>"
  "</body></html>";
}

String formatOhm(float value) {
  if (value >= 1000000) return String(value / 1000000.0, 2) + " M ohm";
  if (value >= 1000) return String(value / 1000.0, 2) + " k ohm";
  return String(value, 2) + " ohm";
}

void showResistorResult(String result) {
  oledShow("Mode: RESISTOR", "Result:", result, "Offline calc");

  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + css() +
  "</head><body><h1>Result</h1>"
  "<div class='card'><h2>" + result + "</h2><p>Calculated by ESP32-S3 offline</p></div>"
  "<button class='green' onclick=\"location.href='/resistor'\">Calculate Again</button>"
  "<button class='gray' onclick=\"location.href='/'\">Back Home</button>"
  "</body></html>";

  server.send(200, "text/html", html);
  playWav(Resistorcomplete, sizeof(Resistorcomplete));
}

void handleCalc4() {
  int b1 = server.arg("b1").toInt();
  int b2 = server.arg("b2").toInt();
  float mul = server.arg("mul").toFloat();
  String tol = server.arg("tol");
  float value = ((b1 * 10) + b2) * mul;
  showResistorResult(formatOhm(value) + " ±" + tol + "%");
}

void handleCalc5() {
  int b1 = server.arg("b1").toInt();
  int b2 = server.arg("b2").toInt();
  int b3 = server.arg("b3").toInt();
  float mul = server.arg("mul").toFloat();
  String tol = server.arg("tol");
  float value = ((b1 * 100) + (b2 * 10) + b3) * mul;
  showResistorResult(formatOhm(value) + " ±" + tol + "%");
}

// ================= GAUGE UI =================
String gaugePage() {
  String autoText = gaugeAutoMode ? "AUTO MONITOR: ON / 30 sec" : "AUTO MONITOR: OFF";

  return "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + css() +
  "</head><body><h1>Gauge Analyzer</h1>"
  "<div class='card'><b>" + autoText + "</b><br><br><b>Last Result:</b><br>" + gaugeResult + "</div>"
  "<button class='blue' onclick=\"location.href='/gauge_preview'\">Preview Once</button>"
  "<button class='yellow' onclick=\"location.href='/gauge_analyze'\">Analyze Now</button>"
  "<button class='green' onclick=\"location.href='/gauge_start'\">Start Auto 30s</button>"
  "<button class='red' onclick=\"location.href='/gauge_stop'\">Stop Auto</button>"
  "<button class='gray' onclick=\"location.href='/'\">Back Home</button>"
  "</body></html>";
}

void handleGaugePreview() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + css() +
  "</head><body><h1>Gauge Preview</h1>"
  "<div class='card'><img src='/jpg?t=" + String(millis()) + "'></div>"
  "<button class='yellow' onclick=\"location.href='/gauge_analyze'\">Analyze This</button>"
  "<button class='gray' onclick=\"location.href='/gauge'\">Back Gauge</button>"
  "</body></html>";

  oledShow("Mode: GAUGE", "Preview once", "VGA q10", "");
  server.send(200, "text/html", html);
}

// ================= TELEGRAM =================
void sendTelegram(String msg) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = "https://api.telegram.org/bot" + TELEGRAM_BOT_TOKEN + "/sendMessage";

  https.begin(client, url);
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "chat_id=" + TELEGRAM_CHAT_ID + "&text=" + msg;

  int code = https.POST(body);
  String response = https.getString();

  Serial.print("Telegram code: ");
  Serial.println(code);
  Serial.println(response);

  https.end();
}

// ================= GEMINI =================
String askGeminiGauge(camera_fb_t *fb) {
  String img64 = base64::encode(fb->buf, fb->len);

  
    String prompt =
"You are a professional industrial gauge inspector. "
"The gauge in the image is a HYDRO-PNEUMATIC PRESSURE GAUGE with a fixed scale from 0 to 600 bar. "

"IMPORTANT RULES: "
"1. Use ONLY the visible image. "
"2. Read the NEEDLE TIP, not the needle base. "
"3. Assume the gauge scale is 0 to 600 bar. "
"4. Major numbers are 0, 50, 100, 150, 200, 250, 300, 350, 400, 450, 500, 550, 600. "
"5. Estimate intermediate values proportionally between tick marks. "
"6. Return the nearest 10 bar. "
"7. If the needle is between two values, interpolate from the angle. "
"8. If uncertain, choose the closest visible value. "
"9. Never answer UNKNOWN unless the needle is not visible. "

"COLOR ZONES: "
"GREEN = 0 to 300 bar. "
"YELLOW = above 300 to below 500 bar. "
"RED = 500 bar and above. "

"SAFETY RULE: "
"If the needle is very close to a color boundary, choose the higher-risk zone. "

"ANALYSIS STEPS: "
"Step 1: Locate the center pivot. "
"Step 2: Locate the needle tip. "
"Step 3: Determine the needle angle. "
"Step 4: Convert the angle to a value on the 0-600 bar scale. "
"Step 5: Determine the color zone where the needle tip is located. "
"Step 6: Determine status. "

"STATUS RULES: "
"GREEN = SAFE. "
"YELLOW = WARNING. "
"RED = DANGER. "

"Reply ONLY in this exact format: \n"
"VALUE: <number> bar\n"
"COLOR: GREEN or YELLOW or RED\n"
"STATUS: SAFE or WARNING or DANGER\n"
"CONFIDENCE: HIGH or MEDIUM or LOW\n"
"DETAIL: <short explanation>";

  String payload =
    "{\"contents\":[{\"parts\":["
    "{\"text\":\"" + prompt + "\"},"
    "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + img64 + "\"}}"
    "]}]}";

  Serial.print("Payload size: ");
  Serial.println(payload.length());

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(30000);

  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + GEMINI_API_KEY;

  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");

  int code = https.POST(payload);
  String response = https.getString();

  Serial.print("Gemini HTTP code: ");
  Serial.println(code);
  Serial.println(response);

  https.end();

  if (code <= 0) return "VALUE: unknown\nCOLOR: UNKNOWN\nSTATUS: ERROR\nREASON: Gemini request failed";
  if (code != 200) return "VALUE: unknown\nCOLOR: UNKNOWN\nSTATUS: ERROR\nREASON: Gemini HTTP error " + String(code);

  DynamicJsonDocument doc(30000);
  DeserializationError err = deserializeJson(doc, response);
  if (err) return "VALUE: unknown\nCOLOR: UNKNOWN\nSTATUS: ERROR\nREASON: Gemini JSON parse failed";

  const char* text = doc["candidates"][0]["content"]["parts"][0]["text"];
  if (!text) return "VALUE: unknown\nCOLOR: UNKNOWN\nSTATUS: ERROR\nREASON: No Gemini text result";

  return String(text);
}

String getLineValue(String src, String key) {
  int start = src.indexOf(key);
  if (start < 0) return "";
  start += key.length();
  int end = src.indexOf("\n", start);
  if (end < 0) end = src.length();
  String out = src.substring(start, end);
  out.trim();
  return out;
}

void playGaugeSoundByResult(String result) {
  String status = getLineValue(result, "STATUS:");
  String color = getLineValue(result, "COLOR:");

  if (status.indexOf("DANGER") >= 0 || color.indexOf("RED") >= 0) {
    playWav(GaugeDanger, sizeof(GaugeDanger));
    dangerAlarmActive = true;
    lastDangerBeep = 0;
  }
  else if (status.indexOf("WARNING") >= 0 || color.indexOf("YELLOW") >= 0) {
    dangerAlarmActive = false;
    playWav(GaugeHigh, sizeof(GaugeHigh));
  }
  else if (status.indexOf("SAFE") >= 0 || color.indexOf("GREEN") >= 0) {
    dangerAlarmActive = false;
    playWav(GaugeSafe, sizeof(GaugeSafe));
  }
}

void sendTelegramByStatus(String result) {
  String value = getLineValue(result, "VALUE:");
  String color = getLineValue(result, "COLOR:");
  String status = getLineValue(result, "STATUS:");

  String msg;

  if (status.indexOf("DANGER") >= 0 || color.indexOf("RED") >= 0) {
    msg = "🔴 DANGER\n";
  } else if (status.indexOf("WARNING") >= 0 || color.indexOf("YELLOW") >= 0) {
    msg = "🟠 WARNING / HIGH\n";
  } else if (status.indexOf("SAFE") >= 0 || color.indexOf("GREEN") >= 0) {
    msg = "🟢 SAFE\n";
  } else {
    msg = "⚪ GAUGE RESULT\n";
  }

  msg += "\nValue: " + value;
  msg += "\nColor: " + color;
  msg += "\nStatus: " + status;
  msg += "\n\nRaw:\n" + result;
  msg += "\n\nTime: " + String(millis() / 1000) + " sec";

  sendTelegram(msg);
}

void analyzeGaugeOnce(bool fromAuto) {
  oledShow(fromAuto ? "AUTO GAUGE" : "Mode: GAUGE", "Capturing...", "Gemini analyzing", "");

  // ทิ้งเฟรมเก่าก่อน เพื่อกัน Gemini อ่านภาพค้างจากรอบก่อน
  for (int i = 0; i < 3; i++) {
    camera_fb_t * oldFb = esp_camera_fb_get();
    if (oldFb) esp_camera_fb_return(oldFb);
    delay(200);
  }

  // ถ่ายภาพใหม่จริงสำหรับส่งไปวิเคราะห์
  camera_fb_t * fb = esp_camera_fb_get();

  if (!fb) {
    gaugeResult = "VALUE: unknown\nCOLOR: UNKNOWN\nSTATUS: ERROR\nREASON: Capture failed";
    oledShow("GAUGE", "Capture failed", "", "");
    sendTelegramByStatus(gaugeResult);
    return;
  }

  Serial.print("Image size: ");
  Serial.println(fb->len);

  gaugeResult = askGeminiGauge(fb);
  esp_camera_fb_return(fb);

  String value = getLineValue(gaugeResult, "VALUE:");
  String color = getLineValue(gaugeResult, "COLOR:");
  String status = getLineValue(gaugeResult, "STATUS:");

  oledShow("GAUGE result", value, color, status);

  sendTelegramByStatus(gaugeResult);
  playGaugeSoundByResult(gaugeResult);
}

void handleGaugeAnalyze() {
  analyzeGaugeOnce(false);
  server.send(200, "text/html", gaugePage());
}

void handleGaugeStart() {
  dangerAlarmActive = false;
  gaugeAutoMode = true;
  lastGaugeAnalyze = 0;
  gaugeResult = "Auto monitor started. Analyze every 30 sec.";
  oledShow("Mode: GAUGE", "AUTO ON", "Every 30 sec", "VGA q10");
  server.send(200, "text/html", gaugePage());
}

void handleGaugeStop() {
  dangerAlarmActive = false;
  gaugeAutoMode = false;
  gaugeResult = "Auto monitor stopped.";
  oledShow("Mode: GAUGE", "AUTO OFF", "Stopped", "");
  server.send(200, "text/html", gaugePage());
}

// ================= JPG SNAPSHOT =================
void handleJPG() {
  camera_fb_t * fb = esp_camera_fb_get();

  if (!fb) {
    server.send(500, "text/plain", "Capture failed");
    return;
  }

  WiFiClient client = server.client();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/jpeg");
  client.print("Content-Length: ");
  client.println(fb->len);
  client.println("Connection: close");
  client.println();

  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// ================= CAMERA SETUP =================
void setupCamera() {
  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 10;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera failed: 0x%x\n", err);
    oledShow("Camera failed", String(err, HEX), "", "");
    while (true) delay(1000);
  }

  sensor_t * s = esp_camera_sensor_get();

  Serial.print("Sensor PID: 0x");
  Serial.println(s->id.PID, HEX);

  s->set_framesize(s, FRAMESIZE_VGA);
  s->set_quality(s, 10);
  s->set_vflip(s, 0);
  s->set_hmirror(s, 0);

  Serial.println("Camera init OK VGA q10");
}

// ================= HANDLERS =================
void handleHome() {
  dangerAlarmActive = false;
  gaugeAutoMode = false;
  oledShow("Phone connected", "Mode: HOME", WiFi.localIP().toString(), "");
  server.send(200, "text/html", homePage());
}

void handleResistor() {
  dangerAlarmActive = false;
  gaugeAutoMode = false;
  oledShow("Mode: RESISTOR", "Offline calculate", "4/5 Band", "");
  server.send(200, "text/html", resistorPage());
  playWav(Resistormode, sizeof(Resistormode));
}

void handleGauge() {
  dangerAlarmActive = false;
  oledShow("Mode: GAUGE", gaugeAutoMode ? "AUTO ON" : "Manual mode", "VGA q10", "");
  server.send(200, "text/html", gaugePage());
  playWav(Gaugemode, sizeof(Gaugemode));
}

void testInternet() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(10000);
  https.begin(client, "https://www.google.com");
  int code = https.GET();

  Serial.print("Internet test code: ");
  Serial.println(code);
  https.end();
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  setupSpeaker();

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  oledShow("Booting...", "Camera init", "VGA q10", "");
  setupCamera();

  oledShow("Connecting WiFi", ssid, "", "");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  deviceURL = "http://" + WiFi.localIP().toString();

  Serial.println();
  Serial.print("Open: ");
  Serial.println(deviceURL);

  testInternet();

  oledShow("WiFi connected", deviceURL, "Open on phone", "");

  playWav(Start, sizeof(Start));

  sendTelegram("🟢 ESP32 AI Meter Reader Online\nOpen: " + deviceURL);

  server.on("/", handleHome);
  server.on("/resistor", handleResistor);
  server.on("/calc4", handleCalc4);
  server.on("/calc5", handleCalc5);
  server.on("/gauge", handleGauge);
  server.on("/gauge_preview", handleGaugePreview);
  server.on("/gauge_analyze", handleGaugeAnalyze);
  server.on("/gauge_start", handleGaugeStart);
  server.on("/gauge_stop", handleGaugeStop);
  server.on("/jpg", handleJPG);

  server.begin();
  Serial.println("Web server started");
}

// ================= LOOP =================
void loop() {
  server.handleClient();
  dangerBeepLoop();

  if (gaugeAutoMode) {
    unsigned long now = millis();

    if (lastGaugeAnalyze == 0 || now - lastGaugeAnalyze >= GAUGE_INTERVAL) {
      lastGaugeAnalyze = now;
      analyzeGaugeOnce(true);
    }
  }
}