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

#include <alignment/rotate270.h>
#include <core/clock.h>
#include <core/mixers/fixedpitch/quadxbf.h>
#include <escs/dshot/bitbang.h>
#include <hackflight.h>
#include <imus/softquat/mpu6000.h>
#include <leds/stm32f4.h>
#include <tasks/receivers/sbus.h>
#include <serial.h>

#include "hardware_init.h"

#include <vector>
using namespace std;

int main(void)
{
    hardwareInit();

    spiInit(
            0x15,  // sck  = PA5
            0x16,  // miso = PA6
            0x17); // mosi = PA7

    spiInitBusDMA();

    static AnglePidController anglePid(
        1.441305,     // Rate Kp
        48.8762,      // Rate Ki
        0.021160,     // Rate Kd
        0.0165048,    // Rate Kf
        0.0); // 3.0; // Level Kp

    // static Mpu6000Imu imu(0); // dummy value for IMU interrupt pin
    static Mpu6000 imu(
            NULL,
            0x14,  // CS pin = PA4
            0x34,  // EXTI pin = PC4
            2000); // gyro scale DPS

    vector<PidController *> pids = {&anglePid};

    vector<uint8_t> motorPins = {0x20, 0x21, 0x13, 0x12};

    static SbusReceiver rx(SERIAL_PORT_USART3);

    static Mixer mixer = QuadXbfMixer::make();

    static DshotBitbangEsc esc(motorPins);

    static Stm32F4Led led(0x25); // PB5

    static Hackflight hf(rx, imu, imuRotate270, pids, mixer, esc, led);

    hf.begin();

    while (true) {

        hf.step();
    }

    return 0;
}
