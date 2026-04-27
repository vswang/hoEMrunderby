#include <WiFi.h>

#define PITCH_NOW 8 // D5
#define FASTBALL_PIN 9 // D6
#define CHANGEUP_PIN 10 // D7
#define START_STOP_PIN 17 // D8

// --- Configuration ---
const char* ssid     = "FluidNC";
const char* password = "hyongkim";
const char* dlc32_ip = "192.168.0.1"; // Change to your DLC32 IP
const uint16_t port  = 23;            // Standard Telnet Port for FluidNC

enum class pitch_type { // add more pitches here as needed
  NONE,
  FASTBALL,
  CHANGEUP,
  START_STOP
};

static pitch_type selected_pitch = pitch_type::NONE;

// G-code File Pitch Commands --> make sure that file names match
const char* fastball   = "$LocalFS/Run=fastball.gcode";
const char* changeup   = "$LocalFS/Run=changeup.gcode";
const char* start_stop = "$LocalFS/Run=start_stop.gcode";

WiFiClient client;
bool isHomed = false;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected to DLC32!");

  pinMode(PITCH_NOW, INPUT_PULLUP);
  pinMode(FASTBALL_PIN, INPUT_PULLUP);
  pinMode(CHANGEUP_PIN, INPUT_PULLUP);
  pinMode(START_STOP_PIN, INPUT_PULLUP);
}

void loop() {
  if (!client.connected()) {
    if (client.connect(dlc32_ip, port)) {
      Serial.println("Telnet Connected!");
      // Home the machine immediately upon connection
      Serial.println("Homing Gantry...");
      client.println("$H"); 
    } else {
      delay(2000);
      return;
    }
  }

  if (digitalRead(PITCH_NOW) == LOW) {
    switch (selected_pitch) {
      case pitch_type::FASTBALL:
        Serial.println("Fastball released.");
        client.println(fastball); 
        break;

      case pitch_type::CHANGEUP:
        Serial.println("Changeup released.");
        client.println(changeup); 
        break;

      case pitch_type::START_STOP:
        Serial.println("Start-stop released.");
        client.println(start_stop); 
        break;

      case pitch_type::NONE:
        Serial.println("No pitch selected.");
        break;
    }
    selected_pitch = pitch_type::NONE;
    // waiting for debounce
    while (digitalRead(PITCH_NOW) == LOW) {
      delay(10); 
    }
  }

  if (digitalRead(FASTBALL) == LOW) {
    selected_pitch = pitch_type::FASTBALL;
    Serial.println("Fastball selected.")
    while (digitalRead(FASTBALL) == LOW) {
      delay(10); 
    }
  }

  else if (digitalRead(CHANGEUP) == LOW) {
    selected_pitch = pitch_type::CHANGEUP;
    Serial.println("Changeup selected.")
    while (digitalRead(CHANGEUP) == LOW) {
      delay(10); 
    }
  }

  else if (digitalRead(START_STOP) == LOW) {
    selected_pitch = pitch_type::START_STOP;
    Serial.println("Start-stop selected.")
    while (digitalRead(START_STOP) == LOW) {
      delay(10); 
    }
  }

  // Example: Move X by 10mm when you type 'm' in Serial Monitor
  if (Serial.available()) {
    char inChar = Serial.read();
    if (inChar == 'm') {
      Serial.println("Moving X 10mm");
      // G91 = Relative mode, G0 = Rapid move, X10 = 10mm, F1000 = Speed
      client.println("G91 G0 X10 F1000"); 
    }
    if (inChar == 'h') {
      Serial.println("Homing Gantry...");
      // G91 = Relative mode, G0 = Rapid move, X10 = 10mm, F1000 = Speed
      client.println("$H"); 
    }
  }

  // Print feedback from DLC32 (like 'ok' or 'ALARM')
  if (client.available()) {
    String response = client.readStringUntil('\n');
    Serial.println("DLC32: " + response);
  }
}
