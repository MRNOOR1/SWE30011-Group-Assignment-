#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// —— Edge 1 MAC (door/display node) ——  
static const uint8_t EDGE1_MAC[6] = { 0x64, 0xE8, 0x33, 0x73, 0xD7, 0x80 };

// —— Payload (packed for alignment) ——  
#pragma pack(push,1)
struct EdgeMsg {
  uint8_t type;   // 0=request, 1=response
  bool    seated;
};
#pragma pack(pop)

// —— Log prefixes ——  
#define LOG_RX    "[E2_RX]"
#define LOG_TX    "[E2_TX]"
#define LOG_ERR   "[E2_ERR]"
#define LOG_SETUP "[E2_OK]"

// —— Send‐status callback ——  
void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0],mac[1],mac[2],
           mac[3],mac[4],mac[5]);
  Serial.printf("%s to %s : %s\n",
                LOG_TX, macStr,
                status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// —— Receive callback ——  
void onDataRecv(const esp_now_recv_info_t* info,
                const uint8_t* data, int len) {
  if (len != sizeof(EdgeMsg)) {
    Serial.printf("%s bad len %d (expected %d)\n",
                  LOG_ERR, len, sizeof(EdgeMsg));
    return;
  }

  EdgeMsg req;
  memcpy(&req, data, sizeof(req));
  if (req.type != 0) {
    // not a posture request
    return;
  }

  Serial.printf("\n%s POSTURE request\n", LOG_RX);
  Serial.print  ("? Are you seated? (y/n): ");
  while (!Serial.available()) delay(10);

  char c = Serial.read();
  // flush
  while (Serial.available()) Serial.read();

  bool seated = (c=='y' || c=='Y');
  Serial.printf("%s Answered: %s\n",
                LOG_RX, seated ? "YES" : "NO");

  // send response
  EdgeMsg resp = { 1, seated };
  esp_err_t r = esp_now_send(EDGE1_MAC, (uint8_t*)&resp, sizeof(resp));
  if (r == ESP_OK) {
    Serial.printf("%s Response sent\n", LOG_TX);
  } else {
    Serial.printf("%s Response FAILED (%d)\n", LOG_ERR, r);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // 1) STA mode on channel 1
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE) == ESP_OK) {
    Serial.printf("%s Locked to channel 1\n", LOG_SETUP);
  } else {
    Serial.printf("%s Channel lock FAILED\n", LOG_ERR);
  }

  // 2) Print our MAC
  Serial.printf("%s Edge 2 MAC: %s\n",
                LOG_SETUP, WiFi.macAddress().c_str());

  // 3) Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.printf("%s ESP-NOW init FAILED\n", LOG_ERR);
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);
  Serial.printf("%s ESP-NOW ready\n", LOG_SETUP);

  // 4) Add Edge 1 as peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, EDGE1_MAC, 6);
  peer.channel = 1;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    Serial.printf("%s Added Edge 1 as peer\n", LOG_SETUP);
  } else {
    Serial.printf("%s Failed to add Edge 1\n", LOG_ERR);
  }

  Serial.printf("%s Setup complete\n", LOG_SETUP);
}

void loop() {
  // all work in onDataRecv()
  delay(100);
}
