#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// MAC of Edge 1 (the peer you're sending to)
static const uint8_t EDGE1_MAC[6] = { 0x64, 0xE8, 0x33, 0x73, 0xD7, 0x80 };

// Payloads
#pragma pack(push,1)
struct SensorMsg {
  bool    seated;
  float   temperature;
  uint16_t co2;
};

struct SetpointMsg {
  float setpoint;
};

struct ReportMsg {
  uint16_t seatedSecs;
  uint16_t breakSecs;
  float    temperature;
  uint16_t co2;
};
#pragma pack(pop)

// Global variables
float currentSetpoint = 22.0f;

// ESP-NOW Send Status
void onSendStatus(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.print("Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ OK" : "❌ FAIL");
}

// ESP-NOW Receive Callback
void onReceiveData(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           info->src_addr[0], info->src_addr[1], info->src_addr[2],
           info->src_addr[3], info->src_addr[4], info->src_addr[5]);
  Serial.print("📡 From: "); Serial.println(macStr);

  if (len == sizeof(SetpointMsg)) {
    SetpointMsg msg;
    memcpy(&msg, data, sizeof(msg));
    currentSetpoint = msg.setpoint;
    Serial.printf("📥 Setpoint received: %.2f°C\n", currentSetpoint);
  } else if (len == sizeof(ReportMsg)) {
    ReportMsg report;
    memcpy(&report, data, sizeof(report));
    Serial.printf("📊 Report — Seated: %us | Break: %us | Temp: %.1f°C | CO₂: %u\n",
                  report.seatedSecs,
                  report.breakSecs,
                  report.temperature,
                  report.co2);
  } else {
    Serial.printf("❗ Unknown data received (%d bytes)\n", len);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Setup Wi-Fi and ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    while (true) delay(1000);
  }

  esp_now_register_send_cb(onSendStatus);
  esp_now_register_recv_cb(onReceiveData);

  // Register Edge1 as peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, EDGE1_MAC, 6);
  peer.channel = 1;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("❌ Failed to add Edge1 peer");
    while (true) delay(1000);
  }

  Serial.println("✅ Ready to receive & send Setpoint via Serial");
  Serial.println("🔁 Type 'setpoint 24.5' in Serial to send a new setpoint");
}

void loop() {
  // Check for Serial command to send Setpoint
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("setpoint")) {
      float value = input.substring(8).toFloat();
      if (value > 0.0f && value < 100.0f) {
        SetpointMsg msg;
        msg.setpoint = value;
        esp_err_t res = esp_now_send(EDGE1_MAC, (uint8_t*)&msg, sizeof(msg));
        if (res == ESP_OK) {
          Serial.printf("📤 Setpoint sent: %.2f°C\n", value);
        } else {
          Serial.printf("❌ Failed to send setpoint: %d\n", res);
        }
      } else {
        Serial.println("⚠️ Invalid setpoint value. Use: setpoint 23.5");
      }
    } else {
      Serial.println("⚠️ Unknown command. Try: setpoint 25.0");
    }
  }

  delay(100);  // keep loop responsive
}
