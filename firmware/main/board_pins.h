/*
 * Liahona ball — ALL pin assignments and hardware settings in one place.
 *
 * Physical header pins refer to the 40-pin header on the
 * Waveshare ESP32-P4-WIFI6-Touch-LCD-4C. Change wiring here only.
 */
#pragma once
#include "driver/gpio.h"

/* ---- Shared I2C bus (SH1.0 / Stemma QT port + header pins 3/5) ----
 * The bus itself is created by the Waveshare BSP (SDA GPIO7, SCL GPIO8).
 * On-board devices: ES8311 codec 0x18, mic ADC 0x40, GT911 touch 0x5D. */
#define I2C_ADDR_GPS        0x10    /* Adafruit PA1010D mini GPS        */
#define I2C_ADDR_MAG        0x1C    /* Adafruit LIS3MDL magnetometer    */
#define I2C_ADDR_NFC        0x24    /* PN532 (DIP: 1 ON, 2 OFF = I2C)   */

/* ---- LoRa radio (SX1262 module), SPI ---- */
#define LORA_PIN_SCK        GPIO_NUM_50   /* header pin 23 */
#define LORA_PIN_MOSI       GPIO_NUM_3    /* header pin 19 */
#define LORA_PIN_MISO       GPIO_NUM_2    /* header pin 21 */
#define LORA_PIN_NSS        GPIO_NUM_49   /* header pin 26 */
#define LORA_PIN_RESET      GPIO_NUM_4    /* header pin 18 */
#define LORA_PIN_BUSY       GPIO_NUM_5    /* header pin 16 */
#define LORA_PIN_DIO1       GPIO_NUM_22   /* header pin 12 */

/* ---- Band LEDs (WS2812 / NeoPixel), via 74AHCT125 + 330 ohm ---- */
#define LED_PIN_DATA        GPIO_NUM_47   /* header pin 37 */
#define LED_COUNT           24            /* TODO: set to the real LED count */
#define LED_MAX_BRIGHTNESS  64            /* 0-255; keeps current and heat down */

/* ---- Lid switch (SS-5GL style snap switch) ----
 * COM -> GND (pin 39), NO -> GPIO48 (pin 33). Internal pull-up.
 * Pressed (lid closed) reads LOW. */
#define LID_PIN             GPIO_NUM_48   /* header pin 33 */
#define LID_CLOSED_LEVEL    0

/* ---- Battery sense (optional): BAT -> 100k -> GPIO21 -> 100k -> GND ---- */
#define BATT_PIN            GPIO_NUM_21   /* header pin 11 */
#define BATT_DIVIDER        2.0f          /* 100k / 100k */
#define BATT_PRESENT_MIN_V  2.5f          /* below this = not wired */

/* ---- Compass ----
 * Magnetic declination where the ball is used (degrees, east positive).
 * GPS bearings are true north; the magnetometer gives magnetic north.
 * Camp: Tieton Rd, Naches WA (~46.66 N, 121.0 W): +14.3 (WMM2025, 2026-27).
 * Changes < 0.1 deg/year; recheck at ngdc.noaa.gov for a different site. */
#define MAG_DECLINATION_DEG  14.3f

/* ---- Content on the microSD card ---- */
#define CONTENT_DIR         "/sdcard/liahona"
#define CONTENT_MANIFEST    CONTENT_DIR "/manifest.json"
