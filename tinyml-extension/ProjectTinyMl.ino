#include "esp_camera.h"
#include "img_converters.h"
#include "esp_heap_caps.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"
#include <math.h>

// =====================================================
// EDGE IMPULSE
// ถ้าชื่อนี้ไม่ตรง ให้ใช้บรรทัด include จาก example ที่ Verify ผ่าน
// =====================================================
#include <Gauge_TinyML_Classifier_inferencing.h>

// =====================================================
// WIFI
// =====================================================
const char* ssid = " ";
const char* password = " ";

// =====================================================
// OLED
// =====================================================
#define OLED_SDA 1
#define OLED_SCL 2
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// =====================================================
// MAX98357A I2S SPEAKER
// =====================================================
#define I2S_DOUT 41
#define I2S_LRC  40
#define I2S_BCLK 39

#define BEEP_VOLUME 25000

// =====================================================
// CAMERA PINS
// =====================================================
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

// =====================================================
// WEB / SYSTEM STATE
// =====================================================
WebServer server(80);

String deviceURL = "";
String gaugeResult = "ยังไม่ได้วิเคราะห์";

bool gaugeAutoMode = false;
bool dangerAlarmActive = false;

volatile bool analyzeRequested = false;
bool analysisBusy = false;
bool eiImageReady = false;

unsigned long lastGaugeAnalyze = 0;
unsigned long lastDangerBeep = 0;

const unsigned long GAUGE_INTERVAL = 30000;

// =====================================================
// EDGE IMPULSE IMAGE
// =====================================================
#define EI_IMG_W EI_CLASSIFIER_INPUT_WIDTH
#define EI_IMG_H EI_CLASSIFIER_INPUT_HEIGHT

uint8_t* ei_snapshot_buf = nullptr;

const float MIN_CONFIDENCE = 0.55;

// =====================================================
// OLED
// =====================================================
void oledShow(
  String line1,
  String line2,
  String line3 = "",
  String line4 = ""
) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("TinyML Gauge");

  display.drawLine(
    0,
    10,
    SCREEN_WIDTH - 1,
    10,
    SSD1306_WHITE
  );

  display.setCursor(0, 16);
  display.println(line1);

  display.setCursor(0, 29);
  display.println(line2);

  display.setCursor(0, 42);
  display.println(line3);

  display.setCursor(0, 55);
  display.println(line4);

  display.display();
}

// =====================================================
// SPEAKER
// =====================================================
void setupSpeaker() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(
      I2S_MODE_MASTER |
      I2S_MODE_TX
    ),

    .sample_rate = 24000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,

    .intr_alloc_flags = 0,
    .dma_buf_count = 6,
    .dma_buf_len = 256,

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

  esp_err_t err = i2s_driver_install(
    I2S_NUM_0,
    &config,
    0,
    nullptr
  );

  if (err != ESP_OK) {
    Serial.printf(
      "I2S install error: %d\n",
      err
    );
    return;
  }

  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void playBeep(
  int frequency,
  int durationMs,
  int volume
) {
  const int sampleRate = 24000;
  const int totalSamples =
    sampleRate * durationMs / 1000;

  for (int i = 0; i < totalSamples; i++) {
    float timeValue =
      (float)i / (float)sampleRate;

    int16_t sample =
      (int16_t)(
        sin(
          2.0f *
          PI *
          frequency *
          timeValue
        ) *
        volume
      );

    size_t written = 0;

    i2s_write(
      I2S_NUM_0,
      &sample,
      sizeof(sample),
      &written,
      portMAX_DELAY
    );
  }
}

void beepSafe() {
  playBeep(1200, 70, BEEP_VOLUME);
}

void beepWarning() {
  playBeep(1550, 70, BEEP_VOLUME);
  delay(50);
  playBeep(1550, 70, BEEP_VOLUME);
}

void beepDangerOnce() {
  playBeep(2300, 80, BEEP_VOLUME);
  delay(40);
  playBeep(1900, 80, BEEP_VOLUME);
}

void dangerBeepLoop() {
  if (!dangerAlarmActive) {
    return;
  }

  if (
    millis() - lastDangerBeep >= 1000
  ) {
    lastDangerBeep = millis();
    beepDangerOnce();
  }
}

// =====================================================
// WEB STYLE
// =====================================================
String css() {
  return R"rawliteral(
<style>
body{
  margin:0;
  font-family:Arial,sans-serif;
  background:#0f172a;
  color:white;
  text-align:center;
}

h1{
  font-size:27px;
  margin-top:24px;
}

.card{
  background:#1e293b;
  margin:16px;
  padding:18px;
  border-radius:20px;
  white-space:pre-line;
  overflow-wrap:anywhere;
}

button{
  width:88%;
  height:54px;
  margin:7px;
  border:0;
  border-radius:14px;
  font-size:17px;
  font-weight:bold;
}

.green{background:#22c55e;}
.orange{background:#f97316;}
.yellow{background:#eab308;}
.blue{background:#3b82f6;color:white;}
.gray{background:#64748b;color:white;}
.red{background:#ef4444;color:white;}
.purple{background:#a855f7;color:white;}

img{
  width:94%;
  max-width:640px;
  border-radius:18px;
  background:#020617;
  image-rendering:auto;
}

.small{
  font-size:13px;
  color:#cbd5e1;
}

#loading{
  display:none;
  font-weight:bold;
  color:#facc15;
}
</style>
)rawliteral";
}

// =====================================================
// WEB PAGE
// =====================================================
String homePage() {
  return
    "<html>"
    "<head>"
    "<meta name='viewport' "
    "content='width=device-width,initial-scale=1'>"
    + css() +
    "</head>"

    "<body>"
    "<h1>TinyML Gauge Reader</h1>"

    "<div class='card'>"
    "ESP32-S3 Offline AI<br>"
    "Mirror ON / VGA / TinyML"
    "</div>"

    "<button class='orange' "
    "onclick=\"location.href='/gauge'\">"
    "Gauge Analyzer"
    "</button>"

    "</body>"
    "</html>";
}

String gaugePage() {
  String autoText =
    gaugeAutoMode
      ? "AUTO MONITOR: ON / 30 sec"
      : "AUTO MONITOR: OFF";

  return
    "<html>"

    "<head>"
    "<meta name='viewport' "
    "content='width=device-width,initial-scale=1'>"
    + css() +

    R"rawliteral(
<script>
let statusTimer = null;

function refreshStatus(){
  fetch('/status?t=' + Date.now(), {
    cache:'no-store'
  })
  .then(response => response.text())
  .then(text => {
    document.getElementById('result').textContent = text;

    if(text.startsWith('ANALYZING')){
      document.getElementById('loading').style.display = 'block';
    }
    else{
      document.getElementById('loading').style.display = 'none';
    }
  })
  .catch(error => {
    console.log(error);
  });
}

function analyzeNow(){
  document.getElementById('loading').style.display = 'block';
  document.getElementById('result').textContent =
    'ANALYZING...\nกรุณารอสักครู่';

  fetch('/gauge_analyze?t=' + Date.now(), {
    cache:'no-store'
  })
  .then(() => {
    refreshStatus();
  });
}

function startAuto(){
  fetch('/gauge_start?t=' + Date.now(), {
    cache:'no-store'
  })
  .then(() => refreshStatus());
}

function stopAuto(){
  fetch('/gauge_stop?t=' + Date.now(), {
    cache:'no-store'
  })
  .then(() => refreshStatus());
}

function openEIImage(){
  window.open(
    '/ei_input?t=' + Date.now(),
    '_blank'
  );
}

window.onload = function(){
  refreshStatus();
  statusTimer = setInterval(refreshStatus, 600);
};
</script>
)rawliteral"

    "</head>"

    "<body>"
    "<h1>Gauge Analyzer</h1>"

    "<div class='card'>"
    "<b>" + autoText + "</b>"
    "<br><br>"

    "<div id='loading'>"
    "TinyML กำลังวิเคราะห์..."
    "</div>"

    "<b>Last Result</b><br>"
    "<span id='result'>"
    + gaugeResult +
    "</span>"
    "</div>"

    "<button class='blue' "
    "onclick=\"location.href='/gauge_preview'\">"
    "Preview Camera"
    "</button>"

    "<button class='yellow' "
    "onclick='analyzeNow()'>"
    "Analyze Now"
    "</button>"

    "<button class='purple' "
    "onclick='openEIImage()'>"
    "View TinyML Input 96x96"
    "</button>"

    "<button class='green' "
    "onclick='startAuto()'>"
    "Start Auto 30s"
    "</button>"

    "<button class='red' "
    "onclick='stopAuto()'>"
    "Stop Auto"
    "</button>"

    "<button class='gray' "
    "onclick=\"location.href='/'\">"
    "Back Home"
    "</button>"

    "<div class='card small'>"
    "Analyze Now จะตอบสนองทันที "
    "และหน้าเว็บจะดึงผลใหม่อัตโนมัติ"
    "</div>"

    "</body>"
    "</html>";
}

String previewPage() {
  return
    "<html>"

    "<head>"
    "<meta name='viewport' "
    "content='width=device-width,initial-scale=1'>"
    + css() +
    "</head>"

    "<body>"
    "<h1>Camera Preview</h1>"

    "<div class='card'>"
    "<img src='/jpg?t="
    + String(millis()) +
    "'>"
    "</div>"

    "<button class='blue' "
    "onclick=\"location.reload()\">"
    "Refresh Image"
    "</button>"

    "<button class='yellow' "
    "onclick=\"location.href='/gauge'\">"
    "Back and Analyze"
    "</button>"

    "</body>"
    "</html>";
}

// =====================================================
// STRING HELPER
// =====================================================
String getLineValue(
  String source,
  String key
) {
  int start = source.indexOf(key);

  if (start < 0) {
    return "";
  }

  start += key.length();

  int end = source.indexOf(
    "\n",
    start
  );

  if (end < 0) {
    end = source.length();
  }

  String output =
    source.substring(start, end);

  output.trim();
  return output;
}

// =====================================================
// CAMERA FRAME HELPER
// =====================================================
void discardCameraFrames(
  int count,
  int frameDelayMs
) {
  for (int i = 0; i < count; i++) {
    camera_fb_t* frame =
      esp_camera_fb_get();

    if (frame) {
      esp_camera_fb_return(frame);
    }

    delay(frameDelayMs);
  }
}

camera_fb_t* captureFreshFrame() {
  /*
    ใช้ CAMERA_GRAB_LATEST + fb_count 2
    แล้วทิ้ง 2 เฟรมแบบสั้น ๆ

    ไม่รอ 2-3 วินาทีเหมือนโค้ดก่อน
  */

  discardCameraFrames(2, 35);

  return esp_camera_fb_get();
}

// =====================================================
// CAMERA SETUP
// =====================================================
void setupCamera() {
  camera_config_t config = {};

  config.ledc_channel =
    LEDC_CHANNEL_0;

  config.ledc_timer =
    LEDC_TIMER_0;

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

  config.pixel_format =
    PIXFORMAT_JPEG;

  /*
    VGA 640x480 เพื่อให้ภาพต้นฉบับชัด
    จากนั้น crop และย่อเป็น 96x96
  */
  config.frame_size =
    FRAMESIZE_VGA;

  /*
    ค่า JPEG ยิ่งน้อยยิ่งคุณภาพดี
    8 ชัดกว่า 10 แต่ไฟล์ใหญ่ขึ้น
  */
  config.jpeg_quality = 8;

  /*
    ใช้ 2 buffer และ Grab Latest
    เพื่อให้ได้ภาพล่าสุดและไม่ต้อง flush เยอะ
  */
  config.fb_count = 2;

  config.fb_location =
    CAMERA_FB_IN_PSRAM;

  config.grab_mode =
    CAMERA_GRAB_LATEST;

  esp_err_t error =
    esp_camera_init(&config);

  if (error != ESP_OK) {
    Serial.printf(
      "Camera failed: 0x%x\n",
      error
    );

    oledShow(
      "Camera failed",
      String(error, HEX),
      "",
      ""
    );

    while (true) {
      delay(1000);
    }
  }

  sensor_t* sensor =
    esp_camera_sensor_get();

  Serial.print("Sensor PID: 0x");
  Serial.println(
    sensor->id.PID,
    HEX
  );

  sensor->set_framesize(
    sensor,
    FRAMESIZE_VGA
  );

  sensor->set_quality(
    sensor,
    8
  );

  // เปิด Mirror ตามที่ต้องการ
  sensor->set_hmirror(
    sensor,
    1
  );

  // ถ้าภาพกลับหัว เปลี่ยนเป็น 1
  sensor->set_vflip(
    sensor,
    0
  );

  // ปรับภาพ
  sensor->set_brightness(sensor, 0);
  sensor->set_contrast(sensor, 1);
  sensor->set_saturation(sensor, 0);

  sensor->set_whitebal(sensor, 1);
  sensor->set_awb_gain(sensor, 1);

  sensor->set_exposure_ctrl(sensor, 1);
  sensor->set_aec2(sensor, 1);
  sensor->set_ae_level(sensor, 0);

  sensor->set_gain_ctrl(sensor, 1);

  sensor->set_bpc(sensor, 1);
  sensor->set_wpc(sensor, 1);
  sensor->set_lenc(sensor, 1);

  /*
    ให้ sensor ปรับ exposure ตอนเปิดเครื่อง
    ทำครั้งเดียว ไม่ทำทุกครั้งที่กด Analyze
  */
  delay(600);
  discardCameraFrames(4, 50);

  Serial.println(
    "Camera ready: VGA q8 Mirror ON"
  );
}

// =====================================================
// IMAGE CROP + RESIZE
// =====================================================
void resizeCenterCropRGB888(
  uint8_t* source,
  int sourceWidth,
  int sourceHeight,
  uint8_t* destination,
  int destinationWidth,
  int destinationHeight
) {
  /*
    ตัดภาพตรงกลางให้เป็นสี่เหลี่ยม

    VGA 640x480
    จะ crop ตรงกลาง 480x480
    แล้ว resize เป็น 96x96
  */

  int cropSize =
    min(sourceWidth, sourceHeight);

  int cropX =
    (sourceWidth - cropSize) / 2;

  int cropY =
    (sourceHeight - cropSize) / 2;

  for (
    int destinationY = 0;
    destinationY < destinationHeight;
    destinationY++
  ) {
    int sourceY =
      cropY +
      (
        destinationY *
        cropSize
      ) /
      destinationHeight;

    for (
      int destinationX = 0;
      destinationX < destinationWidth;
      destinationX++
    ) {
      int sourceX =
        cropX +
        (
          destinationX *
          cropSize
        ) /
        destinationWidth;

      int sourceIndex =
        (
          sourceY *
          sourceWidth +
          sourceX
        ) *
        3;

      int destinationIndex =
        (
          destinationY *
          destinationWidth +
          destinationX
        ) *
        3;

      destination[destinationIndex + 0] =
        source[sourceIndex + 0];

      destination[destinationIndex + 1] =
        source[sourceIndex + 1];

      destination[destinationIndex + 2] =
        source[sourceIndex + 2];
    }
  }
}

// =====================================================
// EDGE IMPULSE DATA CALLBACK
// =====================================================
int eiCameraGetData(
  size_t offset,
  size_t length,
  float* output
) {
  size_t pixelIndex =
    offset * 3;

  for (
    size_t i = 0;
    i < length;
    i++
  ) {
    uint8_t red =
      ei_snapshot_buf[pixelIndex + 0];

    uint8_t green =
      ei_snapshot_buf[pixelIndex + 1];

    uint8_t blue =
      ei_snapshot_buf[pixelIndex + 2];

    output[i] =
      (float)(
        (red << 16) |
        (green << 8) |
        blue
      );

    pixelIndex += 3;
  }

  return 0;
}

// =====================================================
// RUN TINYML
// =====================================================
String runTinyMLGauge(
  camera_fb_t* frame
) {
  if (!frame) {
    return
      "COLOR: UNKNOWN\n"
      "STATUS: ERROR\n"
      "CONFIDENCE: 0%\n"
      "DETAIL: No camera frame";
  }

  int sourceWidth =
    frame->width;

  int sourceHeight =
    frame->height;

  if (
    sourceWidth <= 0 ||
    sourceHeight <= 0
  ) {
    sourceWidth = 640;
    sourceHeight = 480;
  }

  size_t rgbLength =
    sourceWidth *
    sourceHeight *
    3;

  uint8_t* rgb888 =
    (uint8_t*)heap_caps_malloc(
      rgbLength,
      MALLOC_CAP_SPIRAM |
      MALLOC_CAP_8BIT
    );

  if (!rgb888) {
    rgb888 =
      (uint8_t*)malloc(rgbLength);
  }

  if (!rgb888) {
    return
      "COLOR: UNKNOWN\n"
      "STATUS: ERROR\n"
      "CONFIDENCE: 0%\n"
      "DETAIL: RGB allocation failed";
  }

  bool decodeSuccess =
    fmt2rgb888(
      frame->buf,
      frame->len,
      PIXFORMAT_JPEG,
      rgb888
    );

  if (!decodeSuccess) {
    free(rgb888);

    return
      "COLOR: UNKNOWN\n"
      "STATUS: ERROR\n"
      "CONFIDENCE: 0%\n"
      "DETAIL: JPEG decode failed";
  }

  size_t modelImageLength =
    EI_IMG_W *
    EI_IMG_H *
    3;

  if (!ei_snapshot_buf) {
    ei_snapshot_buf =
      (uint8_t*)heap_caps_malloc(
        modelImageLength,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
      );

    if (!ei_snapshot_buf) {
      ei_snapshot_buf =
        (uint8_t*)malloc(
          modelImageLength
        );
    }
  }

  if (!ei_snapshot_buf) {
    free(rgb888);

    return
      "COLOR: UNKNOWN\n"
      "STATUS: ERROR\n"
      "CONFIDENCE: 0%\n"
      "DETAIL: Model image allocation failed";
  }

  resizeCenterCropRGB888(
    rgb888,
    sourceWidth,
    sourceHeight,
    ei_snapshot_buf,
    EI_IMG_W,
    EI_IMG_H
  );

  free(rgb888);

  /*
    ตั้ง flag เพื่อให้ /ei_input
    สามารถแสดงภาพล่าสุดที่เข้าโมเดล
  */
  eiImageReady = true;

  signal_t signal;

  signal.total_length =
    EI_IMG_W *
    EI_IMG_H;

  signal.get_data =
    &eiCameraGetData;

  ei_impulse_result_t result = {};

  EI_IMPULSE_ERROR inferenceError =
    run_classifier(
      &signal,
      &result,
      false
    );

  if (
    inferenceError !=
    EI_IMPULSE_OK
  ) {
    return
      "COLOR: UNKNOWN\n"
      "STATUS: ERROR\n"
      "CONFIDENCE: 0%\n"
      "DETAIL: Inference failed";
  }

  String bestLabel = "";
  float bestScore = -1.0f;

  Serial.println();
  Serial.println("TinyML scores:");

  for (
    size_t index = 0;
    index < EI_CLASSIFIER_LABEL_COUNT;
    index++
  ) {
    String label =
      String(
        result.classification[index].label
      );

    float score =
      result.classification[index].value;

    Serial.print(label);
    Serial.print(": ");
    Serial.println(score, 4);

    if (score > bestScore) {
      bestScore = score;
      bestLabel = label;
    }
  }

  bestLabel.toLowerCase();

  String color = "UNKNOWN";
  String status = "UNKNOWN";

  if (bestScore < MIN_CONFIDENCE) {
    color = "UNKNOWN";
    status = "UNCERTAIN";
  }
  else if (
    bestLabel.indexOf("danger") >= 0
  ) {
    color = "RED";
    status = "DANGER";
  }
  else if (
    bestLabel.indexOf("warning") >= 0
  ) {
    color = "YELLOW";
    status = "WARNING";
  }
  else if (
    bestLabel.indexOf("safe") >= 0
  ) {
    color = "GREEN";
    status = "SAFE";
  }

  String output = "";

  output +=
    "COLOR: " +
    color +
    "\n";

  output +=
    "STATUS: " +
    status +
    "\n";

  output +=
    "CONFIDENCE: " +
    String(bestScore * 100.0f, 1) +
    "%\n";

  output +=
    "DETAIL: TinyML class = " +
    bestLabel;

  return output;
}

// =====================================================
// RESULT BEEP
// =====================================================
void playGaugeBeepByResult(
  String result
) {
  String status =
    getLineValue(
      result,
      "STATUS:"
    );

  dangerAlarmActive = false;

  if (
    status.indexOf("DANGER") >= 0
  ) {
    dangerAlarmActive = true;
    lastDangerBeep = millis();
    beepDangerOnce();
  }
  else if (
    status.indexOf("WARNING") >= 0
  ) {
    beepWarning();
  }
  else if (
    status.indexOf("SAFE") >= 0
  ) {
    beepSafe();
  }
}

// =====================================================
// ANALYZE
// =====================================================
void analyzeGaugeOnce(
  bool fromAuto
) {
  if (analysisBusy) {
    return;
  }

  analysisBusy = true;

  oledShow(
    fromAuto
      ? "AUTO GAUGE"
      : "GAUGE ANALYZE",

    "Capturing latest",
    "TinyML running",
    ""
  );

  Serial.println();
  Serial.println(
    "===== ANALYZE START ====="
  );

  unsigned long startTime =
    millis();

  camera_fb_t* frame =
    captureFreshFrame();

  if (!frame) {
    gaugeResult =
      "COLOR: UNKNOWN\n"
      "STATUS: ERROR\n"
      "CONFIDENCE: 0%\n"
      "DETAIL: Capture failed";

    oledShow(
      "GAUGE",
      "Capture failed",
      "",
      ""
    );

    analysisBusy = false;
    return;
  }

  Serial.print("Frame: ");
  Serial.print(frame->width);
  Serial.print("x");
  Serial.print(frame->height);
  Serial.print(" / ");
  Serial.print(frame->len);
  Serial.println(" bytes");

  gaugeResult =
    runTinyMLGauge(frame);

  esp_camera_fb_return(frame);

  String color =
    getLineValue(
      gaugeResult,
      "COLOR:"
    );

  String status =
    getLineValue(
      gaugeResult,
      "STATUS:"
    );

  String confidence =
    getLineValue(
      gaugeResult,
      "CONFIDENCE:"
    );

  oledShow(
    "GAUGE RESULT",
    status,
    color,
    confidence
  );

  Serial.println(gaugeResult);

  Serial.print("Total analyze time: ");
  Serial.print(
    millis() - startTime
  );
  Serial.println(" ms");

  Serial.println(
    "===== ANALYZE END ====="
  );

  playGaugeBeepByResult(
    gaugeResult
  );

  analysisBusy = false;
}

// =====================================================
// BMP FUNCTIONS FOR /ei_input
// =====================================================
void write16(
  uint8_t* buffer,
  int position,
  uint16_t value
) {
  buffer[position + 0] =
    value & 0xFF;

  buffer[position + 1] =
    (value >> 8) & 0xFF;
}

void write32(
  uint8_t* buffer,
  int position,
  uint32_t value
) {
  buffer[position + 0] =
    value & 0xFF;

  buffer[position + 1] =
    (value >> 8) & 0xFF;

  buffer[position + 2] =
    (value >> 16) & 0xFF;

  buffer[position + 3] =
    (value >> 24) & 0xFF;
}

// =====================================================
// WEB HANDLERS
// =====================================================
void handleHome() {
  dangerAlarmActive = false;
  gaugeAutoMode = false;

  oledShow(
    "Mode: HOME",
    WiFi.localIP().toString(),
    "Open browser",
    ""
  );

  server.send(
    200,
    "text/html",
    homePage()
  );
}

void handleGauge() {
  dangerAlarmActive = false;

  oledShow(
    "Mode: GAUGE",
    gaugeAutoMode
      ? "AUTO ON"
      : "Manual mode",

    "Mirror ON VGA",
    ""
  );

  server.send(
    200,
    "text/html",
    gaugePage()
  );
}

void handleGaugePreview() {
  oledShow(
    "Mode: GAUGE",
    "Camera preview",
    "Mirror ON VGA",
    ""
  );

  server.send(
    200,
    "text/html",
    previewPage()
  );
}

void handleGaugeAnalyze() {
  /*
    สำคัญ:
    ตอบเว็บทันที ไม่วิเคราะห์ใน handler

    ทำให้ปุ่มไม่ค้าง
  */

  if (!analysisBusy) {
    analyzeRequested = true;
    gaugeResult =
      "ANALYZING...\n"
      "กำลังถ่ายภาพล่าสุดและประมวลผล";
  }

  server.sendHeader(
    "Cache-Control",
    "no-store"
  );

  server.send(
    202,
    "text/plain",
    "Analyze request accepted"
  );
}

void handleGaugeStart() {
  dangerAlarmActive = false;
  gaugeAutoMode = true;

  lastGaugeAnalyze = 0;

  gaugeResult =
    "AUTO MONITOR ON\n"
    "Analyze every 30 seconds";

  server.send(
    200,
    "text/plain",
    "Auto started"
  );
}

void handleGaugeStop() {
  dangerAlarmActive = false;
  gaugeAutoMode = false;
  analyzeRequested = false;

  gaugeResult =
    "AUTO MONITOR OFF\n"
    "Stopped";

  oledShow(
    "Mode: GAUGE",
    "AUTO OFF",
    "Stopped",
    ""
  );

  server.send(
    200,
    "text/plain",
    "Auto stopped"
  );
}

void handleStatus() {
  server.sendHeader(
    "Cache-Control",
    "no-store, no-cache, must-revalidate"
  );

  String output;

  if (analysisBusy) {
    output =
      "ANALYZING...\n"
      "TinyML กำลังประมวลผล";
  }
  else {
    output = gaugeResult;
  }

  server.send(
    200,
    "text/plain; charset=utf-8",
    output
  );
}

// =====================================================
// NORMAL CAMERA JPEG
// =====================================================
void handleJPG() {
  camera_fb_t* frame =
    captureFreshFrame();

  if (!frame) {
    server.send(
      500,
      "text/plain",
      "Capture failed"
    );
    return;
  }

  WiFiClient client =
    server.client();

  client.println(
    "HTTP/1.1 200 OK"
  );

  client.println(
    "Content-Type: image/jpeg"
  );

  client.println(
    "Cache-Control: no-store, no-cache, must-revalidate, max-age=0"
  );

  client.println(
    "Pragma: no-cache"
  );

  client.println(
    "Expires: 0"
  );

  client.print(
    "Content-Length: "
  );

  client.println(frame->len);

  client.println(
    "Connection: close"
  );

  client.println();

  client.write(
    frame->buf,
    frame->len
  );

  esp_camera_fb_return(frame);
}

// =====================================================
// SHOW ACTUAL 96x96 MODEL INPUT
// =====================================================
void handleEIInputImage() {
  if (
    !ei_snapshot_buf ||
    !eiImageReady
  ) {
    server.send(
      404,
      "text/plain",
      "No TinyML input image yet. Press Analyze Now first."
    );
    return;
  }

  int width = EI_IMG_W;
  int height = EI_IMG_H;

  int rowSize =
    (width * 3 + 3) & ~3;

  int imageSize =
    rowSize * height;

  int fileSize =
    54 + imageSize;

  uint8_t header[54] = {};

  header[0] = 'B';
  header[1] = 'M';

  write32(header, 2, fileSize);
  write32(header, 10, 54);
  write32(header, 14, 40);

  write32(header, 18, width);
  write32(header, 22, height);

  write16(header, 26, 1);
  write16(header, 28, 24);

  write32(
    header,
    34,
    imageSize
  );

  WiFiClient client =
    server.client();

  client.println(
    "HTTP/1.1 200 OK"
  );

  client.println(
    "Content-Type: image/bmp"
  );

  client.println(
    "Cache-Control: no-store, no-cache, must-revalidate, max-age=0"
  );

  client.println(
    "Pragma: no-cache"
  );

  client.println(
    "Expires: 0"
  );

  client.print(
    "Content-Length: "
  );

  client.println(fileSize);

  client.println(
    "Connection: close"
  );

  client.println();

  client.write(
    header,
    sizeof(header)
  );

  uint8_t padding[3] = {
    0,
    0,
    0
  };

  int paddingSize =
    rowSize -
    width * 3;

  /*
    BMP เก็บแถวจากล่างขึ้นบน
  */
  for (
    int y = height - 1;
    y >= 0;
    y--
  ) {
    for (
      int x = 0;
      x < width;
      x++
    ) {
      int index =
        (
          y *
          width +
          x
        ) *
        3;

      uint8_t red =
        ei_snapshot_buf[index + 0];

      uint8_t green =
        ei_snapshot_buf[index + 1];

      uint8_t blue =
        ei_snapshot_buf[index + 2];

      uint8_t bgr[3] = {
        blue,
        green,
        red
      };

      client.write(
        bgr,
        3
      );
    }

    if (paddingSize > 0) {
      client.write(
        padding,
        paddingSize
      );
    }
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  setupSpeaker();

  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      0x3C
    )
  ) {
    Serial.println(
      "OLED initialization failed"
    );
  }

  oledShow(
    "Booting...",
    "Camera init",
    "TinyML",
    ""
  );

  setupCamera();

  oledShow(
    "Connecting WiFi",
    ssid,
    "",
    ""
  );

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    ssid,
    password
  );

  unsigned long wifiStart =
    millis();

  while (
    WiFi.status() != WL_CONNECTED
  ) {
    delay(300);
    Serial.print(".");

    if (
      millis() - wifiStart >
      20000
    ) {
      Serial.println();
      Serial.println(
        "WiFi timeout, restarting"
      );

      ESP.restart();
    }
  }

  deviceURL =
    "http://" +
    WiFi.localIP().toString();

  Serial.println();
  Serial.print("Open: ");
  Serial.println(deviceURL);

  oledShow(
    "WiFi connected",
    WiFi.localIP().toString(),
    "Open browser",
    ""
  );

  beepSafe();

  server.on(
    "/",
    HTTP_GET,
    handleHome
  );

  server.on(
    "/gauge",
    HTTP_GET,
    handleGauge
  );

  server.on(
    "/gauge_preview",
    HTTP_GET,
    handleGaugePreview
  );

  server.on(
    "/gauge_analyze",
    HTTP_GET,
    handleGaugeAnalyze
  );

  server.on(
    "/gauge_start",
    HTTP_GET,
    handleGaugeStart
  );

  server.on(
    "/gauge_stop",
    HTTP_GET,
    handleGaugeStop
  );

  server.on(
    "/status",
    HTTP_GET,
    handleStatus
  );

  server.on(
    "/jpg",
    HTTP_GET,
    handleJPG
  );

  server.on(
    "/ei_input",
    HTTP_GET,
    handleEIInputImage
  );

  server.begin();

  Serial.println(
    "Web server started"
  );
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  server.handleClient();

  dangerBeepLoop();

  /*
    Manual Analyze:
    HTTP handler แค่ตั้ง flag
    แล้ว loop ค่อยมาวิเคราะห์
  */
  if (
    analyzeRequested &&
    !analysisBusy
  ) {
    analyzeRequested = false;
    analyzeGaugeOnce(false);
  }

  /*
    Auto Analyze 30 seconds
  */
  if (
    gaugeAutoMode &&
    !analysisBusy &&
    !analyzeRequested
  ) {
    unsigned long now =
      millis();

    if (
      lastGaugeAnalyze == 0 ||
      now - lastGaugeAnalyze >=
      GAUGE_INTERVAL
    ) {
      lastGaugeAnalyze = now;
      analyzeGaugeOnce(true);
    }
  }

  delay(2);
}