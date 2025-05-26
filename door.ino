#include <WiFi.h>
#include <esp_now.h>

// —— Pins ——
#define DOOR_PIN    10    // reed switch (INPUT_PULLUP)
#define BUZZER_PIN  11    // beeper on open only
#define LED_PIN     12    // status LED on any change

// —— Edge device MAC ——
uint8_t edgeMac[6] = { 0x64, 0xE8, 0x33, 0x73, 0xD7, 0x80 };

// —— Payload ——
typedef struct {
  bool doorOpen;
} door_payload_t;

door_payload_t payload;
bool lastDoorOpen;

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(DOOR_PIN,   INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN,    OUTPUT);

  // capture initial state
  lastDoorOpen = (digitalRead(DOOR_PIN) == HIGH);

  // ESP-NOW init
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) { delay(1000); }
  }
  // register peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, edgeMac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add peer");
    while (true) { delay(1000); }
  }

  Serial.println("Door ESP32 ready");
}

void loop() {
  bool doorOpen = (digitalRead(DOOR_PIN) == HIGH);

  // on any transition
  if (doorOpen != lastDoorOpen) {
    // blink LED
    digitalWrite(LED_PIN, HIGH);
    // beep on open
    if (doorOpen) digitalWrite(BUZZER_PIN, HIGH);

    delay(100);

    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    // send new statea
    payload.doorOpen = doorOpen;
    esp_err_t err = esp_now_send(edgeMac, (uint8_t*)&payload, sizeof(payload));
    Serial.printf("Door %s → sent doorOpen=%s (err=%d)\n",
                  doorOpen ? "opened" : "closed",
                  doorOpen ? "true" : "false",
                  err);
  }

  lastDoorOpen = doorOpen;
  delay(50);  // debounce + CPU relief
}
