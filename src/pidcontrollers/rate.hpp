/*
   Angular-velocity-based PID controller for roll and pitch

   Copyright (c) 2018 Juan Gallostra and Simon D. Levy

   MIT License
 */

#pragma once

#include "pidcontroller.hpp"
#include "angvel.hpp"


namespace hf {

    class RatePid : public PidController {

        private: 

            // Rate mode uses a rate controller for roll, pitch
            _AngularVelocityPid _rollPid;
            _AngularVelocityPid _pitchPid;

        public:

            RatePid(const float Kp, const float Ki, const float Kd) 
            {
                _rollPid.begin(Kp, Ki, Kd);
                _pitchPid.begin(Kp, Ki, Kd);
            }

            void modifyDemands(State * state, float * demands)
            {
                demands[DEMANDS_ROLL]  = _rollPid.compute(demands[DEMANDS_ROLL],  state->x[State::DPHI]);
                demands[DEMANDS_PITCH] = _pitchPid.compute(demands[DEMANDS_PITCH], state->x[State::DTHETA]);
            }

            virtual void resetOnInactivity(bool inactive) override
            {
                // Check throttle-down for integral reset
                _rollPid.resetOnInactivity(inactive);
                _pitchPid.resetOnInactivity(inactive);
            }

    };  // class RatePid

} // namespace hf
