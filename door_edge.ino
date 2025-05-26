#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// —— TFT display instance ——  
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// —— Packed payloads ——  
#pragma pack(push,1)
struct DoorMsg  { bool doorOpen; };
struct EdgeMsg  { uint8_t type; bool seated; };  // type 0=req, 1=resp
#pragma pack(pop)

// —— Peer MAC addresses ——  
uint8_t doorMac[]      = {0x3C,0x84,0x27,0xC7,0x0C,0x04};
uint8_t postureMac[]   = {0xCC,0x7B,0x5C,0x27,0x1E,0x40};

// —— State variables ——  
bool        lastDoorOpen     = false;
bool        waitingBreak     = false;
bool        inBreak          = false;
unsigned    long breakStart  = 0;

// —— Helpers for TFT ——  
void showHeader(const char* t) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10,10);
  tft.print(t);
}

void showDoor(const char* s) {
  showHeader("Door");
  tft.setTextSize(3);
  tft.setCursor(10,50);
  tft.print(s);
}

void showPosture(const char* s) {
  showHeader("Posture");
  tft.setTextSize(3);
  tft.setCursor(10,50);
  tft.print(s);
}

void showBreakStart() {
  showHeader("Break Started");
  tft.setTextSize(2);
  tft.setCursor(10,80);
  tft.print("Timer running...");
}

void showBreakDuration(unsigned long secs, const char* category) {
  showHeader("Break Ended");
  tft.setTextSize(2);
  tft.setCursor(10,50);
  tft.printf("Duration: %lus", secs);
  tft.setCursor(10,85);
  tft.print(category);
}

// —— Serial log callbacks ——  
void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  char m[18];
  snprintf(m,sizeof(m),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  Serial.printf("[TX] %s -> %s\n",
                m,
                status==ESP_NOW_SEND_SUCCESS?"OK":"FAIL");
}

// —— Incoming ESP-NOW handler ——  
void onDataRecv(const esp_now_recv_info_t* info,
                const uint8_t* data, int len) {
  // Figure out who sent us what
  if (len == sizeof(DoorMsg)) {
    DoorMsg m; memcpy(&m,data,len);
    bool open = m.doorOpen;
    Serial.printf("[RX] Door %s\n", open?"OPEN":"CLOSED");
    showDoor(open?"OPEN":"CLOSED");

    // On door-open we ask posture
    if (open) {
      waitingBreak = false;
      DoorMsg dm; // reused struct
      EdgeMsg req = {0,false};
      esp_now_send(postureMac,(uint8_t*)&req,sizeof(req));
      Serial.println("[TX] Posture Request");
    }
    // On door-close: if we had seen a “not seated” response,
    // that means the person just stepped out → start break timer.
    else if (inBreak == false && waitingBreak) {
      waitingBreak = false;
      inBreak     = true;
      breakStart  = millis();
      Serial.println("[BREAK] Started");
      showBreakStart();
    }

    lastDoorOpen = open;
  }
  else if (len == sizeof(EdgeMsg)) {
    EdgeMsg r; memcpy(&r,data,len);
    Serial.printf("[RX] Posture Resp: %s\n", r.seated?"SEATED":"STANDING");
    showPosture(r.seated?"SEATED":"STAND");

    // If they’re not seated, arm the break on next door-close
    if (r.type==1 && !r.seated) {
      waitingBreak = true;
    }
    // If they come back seated *during* a break, end it immediately
    else if (r.type==1 && r.seated && inBreak) {
      inBreak = false;
      unsigned long secs = (millis() - breakStart)/1000;
      const char* cat;
      if (secs < 300)       cat = "Bathroom";
      else if (secs > 1800) cat = "Break";
      else                  cat = "Short Break";

      Serial.printf("[BREAK] Ended: %lus (%s)\n", secs, cat);
      showBreakDuration(secs, cat);
    }
  }
  else {
    Serial.printf("[RX] Unknown %d bytes\n", len);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // TFT init
  pinMode(TFT_BACKLITE, OUTPUT); digitalWrite(TFT_BACKLITE, HIGH);
  pinMode(TFT_I2C_POWER, OUTPUT);digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);
  tft.init(135,240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  showHeader("Initializing...");

  // Wi-Fi + ESP-NOW on channel 1
  WiFi.mode(WIFI_STA); WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  Serial.printf("Channel: %d\n",1);
  Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());

  if (esp_now_init()!=ESP_OK) {
    Serial.println("ESP-NOW failed"); 
    showHeader("ESP-NOW Error"); 
    while(1)delay(500);
  }
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  // Add peers
  esp_now_peer_info_t p = {};
  p.channel = 1; p.encrypt = false;
  memcpy(p.peer_addr,doorMac,6);
  esp_now_add_peer(&p);
  memcpy(p.peer_addr,postureMac,6);
  esp_now_add_peer(&p);

  showHeader("Ready");
  Serial.println("Initialization complete");
}

void loop() {
  // During a break, update the timer every second
  static unsigned long last = 0;
  if (inBreak && millis()-last > 1000) {
    unsigned long secs = (millis()-breakStart)/1000;
    tft.fillRect(0,160,240,20,ST77XX_BLACK);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(10,160);
    tft.printf("Break: %lus", secs);
    last = millis();
  }
}
