/*
   Class definition for MPU6000, MPU6500 IMUs using SPI bus

   Copyright (c) 2022 Simon D. Levy

   MIT License
 */

#include <stdint.h>

#include "imu/real/softquat.h"

class Mpu6x00 : public SoftQuatImu {

    public:

        typedef enum {

            GYRO_250DPS,
            GYRO_500DPS,
            GYRO_1000DPS,
            GYRO_2000DPS

        } gyroScale_e;

        typedef enum {

            ACCEL_2G,
            ACCEL_4G,  
            ACCEL_8G,  
            ACCEL_16G

        } accelScale_e;

    private:

        // Registers
        static const uint8_t REG_SMPLRT_DIV   = 0x19;
        static const uint8_t REG_CONFIG       = 0x1A;
        static const uint8_t REG_GYRO_CONFIG  = 0x1B;
        static const uint8_t REG_ACCEL_CONFIG = 0x1C;
        static const uint8_t REG_INT_PIN_CFG  = 0x37;
        static const uint8_t REG_INT_ENABLE   = 0x38;
        static const uint8_t REG_GYRO_XOUT_H  = 0x43;
        static const uint8_t REG_USER_CTRL    = 0x6A;
        static const uint8_t REG_PWR_MGMT_1   = 0x6B;
        static const uint8_t REG_PWR_MGMT_2   = 0x6C;
        static const uint8_t REG_WHO_AM_I     = 0x75;

        // Configuration bits  
        static const uint8_t BIT_RAW_RDY_EN       = 0x01;
        static const uint8_t BIT_CLK_SEL_PLLGYROZ = 0x03;
        static const uint8_t BIT_INT_ANYRD_2CLEAR = 0x10;
        static const uint8_t BIT_I2C_IF_DIS       = 0x10;
        static const uint8_t BIT_H_RESET          = 0x80;

        // Any interrupt interval less than this will be recognised as the
        // short interval of ~79us
        static const uint8_t SHORT_THRESHOLD = 82 ;

        // 20 MHz max SPI frequency
        static const uint32_t MAX_SPI_CLK_HZ = 20000000;

        // Sample rate = 200Hz    Fsample= 1Khz/(4+1) = 200Hz     
        // Sample rate = 50Hz    Fsample= 1Khz/(19+1) = 50Hz     
        uint8_t m_sampleRateDivisor;

        gyroScale_e m_gyroScale;

        accelScale_e m_accelScale;

        int32_t m_shortPeriod;

        uint8_t m_buffer[15];

        int16_t getValue(const uint8_t k)
        {
            return (int16_t)(m_buffer[k] << 8 | m_buffer[k+1]);
        }

        static uint16_t gyroScaleToInt(const gyroScale_e gyroScale)
        {
            return
                gyroScale == GYRO_250DPS ?  250 : 
                gyroScale == GYRO_500DPS ?  500 : 
                gyroScale == GYRO_1000DPS ?  1000 : 
                2000;
        }

    protected:

        // 1 MHz max SPI frequency for initialisation
        static const uint32_t MAX_SPI_INIT_CLK_HZ = 1000000;

        uint8_t m_csPin;

        virtual void writeRegister(const uint8_t reg, const uint8_t val) = 0;

        virtual void readRegisters(
                const uint8_t addr, uint8_t * buffer, const uint8_t count) = 0;

        virtual uint8_t readRegister(const uint8_t addr) = 0;

        virtual void setClockDivider(uint32_t divider) = 0;

        uint16_t calculateSpiDivisor(const uint32_t freq)
        {
            uint32_t clk = m_board->getClockSpeed() / 2;

            uint16_t divisor = 2;

            clk >>= 1;

            for (; (clk > freq) && (divisor < 256); divisor <<= 1, clk >>= 1);

            return divisor;
        }

        virtual bool gyroIsReady(void) override
        {
            // If we call this infrequently enough, gyro will always be ready
            readGyro();
            return true;
        }

        virtual void begin(void) override
        {
            m_shortPeriod = m_board->getClockSpeed() / 1000000 * SHORT_THRESHOLD;

            // Chip reset
            writeRegister(REG_PWR_MGMT_1, BIT_H_RESET);
            delay(100);

            // Check ID
            readRegister(REG_WHO_AM_I);

            // Clock Source PPL with Z axis gyro reference
            writeRegister(REG_PWR_MGMT_1, BIT_CLK_SEL_PLLGYROZ);
            delayMicroseconds(7);

            // Disable Primary I2C Interface
            writeRegister(REG_USER_CTRL, BIT_I2C_IF_DIS);
            delayMicroseconds(15);

            writeRegister(REG_PWR_MGMT_2, 0x00);
            delayMicroseconds(15);

            // Accel Sample Rate 1kHz
            // Gyroscope Output Rate =  1kHz when the DLPF is enabled
            writeRegister(REG_SMPLRT_DIV, 0);
            delayMicroseconds(15);

            // Gyro +/- 2000 DPS Full Scale
            writeRegister(REG_GYRO_CONFIG, m_gyroScale << 3);
            delayMicroseconds(15);

            // Accel +/- 16 G Full Scale
            writeRegister(REG_ACCEL_CONFIG, m_accelScale << 3);
            delayMicroseconds(15);

            // INT_ANYRD_2CLEAR
            writeRegister(REG_INT_PIN_CFG, 0x10);

            delayMicroseconds(15);

            writeRegister(REG_INT_ENABLE, BIT_RAW_RDY_EN);
            delayMicroseconds(15);

            setClockDivider(calculateSpiDivisor(MAX_SPI_CLK_HZ));
            delayMicroseconds(1);

            setClockDivider(calculateSpiDivisor(MAX_SPI_INIT_CLK_HZ));

            // Accel and Gyro DLPF Setting
            writeRegister(REG_CONFIG, 0); // no gyro DLPF
            delayMicroseconds(1);

            setClockDivider(calculateSpiDivisor(MAX_SPI_CLK_HZ));
        }

        virtual int16_t readRawGyro(uint8_t k) override
        {
            return getValue(1 + k*2);
        }


        Mpu6x00(
                const rotateFun_t rotateFun,
                const uint8_t csPin,
                const uint8_t sampleRateDivisor = 19,
                const gyroScale_e gyroScale = GYRO_2000DPS,
                const accelScale_e accelScale = ACCEL_2G)
            : SoftQuatImu(rotateFun, gyroScaleToInt(gyroScale) / 32768.)
        {
            m_csPin = csPin;
            m_sampleRateDivisor = sampleRateDivisor;
            m_gyroScale = gyroScale;
            m_accelScale = accelScale;
        }

    public:

        void handleInterrupt(void)
        {
            static uint32_t prevTime;

            // Ideally we'd use a time to capture such information, but
            // unfortunately the port used for EXTI interrupt does not have an
            // associated timer
            uint32_t nowCycles = m_board->getCycleCounter();
            int32_t gyroLastPeriod = intcmp(nowCycles, prevTime);

            // This detects the short (~79us) EXTI interval of an MPU6xxx gyro
            if ((m_shortPeriod == 0) || (gyroLastPeriod < m_shortPeriod)) {

                m_gyroSyncTime = prevTime;
            }

            prevTime = nowCycles;

            RealImu::handleInterrupt();
        }

        void readGyro(void)
        {
            readRegisters(REG_GYRO_XOUT_H, m_buffer, 7);
        }

}; // class Mpu6x00
