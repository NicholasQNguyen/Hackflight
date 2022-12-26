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

#pragma once

#include <stdint.h>

#include "task.h"
#include "esc.h"
#include "imu.h"
#include "msp/usb.h"
#include "receiver.h"

class UsbTask : public Task {

    private:

        static int16_t rad2degi(float rad)
        {
            return (int16_t)Math::rad2deg(rad);
        }

        static float scale(const float value)
        {
            return 1000 + 1000 * value;
        }

        Esc *            m_esc;
        Arming *         m_arming;
        Receiver *       m_receiver;
        VehicleState *   m_vstate;

        UsbMsp m_msp;

        bool m_gotRebootRequest;

        void sendShorts(
                const uint8_t messageType, const int16_t src[], const uint8_t count)
        {
            m_msp.serializeShorts(messageType, src, count);
        }

    protected:

        virtual void fun(uint32_t usec) override
        {
            (void)usec;

            while (m_msp.available()) {

                auto byte = m_msp.read();

                if (m_msp.isIdle() && byte == 'R') {
                    m_gotRebootRequest = true;
                }

                auto messageType = m_msp.parse(byte);

                switch (messageType) {

                    case 105: // RC
                        {
                            int16_t channels[] = {
                                (int16_t)m_receiver->getRawThrottle(),
                                (int16_t)m_receiver->getRawRoll(),
                                (int16_t)m_receiver->getRawPitch(),
                                (int16_t)m_receiver->getRawYaw(),
                                (int16_t)scale(m_receiver->getRawAux1()),
                                (int16_t)scale(m_receiver->getRawAux2())
                            };

                            sendShorts(105, channels, 6);

                        } break;

                    case 108: // ATTITUDE
                        {
                            int16_t angles[] = {
                                (int16_t)(10 * rad2degi(m_vstate->phi)),
                                (int16_t)(10 * rad2degi(m_vstate->theta)),
                                (int16_t)rad2degi(m_vstate->psi)
                            };

                            sendShorts(108, angles, 3);

                        } break;

                    case 214: // SET_MOTORS
                        {
                            motors[0] =
                                m_esc->convertFromExternal(m_msp.parseShort(0));
                            motors[1] =
                                m_esc->convertFromExternal(m_msp.parseShort(1));
                            motors[2] =
                                m_esc->convertFromExternal(m_msp.parseShort(2));
                            motors[3] =
                                m_esc->convertFromExternal(m_msp.parseShort(3));

                        } break;

                    default:
                        break;
                }
            }
        }

    public:

        UsbTask(void) 
            : Task(100) // Hz
        { 
        }

        float motors[MAX_SUPPORTED_MOTORS];

        void begin(
                Esc * esc,
                Arming * arming,
                Receiver * receiver,
                VehicleState * vstate)
        {
            m_msp.begin();

            m_esc = esc;
            m_arming = arming;
            m_vstate = vstate;
            m_receiver = receiver;
        }

        bool gotRebootRequest(void)
        {
            return m_gotRebootRequest;
        }

}; // class UsbMsp
