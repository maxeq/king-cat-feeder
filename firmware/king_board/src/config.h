#pragma once
// Pins and tuning constants. Plain C++ (no Arduino types), so src/logic and the
// host-side tests can include it too. Times are milliseconds.

// pins
const int PIN_ROTOR = 4;  // FS90R, continuous
const int PIN_LID = 5;    // MF90
const int PIN_SC_DT = 6;  // HX711
const int PIN_SC_SCK = 7;
const int PIN_LED = 8;  // S8050, PWM
const int PIN_IND = 9;
const int PIN_BTN = 10;     // to GND
const int PIN_PIR = 11;     // HC-SR501
const int PIN_AI_SDA = 17;  // Grove Vision AI V2
const int PIN_AI_SCL = 18;
const int PIN_AUX = 21;  // spare

// servos
const int ROT_STOP = 1500, ROT_FWD = 1700, ROT_BACK = 1300;
const int ROT_FWD_SLOW = 1600;                            // slower feed jams less
const int ANG_CLOSED = 175, ANG_OPEN = 75, STEP_MS = 15;  // servo is upside down, so closed > open
const unsigned long ROTOR_MAX_RUN_MS = 20000;             // safety stop for a rotor left running
const int LED_CH = 4;

// dosing
const float DOSE_TARGET_G = 10;  // grams per dose
const unsigned long DOSE_TIMEOUT = 90000;
const float CYCLE_MIN_G = 1.0;               // less per cycle = nothing came out
const float CAT_JUMP_G = 15.0;               // more per cycle = cat on the tray
const unsigned long SETTLE_MS = 700;         // let the servo vibration die down before weighing
const unsigned long WEIGH_WINDOW_MS = 1500;  // collect up to 5 samples; fewer than 3 = scale fault
const unsigned long CAT_PAUSE_MS = 2000;
const float TRAY_LOADED_G = 20;  // boot reading this far from the saved tare = tray not empty

// sensors and light
const unsigned long PIR_WARMUP_MS = 2000;  // HC-SR501 reads garbage right after power-up
const unsigned long PIR_BLIND_MS = 2500;   // rotor vibration trips the PIR
const unsigned long AUTO_PULSE_MS = 2000;  // auto mode: rotor pulse on motion
const unsigned long MOTION_LIGHT_MS = 5000;
const unsigned long LIGHT_RAMP_MS = 300;  // soft start so the light doesn't startle the cat

// system
const unsigned long CONTROL_TICK_MS = 10;
const unsigned WATCHDOG_S = 15;  // longest step without a feed is a ~10 s camera exchange
