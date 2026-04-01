#include "servo_control.h"
#include <math.h>

static int g_servoPin = -1;

static float g_currentAngleDeg = 0.0f;
static float g_targetAngleDeg  = 0.0f;
static float g_stepDeg         = 3.0f;

static unsigned long g_lastStepMs = 0;
static const unsigned long SERVO_STEP_INTERVAL_MS = 15;

// Maps 0..270 deg to 500..2500 us for your ANNIMOS servo
static int angleToPulseUs(float angleDeg) {
  if (angleDeg < 0.0f) angleDeg = 0.0f;
  if (angleDeg > 270.0f) angleDeg = 270.0f;
  return (int)(500.0f + (angleDeg / 270.0f) * 2000.0f);
}

// Send one 20 ms pulse frame
static void writeServoPulseUs(int us) {
  digitalWrite(g_servoPin, HIGH);
  delayMicroseconds(us);
  digitalWrite(g_servoPin, LOW);
  delayMicroseconds(20000 - us);
}

void servoSetup(int pin) {
  g_servoPin = pin;
  pinMode(g_servoPin, OUTPUT);

  g_currentAngleDeg = 0.0f;
  g_targetAngleDeg = 0.0f;
  g_lastStepMs = millis();
}

void servoSetAngle(float angleDeg) {
  if (angleDeg < 0.0f) angleDeg = 0.0f;
  if (angleDeg > 270.0f) angleDeg = 270.0f;

  g_currentAngleDeg = angleDeg;
  g_targetAngleDeg = angleDeg;
}

void servoMoveTo(float targetAngleDeg, float stepDeg) {
  if (targetAngleDeg < 0.0f) targetAngleDeg = 0.0f;
  if (targetAngleDeg > 270.0f) targetAngleDeg = 270.0f;
  if (stepDeg <= 0.0f) stepDeg = 1.0f;

  g_targetAngleDeg = targetAngleDeg;
  g_stepDeg = stepDeg;
}

float servoGetAngle() {
  return g_currentAngleDeg;
}

bool servoAtTarget() {
  return fabs(g_currentAngleDeg - g_targetAngleDeg) < 0.5f;
}

void servoUpdate() {
  unsigned long nowMs = millis();

  if (!servoAtTarget() && (nowMs - g_lastStepMs >= SERVO_STEP_INTERVAL_MS)) {
    g_lastStepMs = nowMs;

    if (g_currentAngleDeg < g_targetAngleDeg) {
      g_currentAngleDeg += g_stepDeg;
      if (g_currentAngleDeg > g_targetAngleDeg) {
        g_currentAngleDeg = g_targetAngleDeg;
      }
    } else if (g_currentAngleDeg > g_targetAngleDeg) {
      g_currentAngleDeg -= g_stepDeg;
      if (g_currentAngleDeg < g_targetAngleDeg) {
        g_currentAngleDeg = g_targetAngleDeg;
      }
    }
  }

  int pulseUs = angleToPulseUs(g_currentAngleDeg);
  writeServoPulseUs(pulseUs);
}
