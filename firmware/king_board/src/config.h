#pragma once
#include <stdint.h>
// Pins and tuning constants. Plain C++ (no Arduino types), so src/logic and the
// host-side tests can include it too. Times are milliseconds, all uint32_t like millis().

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
const int ROT_FWD_SLOW = 1600;                      // slower feed jams less
const int SERVO_MIN_US = 500, SERVO_MAX_US = 2500;  // pulse range for both servos
const int ANG_CLOSED = 175, ANG_OPEN = 75;          // servo is upside down, so closed > open
const int ANG_NUDGE_MIN = 40, ANG_NUDGE_MAX = 180;  // console calibration nudges stay in here
const uint32_t STEP_MS = 15;                        // door: one degree per step
const uint32_t DOOR_ATTACH_MS = 200;                // door: let the servo grab its angle before moving
const int DOOR_SAVE_EVERY_DEG = 10;                 // door: remember the angle this often mid-move
const uint32_t ROTOR_MAX_RUN_MS = 20000;            // safety stop for a rotor left running
const uint32_t ROTOR_IDLE_DETACH_MS = 1500;         // FS90R creeps at 1500 us: detach this long after a stop
const int LED_CH = 4;
const uint32_t LED_FREQ_HZ = 5000;
const uint8_t LED_BITS = 8;          // levels 0..255
const int LIGHT_DEFAULT_LEVEL = 51;  // 0..255, until the web slider saves one

// dosing
const float DOSE_TARGET_G = 10;   // grams per dose
const float TARGET_TOL_G = 0.5f;  // this close to the target counts as reached
const uint32_t DOSE_TIMEOUT = 90000;
const float CYCLE_MIN_G = 1.0;          // less per cycle = nothing came out
const float CAT_JUMP_G = 15.0;          // more per cycle = cat on the tray
const float CONFIRM_TOL_G = 2.0f;       // two weighs this close = food lying still, not a paw
const float STEP_MAX_G = 6.0f;          // kibble trickles; a jump this big between two readings is not food
const float CAT_LEAVE_SHARE = 0.7f;     // a drop of this share of the jump means the load is leaving
const float CAT_LEAVE_MIN_G = 0.5f;     // ...but never less than this (scale noise is ~0.2 g)
const uint32_t SETTLE_MS = 700;         // let the servo vibration die down before weighing
const uint32_t WEIGH_WINDOW_MS = 1500;  // collect up to WEIGH_SAMPLES samples
const int WEIGH_SAMPLES = 5;
const int WEIGH_MIN_SAMPLES = 3;  // fewer in a window = scale fault
const uint32_t CAT_PAUSE_MS = 2000;
const float TRAY_LOADED_G = 20;  // boot reading this far from the saved tare = tray not empty

// dose programs: rotor segment lengths
const uint32_t SHAKE_MS = 1000;      // each of the 3 loosening moves before the first weigh
const uint32_t UNLOAD_MS = 500;      // a normal cycle backs off first...
const uint32_t FEED_SLOW_MS = 6000;  // ...then feeds slowly
const uint32_t ROCK_BACK_MS = 1000;  // escalation 1: rocking
const uint32_t ROCK_FWD_MS = 2000;
const uint32_t PUSH_SLOW_MS = 4000;      // escalations 1-3 end with a slow push
const int EMPTY_CYCLES_TO_ESCALATE = 2;  // cycles in a row under CYCLE_MIN_G before the next level
const int MAX_ESCALATION = 3;            // past the last level the dose ends as Jammed
const uint32_t HAMMER_MS = 300;          // escalation 2: short knocks
const int HAMMER_KNOCKS = 6;
const uint32_t LONG_REVERSE_MS = 4000;  // escalation 3: long reverse, quick knocks, push through
const uint32_t KNOCK_MS = 250;
const int KNOCKS = 4;
const uint32_t TIMED_KNOCK_MS = 1000;  // timed dose (no scale): back, forward...
const uint32_t TIMED_RUN_MS = 3000;    // ...then a long forward and a long back

// scale
const float SCALE_CAL = 363.7f;  // HX711 counts per gram, positive sign
const int SCALE_TARE_SAMPLES = 5;
const uint32_t SCALE_PIN_SETTLE_MS = 5;       // setup: after setting the pins, before probing DT
const int SCALE_DUMP_SAMPLES = 6;             // console 'r'
const uint32_t SCALE_LOST_MS = 3000;          // no reading for this long = cable gone
const uint32_t SCALE_FIRST_RETRY_MS = 10000;  // missing at boot: look again this long after start
const uint32_t SCALE_RETRY_MS = 5000;         // then look this often
const uint32_t SCALE_PROBE_MS = 200;          // a plugged-in HX711 pulls DT low within this
const uint32_t SCALE_BOOT_PROBE_MS = 500;
const uint32_t SCALE_BOOT_READY_MS = 1500;  // setup only: first conversion after power-up
const uint32_t SCALE_SAMPLE_WAIT_MS = 300;  // setup only: each tare sample (10 per second)

// sensors and light
const uint32_t PIR_WARMUP_MS = 2000;   // HC-SR501 reads garbage right after power-up
const uint32_t PIR_BLIND_MS = 2500;    // rotor vibration trips the PIR
const uint32_t PIR_TEST_STEP_MS = 50;  // self-test: each pull settles this long
const uint32_t AUTO_PULSE_MS = 2000;   // auto mode: rotor pulse on motion
const uint32_t MOTION_LIGHT_MS = 5000;
const uint32_t LIGHT_RAMP_MS = 300;  // soft start so the light doesn't startle the cat
const uint32_t BUTTON_DEBOUNCE_MS = 300;
const uint32_t INDICATOR_BLINK_MS = 400;  // each phase

// camera (Grove Vision AI V2)
const uint32_t AI_FRAME_TIMEOUT_MS = 4000;  // one base64 jpeg at 50 kHz takes seconds
const uint32_t AI_RAW_TIMEOUT_MS = 3000;    // debug: raw AT reply
const uint32_t AI_SCAN_HZ = 100000;         // console 'g': the bus scan

// system
const uint32_t CONTROL_TICK_MS = 10;
const uint32_t USB_SETTLE_MS = 1500;          // boot: before the first serial line
const uint32_t WORKER_IDLE_MS = 1000;         // an idle worker wakes this often to feed the watchdog
const uint32_t TICK_OVERRUN_HOLD_MS = 10000;  // TickOverrun stays set this long after an overrun
const int CONTROL_PRIORITY = 5;               // above loop() and the workers; below the core's system tasks
const int WORKER_PRIORITY = 1;                // camera and NVS workers, same as loop(): they may take seconds
const unsigned WATCHDOG_S = 15;               // longest step without a feed is a ~10 s camera exchange
const uint32_t LOCK_TIMEOUT_MS = 1000;        // locks guard copies of a few bytes; this long means a bug
const uint32_t PUBLISH_LOCK_MS = 2;           // the control tick never waits longer for the status lock
const uint32_t HALT_ACK_MS = 100;  // OTA waits this long for the control task to reach the safe state

// network
const uint32_t WEB_SEND_MS = 8000;  // one whole response; a slower client is dropped
const uint32_t SOCK_WAIT_MS = 50;  // full socket buffer: wait this long for the peer, then check the deadline
const uint32_t TELNET_LINE_MS = 500;     // one console line; a slower client is dropped
const uint32_t TELNET_LOGIN_MS = 30000;  // to type the password
const uint32_t HEARTBEAT_MS = 2000;      // status line into the web log
const uint32_t WIFI_RETRY_MS = 10000;
const int WIFI_CONNECT_TRIES = 40;  // at boot, WIFI_CONNECT_STEP_MS apart
const uint32_t WIFI_CONNECT_STEP_MS = 500;
