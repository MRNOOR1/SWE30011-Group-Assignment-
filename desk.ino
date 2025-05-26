#include <WiFi.h>
#include <esp_now.h>

// ← REPLACE this with your Edge 2’s MAC (printed by Edge 2 on startup)
static const uint8_t EDGE2_MAC[6] = { 0xCC, 0x7B, 0x5C, 0x27, 0x1E, 0x40 };

// Very simple payload: just one byte
#pragma pack(push,1)
struct SensorMsg {
  bool seated;  // true = seated, false = standing
};
#pragma pack(pop)

// Called when a send completes
void onSendStatus(const uint8_t *mac, esp_now_send_status_t status) {
  char buf[18];
  snprintf(buf,sizeof(buf),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  Serial.printf("SENSOR ▶ to %s : %s\n",
                buf,
                status==ESP_NOW_SEND_SUCCESS?"OK":"FAIL");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Bring up the radio in STA mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) delay(500);
  }
  esp_now_register_send_cb(onSendStatus);

  // Register Edge 2 as a peer (default channel 0)
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, EDGE2_MAC, 6);
  peer.channel = 0;       // default channel
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add Edge2 as peer");
    while (true) delay(500);
  }

  Serial.println("Sensor node ready");
  Serial.println("Enter 's' for seated, 't' for standing:");
}

void loop() {
  if (!Serial.available()) { 
    delay(50);
    return;
  }

  char c = Serial.read();
  while (Serial.available()) Serial.read();  // flush

  bool seated;
  if (c=='s' || c=='S') {
    seated = true;
    Serial.println("You entered: SEATED");
  }
  else if (c=='t' || c=='T') {
    seated = false;
    Serial.println("You entered: STANDING");
  }
  else {
    Serial.println("Invalid input. Use 's' or 't'.");
    return;
  }

  // Build and send the 1-byte message
  SensorMsg msg = { seated };
  esp_err_t res = esp_now_send(EDGE2_MAC, (uint8_t*)&msg, sizeof(msg));
  if (res != ESP_OK) {
    Serial.printf("Failed to send message: %d\n", res);
  }

  Serial.println("Enter 's' for seated, 't' for standing:");
}
