#include <Arduino.h>
#include "servo_control.h"

// p starts pitch
// after pitch delay, go to READY_TO_SWING
// if s is pressed in time, servo swings and then waits for result
// if s is not pressed in time, it still goes to WAITING_FOR_RESULT so the ball 
// can naturally fall into the out pocket in WAITING_FOR_RESULT:
// pocket 1 → points
// pocket 2 → points
// out pocket → OUT state, 0 points
// nothing detected by timeout → ERROR_STATE

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
  ERROR_STATE
};


// receiving the wireless signal
typedef struct struct_message {
    int velo;
    bool swing;
} struct_message;

struct_message myData;

static bool swingSignal;
static int velo;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  // Serial.print("Velocity: ");
  // Serial.println(myData.velo);
  // Serial.print("Swing: ");
  // Serial.println(myData.swing);
  // Serial.println();
  velo = myData.velo;
  swingSignal = myData.swing;
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

  if (state == GameState::IDLE) {
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
    Serial.println("Press s to swing");
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

    if (swingOccurred) {
      servoMoveTo(0.0f, 5.0f);
      enterState(GameState::RESETTING);
    } else {
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
  Serial.print("Total score: ");
  Serial.println(totalScore);

  enterState(GameState::IDLE);
}

void loop() {
  // serial commands:
  // p = start pitch
  // s = swing
  // r = reset score

  if (Serial.available() > 0) {
    char cmd = Serial.read();

    if ((cmd == 'p' || cmd == 'P') &&
        (state == GameState::IDLE || state == GameState::ERROR_STATE)) {
      Serial.println("Pitch command received");
      enterState(GameState::PITCHING);
    }
    // else if ((cmd == 's' || cmd == 'S') && state == GameState::READY_TO_SWING) {
    else if ((swingSignal == true) && state == GameState::READY_TO_SWING) {
      Serial.println("Swing command received");
      swingOccurred = true;
      servoMoveTo(270.0f, 5.0f);
      enterState(GameState::WAITING_FOR_RESULT);
    }
    else if (cmd == 'r' || cmd == 'R') {
      totalScore = 0;
      Serial.println("Score reset to 0");
    }
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
      totalScore += POCKET1_POINTS;
      Serial.print("HIT: Pocket 1 triggered, +");
      Serial.print(POCKET1_POINTS);
      Serial.println(" points");
      Serial.print("Total score: ");
      Serial.println(totalScore);

      if (swingOccurred) {
        servoMoveTo(0.0f, 5.0f);
        enterState(GameState::RESETTING);
      } else {
        enterState(GameState::IDLE);
      }
    }
    else if (p2Blocked && !prevPocket2Blocked) {
      totalScore += POCKET2_POINTS;
      Serial.print("HIT: Pocket 2 triggered, +");
      Serial.print(POCKET2_POINTS);
      Serial.println(" points");
      Serial.print("Total score: ");
      Serial.println(totalScore);

      if (swingOccurred) {
        servoMoveTo(0.0f, 5.0f);
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
        servoMoveTo(0.0f, 5.0f);
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
