#include <Arduino.h>
#include "servo_control.h"
#include <esp_now.h>
#include <WiFi.h>

// p starts pitch
// after pitch delay, go to READY_TO_SWING
// if swing is received in time, servo swings and then waits for result
// if swing is not received in time, it still goes to WAITING_FOR_RESULT so the ball
// can naturally fall into the out pocket in WAITING_FOR_RESULT:
// pocket 1 -> points
// pocket 2 -> points
// out pocket -> OUT state, 0 points
// nothing detected by timeout -> ERROR_STATE

// breakbeams on 5V from external power
// servo on 7V 3A separate power


static const int SERVO_PIN   = D6;
static const int POCKET1_PIN = D2;
static const int POCKET2_PIN = D4;
static const int OUT_PIN     = D5;

static const int POCKET1_POINTS = 10;
static const int POCKET2_POINTS = 20;

enum class GameState {
  IDLE,
  PITCHING,
  READY_TO_SWING,
  WAITING_FOR_RESULT,
  OUT,
  RESETTING,
  GAME_OVER,
  ERROR_STATE
};

static byte bases = 0;   // 8-bit base state
static unsigned char baseballScore = 0;
static unsigned char outs = 0;
static char hit_type[5] = "SDTH";

// receiving the wireless signal
typedef struct struct_message {
  int velo;
  int swing;
} struct_message;

struct_message myData;

static volatile bool swingSignal = false;   // CHANGED: made volatile and initialized
static volatile int velo = 0;               // CHANGED: made volatile and initialized

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.print("Velocity: ");
  Serial.println(myData.velo);
  Serial.print("Swing: ");
  Serial.println(myData.swing);
  Serial.println();

  velo = myData.velo;
  swingSignal = (myData.swing != 0);        // CHANGED: explicitly convert to bool
}

static GameState state = GameState::IDLE;

// edge detect for sensors
static bool prevPocket1Blocked = false;
static bool prevPocket2Blocked = false;
static bool prevOutBlocked     = false;

// timers
static unsigned long pitchStartTime = 0;
static const unsigned long PITCH_DELAY_MS = 2000;

static unsigned long readySwingStartTime = 0;
static const unsigned long SWING_TIMEOUT_MS = 3000;

static unsigned long resultStartTime = 0;
static const unsigned long RESULT_TIMEOUT_MS = 10000;

// whether the servo actually swung this round
static bool swingOccurred = false;

// score
static int totalScore = 0;

// baseball scoreboard state
int countBits(byte x) {
  int count = 0;
  while (x) {
    count += x & 1;
    x >>= 1;
  }
  return count;
}

void printScoreboard() {
  Serial.print("Runs: ");
  Serial.println(baseballScore);
  Serial.print("Outs: ");
  Serial.println(outs);
  Serial.print("Bases: ");
  Serial.println(bases, BIN);
  Serial.println();
}

void resetScoreboard() {
  bases = 0;
  baseballScore = 0;
  outs = 0;
}

bool pocket1Blocked() {
  return digitalRead(POCKET1_PIN) == LOW;
}

bool pocket2Blocked() {
  return digitalRead(POCKET2_PIN) == LOW;
}

bool outBlocked() {
  return digitalRead(OUT_PIN) == LOW;
}

void enterState(GameState newState) {
  state = newState;

  if (state == GameState::GAME_OVER) {
    Serial.println("State: GAME_OVER");
    Serial.println("GAME OVER! Press 'r' to restart.");
  }
  else if (state == GameState::IDLE) {
    swingOccurred = false;
    Serial.println("State: IDLE");
    Serial.println("Press p to pitch");
  }
  else if (state == GameState::PITCHING) {
    pitchStartTime = millis();
    swingOccurred = false;
    Serial.println("State: PITCHING");
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

    // clear stale edges when result monitoring starts
    prevPocket1Blocked = pocket1Blocked();
    prevPocket2Blocked = pocket2Blocked();
    prevOutBlocked     = outBlocked();

    Serial.println("State: WAITING_FOR_RESULT");
  }
  else if (state == GameState::OUT) {
    Serial.println("State: OUT");
    Serial.println("Ball fell into out pocket. 0 points.");
    outs += 1;
    printScoreboard();
    if (outs >= 3) {
      enterState(GameState::GAME_OVER);
    } 
    else if (swingOccurred) {
      servoMoveTo(0.0f, 25.0f);             // CHANGED: faster reset step
      enterState(GameState::RESETTING);
    } 
    else {
      enterState(GameState::IDLE);
    }
  }
  else if (state == GameState::RESETTING) {
    Serial.println("State: RESETTING");
  }
  else if (state == GameState::ERROR_STATE) {
    Serial.println("State: ERROR");
    Serial.println("No score pocket or out pocket detected before timeout.");
    Serial.println("Press p to try again.");
  }
}

void hit(int type) {
  Serial.println("Hit detected!");

  byte scoreMask = 0b1111000;

  bases <<= (type + 1);
  bases += (0b1 << type);

  byte overflow = bases & scoreMask;
  int runs = countBits(overflow);
  baseballScore += runs;
  bases &= 0x7;

  Serial.print("Type: ");
  Serial.println(hit_type[type]);
  Serial.print("Runs scored: ");
  Serial.println(runs);
  Serial.print("Total score: ");
  Serial.println(baseballScore);
  Serial.print("Outs: ");
  Serial.println(outs);
  Serial.print("Bases: ");
  Serial.println(bases, BIN);
  Serial.println();

  enterState(GameState::RESETTING);
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  servoSetup(SERVO_PIN);
  servoSetAngle(0.0f);

  pinMode(POCKET1_PIN, INPUT_PULLUP);
  pinMode(POCKET2_PIN, INPUT_PULLUP);
  pinMode(OUT_PIN, INPUT_PULLUP);

  prevPocket1Blocked = pocket1Blocked();
  prevPocket2Blocked = pocket2Blocked();
  prevOutBlocked     = outBlocked();

  Serial.println("Game starting...");
  // Serial.print("Total score: ");
  // Serial.println(totalScore);
  printScoreboard();

  enterState(GameState::IDLE);
}

void loop() {
  // serial commands:
  // p = start pitch
  // r = reset score

  if (Serial.available() > 0) {
    char cmd = Serial.read();

    if ((cmd == 'p' || cmd == 'P') &&
        (state == GameState::IDLE || state == GameState::ERROR_STATE)) {
      Serial.println("Pitch command received");
      enterState(GameState::PITCHING);
    }
    else if (cmd == 'r' || cmd == 'R') {
      // totalScore = 0;
      resetScoreboard();
      enterState(GameState::IDLE);
      Serial.println("Game reset.");
    }
    // THIS STILL NEEDS TO BE INTEGRATED WITH POCKET DETECTION
    else if (state == GameState::WAITING_FOR_RESULT) {
      if (cmd == 's') {
        hit(0);
      } else if (cmd == 'd') {
        hit(1);
      } else if (cmd == 't') {
        hit(2);
      } else if (cmd == 'h') {
        hit(3);
      } else if (cmd == 'o') {
        enterState(GameState::OUT);
      }
    }
  }

  // CHANGED: wireless swing check moved OUTSIDE serial block
  // so it can trigger even when no serial character is present
  if (swingSignal && state == GameState::READY_TO_SWING) {
    Serial.println("Wireless swing received");
    swingOccurred = true;
    swingSignal = false;                    // CHANGED: clear latched event immediately
    servoMoveTo(270.0f, 25.0f);            // CHANGED: faster swing step
    enterState(GameState::WAITING_FOR_RESULT);
  }

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
    bool p1Blocked = pocket1Blocked();
    bool p2Blocked = pocket2Blocked();
    bool outNow    = outBlocked();

    if (p1Blocked && !prevPocket1Blocked) {
      // totalScore += POCKET1_POINTS;
      // Serial.print("HIT: Pocket 1 triggered, +");
      // Serial.print(POCKET1_POINTS);
      // Serial.println(" points");
      // Serial.print("Total score: ");
      // Serial.println(totalScore);
      hit(0); // use hit() function to handle scoring and state transition

      if (swingOccurred) {
        servoMoveTo(0.0f, 25.0f);          // CHANGED: faster reset step
        enterState(GameState::RESETTING);
      } else {
        enterState(GameState::IDLE);
      }
    }
    else if (p2Blocked && !prevPocket2Blocked) {
      // totalScore += POCKET2_POINTS;
      // Serial.print("HIT: Pocket 2 triggered, +");
      // Serial.print(POCKET2_POINTS);
      // Serial.println(" points");
      // Serial.print("Total score: ");
      // Serial.println(totalScore);
      hit(1);

      if (swingOccurred) {
        servoMoveTo(0.0f, 25.0f);          // CHANGED: faster reset step
        enterState(GameState::RESETTING);
      } else {
        enterState(GameState::IDLE);
      }
    }
    else if (outNow && !prevOutBlocked) {
      enterState(GameState::OUT);
    }
    else if (millis() - resultStartTime >= RESULT_TIMEOUT_MS) {
      if (swingOccurred) {
        servoMoveTo(0.0f, 25.0f);          // CHANGED: faster reset step
      }
      enterState(GameState::ERROR_STATE);
    }

    prevPocket1Blocked = p1Blocked;
    prevPocket2Blocked = p2Blocked;
    prevOutBlocked     = outNow;
  }
  else if (state == GameState::RESETTING) {
    if (servoAtTarget()) {
      enterState(GameState::IDLE);
    }
  }

  servoUpdate();
}