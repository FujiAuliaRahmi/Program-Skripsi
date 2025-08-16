#include "esp_camera.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include "board_config.h"

const char *ssid = "Apayaa";
const char *password = "00000000";

const char* mqtt_server = "test.mosquitto.org";
const int mqtt_port = 1883;
const char* client_id = "SkripsiFuji_Webserver_001";

const char* topic_camera_meta  = "SkripsiFuji/blindspot/camera/meta";
const char* topic_camera_chunk = "SkripsiFuji/blindspot/camera/chunk";
const char* topic_camera_done  = "SkripsiFuji/blindspot/camera/done";
const char* topic_status       = "SkripsiFuji/blindspot/camera/status";

const size_t CHUNK_SIZE = 1024;
const unsigned long capture_interval = 3000;
const unsigned long status_interval = 5000;

WiFiClient espClient;
PubSubClient mqtt_client(espClient);
unsigned long last_capture = 0;
unsigned long last_status = 0;
int image_counter = 0;

void startCameraServer();
void setupLedFlash();
void connectMQTT();
void publishStatus(const char* status, const char* message);
void captureAndPublishImage();

void connectMQTT() {
  while (!mqtt_client.connected()) {
    Serial.print("📡 Connecting to MQTT broker: ");
    Serial.println(mqtt_server);

    if (mqtt_client.connect(client_id)) {
      Serial.println("✅ MQTT connected successfully!");
      publishStatus("online", "Camera module started successfully");
    } else {
      Serial.print("❌ MQTT connection failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    }
  }
}

void publishStatus(const char* status, const char* message) {
  StaticJsonDocument<200> statusDoc;
  statusDoc["device_id"] = client_id;
  statusDoc["timestamp"] = millis();
  statusDoc["status"] = status;
  statusDoc["message"] = message;
  statusDoc["images_captured"] = image_counter;

  char statusBuffer[200];
  serializeJson(statusDoc, statusBuffer);

  mqtt_client.publish(topic_status, statusBuffer, true);
  Serial.println("📊 Status published");
}

void captureAndPublishImage() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Camera capture failed");
    publishStatus("error", "Camera capture failed");
    return;
  }

  Serial.printf("📷 Image captured: %d bytes\n", fb->len);

  String base64Image = base64::encode(fb->buf, fb->len);
  size_t totalSize = base64Image.length();
  size_t totalChunks = (totalSize + CHUNK_SIZE - 1) / CHUNK_SIZE;

  StaticJsonDocument<200> metaDoc;
  metaDoc["device_id"] = client_id;
  metaDoc["image_id"] = image_counter;
  metaDoc["size"] = totalSize;
  metaDoc["chunks"] = totalChunks;
  metaDoc["width"] = fb->width;
  metaDoc["height"] = fb->height;
  metaDoc["format"] = "jpeg";

  char metaBuffer[256];
  serializeJson(metaDoc, metaBuffer);
  mqtt_client.publish(topic_camera_meta, metaBuffer);
  Serial.printf("📤 Metadata published (chunks: %d)\n", totalChunks);

  for (size_t i = 0; i < totalChunks; i++) {
    String chunk = base64Image.substring(i * CHUNK_SIZE, (i + 1) * CHUNK_SIZE);

    StaticJsonDocument<256> chunkDoc;
    chunkDoc["id"] = image_counter;
    chunkDoc["idx"] = i;
    chunkDoc["data"] = chunk;

    String chunkJson;
    serializeJson(chunkDoc, chunkJson);

    bool published = mqtt_client.publish(topic_camera_chunk, chunkJson.c_str());
    if (!published) {
      Serial.printf("❌ Failed to publish chunk %d\n", i);
      publishStatus("error", "Failed to publish image chunk");
      esp_camera_fb_return(fb);
      return;
    }
    delay(5);
  }

  StaticJsonDocument<50> doneDoc;
  doneDoc["id"] = image_counter;
  char doneBuffer[50];
  serializeJson(doneDoc, doneBuffer);
  mqtt_client.publish(topic_camera_done, doneBuffer);
  Serial.printf("✅ Image %d published in %d chunks\n", image_counter, totalChunks);

  image_counter++;
  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

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
  config.frame_size = FRAMESIZE_VGA; // 640x480
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_VGA; // pastikan tetap VGA
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }

  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_VGA); // pastikan VGA
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setBufferSize(2048);
  connectMQTT();
  startCameraServer();
}

void loop() {
  if (!mqtt_client.connected()) {
    connectMQTT();
  }
  mqtt_client.loop();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi disconnected, reconnecting...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\n✅ WiFi reconnected!");
  }

  unsigned long current_time = millis();

  if (current_time - last_capture >= capture_interval) {
    captureAndPublishImage();
    last_capture = current_time;
  }

  if (current_time - last_status >= status_interval) {
    publishStatus("online", "Camera operating normally");
    last_status = current_time;
  }

  delay(10);
}
