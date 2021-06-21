/*
   Positon-hold PID controller using optical flow (body-frame velocity)

   Copyright (c) 2018 Juan Gallostra and Simon D. Levy

   MIT License
   */

#pragma once

#include "pidcontroller.hpp"

#include <rft_closedloops/pidcontroller.hpp>

namespace hf {

    class FlowHoldPid : public PidController {

        private:

            rft::DofPid rollPid;
            rft::DofPid pitchPid;

        protected:

            virtual void modifyDemands(State * state, float * demands) override
            {
                demands[DEMANDS_PITCH] += (0.5 - fabs(demands[DEMANDS_PITCH])) * pitchPid.compute(0, state->x[State::DX]);
                demands[DEMANDS_ROLL]  += (0.5 - fabs(demands[DEMANDS_ROLL]))  * rollPid.compute(0, state->x[State::DY]);
            }

        public:

            FlowHoldPid(const float Kp, float Ki)
            {
                rollPid.begin(Kp, Ki, 0);
                pitchPid.begin(Kp, Ki, 0);
            }

    };  // class FlowHoldPid

} // namespace hf
