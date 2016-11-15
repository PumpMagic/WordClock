/**
 * Word Clock
 * 
 * A clock that displays the time using words rather than numbers.
 * Implemented as an LED array mounted inside a shadow box and controlled by
 * an ATMega328P using a DS3231, some CD4094s and some ULN2003As.
 * 
 * Software by Ryan Conway.
 * Project inspired by the Instructables project by Scott Bezek.
 * Depends on Adafruit's fork of RTClib: https://github.com/adafruit/RTClib
 */

#include <Wire.h>
#include "RTClib.h"


// Hardware constants
#define REGISTER_STROBE_PIN 6
#define REGISTER_DATA_PIN   7
#define REGISTER_CLOCK_PIN  8
#define REGISTER_OUTPUT_ENABLE_PIN 9
#define BRIGHTNESS_ADJUST_PIN  A0
#define MINUTE_ADVANCE_BUTTON_PIN 10
#define HOUR_ADVANCE_BUTTON_PIN 11
#define REGISTER_STROBE_DURATION_MS 2

// Tuneable software constants
#define BUTTON_DEBOUNCE_MILLIS 100
#define RTC_QUERY_PERIOD_MILLIS 5000
#define RTC_RESET_YEAR    2016
#define RTC_RESET_MONTH   1
#define RTC_RESET_DAY     1
#define RTC_RESET_HOUR    0
#define RTC_RESET_MINUTE  0
#define RTC_RESET_SECOND  0

// Non-tuneable software constants
#define BITS_PER_BYTE 8

// Word bit positions
#define WORD_BIT_MAX 20
typedef enum {
  TEN_M     = 20,
  HALF_M    = 19,
  QUARTER_M = 18,
  TWENTY_M  = 17,
  FIVE_M    = 16,
  MINUTES   = 15,
  PAST      = 14,
  TO        = 13,
  ONE_H     = 12,
  TWO_H     = 11,
  THREE_H   = 10,
  FOUR_H    = 9,
  FIVE_H    = 8,
  SIX_H     = 7,
  SEVEN_H   = 6,
  EIGHT_H   = 5,
  NINE_H    = 4,
  TEN_H     = 3,
  ELEVEN_H  = 2,
  TWELVE_H  = 1,
  OCLOCK    = 0
} word_bit_map;

// Global state
static uint32_t localWordRegister = 0;
static uint8_t currentHour = 0;
static uint8_t currentMinute = 0;
static uint8_t currentSecond = 0;
static bool timeLocallyUpdated = false;
static RTC_DS3231 rtc;


/**
 * Locally "enable" a given word by raising the corresponding bit to the local word register
 * Note that this function will not update the display - to do so call flushWordRegister()
 */
void enableWord(word_bit_map theWord) {
  localWordRegister |= (1 << theWord);
}


/**
 * Locally "disable" all words by lowering all bits of the local word register
 * Note that this function will not update the display - to do so call flushWordRegister()
 */
void disableAllWords() {
  localWordRegister = 0;
}


/**
 * Get a specific byte of a number, where byte 0 is the least significant, byte 1 is one higher, etc.
 */
uint8_t getByte(uint32_t number, uint8_t byteIndex) {
  uint32_t mask = 0xFF << (byteIndex * BITS_PER_BYTE);
  uint32_t masked = number & mask;
  uint32_t targetByte = masked >> (byteIndex * BITS_PER_BYTE);
  
  return targetByte;
}


/**
 * Flush the local word register out to the external word register (the cascaded 8-bit latches).
 * 
 * This is done in such an order that the "furthest" 8-bit latch will have the least significant byte
 * and the "closest" one will have the most significant byte
 */ 
void flushWordRegister() {
  shiftOut(REGISTER_DATA_PIN, REGISTER_CLOCK_PIN, MSBFIRST, getByte(localWordRegister, 0));
  shiftOut(REGISTER_DATA_PIN, REGISTER_CLOCK_PIN, MSBFIRST, getByte(localWordRegister, 1));
  shiftOut(REGISTER_DATA_PIN, REGISTER_CLOCK_PIN, MSBFIRST, getByte(localWordRegister, 2));
  digitalWrite(REGISTER_STROBE_PIN, HIGH);
  delay(REGISTER_STROBE_DURATION_MS);
  digitalWrite(REGISTER_STROBE_PIN, LOW); 
}


/**
 * Update the local word register given the current hour and minute
 * (typically as obtained from an RTC or other clock)
 */
void updateLocalWordRegister(uint8_t hour, uint8_t minute) {
  // Start from a clean slate
  disableAllWords();

  // Handle the "minute" part of the time
  if (minute < 5) {
    // nothing
  } else if (minute >= 5 && minute < 10) { 
    enableWord(FIVE_M);
  } else if (minute >= 10 && minute < 15) { 
    enableWord(TEN_M);
  } else if (minute >= 15 && minute < 20) {
    enableWord(QUARTER_M); 
  } else if (minute >= 20 && minute < 25) { 
    enableWord(TWENTY_M);
  } else if (minute >= 25 && minute < 30) { 
    enableWord(TWENTY_M);
    enableWord(FIVE_M);
  } else if (minute >= 30 && minute < 35) {
    enableWord(HALF_M);
  } else if (minute >= 35 && minute < 40) { 
    enableWord(TWENTY_M);
    enableWord(FIVE_M);
  } else if (minute >= 40 && minute < 45) { 
    enableWord(TWENTY_M);
  } else if (minute >= 45 && minute < 50) {
    enableWord(QUARTER_M);
  } else if (minute >= 50 && minute < 55) { 
    enableWord(TEN_M);
  } else if (minute >= 55) { 
    enableWord(FIVE_M);
  }

  // Handle the "hour" part of the time
  if (minute < 35) {
    if (minute >= 5) {
      enableWord(PAST);
    }
    
    switch (hour) {
    case 1:
      enableWord(ONE_H);
      break;
    case 2:
      enableWord(TWO_H);
      break;
    case 3:
      enableWord(THREE_H);
      break;
    case 4:
      enableWord(FOUR_H);
      break;
    case 5:
      enableWord(FIVE_H);
      break;
    case 6:
      enableWord(SIX_H);
      break;
    case 7:
      enableWord(SEVEN_H);
      break;
    case 8:
      enableWord(EIGHT_H);
      break;
    case 9:
      enableWord(NINE_H);
      break;
    case 10:
      enableWord(TEN_H);
      break;
    case 11:
      enableWord(ELEVEN_H);
      break;
    case 12:
      enableWord(TWELVE_H);
      break;
    }
  } else {
    enableWord(TO);
    
    switch (hour) {
    case 1: 
      enableWord(TWO_H);
      break;
    case 2: 
      enableWord(THREE_H);
      break;
    case 3: 
      enableWord(FOUR_H);
      break;
    case 4:
      enableWord(FIVE_H);
      break;
    case 5:
      enableWord(SIX_H);
      break;
    case 6:
      enableWord(SEVEN_H);
      break;
    case 7:
      enableWord(EIGHT_H);
      break;
    case 8:
      enableWord(NINE_H);
      break;
    case 9:
      enableWord(TEN_H);
      break;
    case 10:
      enableWord(ELEVEN_H);
      break;
    case 11:
      enableWord(TWELVE_H);
      break;
    case 12:
      enableWord(ONE_H);
      break;
    }
  }
}


/**
 * Update the clock display given the current hour and minute
 * (typically as obtained from an RTC or other clock)
 */
void updateDisplay(uint8_t hour, uint8_t minute) {
  updateLocalWordRegister(hour, minute);
  flushWordRegister();
}


/**
 * Test the display, one word at a time
 */
void testDisplay1() {
  for (uint8_t i = 0; i <= WORD_BIT_MAX; i++) {
    disableAllWords();
    enableWord(i);
    flushWordRegister();
    delay(500);
  }
}


/**
 * Test the display by iterating over all minutes of the day, starting at 00:00 and ending at 23:59
 * Do this on a simulation time scale where 1 second in real life = 5 simulated minutes
 * i.e., this test takes (24*60/5) = 288 seconds to complete
 */
void testDisplay2() {
  uint8_t hour = 0;
  uint8_t minute = 0;

  while (hour < 24) {
    updateDisplay(hour, minute);
    delay(1000);
    minute += 5;
    if (minute >= 60) {
      hour += 1;
      minute = 0;
    }
  }
}


/**
 * Setup function
 * This gets called automatically on boot
 */
void setup() {
  pinMode(REGISTER_STROBE_PIN, OUTPUT);
  pinMode(REGISTER_DATA_PIN, OUTPUT);
  pinMode(REGISTER_CLOCK_PIN, OUTPUT);
  pinMode(REGISTER_OUTPUT_ENABLE_PIN, OUTPUT);
  
  pinMode(BRIGHTNESS_ADJUST_PIN, INPUT);
  pinMode(MINUTE_ADVANCE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(HOUR_ADVANCE_BUTTON_PIN, INPUT_PULLUP);

  testDisplay1();
  testDisplay2();

  //@todo consider doing something on RTC failure
  rtc.begin();

  // If the RTC's lost power, reset it
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(RTC_RESET_YEAR, RTC_RESET_MONTH, RTC_RESET_DAY, RTC_RESET_HOUR, RTC_RESET_MINUTE, RTC_RESET_SECOND));
  }
  
  // display the current time
  DateTime now = rtc.now();
}


/**
 * Handle the external brightness control
 * @todo consider adding schmitt triggers to handle hysteresis
 */
void handleBrightnessControl() {
  // Read the analog brightness pin
  int analogBrightnessValue = analogRead(BRIGHTNESS_ADJUST_PIN);

  // Map the read analog value to a PWM duty cycle value
  // analogRead returns [0, 1023] for [0, 5]V. The voltage range is actually less thanks to nonideal potentiometer
  // properties and finite pin input impedance, so condense it
  // The output PWM duty cycle range is [0, 255]
  int pwmDuty = 255;
  if (analogBrightnessValue < 100) {
    pwmDuty = 0;
  } else if (analogBrightnessValue >= 924) {
    pwmDuty = 255;
  } else {
    // map [100, 923] to [0, 255]
    double divideFactor = (923-100) / 255;
    double pwmDutyDouble = ((double)(analogBrightnessValue - 100)) / 3.23;
    pwmDuty = (int) pwmDutyDouble;
  }

  digitalWrite(REGISTER_OUTPUT_ENABLE_PIN, pwmDuty);
}


/**
 * Get the hour after the given hour
 */
static uint8_t hourAfter(uint8_t hour) {
  if (hour < 23) {
    return hour + 1;
  }

  return 0;
}


/**
 * Get the minute after the given minute
 */
static uint8_t minuteAfter(uint8_t minute) {
  if (minute < 59) {
    return minute + 1;
  }

  return 0;
}


/**
 * Handle the external time adjust buttons
 * Return whether or not the user is currently in the process of changing the time
 * @todo duplicated code
 */
void handleTimeAdjustButtons() {
  static unsigned long lastHourButtonChange = 0;
  static unsigned long lastMinuteButtonChange = 0;
  static int lastHourButtonValue = 1;
  static int lastMinuteButtonValue = 1;
  static bool hourButtonBeingHeld = false;
  static bool minuteButtonBeingHeld = false;
  static unsigned long nextHourAdvanceTime = 0;
  static unsigned long nextMinuteAdvanceTime = 0;

  static unsigned long updateRTCTime = 0;

  int hourButtonValue = digitalRead(HOUR_ADVANCE_BUTTON_PIN);
  int minuteButtonValue = digitalRead(MINUTE_ADVANCE_BUTTON_PIN);

  int timeNow = millis();
  if (hourButtonValue != lastHourButtonValue) {
    lastHourButtonChange = timeNow;
  }
  if (minuteButtonValue != lastMinuteButtonValue) {
    lastMinuteButtonChange = timeNow;
  }

  if ((timeNow - lastHourButtonChange) > BUTTON_DEBOUNCE_MILLIS) {
    // the button state is settled
    // if it's being depressed (connected to ground), then increment the hour once immediately
    // and then once every two seconds
    if (hourButtonValue == LOW) {
      if (hourButtonBeingHeld == false) {
        currentHour = hourAfter(currentHour);
        currentSecond = 0;
        timeLocallyUpdated = true;
        nextHourAdvanceTime = timeNow + 2000;
        updateRTCTime = timeNow + 5000;
        updateDisplay(currentHour, currentMinute);
        hourButtonBeingHeld = true;
      } else if (timeNow >= nextHourAdvanceTime) {
        currentHour = hourAfter(currentHour);
        currentSecond = 0;
        nextHourAdvanceTime = timeNow + 2000;
        updateDisplay(currentHour, currentMinute);
        updateRTCTime = timeNow + 5000;
      }
    } else {
      hourButtonBeingHeld = false;
    }
  }

  if ((timeNow - lastMinuteButtonChange) > BUTTON_DEBOUNCE_MILLIS) {
    // the button state is settled
    // if it's being depressed (connected to ground), then increment the minute once immediately
    // and then once every two seconds
    if (minuteButtonValue == LOW) {
      if (minuteButtonBeingHeld == false) {
        currentMinute = minuteAfter(currentMinute);
        currentSecond = 0;
        timeLocallyUpdated = true;
        nextMinuteAdvanceTime = timeNow + 2000;
        updateRTCTime = timeNow + 5000;
        updateDisplay(currentHour, currentMinute);
        minuteButtonBeingHeld = true;
      } else if (timeNow >= nextMinuteAdvanceTime) {
        currentMinute = minuteAfter(currentMinute);
        currentSecond = 0;
        nextMinuteAdvanceTime = timeNow + 2000;
        updateRTCTime = timeNow + 5000;
        updateDisplay(currentHour, currentMinute);
      }
    } else {
      minuteButtonBeingHeld = false;
    }
  }

  if (timeLocallyUpdated == true && timeNow > updateRTCTime && hourButtonBeingHeld == false && minuteButtonBeingHeld == false) {
    DateTime timeRTC = rtc.now();
    rtc.adjust(DateTime(timeRTC.year(), timeRTC.month(), timeRTC.day(), currentHour, currentMinute, currentSecond));
    timeLocallyUpdated = false;
    updateRTCTime = 0;
  }
}


/**
 * Loop function
 * This gets called repeatedly and indefinitely after setup() is called
 */
void loop() {
  static unsigned long lastRTCQueryTime = 0;
  
  handleBrightnessControl();
  handleTimeAdjustButtons();

  unsigned long timeNow = millis();
  if ((timeNow - lastRTCQueryTime) > RTC_QUERY_PERIOD_MILLIS) {
    if (timeLocallyUpdated == false) {
      DateTime timeRTC = rtc.now();
      currentHour = timeRTC.hour();
      currentMinute = timeRTC.minute();
      currentSecond = timeRTC.second();
      updateDisplay(currentHour, currentMinute);
      lastRTCQueryTime = timeNow;
    }
  }
  
  delay(1);
}

