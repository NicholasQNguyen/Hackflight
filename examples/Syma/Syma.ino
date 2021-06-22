/*
   Hackflight sketch for Ladybug Flight Controller with Spektrum DSMX receiver

   Additional libraries needed:

       https://github.com/simondlevy/EM7180
       https://github.com/simondlevy/CrossPlatformDataBus
       https://github.com/simondlevy/SpektrumDSM 

   Hardware support for Ladybug flight controller:

       https://github.com/simondlevy/grumpyoldpizza

   Copyright (c) 2018 Simon D. Levy

   MIT License

 */

#include "hackflight.hpp"
#include "boards/ladybugfc.hpp"
#include "receivers/arduino/dsmx/dsmx_serial1.hpp"
#include "mixers/quadxmw.hpp"
#include "pidcontrollers/rate.hpp"
#include "pidcontrollers/yaw.hpp"
#include "pidcontrollers/level.hpp"
#include "sensors/usfs.hpp"

static constexpr uint8_t CHANNEL_MAP[6] = {0, 1, 2, 3, 6, 4};
static constexpr float DEMAND_SCALE = 4.0f;

static hf::LadybugFC board;

static hf::DSMX_Receiver_Serial1 receiver = hf::DSMX_Receiver_Serial1(CHANNEL_MAP, DEMAND_SCALE);  

static hf::MixerQuadXMW mixer(&hf::ladybugFcMotors);

static hf::Hackflight h = hf::Hackflight(&board, &receiver, &mixer);

static hf::RatePid ratePid = hf::RatePid(0.225, 0.001875, 0.375);
static hf::YawPid yawPid = hf::YawPid(1.0625, 0.005625f);
static hf::LevelPid levelPid = hf::LevelPid(0.20f);

static hf::UsfsGyrometer gyrometer;
static hf::UsfsQuaternion quaternion; // not really a sensor, but we treat it like one!

void setup(void)
{
    // Add sensors
    h.addSensor(&quaternion);
    h.addSensor(&gyrometer);

    // Add PID controllers
    h.addPidController(&levelPid);
    h.addPidController(&ratePid);
    h.addPidController(&yawPid);

    // Adjust trim
    receiver.setTrim(0, 0.05, 0.05);

    // Start Hackflight firmware
    h.begin();
}

void loop(void)
{
    h.update();
}
