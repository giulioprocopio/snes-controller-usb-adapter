#include <Arduino.h>
#include <Keyboard.h>

#include <stdint.h>

#define DEBUG 1 // Set to 1 to enable debug serial output.

#include "hardware.h"

#define H(pin) digitalWrite((pin), HIGH)
#define L(pin) digitalWrite((pin), LOW)
#define T(pin) digitalWrite((pin), !digitalRead((pin)))
#define B(pin, period, duty)                                                   \
  do {                                                                         \
    T(pin);                                                                    \
    delay((period) * (duty));                                                  \
    T(pin);                                                                    \
    delay((period) * (1 - (duty)));                                            \
  } while (0)
#define B50(pin, period) B(pin, period, 0.5)
#define R(pin) digitalRead(pin)

uint16_t state, rep_state;

enum Button {
  // Order: left to right, top to bottom.
  BTN_L = 0,
  BTN_R,
  BTN_UP,
  BTN_X,
  BTN_LEFT,
  BTN_RIGHT,
  BTN_SELECT,
  BTN_START,
  BTN_Y,
  BTN_A,
  BTN_DOWN,
  BTN_B,
  BTN_NC // Not connected.
};

char button_to_char_decode[][7] = {
    "L",      "R",     "UP", "X", "LEFT", "RIGHT",
    "SELECT", "START", "Y",  "A", "DOWN", "B",
    "?" // Should never be used.
};

char button_to_key[] = {'l',
                        'r',
                        KEY_UP_ARROW,
                        'x',
                        KEY_LEFT_ARROW,
                        KEY_RIGHT_ARROW,
                        KEY_RIGHT_SHIFT,
                        KEY_RETURN,
                        'y',
                        'a',
                        KEY_DOWN_ARROW,
                        'b',
                        '\0'};

Button cycle_to_button[] = {
    BTN_NC,    BTN_NC,   BTN_NC,   BTN_NC, BTN_R,     BTN_L,      BTN_X, BTN_A,
    BTN_RIGHT, BTN_LEFT, BTN_DOWN, BTN_UP, BTN_START, BTN_SELECT, BTN_Y, BTN_B};

void setup() {
  Serial.begin(9600);

  pinMode(CLOCK_PIN, OUTPUT);
  H(CLOCK_PIN);
  pinMode(DATA_PIN, INPUT);
  pinMode(LATCH_PIN, OUTPUT);
  L(LATCH_PIN);
  pinMode(LED_PIN, OUTPUT);
  H(LED_PIN);
  pinMode(LOCK_PIN, INPUT_PULLUP);

  // State is 1 when button is not pressed, 0 when pressed.
  state = 0xFFFF;
  rep_state = 0xFFFF;
}

/**
 * Latch the current state of the buttons.
 *
 * Consider the following SNES controller connector pinout:
 *
 *  ----------------------------- ---------------------
 * |                             |                      \
 * | (1)     (2)     (3)     (4) |   (5)     (6)     (7) |
 * |                             |                      /
 *  ----------------------------- ---------------------
 *
 * Pin   Description   Color of wire in cable (SNES controller)
 * ===   ===========   ========================================
 * 1     +5V           White
 * 2     Clock         Yellow
 * 3     Latch         Orange
 * 4     Serial        Red
 * 5     ?             N/A
 * 6     ?             N/A
 * 7     Ground        Brown
 *
 * This function implements the protocol used by the SNES.  Every 16.67ms (or
 * about 60Hz), the SNES CPU sends out a 12us wide, positive going data latch
 * pulse on pin 3.  This instructs the ICs in the controller to latch the state
 * of all buttons internally.  Six microsenconds after the fall of the data
 * latch pulse, the CPU sends out 16 data clock pulses on pin 2.  These are 50%
 * duty cycle with 12us per full cycle.  The controllers serially shift the
 * latched button states out pin 4 on every rising edge of the clock, and the
 * CPU samples the data on every falling edge.
 */
void read_state(void) {
  H(LATCH_PIN);
  delayMicroseconds(12);
  L(LATCH_PIN);

  uint16_t tmp_state = 0;

  for (int i = 16; i > 0;) {
    i--;

    delayMicroseconds(6);
    L(CLOCK_PIN);

    tmp_state <<= 1;
    if (R(DATA_PIN) == HIGH) {
      tmp_state |= 1;
    }

    delayMicroseconds(6);
    H(CLOCK_PIN);
  }

  state = tmp_state;
}

/**
 * Print the current state of the buttons to the serial port.
 */
void debug_state(void) {
  Serial.print("State: ");
  Serial.print(state, HEX);
  Serial.print(" (");
  bool first = true;
  for (int i = 0; i < 16; i++) {
    if (!(state & (1 << i))) {
      if (!first) {
        Serial.print(", ");
      } else {
        first = false;
      }
      Serial.print(button_to_char_decode[cycle_to_button[i]]);
    }
  }
  Serial.println(")");
}

/**
 * Write the current state of the buttons to the keyboard.
 */
void write_state(void) {
  uint16_t diff = state ^ rep_state, tmp_state = state;
  char key;

  for (int i = 0; i < 16; i++) {
    if (diff & 1) {
      key = button_to_key[cycle_to_button[i]];
      if (tmp_state & 1) {
        Keyboard.release(key);
      } else {
        Keyboard.press(key);
      }
    }

    diff >>= 1;
    tmp_state >>= 1;
  }

  rep_state = state;
}

// Macro construct to unpress all keys and do nothing until the condition is
// false.
#define BLOCK(macro)                                                           \
  if ((macro)) {                                                               \
    Keyboard.releaseAll();                                                     \
    rep_state = 0xFFFF;                                                        \
    while ((macro)) {
#define KCOLB                                                                  \
  }                                                                            \
  }

#define IS_LOCKED (R(LOCK_PIN) == HIGH)
// This relies on the fact that even if the controller is connected and all
// buttons are pressed, the state will be 0xF000, because those four bits in the
// controller registers are not button-related and are always 1.
#define IS_CONNECTED (state & 0xF000)

void loop() {
  BLOCK(IS_LOCKED)
  // Lock is engaged, unpress all keys and do nothing.  Blink at 2 Hz.
  if (DEBUG) {
    Serial.println("Lock engaged");
  }
  B50(LED_PIN, 500);
  KCOLB

  read_state();

  BLOCK(!IS_CONNECTED)
  // Controller is not connected.  Blink at 1 Hz.
  read_state();
  if (DEBUG) {
    Serial.println("Controller not connected");
  }
  B50(LED_PIN, 1000);
  KCOLB

  if (DEBUG) {
    debug_state();
  }
  write_state();
}
