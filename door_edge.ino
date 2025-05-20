#include <WiFi.h>
#include <esp_now.h>

// —— Door ESP32 MAC ——
uint8_t doorMac[6] = { 0x3C, 0x84, 0x27, 0xC7, 0x0C, 0x04 };

// —— Payload struct ——
typedef struct {
  bool doorOpen;
} door_payload_t;

unsigned long breakStart = 0;
bool inBreak = false;

void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  door_payload_t p;
  memcpy(&p, data, sizeof(p));

  if (p.doorOpen) {
    // —— OPEN: ask once
    Serial.println("\n🔔 Door opened!");
    Serial.print("Q: Are you seated? (y/n) ");
    while (!Serial.available()) delay(10);
    char ans = Serial.read(); Serial.readStringUntil('\n');

    if (ans=='n' || ans=='N') {
      // start break
      breakStart = millis();
      inBreak = true;
      Serial.println("A: No → Break started.");
    } else {
      // if they say yes *and* a break was running, end it
      if (inBreak) {
        unsigned long sec = (millis() - breakStart) / 1000;
        inBreak = false;
        Serial.printf("A: Yes → Break ended: %lus\n", sec);
      } else {
        Serial.println("A: Yes → No break.");
      }
    }

  } else {
    // —— CLOSE: simple log only
    Serial.println("\n🚪 Door closed!");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Edge ESP32 ready.");

  // ESP-NOW init
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) { delay(1000); }
  }

  esp_now_register_recv_cb(onDataRecv);

  // register Door as peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, doorMac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add peer");
    while (true) { delay(1000); }
  }
}

void loop() {
  // if in break, show elapsed
  if (inBreak) {
    unsigned long elapsed = (millis() - breakStart) / 1000;
    Serial.printf("  ⏱ In break: %lus\r", elapsed);
  }
  delay(500);
}
