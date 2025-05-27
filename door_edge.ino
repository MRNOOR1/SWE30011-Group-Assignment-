#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// —— TFT display instance ——
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// —— Peer MACs ——
static const uint8_t doorMac[6]   = {0x3C,0x84,0x27,0xC7,0x0C,0x04};  // Door sensor
static const uint8_t sensorMac[6] = {0x78,0x42,0x1C,0x67,0x71,0x20};  // Desk sensor
static const uint8_t edge2Mac[6]  = {0xCC,0x7B,0x5C,0x27,0x1E,0x40};  // Edge2

#pragma pack(push,1)
struct DoorMsg    { bool doorOpen; };
struct SensorMsg  { bool seated; float temperature; uint16_t co2; };
struct ClimateMsg { uint8_t cmd; };
struct SetpointMsg{ float setpoint; };
struct ReportMsg  { uint16_t seatedSecs; uint16_t breakSecs; float temperature; uint16_t co2; };
#pragma pack(pop)

// —— Setpoint received from Edge2 ——
float setpointTemp = 22.0f;

// —— State ——
bool         doorOpen      = false;
bool         sensorSeated  = true;
float        sensorTemp    = 0;
uint16_t     sensorCO2     = 0;
bool         inBreak       = false;
unsigned long breakStart   = 0;
unsigned long seatedStart  = 0;
uint16_t     lastSeatedSec = 0;

// —— Climate commands enum ——
enum ClimateCmd : uint8_t { CMD_OFF=0, CMD_HEATER=1, CMD_COOLER=2 };

// —— Dashboard ——
void showDashboard() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10,10);
  tft.setTextColor(doorOpen ? ST77XX_GREEN : ST77XX_RED);
  tft.printf("Door: %s", doorOpen?"OPEN":"CLOSED");
  tft.setCursor(10,40);
  tft.setTextColor(sensorSeated?ST77XX_GREEN:ST77XX_RED);
  tft.printf("Seated: %s", sensorSeated?"YES":"NO");
  tft.setCursor(10,70);
  tft.setTextColor(ST77XX_WHITE);
  tft.printf("T:%.1fC CO2:%uppm", sensorTemp, sensorCO2);
  tft.setCursor(10,100);
  if (inBreak) {
    unsigned long secs = (millis()-breakStart)/1000;
    tft.printf("Break: %lus", secs);
  } else {
    tft.print("Break: --");
  }
}

// —— Send climate command to door sensor ——
void sendClimate(ClimateCmd cmd) {
  ClimateMsg m = { uint8_t(cmd) };
  esp_now_send(doorMac, (uint8_t*)&m, sizeof(m));
}

// —— Send break report to Edge2 ——
void sendReport() {
  ReportMsg r = {
    lastSeatedSec,
    uint16_t((millis()-breakStart)/1000),
    sensorTemp,
    sensorCO2
  };
  esp_now_send(edge2Mac, (uint8_t*)&r, sizeof(r));
  Serial.printf("[REPORT] seated=%us break=%us T=%.1fC CO2=%uppm\n",
                r.seatedSecs, r.breakSecs, r.temperature, r.co2);
}

// —— ESP-NOW receive callback ——
void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  const uint8_t* mac = info->src_addr;
  char macStr[18];
  snprintf(macStr,sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

  // Setpoint from Edge2
  if (!memcmp(mac, edge2Mac, 6) && len == sizeof(SetpointMsg)) {
    SetpointMsg sp; memcpy(&sp, data, len);
    setpointTemp = sp.setpoint;
    Serial.printf("[RX] Setpoint=%.1fC\n", setpointTemp);
    return;
  }

  // Door event
  if (!memcmp(mac, doorMac,6) && len==sizeof(DoorMsg)) {
    DoorMsg d; memcpy(&d,data,len);
    doorOpen = d.doorOpen;
    Serial.printf("[RX] Door: %s\n", doorOpen?"OPEN":"CLOSED");
    if (!doorOpen && !sensorSeated && !inBreak) {
      inBreak = true;
      breakStart = millis();
      lastSeatedSec = uint16_t((breakStart-seatedStart)/1000);
      Serial.printf("[SEATED] lasted=%us\n", lastSeatedSec);
      if (sensorTemp < setpointTemp) sendClimate(CMD_HEATER);
      else if (sensorTemp > setpointTemp) sendClimate(CMD_COOLER);
      else sendClimate(CMD_OFF);
    }
    showDashboard(); return;
  }

  // Sensor update
  if (!memcmp(mac, sensorMac,6) && len==sizeof(SensorMsg)) {
    SensorMsg s; memcpy(&s,data,len);
    if (s.seated && !sensorSeated) seatedStart = millis();
    sensorSeated = s.seated;
    sensorTemp   = s.temperature;
    sensorCO2    = s.co2;
    Serial.printf("[RX] Sensor: seated=%s T=%.1fC CO2=%uppm\n",
                  sensorSeated?"YES":"NO",sensorTemp,sensorCO2);
    if (inBreak && sensorSeated) {
      inBreak = false;
      sendReport();
      sendClimate(CMD_OFF);
    }
    showDashboard(); return;
  }
}

void setup() {
  Serial.begin(115200);
  // TFT init
  pinMode(TFT_BACKLITE, OUTPUT); digitalWrite(TFT_BACKLITE, HIGH);
  pinMode(TFT_I2C_POWER, OUTPUT); digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);
  tft.init(135,240); tft.setRotation(3);
  seatedStart = millis();
  showDashboard();

  // Wi-Fi & ESP-NOW
  WiFi.mode(WIFI_STA); WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init()!=ESP_OK) while(1);
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t p={}; p.channel=1; p.encrypt=false;
  memcpy(p.peer_addr,doorMac,6); esp_now_add_peer(&p);
  memcpy(p.peer_addr,sensorMac,6); esp_now_add_peer(&p);
  memcpy(p.peer_addr,edge2Mac,6);  esp_now_add_peer(&p);

  Serial.println("Edge1 ready");
}

void loop() { delay(100); }
