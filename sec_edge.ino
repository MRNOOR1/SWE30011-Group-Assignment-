#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ——— Wi-Fi / HTTP settings ———
const char* SSID         = "GuerrillaMan";
const char* PASSWORD     = "12345678";
const char* REPORT_URL   = "https://iot-project-crbccbf8eygyh9aq.australiaeast-01.azurewebsites.net/api/report";
const char* SETTINGS_URL = "https://iot-project-crbccbf8eygyh9aq.australiaeast-01.azurewebsites.net/api/settings";

// ——— ESP-NOW peer (Edge1) ———
static const uint8_t EDGE1_MAC[6] = {0x64,0xE8,0x33,0x73,0xD7,0x80};
esp_now_peer_info_t peerInfo;

#pragma pack(push,1)
struct SetpointMsg { float setpoint; };
struct ReportMsg {
  uint16_t seatedSecs;
  uint16_t breakSecs;
  float    temperature;
  uint16_t co2;
};
#pragma pack(pop)

// Buffers & flags for deferred processing
ReportMsg pendingReport;
bool reportPending = false;

// Current setpoint
float currentSetpoint = 22.0f;

// ——— Forward declarations ———
void handleReport(const ReportMsg& report);

// ——— Callbacks ———
void onSendStatus(const uint8_t* mac, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send status: ");
  Serial.println(status==ESP_NOW_SEND_SUCCESS?"✅":"❌");
}

void onReceiveData(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  // Print sender MAC
  char macStr[18];
  snprintf(macStr,sizeof(macStr),"%02X:%02X:%02X:%02X:%02X:%02X",
           info->src_addr[0],info->src_addr[1],info->src_addr[2],
           info->src_addr[3],info->src_addr[4],info->src_addr[5]);
  Serial.print("📡 Received from "); Serial.println(macStr);

  // Only copy and flag, avoid heavy work
  if (len==sizeof(ReportMsg)) {
    memcpy(&pendingReport, data, sizeof(pendingReport));
    reportPending = true;
    Serial.printf("📊 ESP-NOW report queued — seated:%u break:%u temp:%.1f CO₂:%u\n",
                  pendingReport.seatedSecs,
                  pendingReport.breakSecs,
                  pendingReport.temperature,
                  pendingReport.co2);
  } else {
    Serial.printf("❗ Unknown ESP-NOW data (%d bytes)\n", len);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  randomSeed(analogRead(0));

  // Prep ESP-NOW on channel 1
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init()!=ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    while(true) delay(1000);
  }
  esp_now_register_send_cb(onSendStatus);
  esp_now_register_recv_cb(onReceiveData);

  memset(&peerInfo,0,sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, EDGE1_MAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo)!=ESP_OK) {
    Serial.println("❌ ESP-NOW peer add failed"); while(true) delay(1000);
  }

  Serial.println("✅ ESP-NOW ready");
  Serial.println("▶️  Type '1'+ENTER to send a manual test report");
}

void loop() {
  // Handle any queued report in main loop (safe stack)
  if (reportPending) {
    reportPending = false;
    handleReport(pendingReport);
  }

  // Manual test trigger via Serial
  if (Serial.available()) {
    char c = Serial.read();
    if (c=='1') {
      ReportMsg test = {1000,300,25.5f,800};
      Serial.println("\n🔁 Manual test report");
      handleReport(test);
    }
    while(Serial.available()) Serial.read();
  }

  delay(100);
}

void handleReport(const ReportMsg& report) {
  // 1) Deinit ESP-NOW
  esp_now_deinit();
  Serial.println("🛑 ESP-NOW deinitialized");

  // 2) Restore Wi-Fi scan and connect
  esp_wifi_set_channel(0, WIFI_SECOND_CHAN_NONE);  // allow multi-channel scan
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  Serial.printf("🔌 Connecting to Wi-Fi '%s'...", SSID);
  WiFi.begin(SSID, PASSWORD);
  unsigned long start=millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-start<20000) {
    Serial.print('.'); delay(500);
  }
  if (WiFi.status()!=WL_CONNECTED) {
    Serial.println("\n❌ Wi-Fi connection timed out");
  } else {
    Serial.printf("\n✅ Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());

    // 3) POST report
    HTTPClient http;
    http.begin(REPORT_URL);
    http.addHeader("Content-Type","application/json");
    String body = String("{") +
      "\"seatedSecs\":"  + report.seatedSecs  + "," +
      "\"breakSecs\":"   + report.breakSecs   + "," +
      "\"temperature\":" + report.temperature + "," +
      "\"co2\":"         + report.co2 +
      "}";
    Serial.println("📤 POST /api/report");
    Serial.println(body);
    int code = http.POST(body);
    if (code>0) {
      Serial.printf("   HTTP %d\n", code);
      Serial.println(http.getString());
    } else {
      Serial.printf("   POST failed: %s\n", http.errorToString(code).c_str());
    }
    http.end();

    // 4) GET settings
    Serial.println("🔄 GET /api/settings");
    http.begin(SETTINGS_URL);
    int g = http.GET();
    if (g>0) {
      String js = http.getString();
      Serial.printf("   HTTP %d, body: %s\n", g, js.c_str());
      int idx=js.indexOf(':');
      int e=js.indexOf('}', idx);
      if (idx>0 && e>idx) {
        currentSetpoint = js.substring(idx+1,e).toFloat();
        Serial.printf("   setTemperature=%.1f°C\n", currentSetpoint);
      }
    } else {
      Serial.printf("   GET failed: %s\n", http.errorToString(g).c_str());
    }
    http.end();

    WiFi.disconnect(true);
    delay(100);
  }

  // 5) Re-init ESP-NOW on channel 1
  Serial.println("🔁 Reinitializing ESP-NOW");
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init()!=ESP_OK) {
    Serial.println("❌ ESP-NOW reinit failed"); return;
  }
  esp_now_register_send_cb(onSendStatus);
  esp_now_register_recv_cb(onReceiveData);
  if (esp_now_add_peer(&peerInfo)!=ESP_OK) {
    Serial.println("❌ ESP-NOW peer re-add failed"); return;
  }

  // 6) Send back setpoint to Edge1
  SetpointMsg sp = { currentSetpoint };
  esp_err_t r = esp_now_send(EDGE1_MAC,(uint8_t*)&sp,sizeof(sp));
  Serial.printf("📤 ESP-NOW setpoint send: %s\n", r==ESP_OK?"✅":"❌");
}
