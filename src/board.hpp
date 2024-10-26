/*

   Support for "turtle board" quadcopter

   Adapted from https://github.com/nickrehm/dRehmFlight

   Copyright (C) 2024 Simon D. Levy

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, in version 3.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http:--www.gnu.org/licenses/>.
 */

#pragma once

#include <Wire.h>

#include <MPU6050.h>
#include <oneshot125.hpp>

#include <hackflight.hpp>
#include <ekf.hpp>
#include <rx.hpp>
#include <utils.hpp>
#include <tasks/blink.hpp>
#include <tasks/comms.hpp>

namespace hf {

    class Board {

        public:

            void init(Receiver & rx, const uint8_t ledPin=LED_BUILTIN)
            {
                _ledPin = ledPin;

                // Set up serial debugging
                Serial.begin(500000);
                delay(500);

                // Start receiver
                rx.begin();

                // Star comms with ESP32 radio
                _commsTask.begin();

                // Initialize LED
                pinMode(_ledPin, OUTPUT); 
                digitalWrite(_ledPin, HIGH);

                delay(5);

                // Indicate entering main loop with some quick blinks
                blinkOnStartup(); 

                // Initialize IMU communication
                initImu();

                // Initialize the state estimator
                _ekf.initialize();

                delay(5);

                // Arm OneShot125 motors
                armMotor(_m4_usec);
                armMotor(_m2_usec);
                armMotor(_m1_usec);
                armMotor(_m3_usec);
                _motors.arm();
            }

            void readData(
                    float & dt,
                    Receiver & rx,
                    demands_t & demands,
                    state_t & state)
            {
                // Keep track of what time it is and how much time has elapsed
                // since the last loop
                _usec_curr = micros();      
                static uint32_t _usec_prev;
                dt = (_usec_curr - _usec_prev)/1000000.0;
                _usec_prev = _usec_curr;      

                // Arm vehicle if safe
                if (!_gotFailsafe && (_channels[4] > 1500) &&
                        (_channels[0] < 1050)) {
                    _isArmed = true;
                }

                // LED should be on when armed
                if (_isArmed) {
                    digitalWrite(_ledPin, HIGH);
                }

                // Otherwise, blink LED as heartbeat or failsafe rate
                else {
                    _blinkTask.run(_ledPin, _usec_curr,
                            _gotFailsafe ? 
                            FAILSAFE_BLINK_RATE_HZ : 
                            HEARTBEAT_BLINK_RATE_HZ);
                }


                // Read IMU
                int16_t ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
                _mpu6050.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

                // Convert accelerometer to Gs, with offset
                const axis3_t accel =  {
                    ax / ACCEL_SCALE_FACTOR - ACC_ERROR_X,
                    ay / ACCEL_SCALE_FACTOR - ACC_ERROR_Y,
                    az / ACCEL_SCALE_FACTOR - ACC_ERROR_Z
                };

                // Convert gryo to deg/sec, with offset, negating Z for
                // nose-right positive
                const axis3_t gyro = {
                    gx / GYRO_SCALE_FACTOR - GYRO_ERROR_X, 
                    gy / GYRO_SCALE_FACTOR - GYRO_ERROR_Y,
                    -gz / GYRO_SCALE_FACTOR - GYRO_ERROR_Z
                };

                // Run state estimator to get Euler angles from IMU values
                _ekf.accumulate_gyro(gyro);
                _ekf.accumulate_accel(accel);
                _ekf.predict(_usec_curr/1000);
                _ekf.finalize();
                _ekf.get_vehicle_state(state.phi, state.theta, state.psi);

                // Get angular velocities directly from gyro
                state.dphi = gyro.x;
                state.dtheta = gyro.y;
                state.dpsi = gyro.z;

                //printf("%+3.3f  %+3.3f  %+3.3f\n", state.dphi, state.dtheta, state.dpsi);

                // Convert stick demands to appropriate intervals
                demands.thrust = rx.map(_channels[0],  0.,  1.);
                demands.roll   = rx.map(_channels[1], -1,  +1) *
                    PITCH_ROLL_PRESCALE;
                demands.pitch  = rx.map(_channels[2], -1,  +1) *
                    PITCH_ROLL_PRESCALE;
                demands.yaw    = rx.map(_channels[3], -1,  +1) *
                    YAW_PRESCALE;

                // Run comms
                _commsTask.run(state, _usec_curr, COMMS_RATE_HZ);
            }

            void runMotors(Receiver & rx, const quad_motors_t & motors)
            {
                // Rescale motor values for OneShot125
                _m1_usec = scaleMotor(motors.m1);
                _m2_usec = scaleMotor(motors.m2);
                _m3_usec = scaleMotor(motors.m3);
                _m4_usec = scaleMotor(motors.m4);

                // Turn off motors under various conditions
                cutMotors(_channels[4], _isArmed); 

                // Run motors
                runMotors(); 

                // Get vehicle commands for next loop iteration
                rx.read(_channels, _gotFailsafe);

                // Disarm immiedately on failsafe
                if (_gotFailsafe) {
                    _isArmed = false;
                }

                runLoopDelay(_usec_curr);
            }

        private:

            // FAFO -----------------------------------------------------------
            static const uint32_t LOOP_FREQ_HZ = 2000;

            // IMU ------------------------------------------------------------

            static const uint8_t GYRO_SCALE = MPU6050_GYRO_FS_250;
            static constexpr float GYRO_SCALE_FACTOR = 131.0;

            static const uint8_t ACCEL_SCALE = MPU6050_ACCEL_FS_2;
            static constexpr float ACCEL_SCALE_FACTOR = 16384.0;

            MPU6050 _mpu6050;

            // Motors ---------------------------------------------------------
            const std::vector<uint8_t> MOTOR_PINS = { 3, 4, 5, 6 };
            OneShot125 _motors = OneShot125(MOTOR_PINS);
            uint8_t _m1_usec, _m2_usec, _m3_usec, _m4_usec;

            // Blinkenlights --------------------------------------------------
            static constexpr float HEARTBEAT_BLINK_RATE_HZ = 1.5;
            static constexpr float FAILSAFE_BLINK_RATE_HZ = 0.25;
            uint8_t _ledPin;
            BlinkTask _blinkTask;

            // Comms ----------------------------------------------------------
            static constexpr float COMMS_RATE_HZ = 20;//100;
            CommsTask _commsTask;

            // Max pitch angle in degrees for angle mode (maximum ~70 degrees),
            // deg/sec for rate mode
            static constexpr float PITCH_ROLL_PRESCALE = 30.0;    

            // Max yaw rate in deg/sec
            static constexpr float YAW_PRESCALE = 160.0;     

            // IMU calibration parameters
            static constexpr float ACC_ERROR_X = 0.0;
            static constexpr float ACC_ERROR_Y = 0.0;
            static constexpr float ACC_ERROR_Z = 0.0;
            static constexpr float GYRO_ERROR_X = 0.0;
            static constexpr float GYRO_ERROR_Y= 0.0;
            static constexpr float GYRO_ERROR_Z = 0.0;

            uint32_t _usec_curr;

            uint16_t _channels[6];

            // Safety
            bool _isArmed;
            bool _gotFailsafe;

            // State estimation
            EKF  _ekf;

            static void runLoopDelay(const uint32_t usec_curr)
            {
                //DESCRIPTION: Regulate main loop rate to specified frequency
                //in Hz
                /*
                 * It's good to operate at a constant loop rate for filters to
                 * remain stable and whatnot. Interrupt routines running in the
                 * background cause the loop rate to fluctuate. This function
                 * basically just waits at the end of every loop iteration
                 * until the correct time has passed since the start of the
                 * current loop for the desired loop rate in Hz. 2kHz is a good
                 * rate to be at because the loop nominally will run between
                 * 2.8kHz - 4.2kHz. This lets us have a little room to add
                 * extra computations and remain above 2kHz, without needing to
                 * retune all of our filtering parameters.
                 */
                float invFreq = 1.0/LOOP_FREQ_HZ*1000000.0;

                unsigned long checker = micros();

                // Sit in loop until appropriate time has passed
                while (invFreq > (checker - usec_curr)) {
                    checker = micros();
                }
            }

            void blinkOnStartup(void)
            {
                // These constants are arbitrary, so we hide them here
                static const uint8_t  BLINK_INIT_COUNT = 10;
                static const uint32_t BLINK_INIT_TIME_MSEC = 100;

                for (uint8_t j=0; j<BLINK_INIT_COUNT; j++) {
                    digitalWrite(_ledPin, LOW);
                    delay(BLINK_INIT_TIME_MSEC);
                    digitalWrite(_ledPin, HIGH);
                    delay(BLINK_INIT_TIME_MSEC);
                }
            }

            void initImu() 
            {
                Wire.begin();

                //Note this is 2.5 times the spec sheet 400 kHz max...
                Wire.setClock(1000000); 

                _mpu6050.initialize();

                if (!_mpu6050.testConnection()) {
                    printf("MPU6050 initialization unsuccessful\n");
                    printf("Check MPU6050 wiring or try cycling power\n");
                    while(true) {}
                }

                // From the reset state all registers should be 0x00, so we
                // should be at max sample rate with digital low pass filter(s)
                // off.  All we need to do is set the desired fullscale ranges
                _mpu6050.setFullScaleGyroRange(GYRO_SCALE);
                _mpu6050.setFullScaleAccelRange(ACCEL_SCALE);
            }

            void runMotors() 
            {
                _motors.set(0, _m1_usec);
                _motors.set(1, _m2_usec);
                _motors.set(2, _m3_usec);
                _motors.set(3, _m4_usec);

                _motors.run();
            }

            void cutMotors(const uint32_t chan_5, bool & isArmed) 
            {
                if (chan_5 < 1500 || !isArmed) {
                    isArmed = false;
                    _m1_usec = 120;
                    _m2_usec = 120;
                    _m3_usec = 120;
                    _m4_usec = 120;

                }
            }

            static void armMotor(uint8_t & m_usec)
            {
                // OneShot125 range from 125 to 250 usec
                m_usec = 125;
            }

            static uint8_t scaleMotor(const float mval)
            {
                return Utils::u8constrain(mval*125 + 125, 125, 250);

            }


    };

}
