#include <WiFi.h>
#include <esp_camera.h>

const char* ssid = "Lab_HCMUTE_Embedded";
const char* password = "embedded_system_ha";
const char* server_ip = "192.168.1.50"; /* IP tĩnh đã gán cho Pi Zero 2 W */
const int server_port = 8080;

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

uint32_t seq = 1;
bool is_busy = false;

/* Vùng ROI cố định để xử lý ảnh (Toạ độ máng pickup gỗ tròn) */
const int roi_x = 100;
const int roi_y = 120;
const int roi_w = 80;
const int roi_h = 80;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  
  /* Khởi tạo Driver Camera OV2640 chuyên dụng */
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIO_C_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE; /* Grayscale để so khác biệt điểm ảnh cực nhanh */
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  
  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera Init Failed!");
    return;
  }
  Serial.println("Camera Initialized successfully.");
}

/* Thuật toán tính độ chênh lệch ảnh ROI (Difference algorithm) */
bool detect_object_difference() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return false;
  
  static uint8_t prev_avg = 0;
  uint32_t total_gray = 0;
  int count = 0;
  
  /* Quét qua các điểm ảnh trong vùng ROI đã định vị */
  for (int y = roi_y; y < roi_y + roi_h; y++) {
    for (int x = roi_x; x < roi_x + roi_w; x++) {
      int idx = y * fb->width + x;
      total_gray += fb->buf[idx];
      count++;
    }
  }
  
  uint8_t curr_avg = total_gray / count;
  esp_camera_fb_return(fb);
  
  if (prev_avg == 0) {
    prev_avg = curr_avg;
    return false;
  }
  
  int diff = abs((int)curr_avg - (int)prev_avg);
  prev_avg = curr_avg;
  
  /* Nếu độ chênh lệch màu sắc xám thay đổi vượt ngưỡng 15, chứng tỏ có khối gỗ lăn vào máng */
  return (diff > 15);
}

void send_event_to_pi() {
  WiFiClient client;
  if (!client.connect(server_ip, server_port)) {
    Serial.println("Connection to Pi Zero failed!");
    return;
  }
  
  /* Tạo chuỗi JSON sự kiện */
  String json = "{\"device_id\":\"esp32cam-01\",\"seq\":";
  json += String(seq);
  json += ",\"event\":\"OBJECT_PRESENT\"}";
  
  client.print(String("POST /api/v1/object-event HTTP/1.1\r\n") +
               "Host: " + server_ip + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " + json.length() + "\r\n" +
               "Connection: close\r\n\r\n" +
               json);
               
  /* Chờ phản hồi ACK từ Pi */
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 2000) {
      Serial.println(">>> Timeout waiting ACK from Pi!");
      client.stop();
      return;
    }
  }
  
  String line = client.readStringUntil('\r');
  Serial.print("Response from Pi: ");
  Serial.println(line);
  
  if (line.indexOf("accepted") >= 0) {
    seq++; /* Chỉ tăng seq khi Pi chấp nhận sự kiện */
    Serial.println("Event ACCEPTED by Pi.");
  } else if (line.indexOf("busy") >= 0) {
    Serial.println("Pi is BUSY. Inhibit camera triggers.");
  }
  
  client.stop();
}

void loop() {
  if (detect_object_difference()) {
    Serial.println("Difference detected in ROI! Sending event...");
    send_event_to_pi();
    delay(5000); /* Chống tự kích hoạt lại do chuyển động của chính tay robot gắp gỗ */
  }
  delay(200);
}
