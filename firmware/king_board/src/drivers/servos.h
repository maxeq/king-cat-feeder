#pragma once
#include <stdint.h>

// Rotor (FS90R, continuous) and door (MF90, positional). Non-blocking: commands set
// a target, servosTick() moves towards it. Called from the control task only.
void servosBegin();  // restores the door angle from NVS, so the first move doesn't jump
void servosTick(uint32_t now);
void servosSafe();  // rotor stop, door released (before OTA)

bool rotorSet(int us);  // false if refused: a safety stop is latched until rotorClearTrip()
void rotorClearTrip();  // an operator action (console stop, button) re-arms the rotor
bool rotorTripped();
int rotorUs();

void doorMoveTo(int angle, bool releaseWhenThere);
void doorRelease();  // detach and hold the line low
int doorAngle();
bool doorClosed();
bool doorMoving();
