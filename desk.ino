#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// MAC of Edge 1 (receiver)
static const uint8_t EDGE1_MAC[6] = { 0x64, 0xE8, 0x33, 0x73, 0xD7, 0x80 };

#pragma pack(push, 1)
struct SensorMsg {
  bool     seated;       // 1 byte
  float    temperature;  // 4 bytes
  uint16_t co2;          // 2 bytes
};
#pragma pack(pop)

// Pin assignments
const int flexPin   = 35;  // flex sensor (ADC1)
const int mq135Pin  = 34;  // CO₂ sensor (ADC1)
const int buzzerPin = 16;
const int ledPin    = 17;

// Flex threshold & seated state
const int flexThreshold = 1500;
bool isSeated = false;

// Send callback
void onSendStatus(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.print("Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ OK" : "❌ FAIL");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Configure pins
  pinMode(flexPin,   INPUT);
  pinMode(mq135Pin,  INPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(ledPin,    OUTPUT);
  digitalWrite(buzzerPin, LOW);
  digitalWrite(ledPin,    LOW);

  analogReadResolution(12);

  // Wi-Fi & ESP-NOW on channel 1
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    while (true) delay(500);
  }
  esp_now_register_send_cb(onSendStatus);

  // Add Edge1 as peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, EDGE1_MAC, 6);
  peer.channel = 1;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("❌ Failed to add Edge1 peer");
    while (true) delay(500);
  }

  Serial.println("✅ Desk sensor ready. Will only send when seating changes.");
}

void loop() {
  int flexValue = analogRead(flexPin);
  int co2Value  = analogRead(mq135Pin);

  // Check seating status
  bool nowSeated = flexValue > flexThreshold;

  // Only send message if posture changed
  if (nowSeated != isSeated) {
    isSeated = nowSeated;
    Serial.println(isSeated ? "🪑 Seated" : "🧍 Standing");

    // Build and send SensorMsg
    SensorMsg msg;
    msg.seated      = isSeated;
    msg.temperature = 23.5f;  // hardcoded temperature
    msg.co2         = uint16_t(co2Value);

    esp_err_t res = esp_now_send(EDGE1_MAC, (uint8_t*)&msg, sizeof(msg));
    if (res != ESP_OK) {
      Serial.printf("❌ Send failed: %d\n", res);
    } else {
      Serial.printf("📤 Sent → seated=%d, T=%.1f°C, CO2=%uppm\n",
                    msg.seated, msg.temperature, msg.co2);
    }
  }

  delay(500);  // Polling rate
}
