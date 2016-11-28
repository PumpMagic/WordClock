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
// CD4094 STROBE pin; shared by all of them
#define REGISTER_STROBE_PIN 6
// CD4094 DATA pin; fed only to the first one
#define REGISTER_DATA_PIN 7
// CD4094 CLOCK pin; shared by all of them
#define REGISTER_CLOCK_PIN 8
// CD4094 OUTPUT ENABLE pin; shared by all of them
#define REGISTER_OUTPUT_ENABLE_PIN 10
// Brightness adjustment input; expected to be the output of a potentiometer between 5V and GND
#define BRIGHTNESS_ADJUST_PIN A0
// Minute advance button input; expected to be a normally open signal, pulled up internally and
// connected to GND when depressed
#define MINUTE_ADVANCE_BUTTON_PIN 2
// Hour advance button input; expected to be a normally open signal, pulled up internally and
// connected to GND when depressed
#define HOUR_ADVANCE_BUTTON_PIN 3
//@debug Debug LED pin; used for debugging
#define DEBUG_LED_PIN 9
// Amount of time to hold the CD4094 STROBE signal high after a write
#define REGISTER_STROBE_DURATION_MS 2

// Tuneable software constants
// How long a button's state must be constant before we treat it as valid
#define BUTTON_DEBOUNCE_MS 50UL
// How long a button must be held before we assume the user wants another action
#define BUTTON_HOLD_ACTION_REPEAT_PERIOD 1000UL
// How long to wait after the last time adjustment before we assume the user is done setting the time
// This should be at least a few milliseconds longer than BUTTON_HOLD_ACTION_REPEAT_PERIOD
#define BUTTON_INACTION_RTC_UPDATE_DELAY 5000UL
// If our ADC read returns a value lower than this, we treat it as minimum
#define POTENTIOMETER_ERROR_MARGIN_LOW 50
// If our ADC read returns a value higher than this, we treat it as maximum
#define POTENTIOMETER_ERROR_MARGIN_HIGH 973
// How often we query the RTC for the current time during normal operation
#define RTC_QUERY_PERIOD_MILLIS 5000UL
// What time we reset the RTC to in the event that it's lost its power
#define RTC_RESET_YEAR    2017
#define RTC_RESET_MONTH   1
#define RTC_RESET_DAY     1
#define RTC_RESET_HOUR    0
#define RTC_RESET_MINUTE  0
#define RTC_RESET_SECOND  0

// Non-tuneable software constants
#define BITS_PER_BYTE 8
#define PWM_DUTY_MAX 255
#define MS_PER_SECOND 1000UL

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
// The current time, as understood locally
static uint8_t currentHour = 0;
static uint8_t currentMinute = 0;
static uint8_t currentSecond = 0;
// The last time we queried the RTC
static unsigned long lastRTCQueryTime = 0;
// Whether or not the time has been adjusted locally through the use of the time adjustment buttons
static bool timeLocallyUpdated = false;
// The current time, as understood locally, encoded such that each bit indicates the presence of
// a given word in word_bit_map
static uint32_t localWordRegister = 0;
// Shared reference to the RTC chip
static RTC_DS3231 rtc;


/**
 * Get a specific byte of a number, where byte 0 is the least significant, byte 1 is one higher, etc.
 */
uint8_t getByte(uint32_t number, uint8_t byteIndex) {
  uint32_t shifted = number >> (byteIndex * BITS_PER_BYTE);
  uint8_t targetByte = shifted & 0xFF;
  
  return targetByte;
}


/**
 * Locally "enable" a given word by raising the corresponding bit to the local word register
 * Note that this function will not update the display - to do so call flushWordRegister()
 */
void enableWord(word_bit_map theWord) {
  uint32_t mask = ((uint32_t) 0x01) << theWord;
  localWordRegister |= mask;
}


/**
 * Locally "disable" all words by lowering all bits of the local word register
 * Note that this function will not update the display - to do so call flushWordRegister()
 */
void disableAllWords() {
  localWordRegister = 0;
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

  // Convert 24-hour time to 12-hour time
  if (hour > 12) { hour = hour - 12; }
  if (hour == 0) { hour = 12; }

  // Handle the "minute" part of the time
  if (minute < 5) {
    enableWord(OCLOCK);
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
 * Update the clock display, as well as our local time, using the RTC's reported time
 */
void updateDisplayFromRTC() {
  DateTime timeRTC = rtc.now();
  currentHour = timeRTC.hour();
  currentMinute = timeRTC.minute();
  currentSecond = timeRTC.second();
  updateDisplay(currentHour, currentMinute);
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
  pinMode(DEBUG_LED_PIN, OUTPUT); //@debug
  
  pinMode(BRIGHTNESS_ADJUST_PIN, INPUT);
  pinMode(MINUTE_ADVANCE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(HOUR_ADVANCE_BUTTON_PIN, INPUT_PULLUP);
  
  /*
  testDisplay1();
  testDisplay2();
  */

  //@todo consider doing something on RTC failure
  rtc.begin();

  digitalWrite(DEBUG_LED_PIN, HIGH); //@debug
  // If the RTC's lost power, reset it
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(RTC_RESET_YEAR, RTC_RESET_MONTH, RTC_RESET_DAY,
                        RTC_RESET_HOUR, RTC_RESET_MINUTE, RTC_RESET_SECOND));
    digitalWrite(DEBUG_LED_PIN, LOW); //@debug
  }

  updateDisplayFromRTC();
  lastRTCQueryTime = millis();
}


/**
 * Handle the external brightness control
 * @todo consider adding schmitt triggers to handle hysteresis
 */
void handleBrightnessControl() {
  // Read the analog brightness pin
  int analogBrightnessValue = analogRead(BRIGHTNESS_ADJUST_PIN);

  // Map the read analog value to a PWM duty cycle value
  // analogRead returns [0, 1023] for [0, VCC]V. The voltage range is actually less thanks to nonideal potentiometer
  // properties and finite pin input impedance, so condense it
  // The output PWM duty cycle range is [0, PWM_DUTY_MAX]
  int pwmDuty = PWM_DUTY_MAX;
  if (analogBrightnessValue < POTENTIOMETER_ERROR_MARGIN_LOW) {
    pwmDuty = 0;
  } else if (analogBrightnessValue > POTENTIOMETER_ERROR_MARGIN_HIGH) {
    pwmDuty = PWM_DUTY_MAX;
  } else {
    // map [POTENTIOMETER_ERROR_MARGIN_LOW, POTENTIOMETER_ERROR_MARGIN_HIGH] to [0, PWM_DUTY_MAX]
    double divideFactor = ((double)(POTENTIOMETER_ERROR_MARGIN_HIGH-POTENTIOMETER_ERROR_MARGIN_LOW)) / ((double)PWM_DUTY_MAX);
    double pwmDutyDouble = ((double)(analogBrightnessValue - POTENTIOMETER_ERROR_MARGIN_LOW)) / divideFactor;
    pwmDuty = pwmDutyDouble;
  }

  // For safety, in case of rounding or human errors - limit the PWM duty cycle
  if (pwmDuty > PWM_DUTY_MAX) {
    pwmDuty = PWM_DUTY_MAX;
  }

  analogWrite(REGISTER_OUTPUT_ENABLE_PIN, pwmDuty);
}


/**
 * Advance the current hour
 */
static void advanceHour(uint8_t* hour) {
  if (*hour < 23) {
    *hour = *hour + 1;
  } else {
    *hour = 0;
  }
}


/**
 * Advance the current minute, and adjust the current hour if appropriate
 */
static void advanceMinute(uint8_t* hour, uint8_t* minute) {
  if (*minute < 59) {
    // Typical case
    *minute = *minute + 1;
  } else {
    // Minute 59 -> 0 transition; increment the hour, too
    *minute = 0;
    *hour = *hour + 1;
  }
}


/**
 * Handle the external time adjust buttons
 * Return whether or not the user is currently in the process of changing the time
 * @todo duplicated code
 */
void handleTimeAdjustButtons() {
  // Debouncing variables
  static unsigned long lastHourButtonChange = 0;
  static unsigned long lastMinuteButtonChange = 0;
  static int lastHourButtonValue = HIGH;
  static int lastMinuteButtonValue = HIGH;
  // Variables to enable holding a time adjust button to gradually change the time
  static bool hourButtonBeingHeld = false;
  static bool minuteButtonBeingHeld = false;
  static unsigned long nextHourAdvanceTime = 0;
  static unsigned long nextMinuteAdvanceTime = 0;
  static unsigned long updateRTCTime = 0;


  int hourButtonValue = digitalRead(HOUR_ADVANCE_BUTTON_PIN);
  int minuteButtonValue = digitalRead(MINUTE_ADVANCE_BUTTON_PIN);

  // Debouncing logic
  unsigned long timeNow = millis();
  if (hourButtonValue != lastHourButtonValue) {
    lastHourButtonChange = timeNow;
  }
  if (minuteButtonValue != lastMinuteButtonValue) {
    lastMinuteButtonChange = timeNow;
  }
  lastHourButtonValue = hourButtonValue;
  lastMinuteButtonValue = minuteButtonValue;

  if ((timeNow - lastHourButtonChange) > BUTTON_DEBOUNCE_MS) {
    // the button state is settled
    // if it's being depressed (connected to ground), then increment the hour once immediately
    // and then once every so often until released
    if (hourButtonValue == LOW) {
      if (hourButtonBeingHeld == false) {
        advanceHour(&currentHour);
        currentSecond = 0;
        timeLocallyUpdated = true;
        nextHourAdvanceTime = timeNow + BUTTON_HOLD_ACTION_REPEAT_PERIOD;
        updateRTCTime = timeNow + BUTTON_INACTION_RTC_UPDATE_DELAY;
        updateDisplay(currentHour, currentMinute);
        hourButtonBeingHeld = true;
      } else if (timeNow >= nextHourAdvanceTime) {
        advanceHour(&currentHour);
        currentSecond = 0;
        nextHourAdvanceTime = timeNow + BUTTON_HOLD_ACTION_REPEAT_PERIOD;
        updateDisplay(currentHour, currentMinute);
        updateRTCTime = timeNow + BUTTON_INACTION_RTC_UPDATE_DELAY;
      }
    } else {
      hourButtonBeingHeld = false;
    }
  }

  if ((timeNow - lastMinuteButtonChange) > BUTTON_DEBOUNCE_MS) {
    // the button state is settled
    // if it's being depressed (connected to ground), then increment the minute once immediately
    // and then once every so often until released
    if (minuteButtonValue == LOW) {
      if (minuteButtonBeingHeld == false) {
        advanceMinute(&currentHour, &currentMinute);
        currentSecond = 0;
        timeLocallyUpdated = true;
        nextMinuteAdvanceTime = timeNow + BUTTON_HOLD_ACTION_REPEAT_PERIOD;
        updateRTCTime = timeNow + BUTTON_INACTION_RTC_UPDATE_DELAY;
        updateDisplay(currentHour, currentMinute);
        minuteButtonBeingHeld = true;
      } else if (timeNow >= nextMinuteAdvanceTime) {
        advanceMinute(&currentHour, &currentMinute);
        currentSecond = 0;
        nextMinuteAdvanceTime = timeNow + BUTTON_HOLD_ACTION_REPEAT_PERIOD;
        updateRTCTime = timeNow + BUTTON_INACTION_RTC_UPDATE_DELAY;
        updateDisplay(currentHour, currentMinute);
      }
    } else {
      minuteButtonBeingHeld = false;
    }
  }

  if (timeLocallyUpdated == true && timeNow > updateRTCTime && hourButtonBeingHeld == false && minuteButtonBeingHeld == false) {
    DateTime timeRTC = rtc.now();
    rtc.adjust(DateTime(timeRTC.year(), timeRTC.month(), timeRTC.day(), currentHour, currentMinute, (BUTTON_INACTION_RTC_UPDATE_DELAY / MS_PER_SECOND)));
    timeLocallyUpdated = false;
    updateRTCTime = 0;
  }
}


/**
 * Loop function
 * This gets called repeatedly and indefinitely after setup() is called
 */
void loop() {
  handleBrightnessControl();
  handleTimeAdjustButtons();

  unsigned long timeNow = millis();
  if ((timeNow - lastRTCQueryTime) > RTC_QUERY_PERIOD_MILLIS) {
    if (timeLocallyUpdated == false) {
      updateDisplayFromRTC();
      lastRTCQueryTime = timeNow;
    }
  }
  
  delay(1);
}

