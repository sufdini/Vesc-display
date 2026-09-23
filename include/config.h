// ---------------------------------------------------------------------------
// VESC Display - user configuration
//
// Edit this file to match your vehicle and wiring. Everything the firmware
// needs to turn raw VESC telemetry into speed, distance and battery level
// lives here.
// ---------------------------------------------------------------------------
#pragma once

// ---- Serial link to the VESC ----------------------------------------------
// The VESC "UART" app must be enabled in VESC Tool (App Settings -> General
// -> App to use: "UART" or "PPM and UART") with the same baud rate as below.
#define VESC_UART_BAUD 115200
#define VESC_RX_PIN 26   // ESP32 pin wired to VESC TX
#define VESC_TX_PIN 27   // ESP32 pin wired to VESC RX

// How often to request COMM_GET_VALUES, and how long without a valid reply
// before the display shows the "no connection" state.
#define VESC_POLL_INTERVAL_MS 100
#define VESC_TIMEOUT_MS 1500

// ---- Vehicle geometry ------------------------------------------------------
// Number of magnet poles in the motor (NOT pole pairs). A typical
// e-skateboard / e-bike hub motor has 14 poles; most 63xx outrunners have 14.
#define MOTOR_POLES 14

// Outer diameter of the driven wheel in millimetres.
#define WHEEL_DIAMETER_MM 90.0f

// Wheel revolutions per motor revolution.
//   Direct-drive hub motor:      1.0
//   Belt drive 15T motor / 36T wheel:  15.0 / 36.0
#define GEAR_RATIO (1.0f)

// ---- Battery ---------------------------------------------------------------
// Number of lithium cells in series (e.g. 10S = 10, 12S = 12).
#define BATTERY_CELLS 10

// ---- Units -----------------------------------------------------------------
// 0 = metric (km/h, km)   1 = imperial (mph, mi)
#define USE_IMPERIAL_UNITS 0

// ---- Board (LilyGO T-Display) ----------------------------------------------
#define BUTTON_NEXT_PIN 35   // right button: cycle pages
#define BUTTON_PREV_PIN 0    // left button: previous page
#define SCREEN_ROTATION 1    // 1 = landscape, USB on the right
