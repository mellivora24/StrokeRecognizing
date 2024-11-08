#include <Wire.h>
#include "MAX30100_PulseOximeter.h"
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <MPU6050.h>

#define REPORTING_PERIOD_MS 10000
#define SSID "WiFi_TEST"
#define PASS "admin123456"
#define USER_PASSWORD "admin123456"
#define USER_EMAIL "onhand-devices@gmail.com"
#define FIREBASE_PROJECT_ID "stroke-app-hlt24e"
#define API_KEY "AIzaSyBU3AiSNb01oAfzpjqbW2nfvF__VqsxCKc"

PulseOximeter pox;
MPU6050 mpu;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

float heartRate = 0, spO2 = 0, temperature = 0;
bool fallDetected = false;
uint32_t tsLastReport = 0;
unsigned long fallStartTime = 0;

void onBeatDetected() {}

// Simulate temperature sensor reading
float getTemp() { return random(35, 37); }

// Task to update MAX30100 sensor frequently
void pulseOximeterTask(void *pvParameters) {
  for (;;) {
    pox.update();
    heartRate = pox.getHeartRate();
    spO2 = pox.getSpO2();
    vTaskDelay(50 / portTICK_PERIOD_MS); // Update every 50ms
  }
}

// Task to read MPU6050 data and detect falls
void sensorReadTask(void *pvParameters) {
  for (;;) {
    temperature = getTemp();

    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    float totalAcceleration = sqrt((ax * ax + ay * ay + az * az) / (16384.0f * 16384.0f));

    if (totalAcceleration > 2.3f) {
      if (!fallDetected) {
        fallDetected = true;
        fallStartTime = millis();
        updateFirebase(true);
      }
    }

    if (fallDetected && (millis() - fallStartTime > 1000)) {
      int16_t gx, gy, gz;
      mpu.getRotation(&gx, &gy, &gz);
      if (abs(gx) < 500 && abs(gy) < 500 && abs(gz) < 500 && totalAcceleration < 1.3f) {
        updateFirebase(false);
        fallDetected = false;
      } else if (millis() - fallStartTime > 2000) {
        fallDetected = false;
      }
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// Task to send data to Firebase periodically
void sendDataTask(void *pvParameters) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && (millis() - tsLastReport > REPORTING_PERIOD_MS)) {
      updateFirebase(false);
      tsLastReport = millis();
    }
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

void setup() {
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Initialize MAX30100 Pulse Oximeter with retries
  int maxRetries = 5;
  while (!pox.begin() && maxRetries > 0) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    maxRetries--;
  }
  
  if (maxRetries == 0) for (;;); // Halt if initialization fails

  pox.setOnBeatDetectedCallback(onBeatDetected);

  // Initialize MPU6050
  Wire.begin();
  mpu.initialize();
  if (!mpu.testConnection()) for (;;); // Halt if initialization fails

  // Create tasks with appropriate stack sizes
  xTaskCreatePinnedToCore(pulseOximeterTask, "Pulse Oximeter Task", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(sensorReadTask, "Sensor Task", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(sendDataTask, "Send Data Task", 8192, NULL, 1, NULL, 0);
}

void loop() {}

// Function to update Firebase data
void updateFirebase(bool fallStatus) {
  FirebaseJson content;
  content.set("fields/oxy/doubleValue", spO2);
  content.set("fields/nhiet_do/doubleValue", temperature);
  content.set("fields/nhip_tim/doubleValue", heartRate);
  content.set("fields/state/booleanValue", fallStatus);

  Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", "/con_nguoi/yPpzT2xQT1OXvz07wt2t", content.raw(), "");
}
