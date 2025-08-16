#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

const char* ssid = "********";
const char* password = "***********";

// MQTT Configuration
const char* mqtt_server = "test.mosquitto.org";
const int   mqtt_port   = 1883;
const char* client_id   = "SkripsiFuji_BlindSpot_002";

// MQTT Topics (Hanya untuk mengirim data)
const char* topic_distance      = "SkripsiFuji/blindspot/sensor/distance";
const char* topic_sensor_status = "SkripsiFuji/blindspot/sensor/status";
const char* topic_led_status    = "SkripsiFuji/blindspot/led_status";

// Pin Configuration
#define TRIG_PIN  26
#define ECHO_PIN  27
#define LED_PIN   25

// Threshold
#define LED_TRIGGER_DISTANCE 450 // Trigger LED if distance is <= 450 cm

// Global State Variables
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
bool led_state = false;
bool sensor_error = false;
unsigned long measurement_count = 0;
unsigned long error_count = 0;

// Function Prototypes
void connectWiFi();
void connectMQTT();
void publishDistance(float distance);
void publishStatus(const char* status, const char* message);
void publishLedStatus(bool state);
float readDistance();

// ===== Helper Functions =====

/**
 * @brief Reads the distance from the ultrasonic sensor.
 * @return The distance in centimeters. Returns -1.0 if there's a timeout.
 */
float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) {
    sensor_error = true;
    error_count++;
    return -1.0;
  }
  return (duration * 0.0343) / 2;
}

// ===== MQTT =====

// Tidak ada fungsi onMqttMessage() karena tidak ada perintah masuk
void connectMQTT() {
  mqtt_client.setServer(mqtt_server, mqtt_port);
  // mqtt_client.setCallback() dihapus karena tidak ada pesan masuk
  while (!mqtt_client.connected()) {
    Serial.print("📡 Connecting to MQTT broker: ");
    Serial.println(mqtt_server);
    if (mqtt_client.connect(client_id)) {
      Serial.println("✅ MQTT connected successfully!");
      // mqtt_client.subscribe() dihapus karena tidak ada topik untuk di-subscribe
    } else {
      Serial.print("❌ MQTT connect failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" retry in 5s");
      delay(5000);
    }
  }
}

void publishDistance(float distance) {
  StaticJsonDocument<300> distanceDoc;
  distanceDoc["device_id"] = client_id;
  distanceDoc["timestamp"] = millis();
  distanceDoc["distance_cm"] = distance;
  distanceDoc["distance_m"]  = distance / 100.0;
  distanceDoc["sensor_type"] = "JSN-SR04T";
  distanceDoc["quality"]     = sensor_error ? "poor" : "good";
  distanceDoc["measurement_count"] = measurement_count;
  distanceDoc["error_count"] = error_count;
  distanceDoc["warning_led"] = (distance < LED_TRIGGER_DISTANCE);
  char distanceBuffer[300];
  serializeJson(distanceDoc, distanceBuffer);
  if (mqtt_client.publish(topic_distance, distanceBuffer)) {
    Serial.printf("📏 Distance published: %.2f cm\n", distance);
  } else {
    Serial.println("❌ Failed to publish distance data");
  }
}

void publishStatus(const char* status, const char* message) {
  StaticJsonDocument<400> statusDoc;
  statusDoc["device_id"] = client_id;
  statusDoc["timestamp"] = millis();
  statusDoc["status"] = status;
  statusDoc["message"] = message;
  statusDoc["wifi_rssi"] = WiFi.RSSI();
  statusDoc["free_heap"] = ESP.getFreeHeap();
  statusDoc["uptime_ms"] = millis();
  statusDoc["measurement_count"] = measurement_count;
  statusDoc["error_count"] = error_count;
  statusDoc["error_rate"] = measurement_count > 0 ? (float)error_count / measurement_count : 0.0;
  char statusBuffer[400];
  serializeJson(statusDoc, statusBuffer);
  mqtt_client.publish(topic_sensor_status, statusBuffer, true);
  Serial.println("📊 Status published");
}

void publishLedStatus(bool state) {
  StaticJsonDocument<100> ledStatusDoc;
  ledStatusDoc["device_id"] = client_id;
  ledStatusDoc["timestamp"] = millis();
  ledStatusDoc["led_status"] = state ? "ON" : "OFF";
  char ledStatusBuffer[100];
  serializeJson(ledStatusDoc, ledStatusBuffer);
  if (mqtt_client.publish(topic_led_status, ledStatusBuffer)) {
    Serial.printf("💡 LED status published: %s\n", state ? "ON" : "OFF");
  } else {
    Serial.println("❌ Failed to publish LED status.");
  }
}

void connectWiFi() {
  Serial.print("🔌 Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected successfully!");
}

// ===== Arduino Core =====
void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  connectWiFi();
  connectMQTT();
}

void loop() {
  if (!mqtt_client.connected()) {
    connectMQTT();
  }
  // mqtt_client.loop() tidak diperlukan karena tidak ada pesan masuk
  float distance = readDistance();
  if (distance > 0) {
    measurement_count++;
    sensor_error = false;
    Serial.printf("Jarak sensor terbaca: %.2f cm\n", distance);
    publishDistance(distance);
    
    bool previousLedState = led_state;
    if (distance <= LED_TRIGGER_DISTANCE) {
      digitalWrite(LED_PIN, HIGH);
      led_state = true;
    } else {
      digitalWrite(LED_PIN, LOW);
      led_state = false;
    }

    if (led_state != previousLedState) {
        publishLedStatus(led_state);
    }
  } else {
    sensor_error = true;
    Serial.println("❌ Gagal membaca jarak dari sensor.");
    if (led_state) {
        digitalWrite(LED_PIN, LOW);
        led_state = false;
        publishLedStatus(false);
    }
  }
  static unsigned long lastStatusPublish = 0;
  if (millis() - lastStatusPublish > 60000) {
    publishStatus("OK", "Sensor and MQTT running");
    lastStatusPublish = millis();
  }
  delay(1000);
}

