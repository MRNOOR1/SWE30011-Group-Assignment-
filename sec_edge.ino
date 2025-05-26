#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Peer MACs
static const uint8_t EDGE1_MAC[6]  = {0x64,0xE8,0x33,0x73,0xD7,0x80};
static const uint8_t SENSOR_MAC[6] = {0x78,0x42,0x1C,0x67,0x71,0x20};

#pragma pack(push,1)
struct SensorMsg { bool seated; };
struct EdgeMsg   { uint8_t type; bool seated; }; // 0=req,1=resp
#pragma pack(pop)

// State
bool      lastSeated   = true;
bool      inBreak      = false;
unsigned long breakStart = 0;

// Logs
#define E2_RX  "[E2_RX]"
#define E2_TX  "[E2_TX]"
#define E2_ERR "[E2_ERR]"
#define E2_OK  "[E2_OK]"

// Send-callback
void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  char buf[18];
  snprintf(buf,sizeof(buf),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  Serial.printf("%s to %s : %s\n",
                E2_TX, buf,
                status==ESP_NOW_SEND_SUCCESS?"OK":"FAIL");
}

// Receive-callback
void onDataRecv(const esp_now_recv_info_t* info,
                const uint8_t* data, int len) {
  bool fromEdge1  = !memcmp(info->src_addr, EDGE1_MAC,  6);
  bool fromSensor = !memcmp(info->src_addr, SENSOR_MAC, 6);

  // 1) Sensor → Edge2
  if (fromSensor && len == sizeof(SensorMsg)) {
    SensorMsg m; memcpy(&m,data,sizeof(m));
    lastSeated = m.seated;
    Serial.printf("%s Sensor → seated=%s\n",
                  E2_RX, lastSeated?"YES":"NO");

    // end break immediately if they sat down
    if (inBreak && lastSeated) {
      unsigned long secs = (millis() - breakStart)/1000;
      Serial.printf("%s Break ended: %lus\n", E2_OK, secs);
      inBreak = false;
    }

    // FORWARD to Edge1
    EdgeMsg notify = {1, lastSeated};
    esp_err_t r = esp_now_send(EDGE1_MAC, (uint8_t*)&notify, sizeof(notify));
    Serial.printf("%s Forwarded to Edge1: seated=%s (err=%d)\n",
                  E2_TX, lastSeated?"YES":"NO", r);
    return;
  }

  // 2) Edge1 → posture request
  if (fromEdge1 && len == sizeof(EdgeMsg)) {
    EdgeMsg req; memcpy(&req,data,sizeof(req));
    if (req.type==0) {
      Serial.printf("\n%s Request from Edge1\n", E2_RX);
      // start break on door-close logic lives in Edge1;
      // here we just re-send the lastSeated:
      EdgeMsg resp = {1, lastSeated};
      Serial.printf("%s Replying seated=%s\n",
                    E2_TX, lastSeated?"YES":"NO");
      esp_now_send(EDGE1_MAC,(uint8_t*)&resp,sizeof(resp));
    }
    return;
  }

  // Anything else
  Serial.printf("%s Unknown pkt len=%d\n", E2_ERR, len);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // STA mode & channel lock
  WiFi.mode(WIFI_STA); WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  Serial.printf("%s Edge2 MAC: %s\n", E2_OK, WiFi.macAddress().c_str());

  if (esp_now_init()!=ESP_OK) {
    Serial.printf("%s ESP-NOW init failed\n", E2_ERR);
    while(1) delay(500);
  }
  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);
  Serial.printf("%s ESP-NOW ready\n", E2_OK);

  // Add peers
  esp_now_peer_info_t p={};
  p.channel=1; p.encrypt=false;

  memcpy(p.peer_addr,EDGE1_MAC,6);
  esp_now_add_peer(&p);

  memcpy(p.peer_addr,SENSOR_MAC,6);
  esp_now_add_peer(&p);

  Serial.printf("%s Setup complete\n", E2_OK);
}

void loop() {
  // nothing – all in callbacks
  delay(100);
}
