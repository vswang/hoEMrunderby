// #include <Arduino.h>
// #include <Wire.h>
// #include <Adafruit_MCP23X17.h>

// Adafruit_MCP23X17 mcp;

// #define PITCH_NOW_BUTTON_PIN  11  // B3
// #define PITCH_1_BUTTON_PIN    12  // B4
// #define PITCH_2_BUTTON_PIN    13  // B5
// #define PITCH_3_BUTTON_PIN    14  // B6
// #define RESET_BUTTON_PIN      15  // B7

// void setup() {
//   Serial.begin(115200);
//   delay(1500);

//   Wire.begin(A4, A5);

//   if (!mcp.begin_I2C()) {
//     Serial.println("MCP FAILED");
//     while (1) delay(10);
//   }

//   Serial.println("MCP OK");

//   mcp.pinMode(PITCH_NOW_BUTTON_PIN, INPUT_PULLUP);
//   mcp.pinMode(PITCH_1_BUTTON_PIN, INPUT_PULLUP);
//   mcp.pinMode(PITCH_2_BUTTON_PIN, INPUT_PULLUP);
//   mcp.pinMode(PITCH_3_BUTTON_PIN, INPUT_PULLUP);
//   mcp.pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
// }

// void loop() {
//   Serial.print("PitchNow=");
//   Serial.print(mcp.digitalRead(PITCH_NOW_BUTTON_PIN));

//   Serial.print(" P1=");
//   Serial.print(mcp.digitalRead(PITCH_1_BUTTON_PIN));

//   Serial.print(" P2=");
//   Serial.print(mcp.digitalRead(PITCH_2_BUTTON_PIN));

//   Serial.print(" P3=");
//   Serial.print(mcp.digitalRead(PITCH_3_BUTTON_PIN));

//   Serial.print(" Reset=");
//   Serial.println(mcp.digitalRead(RESET_BUTTON_PIN));

//   delay(200);
// }
#include <Arduino.h>
#include "servo_control.h"
#include <esp_now.h>
#include <WiFi.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"
#include <Wire.h>
#include <Adafruit_MCP23X17.h>

// =====================================================
// ESP32 PINS
// =====================================================

static const int SERVO_PIN = 9; // D6

static const int DF_RX = D8; // ESP RX ← DFPlayer TX
static const int DF_TX = D9; // ESP TX → DFPlayer RX

static const int I2C_SDA_PIN = A4;
static const int I2C_SCL_PIN = A5;

HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfplayer;

Adafruit_MCP23X17 mcp;
Adafruit_7segment matrix = Adafruit_7segment();

// =====================================================
// MCP23017 PIN MAP
// A0..A7 = 0..7
// B0..B7 = 8..15
// =====================================================

// Port A: pocket sensors
#define SINGLE1_PIN  0
#define SINGLE2_PIN  1
#define DOUBLE1_PIN  2
#define DOUBLE2_PIN  3
#define TRIPLE1_PIN  4
#define TRIPLE2_PIN  5
#define HOMER_PIN    6
#define OUT_PIN      7

// Port B: base LEDs
#define FIRST_BASE_LED   8
#define SECOND_BASE_LED  9
#define THIRD_BASE_LED   10

// Port B: physical arcade buttons
#define PITCH_NOW_BUTTON_PIN  11  // B3
#define PITCH_1_BUTTON_PIN    12  // B4
#define PITCH_2_BUTTON_PIN    13  // B5
#define PITCH_3_BUTTON_PIN    14  // B6
#define RESET_BUTTON_PIN      15  // B7

// =====================================================
// GAME STATE
// =====================================================

enum class GameState {
  IDLE,
  PITCHING,
  READY_TO_SWING,
  WAITING_FOR_RESULT,
  OUT,
  RETURN_SERVO,
  GAME_OVER,
  ERROR_STATE
};

static GameState state = GameState::IDLE;

void enterState(GameState newState);

// =====================================================
// BASEBALL STATE
// =====================================================

static byte bases = 0;
static unsigned char runs = 0;
static unsigned char outs = 0;
static char hit_type[5] = "SDTH";

static int selectedPitch = 1;

// =====================================================
// ESPNOW / SWING INPUT
// =====================================================

typedef struct struct_message {
  int velo;
  int swing;
} struct_message;

struct_message myData;

static volatile bool swingSignal = false;
static volatile int velo = 0;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));

  Serial.print("Velocity: ");
  Serial.println(myData.velo);
  Serial.print("Swing: ");
  Serial.println(myData.swing);
  Serial.println();

  velo = myData.velo;
  swingSignal = (myData.swing != 0);
}

// =====================================================
// EDGE TRACKING
// =====================================================

static bool prevSingle1Blocked = false;
static bool prevSingle2Blocked = false;
static bool prevDouble1Blocked = false;
static bool prevDouble2Blocked = false;
static bool prevTriple1Blocked = false;
static bool prevTriple2Blocked = false;
static bool prevHomerBlocked = false;
static bool prevOutBlocked = false;

static bool prevPitchNowPressed = false;
static bool prevPitch1Pressed = false;
static bool prevPitch2Pressed = false;
static bool prevPitch3Pressed = false;
static bool prevResetPressed = false;

// =====================================================
// TIMERS
// =====================================================

static unsigned long pitchStartTime = 0;
static const unsigned long PITCH_DELAY_MS = 2000;

static unsigned long readySwingStartTime = 0;
static const unsigned long SWING_TIMEOUT_MS = 3000;

static unsigned long resultStartTime = 0;
static const unsigned long RESULT_TIMEOUT_MS = 10000;

// =====================================================
// ROUND STATE
// =====================================================

static bool swingOccurred = false;
static bool errorAfterServoReturn = false;

// =====================================================
// HELPERS
// =====================================================

int countBits(byte x) {
  int count = 0;
  while (x) {
    count += x & 1;
    x >>= 1;
  }
  return count;
}

bool single1Blocked() { return mcp.digitalRead(SINGLE1_PIN) == LOW; }
bool single2Blocked() { return mcp.digitalRead(SINGLE2_PIN) == LOW; }
bool double1Blocked() { return mcp.digitalRead(DOUBLE1_PIN) == LOW; }
bool double2Blocked() { return mcp.digitalRead(DOUBLE2_PIN) == LOW; }
bool triple1Blocked() { return mcp.digitalRead(TRIPLE1_PIN) == LOW; }
bool triple2Blocked() { return mcp.digitalRead(TRIPLE2_PIN) == LOW; }
bool homerBlocked()   { return mcp.digitalRead(HOMER_PIN) == LOW; }
bool outBlocked()     { return mcp.digitalRead(OUT_PIN) == LOW; }

bool pitchNowPressed() { return mcp.digitalRead(PITCH_NOW_BUTTON_PIN) == LOW; }
bool pitch1Pressed()   { return mcp.digitalRead(PITCH_1_BUTTON_PIN) == LOW; }
bool pitch2Pressed()   { return mcp.digitalRead(PITCH_2_BUTTON_PIN) == LOW; }
bool pitch3Pressed()   { return mcp.digitalRead(PITCH_3_BUTTON_PIN) == LOW; }
bool resetPressed()    { return mcp.digitalRead(RESET_BUTTON_PIN) == LOW; }

void updateBaseLeds() {
  mcp.digitalWrite(FIRST_BASE_LED,  (bases & 0b001) ? HIGH : LOW);
  mcp.digitalWrite(SECOND_BASE_LED, (bases & 0b010) ? HIGH : LOW);
  mcp.digitalWrite(THIRD_BASE_LED,  (bases & 0b100) ? HIGH : LOW);
}

void displayScore() {
  matrix.clear();

  if (runs > 9) {
    matrix.writeDigitNum(0, runs / 10);
  }

  matrix.writeDigitNum(1, runs % 10);
  matrix.drawColon(false);
  matrix.writeDigitNum(4, outs);
  matrix.writeDisplay();
}

void printScoreboard() {
  Serial.print("Runs: ");
  Serial.println(runs);
  Serial.print("Outs: ");
  Serial.println(outs);
  Serial.print("Bases: ");
  Serial.println(bases, BIN);
  Serial.println();

  updateBaseLeds();
  displayScore();
}

void resetScoreboard() {
  bases = 0;
  runs = 0;
  outs = 0;
  updateBaseLeds();
  displayScore();
}

void fullManualReset() {
  resetScoreboard();

  swingSignal = false;
  swingOccurred = false;
  errorAfterServoReturn = false;
  velo = 0;
  selectedPitch = 1;

  servoSetAngle(90.0f);

  prevSingle1Blocked = single1Blocked();
  prevSingle2Blocked = single2Blocked();
  prevDouble1Blocked = double1Blocked();
  prevDouble2Blocked = double2Blocked();
  prevTriple1Blocked = triple1Blocked();
  prevTriple2Blocked = triple2Blocked();
  prevHomerBlocked = homerBlocked();
  prevOutBlocked = outBlocked();

  prevPitchNowPressed = pitchNowPressed();
  prevPitch1Pressed = pitch1Pressed();
  prevPitch2Pressed = pitch2Pressed();
  prevPitch3Pressed = pitch3Pressed();
  prevResetPressed = resetPressed();

  enterState(GameState::IDLE);
}

// =====================================================
// AUDIO
// =====================================================

void playAudioSingle()  { Serial.println("[AUDIO] Single");   dfplayer.play(1); }
void playAudioDouble()  { Serial.println("[AUDIO] Double");   dfplayer.play(2); }
void playAudioTriple()  { Serial.println("[AUDIO] Triple");   dfplayer.play(3); }
void playAudioHomeRun() { Serial.println("[AUDIO] Home Run"); dfplayer.play(4); }
void playAudioOut()     { Serial.println("[AUDIO] Out");      dfplayer.play(5); }

// =====================================================
// BASEBALL SCORING
// type: 0 single, 1 double, 2 triple, 3 home run
// =====================================================

void hit(int type) {
  Serial.println("Hit detected!");

  if (type == 3) {
    runs += countBits(bases) + 1;
    bases = 0;
  } else {
    byte scoreMask = 0b1111000;

    bases <<= (type + 1);
    bases += (0b1 << type);

    byte overflow = bases & scoreMask;
    runs += countBits(overflow);
    bases &= 0x7;
  }

  Serial.print("Hit Type: ");
  Serial.println(hit_type[type]);
  printScoreboard();
}

void returnServoOrIdle() {
  if (swingOccurred) {
    servoMoveTo(90.0f, 25.0f);
    enterState(GameState::RETURN_SERVO);
  } else {
    enterState(GameState::IDLE);
  }
}

// =====================================================
// STATE ENTRY
// =====================================================

void enterState(GameState newState) {
  state = newState;

  if (state == GameState::GAME_OVER) {
    Serial.println("State: GAME_OVER");
    Serial.println("GAME OVER! Press reset button or 'r' to restart.");
  }
  else if (state == GameState::IDLE) {
    swingOccurred = false;
    errorAfterServoReturn = false;
    Serial.println("State: IDLE");
    Serial.println("Press pitch-now button or 'p' to pitch");
  }
  else if (state == GameState::PITCHING) {
    pitchStartTime = millis();
    swingOccurred = false;
    errorAfterServoReturn = false;

    Serial.print("State: PITCHING");
    Serial.print(" | Selected pitch = ");
    Serial.println(selectedPitch);
    Serial.println("Pitch released, waiting before swing...");
  }
  else if (state == GameState::READY_TO_SWING) {
    readySwingStartTime = millis();
    Serial.println("BALL HAS BEEN PITCHED");
    Serial.println("State: READY_TO_SWING");
    Serial.println("Waiting for wireless swing...");
  }
  else if (state == GameState::WAITING_FOR_RESULT) {
    resultStartTime = millis();

    prevSingle1Blocked = single1Blocked();
    prevSingle2Blocked = single2Blocked();
    prevDouble1Blocked = double1Blocked();
    prevDouble2Blocked = double2Blocked();
    prevTriple1Blocked = triple1Blocked();
    prevTriple2Blocked = triple2Blocked();
    prevHomerBlocked = homerBlocked();
    prevOutBlocked = outBlocked();

    Serial.println("State: WAITING_FOR_RESULT");
  }
  else if (state == GameState::OUT) {
    Serial.println("State: OUT");
    Serial.println("Ball fell into out pocket.");
    outs += 1;
    printScoreboard();

    if (outs >= 3) {
      enterState(GameState::GAME_OVER);
    } else {
      returnServoOrIdle();
    }
  }
  else if (state == GameState::RETURN_SERVO) {
    Serial.println("State: RETURN_SERVO");
  }
  else if (state == GameState::ERROR_STATE) {
    Serial.println("State: ERROR");
    Serial.println("No score pocket or out pocket detected before timeout.");
    Serial.println("Press pitch-now button or 'p' to try again.");
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(2000);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
  } else {
    esp_now_register_recv_cb(OnDataRecv);
  }

  servoSetup(SERVO_PIN);
  servoSetAngle(90.0f);

  dfSerial.begin(9600, SERIAL_8N1, DF_RX, DF_TX);
  delay(2000);

  if (!dfplayer.begin(dfSerial, false, true)) {
    Serial.println("DFPlayer begin FAILED");
  } else {
    Serial.println("DFPlayer begin OK");
    dfplayer.volume(25);
  }

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  if (!mcp.begin_I2C()) {
    Serial.println("Error initializing MCP23017");
    while (1) { delay(10); }
  }

  if (!matrix.begin(0x70)) {
    Serial.println("Seven segment display not found. Check wiring!");
  }
  matrix.setBrightness(15);

  // Pocket sensors on Port A
  mcp.pinMode(SINGLE1_PIN, INPUT_PULLUP);
  mcp.pinMode(SINGLE2_PIN, INPUT_PULLUP);
  mcp.pinMode(DOUBLE1_PIN, INPUT_PULLUP);
  mcp.pinMode(DOUBLE2_PIN, INPUT_PULLUP);
  mcp.pinMode(TRIPLE1_PIN, INPUT_PULLUP);
  mcp.pinMode(TRIPLE2_PIN, INPUT_PULLUP);
  mcp.pinMode(HOMER_PIN, INPUT_PULLUP);
  mcp.pinMode(OUT_PIN, INPUT_PULLUP);

  // Base LEDs on Port B
  mcp.pinMode(FIRST_BASE_LED, OUTPUT);
  mcp.pinMode(SECOND_BASE_LED, OUTPUT);
  mcp.pinMode(THIRD_BASE_LED, OUTPUT);

  mcp.digitalWrite(FIRST_BASE_LED, LOW);
  mcp.digitalWrite(SECOND_BASE_LED, LOW);
  mcp.digitalWrite(THIRD_BASE_LED, LOW);

  // Physical arcade buttons on Port B
  mcp.pinMode(PITCH_NOW_BUTTON_PIN, INPUT_PULLUP);
  mcp.pinMode(PITCH_1_BUTTON_PIN, INPUT_PULLUP);
  mcp.pinMode(PITCH_2_BUTTON_PIN, INPUT_PULLUP);
  mcp.pinMode(PITCH_3_BUTTON_PIN, INPUT_PULLUP);
  mcp.pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  prevSingle1Blocked = single1Blocked();
  prevSingle2Blocked = single2Blocked();
  prevDouble1Blocked = double1Blocked();
  prevDouble2Blocked = double2Blocked();
  prevTriple1Blocked = triple1Blocked();
  prevTriple2Blocked = triple2Blocked();
  prevHomerBlocked = homerBlocked();
  prevOutBlocked = outBlocked();

  prevPitchNowPressed = pitchNowPressed();
  prevPitch1Pressed = pitch1Pressed();
  prevPitch2Pressed = pitch2Pressed();
  prevPitch3Pressed = pitch3Pressed();
  prevResetPressed = resetPressed();

  Serial.println("Game starting...");
  Serial.println("Buttons: pitch 1 / pitch 2 / pitch 3 / pitch now / reset");
  printScoreboard();
  enterState(GameState::IDLE);
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  servoUpdate();

  // ---------------------------------
  // PHYSICAL BUTTON INPUTS
  // ---------------------------------

  bool pitch1Now = pitch1Pressed();
  bool pitch2Now = pitch2Pressed();
  bool pitch3Now = pitch3Pressed();
  bool pitchNowNow = pitchNowPressed();
  bool resetNow = resetPressed();

  if (resetNow && !prevResetPressed) {
    Serial.println("Reset button pressed.");
    fullManualReset();
  }

  if (pitch1Now && !prevPitch1Pressed) {
    selectedPitch = 1;
    Serial.println("Selected pitch: 1");
  }
  else if (pitch2Now && !prevPitch2Pressed) {
    selectedPitch = 2;
    Serial.println("Selected pitch: 2");
  }
  else if (pitch3Now && !prevPitch3Pressed) {
    selectedPitch = 3;
    Serial.println("Selected pitch: 3");
  }
  else if (pitchNowNow && !prevPitchNowPressed &&
           (state == GameState::IDLE || state == GameState::ERROR_STATE)) {
    Serial.print("Pitch now pressed. Selected pitch = ");
    Serial.println(selectedPitch);
    enterState(GameState::PITCHING);
  }

  prevPitch1Pressed = pitch1Now;
  prevPitch2Pressed = pitch2Now;
  prevPitch3Pressed = pitch3Now;
  prevPitchNowPressed = pitchNowNow;
  prevResetPressed = resetNow;

  // ---------------------------------
  // SERIAL COMMANDS
  // p = pitch
  // r = reset
  // w = manual swing
  // s,d,t,h,o = manual result while waiting
  // ---------------------------------

  if (Serial.available() > 0) {
    char cmd = Serial.read();

    if ((cmd == 'p' || cmd == 'P') &&
        (state == GameState::IDLE || state == GameState::ERROR_STATE)) {
      Serial.print("Pitch command received. Selected pitch = ");
      Serial.println(selectedPitch);
      enterState(GameState::PITCHING);
    }
    else if ((cmd == 'w' || cmd == 'W') && state == GameState::READY_TO_SWING) {
      Serial.println("Manual swing received");
      swingOccurred = true;
      servoMoveTo(270.0f, 25.0f);
      enterState(GameState::WAITING_FOR_RESULT);
    }
    else if (cmd == 'r' || cmd == 'R') {
      fullManualReset();
      Serial.println("Game reset.");
    }
    else if (state == GameState::WAITING_FOR_RESULT) {
      if (cmd == 's') {
        playAudioSingle();
        hit(0);
        returnServoOrIdle();
      }
      else if (cmd == 'd') {
        playAudioDouble();
        hit(1);
        returnServoOrIdle();
      }
      else if (cmd == 't') {
        playAudioTriple();
        hit(2);
        returnServoOrIdle();
      }
      else if (cmd == 'h') {
        playAudioHomeRun();
        hit(3);
        returnServoOrIdle();
      }
      else if (cmd == 'o') {
        playAudioOut();
        enterState(GameState::OUT);
      }
    }
  }

  // ---------------------------------
  // WIRELESS SWING
  // ---------------------------------

  if (swingSignal && state == GameState::READY_TO_SWING) {
    Serial.println("Wireless swing received");
    swingOccurred = true;
    swingSignal = false;
    servoMoveTo(270.0f, 25.0f);
    enterState(GameState::WAITING_FOR_RESULT);
  }

  // ---------------------------------
  // STATE MACHINE
  // ---------------------------------

  if (state == GameState::PITCHING) {
    if (millis() - pitchStartTime >= PITCH_DELAY_MS) {
      enterState(GameState::READY_TO_SWING);
    }
  }
  else if (state == GameState::READY_TO_SWING) {
    if (millis() - readySwingStartTime >= SWING_TIMEOUT_MS) {
      Serial.println("No swing entered in time. Watching for ball result...");
      enterState(GameState::WAITING_FOR_RESULT);
    }
  }
  else if (state == GameState::WAITING_FOR_RESULT) {
    bool single1Now = single1Blocked();
    bool single2Now = single2Blocked();
    bool double1Now = double1Blocked();
    bool double2Now = double2Blocked();
    bool triple1Now = triple1Blocked();
    bool triple2Now = triple2Blocked();
    bool homerNow = homerBlocked();
    bool outNow = outBlocked();

    if ((single1Now && !prevSingle1Blocked) ||
        (single2Now && !prevSingle2Blocked)) {
      playAudioSingle();
      hit(0);
      returnServoOrIdle();
    }
    else if ((double1Now && !prevDouble1Blocked) ||
             (double2Now && !prevDouble2Blocked)) {
      playAudioDouble();
      hit(1);
      returnServoOrIdle();
    }
    else if ((triple1Now && !prevTriple1Blocked) ||
             (triple2Now && !prevTriple2Blocked)) {
      playAudioTriple();
      hit(2);
      returnServoOrIdle();
    }
    else if (homerNow && !prevHomerBlocked) {
      playAudioHomeRun();
      hit(3);
      returnServoOrIdle();
    }
    else if (outNow && !prevOutBlocked) {
      playAudioOut();
      enterState(GameState::OUT);
    }
    else if (millis() - resultStartTime >= RESULT_TIMEOUT_MS) {
      if (swingOccurred) {
        errorAfterServoReturn = true;
        servoMoveTo(90.0f, 25.0f);
        enterState(GameState::RETURN_SERVO);
      } else {
        enterState(GameState::ERROR_STATE);
      }
    }

    prevSingle1Blocked = single1Now;
    prevSingle2Blocked = single2Now;
    prevDouble1Blocked = double1Now;
    prevDouble2Blocked = double2Now;
    prevTriple1Blocked = triple1Now;
    prevTriple2Blocked = triple2Now;
    prevHomerBlocked = homerNow;
    prevOutBlocked = outNow;
  }
  else if (state == GameState::RETURN_SERVO) {
    if (servoAtTarget()) {
      if (errorAfterServoReturn) {
        errorAfterServoReturn = false;
        enterState(GameState::ERROR_STATE);
      } else {
        enterState(GameState::IDLE);
      }
    }
  }
}