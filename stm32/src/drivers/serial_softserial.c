/*
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

#include <stdbool.h>
#include <stdint.h>


#include "macros.h"

#include "platform.h"
#include "atomic.h"
#include "nvic.h"
#include "io.h"
#include "serialdev.h"
#include "timer.h"
#include "serial_softserial.h"

#define RX_TOTAL_BITS 10
#define TX_TOTAL_BITS 10

#define MAX_SOFTSERIAL_PORTS 2

#define STOP_BIT_MASK (1 << 0)
#define START_BIT_MASK (1 << (RX_TOTAL_BITS - 1))

typedef enum {
    TIMER_MODE_SINGLE,
    TIMER_MODE_DUAL,
} timerMode_e;

enum {
    TRAILING,
    LEADING
};

#define ICPOLARITY_RISING true
#define ICPOLARITY_FALLING false

typedef struct softSerial_s {
    serialPort_t     port;

    IO_t rxIO;
    IO_t txIO;

    const timerHardware_t *timerHardware;
    const timerHardware_t *exTimerHardware;

    volatile uint8_t rxBuffer[SOFTSERIAL_BUFFER_SIZE];
    volatile uint8_t txBuffer[SOFTSERIAL_BUFFER_SIZE];

    uint8_t          isSearchingForStartBit;
    uint8_t          rxBitIndex;
    uint8_t          rxLastLeadingEdgeAtBitIndex;
    uint8_t          rxEdge;
    uint8_t          rxActive;

    uint8_t          isTransmittingData;
    int8_t           bitsLeftToTransmit;

    uint16_t         internalTxBuffer;  // includes start and stop bits
    uint16_t         internalRxBuffer;  // includes start and stop bits

    uint16_t         transmissionErrors;
    uint16_t         receiveErrors;

    uint8_t          softSerialPortIndex;
    timerMode_e      timerMode;

    timerOvrHandlerRec_t overCb;
    timerCCHandlerRec_t edgeCb;
} softSerial_t;

static const struct serialPortVTable softSerialVTable; // Forward

static softSerial_t softSerialPorts[MAX_SOFTSERIAL_PORTS];

static void applyChangedBits(softSerial_t *softSerial)
{
    if (softSerial->rxEdge == TRAILING) {
        uint8_t bitToSet;
        for (bitToSet = softSerial->rxLastLeadingEdgeAtBitIndex; bitToSet < softSerial->rxBitIndex; bitToSet++) {
            softSerial->internalRxBuffer |= 1 << bitToSet;
        }
    }
}

static void extractAndStoreRxByte(softSerial_t *softSerial)
{
    if ((softSerial->port.mode & MODE_RX) == 0) {
        return;
    }

    uint8_t haveStartBit = (softSerial->internalRxBuffer & START_BIT_MASK) == 0;
    uint8_t haveStopBit = (softSerial->internalRxBuffer & STOP_BIT_MASK) == 1;

    if (!haveStartBit || !haveStopBit) {
        softSerial->receiveErrors++;
        return;
    }

    uint8_t rxByte = (softSerial->internalRxBuffer >> 1) & 0xFF;

    if (softSerial->port.rxCallback) {
        softSerial->port.rxCallback(rxByte, softSerial->port.rxCallbackData, microsISR());
    } else {
        softSerial->port.rxBuffer[softSerial->port.rxBufferHead] = rxByte;
        softSerial->port.rxBufferHead = (softSerial->port.rxBufferHead + 1) % softSerial->port.rxBufferSize;
    }
}

static void serialEnableCC(softSerial_t *softSerial)
{
    TIM_CCxCmd(softSerial->timerHardware->tim, softSerial->timerHardware->channel, TIM_CCx_Enable);
}


void prepareForNextRxByte(softSerial_t *softSerial)
{
    // prepare for next byte
    softSerial->rxBitIndex = 0;
    softSerial->isSearchingForStartBit = true;
    if (softSerial->rxEdge == LEADING) {
        softSerial->rxEdge = TRAILING;
        timerChConfigIC(softSerial->timerHardware, (softSerial->port.options & SERIAL_INVERTED) ? ICPOLARITY_RISING : ICPOLARITY_FALLING, 0);
        serialEnableCC(softSerial);
    }
}


static void processRxState(softSerial_t *softSerial)
{
    if (softSerial->isSearchingForStartBit) {
        return;
    }

    softSerial->rxBitIndex++;

    if (softSerial->rxBitIndex == RX_TOTAL_BITS - 1) {
        applyChangedBits(softSerial);
        return;
    }

    if (softSerial->rxBitIndex == RX_TOTAL_BITS) {

        if (softSerial->rxEdge == TRAILING) {
            softSerial->internalRxBuffer |= STOP_BIT_MASK;
        }

        extractAndStoreRxByte(softSerial);
        prepareForNextRxByte(softSerial);
    }
}


static void onSerialTimerOverflow(timerOvrHandlerRec_t *cbRec, captureCompare_t capture)
{
    UNUSED(capture);
    softSerial_t *self = container_of(cbRec, softSerial_t, overCb);

    processRxState(self);
}

static void onSerialRxPinChange(timerCCHandlerRec_t *cbRec, captureCompare_t capture)
{
    UNUSED(capture);

    softSerial_t *self = container_of(cbRec, softSerial_t, edgeCb);
    bool inverted = self->port.options & SERIAL_INVERTED;

    if ((self->port.mode & MODE_RX) == 0) {
        return;
    }

    if (self->isSearchingForStartBit) {
        // Synchronize the bit timing so that it will interrupt at the center
        // of the bit period.

        TIM_SetCounter(self->timerHardware->tim, self->timerHardware->tim->ARR / 2);

        // For a mono-timer full duplex configuration, this may clobber the
        // transmission because the next callback to the onSerialTimerOverflow
        // will happen too early causing transmission errors.
        // For a dual-timer configuration, there is no problem.

        if ((self->timerMode != TIMER_MODE_DUAL) && self->isTransmittingData) {
            self->transmissionErrors++;
        }

        timerChConfigIC(self->timerHardware, inverted ? ICPOLARITY_FALLING : ICPOLARITY_RISING, 0);
        self->rxEdge = LEADING;

        self->rxBitIndex = 0;
        self->rxLastLeadingEdgeAtBitIndex = 0;
        self->internalRxBuffer = 0;
        self->isSearchingForStartBit = false;
        return;
    }

    if (self->rxEdge == LEADING) {
        self->rxLastLeadingEdgeAtBitIndex = self->rxBitIndex;
    }

    applyChangedBits(self);

    if (self->rxEdge == TRAILING) {
        self->rxEdge = LEADING;
        timerChConfigIC(self->timerHardware, inverted ? ICPOLARITY_FALLING : ICPOLARITY_RISING, 0);
    } else {
        self->rxEdge = TRAILING;
        timerChConfigIC(self->timerHardware, inverted ? ICPOLARITY_RISING : ICPOLARITY_FALLING, 0);
    }
}

static void serialInputPortActivate(softSerial_t *softSerial)
{
    if (softSerial->port.options & SERIAL_INVERTED) {
        const uint8_t pinConfig = (softSerial->port.options & SERIAL_BIDIR_NOPULL) ? IOCFG_AF_PP : IOCFG_AF_PP_PD;
        IOConfigGPIOAF(softSerial->rxIO, pinConfig, softSerial->timerHardware->alternateFunction);
    } else {
        const uint8_t pinConfig = (softSerial->port.options & SERIAL_BIDIR_NOPULL) ? IOCFG_AF_PP : IOCFG_AF_PP_UP;
        IOConfigGPIOAF(softSerial->rxIO, pinConfig, softSerial->timerHardware->alternateFunction);
    }

    softSerial->rxActive = true;
    softSerial->isSearchingForStartBit = true;
    softSerial->rxBitIndex = 0;

    // Enable input capture

    serialEnableCC(softSerial);
}

static void serialInputPortDeActivate(softSerial_t *softSerial)
{
    // Disable input capture

    TIM_CCxCmd(softSerial->timerHardware->tim, softSerial->timerHardware->channel, TIM_CCx_Disable);

    IOConfigGPIO(softSerial->rxIO, IOCFG_IN_FLOATING);
    softSerial->rxActive = false;
}

static void serialOutputPortActivate(softSerial_t *softSerial)
{
    if (softSerial->exTimerHardware)
        IOConfigGPIOAF(softSerial->txIO, IOCFG_OUT_PP, softSerial->exTimerHardware->alternateFunction);
    else
        IOConfigGPIO(softSerial->txIO, IOCFG_OUT_PP);
}

static void serialOutputPortDeActivate(softSerial_t *softSerial)
{
    if (softSerial->exTimerHardware)
        IOConfigGPIOAF(softSerial->txIO, IOCFG_IN_FLOATING, softSerial->exTimerHardware->alternateFunction);
    else
        IOConfigGPIO(softSerial->txIO, IOCFG_IN_FLOATING);
}

static bool isTimerPeriodTooLarge(uint32_t timerPeriod)
{
    return timerPeriod > 0xFFFF;
}

static void serialTimerConfigureTimebase(const timerHardware_t *timerHardwarePtr, uint32_t baud)
{
    uint32_t baseClock = timerClock(timerHardwarePtr->tim);
    uint32_t clock = baseClock;
    uint32_t timerPeriod;

    do {
        timerPeriod = clock / baud;
        if (isTimerPeriodTooLarge(timerPeriod)) {
            if (clock > 1) {
                clock = clock / 2;   // this is wrong - mhz stays the same ... This will double baudrate until ok (but minimum baudrate is < 1200)
            } else {
                // TODO unable to continue, unable to determine clock and timerPeriods for the given baud
            }

        }
    } while (isTimerPeriodTooLarge(timerPeriod));

    timerConfigure(timerHardwarePtr, timerPeriod, baseClock);
}

static void resetBuffers(softSerial_t *softSerial)
{
    softSerial->port.rxBufferSize = SOFTSERIAL_BUFFER_SIZE;
    softSerial->port.rxBuffer = softSerial->rxBuffer;
    softSerial->port.rxBufferTail = 0;
    softSerial->port.rxBufferHead = 0;

    softSerial->port.txBuffer = softSerial->txBuffer;
    softSerial->port.txBufferSize = SOFTSERIAL_BUFFER_SIZE;
    softSerial->port.txBufferTail = 0;
    softSerial->port.txBufferHead = 0;
}

serialPort_t *openSoftSerial(softSerialPortIndex_e portIndex, serialReceiveCallbackPtr rxCallback, void *rxCallbackData, uint32_t baud, portMode_e mode, portOptions_e options)
{
    softSerial_t *softSerial = &(softSerialPorts[portIndex]);

    int pinCfgIndex = portIndex + RESOURCE_SOFT_OFFSET;

    ioTag_t tagRx = IO_TAG_RX[pinCfgIndex];
    ioTag_t tagTx = IO_TAG_TX[pinCfgIndex];

    const timerHardware_t *timerTx = timerAllocate(tagTx, OWNER_SERIAL_TX, RESOURCE_INDEX(portIndex + RESOURCE_SOFT_OFFSET));
    const timerHardware_t *timerRx = (tagTx == tagRx) ? timerTx : timerAllocate(tagRx, OWNER_SERIAL_RX, RESOURCE_INDEX(portIndex + RESOURCE_SOFT_OFFSET));

    IO_t rxIO = IOGetByTag(tagRx);
    IO_t txIO = IOGetByTag(tagTx);

    if (options & SERIAL_BIDIR) {
        // If RX and TX pins are both assigned, we CAN use either with a timer.
        // However, for consistency with hardware UARTs, we only use TX pin,
        // and this pin must have a timer, and it should not be N-Channel.
        if (!timerTx || (timerTx->output & TIMER_OUTPUT_N_CHANNEL)) {
            return NULL;
        }

        softSerial->timerHardware = timerTx;
        softSerial->txIO = txIO;
        softSerial->rxIO = txIO;
        IOInit(txIO, OWNER_SERIAL_TX, RESOURCE_INDEX(portIndex + RESOURCE_SOFT_OFFSET));
    } else {
        if (mode & MODE_RX) {
            // Need a pin & a timer on RX. Channel should not be N-Channel.
            if (!timerRx || (timerRx->output & TIMER_OUTPUT_N_CHANNEL)) {
                return NULL;
            }

            softSerial->rxIO = rxIO;
            softSerial->timerHardware = timerRx;
            if (!((mode & MODE_TX) && rxIO == txIO)) {
                IOInit(rxIO, OWNER_SERIAL_RX, RESOURCE_INDEX(portIndex + RESOURCE_SOFT_OFFSET));
            }
        }

        if (mode & MODE_TX) {
            // Need a pin on TX
            if (!tagTx)
                return NULL;

            softSerial->txIO = txIO;

            if (!(mode & MODE_RX)) {
                // TX Simplex, must have a timer
                if (!timerTx)
                    return NULL;
                softSerial->timerHardware = timerTx;
            } else {
                // Duplex
                softSerial->exTimerHardware = timerTx;
            }
            IOInit(txIO, OWNER_SERIAL_TX, RESOURCE_INDEX(portIndex + RESOURCE_SOFT_OFFSET));
        }
    }

    softSerial->port.vTable = &softSerialVTable;
    softSerial->port.baudRate = baud;
    softSerial->port.mode = mode;
    softSerial->port.options = options;
    softSerial->port.rxCallback = rxCallback;
    softSerial->port.rxCallbackData = rxCallbackData;

    resetBuffers(softSerial);

    softSerial->softSerialPortIndex = portIndex;

    softSerial->transmissionErrors = 0;
    softSerial->receiveErrors = 0;

    softSerial->rxActive = false;
    softSerial->isTransmittingData = false;

    // Configure master timer (on RX); time base and input capture

    serialTimerConfigureTimebase(softSerial->timerHardware, baud);
    timerChConfigIC(softSerial->timerHardware, (options & SERIAL_INVERTED) ? ICPOLARITY_RISING : ICPOLARITY_FALLING, 0);

    // Initialize callbacks
    timerChCCHandlerInit(&softSerial->edgeCb, onSerialRxPinChange);
    timerChOvrHandlerInit(&softSerial->overCb, onSerialTimerOverflow);

    // Configure bit clock interrupt & handler.
    // If we have an extra timer (on TX), it is initialized and configured
    // for overflow interrupt.
    // Receiver input capture is configured when input is activated.

    if ((mode & MODE_TX) && softSerial->exTimerHardware && softSerial->exTimerHardware->tim != softSerial->timerHardware->tim) {
        softSerial->timerMode = TIMER_MODE_DUAL;
        serialTimerConfigureTimebase(softSerial->exTimerHardware, baud);
        timerChConfigCallbacks(softSerial->exTimerHardware, NULL, &softSerial->overCb);
        timerChConfigCallbacks(softSerial->timerHardware, &softSerial->edgeCb, NULL);
    } else {
        softSerial->timerMode = TIMER_MODE_SINGLE;
        timerChConfigCallbacks(softSerial->timerHardware, &softSerial->edgeCb, &softSerial->overCb);
    }

    serialInputPortActivate(softSerial);

    return &softSerial->port;
}
