/*
   Copyright (c) 2022 Simon D. Levy

   This file is part of Hackflight.

   Hackflight is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software
   Foundation, either version 3 of the License, or (at your option) any later
   version.

   Hackflight is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
   PARTICULAR PURPOSE. See the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with
   Hackflight. If not, see <https://www.gnu.org/licenses/>.
 */

#include <Wire.h>
#include <SPI.h>

#include <VL53L5cx.h>
#include <PAA3905_MotionCapture.h>

#include "hackflight.h"
#include "debugger.h"
#include "msp/arduino.h"

// Pins ---------------------------------------------------------------

static const uint8_t VL53L5_INT_PIN  = 15;
static const uint8_t VL53L5_LPN_PIN  = 2;
static const uint8_t PAA3905_CS_PIN  = 5;
static const uint8_t PAA3905_MOT_PIN = 4;
static const uint8_t LED_PIN         = 25;

// MSP message IDs ----------------------------------------------------

static const uint8_t RANGER_MSG_TYPE = 221;  // VL53L5 ranger
static const uint8_t MOCAP_MSG_TYPE  = 222;  // PAA3905 motion capture

// Helper -------------------------------------------------------------

static void sendData(
        ArduinoMsp & msp,
        const uint8_t messageType,
        const int16_t data[],
        const uint8_t count) 
{
    msp.serializeShorts(messageType, data, count);
    msp.sendPayload();
}

// VL53L5 -------------------------------------------------------------

// Set to 0 for continuous mode
static const uint8_t VL53L5_INTEGRAL_TIME_MS = 10;

static VL53L5cx _ranger(
        Wire, 
        VL53L5_LPN_PIN, 
        VL53L5_INTEGRAL_TIME_MS,
        VL53L5cx::RES_4X4_HZ_1);

static volatile bool _gotRangerInterrupt;

static void rangerInterruptHandler() 
{
    _gotRangerInterrupt = true;
}

static void startRanger(void)
{
    Wire.begin();                
    Wire.setClock(400000);      
    delay(1000);

    pinMode(VL53L5_INT_PIN, INPUT);     

    if (VL53L5_INT_PIN > 0) {
        attachInterrupt(VL53L5_INT_PIN, rangerInterruptHandler, FALLING);
    }

    _ranger.begin();
}

static void checkRanger(ArduinoMsp & msp)
{
    static int16_t data[16];

    if (VL53L5_INT_PIN == 0 || _gotRangerInterrupt) {

        _gotRangerInterrupt = false;

        while (!_ranger.dataIsReady()) {
            delay(10);
        }

        _ranger.readData();

        for (auto i=0; i<_ranger.getPixelCount(); i++) {
            data[i] = _ranger.getDistanceMm(i);
        }
    } 

    sendData(msp, RANGER_MSG_TYPE, data, 16);
}

// PAA3905 -----------------------------------------------------------

PAA3905_MotionCapture _mocap(
        SPI,
        PAA3905_CS_PIN,
        PAA3905::DETECTION_STANDARD,
        PAA3905::AUTO_MODE_01,
        PAA3905::ORIENTATION_NORMAL,
        0x2A); // resolution 0x00 to 0xFF

static volatile bool _gotMotionInterrupt;

void motionInterruptHandler()
{
    _gotMotionInterrupt = true;
}

static void startMocap(void)
{
    // Start SPI
    SPI.begin();

    delay(100);

    // Check device ID as a test of SPI communications
    if (!_mocap.begin()) {
        HfDebugger::reportForever("PAA3905 initialization failed");
    }

    pinMode(PAA3905_MOT_PIN, INPUT); 
    attachInterrupt(PAA3905_MOT_PIN, motionInterruptHandler, FALLING);
}

static void checkMocap(ArduinoMsp & msp)
{
    static int16_t data[2];

    if (_gotMotionInterrupt) {

        _gotMotionInterrupt = false;

        _mocap.readBurstMode(); // use burst mode to read all of the data

        if (_mocap.motionDataAvailable()) { 

            uint8_t surfaceQuality = _mocap.getSurfaceQuality();

            uint32_t shutter = _mocap.getShutter();

            PAA3905_MotionCapture::lightMode_t lightMode = _mocap.getLightMode();

            // Send X,Y if surface quality and shutter are above thresholds
            if (_mocap.dataAboveThresholds(lightMode, surfaceQuality, shutter)) {
                data[0] = _mocap.getDeltaX();
                data[1] = _mocap.getDeltaY();
            }
        }
    }

    sendData(msp, MOCAP_MSG_TYPE, data, 2);
}

static void updateLed(void)
{
    static uint32_t _prev;
    static bool _state;

    uint32_t msec = millis();

    if (msec - _prev > 500) {
        _state = !_state;
        digitalWrite(LED_PIN, _state);
        _prev = msec;
    }
}

// ------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);

    pinMode(LED_PIN, OUTPUT);

    startRanger();
    startMocap();
}

void loop()
{
    static ArduinoMsp _msp;

    checkRanger(_msp);
    checkMocap(_msp);

    updateLed();
}
