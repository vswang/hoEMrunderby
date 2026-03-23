#include <Arduino.h>
#include "servo_control.h"

static const int SERVO_PIN  = D6;
static const int BEAM_PIN   = D2;
static const int BUTTON_PIN = D3;

enum class GameState {
  IDLE,
  PITCHING,
  READY_TO_SWING,
  WAITING_FOR_RESULT,
  RESETTING
};

static GameState state = GameState::IDLE;

// transition detect for breakbeam
static bool prevBeamBlocked = false;

// transition detect for button
static bool prevButtonPressed = false;

// pitch delay after button press
static unsigned long pitchStartTime = 0;
static const unsigned long PITCH_DELAY_MS = 2000;

// result timeout after swing starts
static unsigned long resultStartTime = 0;
static const unsigned long RESULT_TIMEOUT_MS = 10000;

bool beamIsBlocked() {
  return digitalRead(BEAM_PIN) == LOW;
}

bool buttonIsPressed() {
  return digitalRead(BUTTON_PIN) == LOW;
}

void enterState(GameState newState) {
  state = newState;

  if (state == GameState::IDLE) {
    Serial.println("State: IDLE");
    Serial.println("Press the physical button to release pitch");
  }
  else if (state == GameState::PITCHING) {
    pitchStartTime = millis();
    Serial.println("State: PITCHING");
    Serial.println("Pitch released, waiting before swing...");
  }
  else if (state == GameState::READY_TO_SWING) {
    Serial.println("State: READY_TO_SWING");
    Serial.println("Press s to swing");
  }
  else if (state == GameState::WAITING_FOR_RESULT) {
    resultStartTime = millis();
    prevBeamBlocked = beamIsBlocked();  // clear stale edge right when swing starts
    Serial.println("State: WAITING_FOR_RESULT");
  }
  else if (state == GameState::RESETTING) {
    Serial.println("State: RESETTING");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  servoSetup(SERVO_PIN);
  servoSetAngle(0.0f);

  pinMode(BEAM_PIN, INPUT_PULLUP);
  prevBeamBlocked = beamIsBlocked();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  prevButtonPressed = buttonIsPressed();

  enterState(GameState::IDLE);
}

void loop() {
  // 1. Read physical pitch-release button
  bool buttonPressed = buttonIsPressed();

  if (buttonPressed && !prevButtonPressed) {
    Serial.println("BUTTON PRESSED");

    if (state == GameState::IDLE) {
      enterState(GameState::PITCHING);
    }
  }

  prevButtonPressed = buttonPressed;

  // 2. Read serial command for swing
  if (Serial.available() > 0) {
    char cmd = Serial.read();

    if ((cmd == 's' || cmd == 'S') && state == GameState::READY_TO_SWING) {
      Serial.println("Swing command received");
      servoMoveTo(270.0f, 5.0f);
      enterState(GameState::WAITING_FOR_RESULT);
    }
  }

  // 3. State machine
  if (state == GameState::PITCHING) {
    if (millis() - pitchStartTime >= PITCH_DELAY_MS) {
      Serial.println("BALL HAS BEEN PITCHED");
      enterState(GameState::READY_TO_SWING);
    }
  }
  else if (state == GameState::WAITING_FOR_RESULT) {
    bool beamBlocked = beamIsBlocked();

    if (beamBlocked && !prevBeamBlocked) {
      Serial.println("HIT: breakbeam triggered");
      servoMoveTo(0.0f, 5.0f);
      enterState(GameState::RESETTING);
    }
    else if (millis() - resultStartTime >= RESULT_TIMEOUT_MS) {
      Serial.println("MISS: timeout, no breakbeam");
      servoMoveTo(0.0f, 5.0f);
      enterState(GameState::RESETTING);
    }

    prevBeamBlocked = beamBlocked;
  }
  else if (state == GameState::RESETTING) {
    if (servoAtTarget()) {
      enterState(GameState::IDLE);
    }
  }

  // 4. Keep servo alive
  servoUpdate();
}