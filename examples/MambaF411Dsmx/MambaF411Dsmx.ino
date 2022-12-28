/*
   Copyright (c) 2022 Simon D. Levy

   This file is part of Hackflight.

   Hackflight is free software: you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation, either version 3 of the License, or (at your option)
   any later version.

   Hackflight is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
   more details.

   You should have received a copy of the GNU General Public License along with
   Hackflight. If not, see <https://www.gnu.org/licenses/>.
 */

#include <hackflight.h>
#include <board/stm32/stm32f4/stm32f411.h>
#include <core/mixers/fixedpitch/quadxbf.h>
#include <esc/dshot.h>
#include <imu/real/softquat/mpu6x00.h>
#include <receiver/real/dsmx.h>

#include <vector>
using namespace std;

// IMU
static const uint8_t MOSI_PIN = PA7;
static const uint8_t MISO_PIN = PA6;
static const uint8_t SCLK_PIN = PA5;
static const uint8_t CS_PIN   = PA4;
static const uint8_t EXTI_PIN = PB0;

static vector <uint8_t> MOTOR_PINS = {PB3, PB4, PB6, PB7};

//static const uint8_t LED_PIN  = PC13; // orange
static const uint8_t LED_PIN  = PC14; // blue

static SPIClass _spi(MOSI_PIN, MISO_PIN, SCLK_PIN);

static AnglePidController _anglePid(
        1.441305,     // Rate Kp
        48.8762,      // Rate Ki
        0.021160,     // Rate Kd
        0.0165048,    // Rate Kf
        0.0); // 3.0; // Level Kp

static Stm32F411Board * _board;
static Mpu6x00 * _imu;
static DsmxReceiver _rx;

static vector<PidController *> _pids = {&_anglePid};

// Motor interrupt
extern "C" void handleDmaIrq(void)
{
    _board->handleDmaIrq(0);
}

// IMU interrupt
static void handleImuInterrupt(void)
{
    _imu->handleInterrupt();
}

// Receiver interrupt
void serialEvent1(void)
{
    _rx.read(Serial1);
}

static Mixer _mixer = QuadXbfMixer::make();

void setup(void)
{
    pinMode(EXTI_PIN, INPUT);
    attachInterrupt(EXTI_PIN, handleImuInterrupt, RISING);  

    static Mpu6x00 imu(RealImu::rotate180, _spi, CS_PIN);

    static DshotEsc esc(&MOTOR_PINS);

    static Stm32F411Board board(_rx, imu, _pids, _mixer, esc, LED_PIN);

    _board = &board;
    _imu = &imu;

    Serial1.begin(115200);

    _board->begin();
}

void loop(void)
{
    _board->step();
}
