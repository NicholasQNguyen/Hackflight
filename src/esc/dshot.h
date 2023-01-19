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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "core/clock.h"
#include "core/constrain.h"
#include "core/motors.h"
#include "esc.h"

class DshotEsc : public Esc {

    protected:

        typedef enum {
            DSHOT150,
            DSHOT300,
            DSHOT600,
        } protocol_t;

    private:

        static const uint8_t MOTOR_COUNT = 4;

        static const uint16_t MIN_VALUE = 48;
        static const uint16_t MAX_VALUE = 2047;
        static const uint16_t STOP_VALUE = 0;
        static const uint16_t VALUE_RANGE = MAX_VALUE - MIN_VALUE;
        static const uint32_t COMMAND_DELAY_US = 1000;
        static const uint32_t INITIAL_DELAY_US = 10000;

        // Time to separate dshot beacon and armining/disarming events
        static const uint32_t BEACON_GUARD_DELAY_US = 1200000;  

        static const uint8_t MAX_COMMANDS = 3;

        typedef enum {
            CMD_MOTOR_STOP = 0,
            CMD_SPIN_DIRECTION_NORMAL = 20,
        } commands_e;

        typedef enum {
            COMMAND_STATE_IDLEWAIT,   // waiting for motors to go idle
            COMMAND_STATE_STARTDELAY, // initial delay before a sequence of cmds
            COMMAND_STATE_ACTIVE,     // actively sending command
            COMMAND_STATE_POSTDELAY   // delay period after the cmd has been sent
        } commandVehicleState_e;

        typedef struct {
            commandVehicleState_e state;
            uint32_t nextCommandCycleDelay;
            uint32_t delayAfterCommandUs;
            uint8_t repeats;
            uint8_t command[Motors::MAX_SUPPORTED];
        } commandControl_t;

        commandControl_t m_commandQueue[MAX_COMMANDS + 1];
        uint8_t m_commandQueueHead;
        uint8_t m_commandQueueTail;

        bool m_enabled = false;

        vector <uint8_t> * m_pins;

        bool isLastCommand(void)
        {
            return ((m_commandQueueTail + 1) % (MAX_COMMANDS + 1) == m_commandQueueHead);
        }

        commandControl_t * addCommand(void)
        {
            auto newHead = (m_commandQueueHead + 1) % (MAX_COMMANDS + 1);
            if (newHead == m_commandQueueTail) {
                return NULL;
            }
            auto * control = &m_commandQueue[m_commandQueueHead];
            m_commandQueueHead = newHead;
            return control;
        }

        static uint32_t commandCyclesFromTime(const uint32_t delayUs)
        {
            // Find the minimum number of motor output cycles needed to
            // provide at least delayUs time delay

            return (delayUs + Clock::PERIOD() - 1) / Clock::PERIOD();
        }

        static float scaleRangef(
                const float x,
                const float srcFrom,
                const float srcTo,
                const float destFrom,
                const float destTo)
        {
            const auto a = (destTo - destFrom) * (x - srcFrom);
            const auto b = srcTo - srcFrom;
            return (a / b) + destFrom;
        }

        // This function is used to synchronize the dshot command output timing with
        // the normal motor output timing tied to the PID loop frequency. A "true"
        // result allows the motor output to be sent, "false" means delay until next
        // loop. So take the example of a dshot command that needs to repeat 10 times
        // at 1ms intervals.  If we have a 8KHz PID loop we'll end up sending the dshot
        // command every 8th motor output.
        bool commandOutputIsEnabled(void)
        {
            commandControl_t* command = &m_commandQueue[m_commandQueueTail];
            switch (command->state) {
                case COMMAND_STATE_IDLEWAIT:
                    command->state = COMMAND_STATE_STARTDELAY;
                    command->nextCommandCycleDelay =
                        commandCyclesFromTime(INITIAL_DELAY_US);
                    break;

                case COMMAND_STATE_STARTDELAY:
                    if (command->nextCommandCycleDelay) {
                        --command->nextCommandCycleDelay;
                        return false;  // Delay motor output until start of command sequence
                    }
                    command->state = COMMAND_STATE_ACTIVE;
                    command->nextCommandCycleDelay = 0;  // first iter of repeat happens now
                    [[fallthrough]];

                case COMMAND_STATE_ACTIVE:
                    if (command->nextCommandCycleDelay) {
                        --command->nextCommandCycleDelay;
                        return false;  // Delay motor output until the next command repeat
                    }

                    command->repeats--;
                    if (command->repeats) {
                        command->nextCommandCycleDelay =
                            commandCyclesFromTime(COMMAND_DELAY_US);
                    } else {
                        command->state = COMMAND_STATE_POSTDELAY;
                        command->nextCommandCycleDelay =
                            commandCyclesFromTime(command->delayAfterCommandUs);
                        if (!isLastCommand() && command->nextCommandCycleDelay > 0) {
                            // Account for the 1 extra motor output loop between
                            // commands.  Otherwise the inter-command delay will be
                            // COMMAND_DELAY_US + 1 loop.
                            command->nextCommandCycleDelay--;
                        }
                    }
                    break;

                case COMMAND_STATE_POSTDELAY:
                    if (command->nextCommandCycleDelay) {
                        --command->nextCommandCycleDelay;
                        return false;  // Delay motor output until end of post-command delay
                    }
                    if (commandQueueUpdate()) {
                        // Will be true if the command queue is not empty and we
                        // want to wait for the next command to start in sequence.
                        return false;
                    }
            }

            return true;
        }

        bool commandQueueIsEmpty(void)
        {
            return m_commandQueueHead == m_commandQueueTail;
        }

        uint16_t commandGetCurrent(const uint8_t index)
        {
            return m_commandQueue[m_commandQueueTail].command[index];
        }

        protocol_t m_protocol;

        bool commandQueueUpdate(void)
        {
            if (!commandQueueIsEmpty()) {
                m_commandQueueTail = (m_commandQueueTail + 1) % (MAX_COMMANDS + 1);
                if (!commandQueueIsEmpty()) {
                    // There is another command in the queue so update it so it's ready
                    // to output in sequence. It can go directly to the
                    // COMMAND_STATE_ACTIVE state and bypass the
                    // COMMAND_STATE_IDLEWAIT and COMMAND_STATE_STARTDELAY
                    // states.
                    commandControl_t* nextCommand = &m_commandQueue[m_commandQueueTail];
                    nextCommand->state = COMMAND_STATE_ACTIVE;
                    nextCommand->nextCommandCycleDelay = 0;
                    return true;
                }
            }
            return false;
        }

        uint16_t prepareDshotPacket(uint16_t value)
        {
            uint16_t packet = value << 1;

            // compute checksum
            unsigned csum = 0;
            unsigned csum_data = packet;
            for (auto i=0; i<3; i++) {
                csum ^=  csum_data;   // xor data by nibbles
                csum_data >>= 4;
            }

            // append checksum
            csum &= 0xf;
            packet = (packet << 4) | csum;

            return packet;
        }

        bool commandIsProcessing(void)
        {
            if (commandQueueIsEmpty()) {
                return false;
            }

            commandControl_t* command = &m_commandQueue[m_commandQueueTail];

            return
                command->state == COMMAND_STATE_STARTDELAY ||
                command->state == COMMAND_STATE_ACTIVE ||
                (command->state == COMMAND_STATE_POSTDELAY && !isLastCommand()); 
        }

    public:

        DshotEsc(vector<uint8_t> * motorPins, protocol_t protocol=DSHOT600) 
        {
            m_pins = motorPins;
            m_protocol = protocol;
        }

        virtual void begin(void) override 
        {
            uint32_t outputFreq =
                m_protocol == DSHOT150 ? 150 :
                m_protocol == DSHOT300 ? 300 :
                600;

            board->dmaInit(m_pins, 1000 * outputFreq);

            m_enabled = true;
        }

        virtual float convertFromExternal(const uint16_t value) override 
        {
            auto constrainedValue = constrain_u16(value, 1000, 2000);

            return constrainedValue == 1000 ?
                (float)CMD_MOTOR_STOP :
                scaleRangef(constrainedValue, 1001, 2000, MIN_VALUE, MAX_VALUE);
        }

        virtual float getMotorValue(const float input) override
        {
            const auto output = valueLow() + (MAX_VALUE - valueLow()) * input;

            const auto lowValue = valueLow();

            return constrain_f(output, lowValue, MAX_VALUE);
        }

        virtual bool isReady(const uint32_t usec) override 
        {
            return usec >= BEACON_GUARD_DELAY_US;
        }

        float valueLow(void)
        {
            return MIN_VALUE + 0.045 * VALUE_RANGE;
        }

        virtual void stop(void) override 
        {
            commandControl_t *commandControl = addCommand();

            if (commandControl) {
                commandControl->repeats = 10;
                commandControl->delayAfterCommandUs = COMMAND_DELAY_US;
                for (auto i=0; i<MOTOR_COUNT; i++) {
                    commandControl->command[i] = CMD_SPIN_DIRECTION_NORMAL;
                }
                // we can skip the motors idle wait state
                commandControl->state = COMMAND_STATE_STARTDELAY;
                commandControl->nextCommandCycleDelay =
                    commandCyclesFromTime(INITIAL_DELAY_US);
            }
        }

        // unsafe
        virtual void write(const float values[]) override
        {
            if (m_enabled) {

                board->dmaUpdateStart();

                for (auto i=0; i <MOTOR_COUNT; i++) {

                    uint16_t ivalue = (uint16_t)values[i];

                    if (commandIsProcessing()) {
                        ivalue = commandGetCurrent(i);
                    }

                    uint16_t packet = prepareDshotPacket(ivalue);

                    board->dmaWriteMotor(i, packet);
                }

                if (!commandQueueIsEmpty()) {
                    if (!commandOutputIsEnabled()) {
                        return;
                    }
                }
                board->dmaUpdateComplete();
            }
        }

}; // class DshotEsc
