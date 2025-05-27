#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// —— Pins ——
#define DOOR_PIN     10    // Reed switch (INPUT_PULLUP)
#define BUZZER_PIN   11    // Beeper on open
#define LED_PIN      12    // Status LED
#define HEATER_LED    9    // Red LED: Heater
#define COOLER_LED    8    // Blue LED: Cooler

// —— MAC of Edge1 (door-display node) ——
static const uint8_t EDGE1_MAC[6] = { 0x64, 0xE8, 0x33, 0x73, 0xD7, 0x80 };

// —— Payload types ——
#pragma pack(push,1)
struct DoorMsg {
  bool doorOpen;
};

enum ClimateCmd : uint8_t {
  CMD_OFF    = 0,
  CMD_HEATER = 1,
  CMD_COOLER = 2
};

struct ClimateMsg {
  ClimateCmd cmd;
};
#pragma pack(pop)

// —— State ——
bool lastDoorOpen;

// —— Handle incoming climate commands from Edge1 ——
void onClimateRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
  if (len != sizeof(ClimateMsg)) return;
  ClimateMsg m;
  memcpy(&m, data, sizeof(m));
  // Turn off both outputs
  digitalWrite(HEATER_LED, LOW);
  digitalWrite(COOLER_LED, LOW);
  // Apply command
  switch (m.cmd) {
    case CMD_HEATER:
      digitalWrite(HEATER_LED, HIGH);
      Serial.println("Climate: HEATER ON");
      break;
    case CMD_COOLER:
      digitalWrite(COOLER_LED, HIGH);
      Serial.println("Climate: COOLER ON");
      break;
    case CMD_OFF:
    default:
      Serial.println("Climate: OFF");
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Configure pins
  pinMode(DOOR_PIN,   INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN,    OUTPUT);
  pinMode(HEATER_LED, OUTPUT);
  pinMode(COOLER_LED, OUTPUT);

  // Initialize LEDs off
  digitalWrite(HEATER_LED, LOW);
  digitalWrite(COOLER_LED, LOW);

  // Read initial door state
  lastDoorOpen = (digitalRead(DOOR_PIN) == HIGH);

  // Wi-Fi and ESP-NOW setup on channel 1
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) delay(500);
  }

  // Register callback for climate control messages
  esp_now_register_recv_cb(onClimateRecv);

  // Add Edge1 as peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, EDGE1_MAC, 6);
  peer.channel = 1;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add Edge1 peer");
    while (true) delay(500);
  }

  Serial.println("Door sensor ready with climate control");
}

void loop() {
  bool doorOpen = (digitalRead(DOOR_PIN) == HIGH);

  // On state change
  if (doorOpen != lastDoorOpen) {
    // Blink status LED
    digitalWrite(LED_PIN, HIGH);
    if (doorOpen) digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    // Send door state to Edge1
    DoorMsg msg = { doorOpen };
    esp_err_t err = esp_now_send(EDGE1_MAC, (uint8_t*)&msg, sizeof(msg));
    Serial.printf("Door %s → sent state=%s (err=%d)\n",
                  doorOpen?"OPENED":"CLOSED",
                  doorOpen?"true":"false",
                  err);
  }
  lastDoorOpen = doorOpen;

  delay(50); // debounce + CPU relief
}
