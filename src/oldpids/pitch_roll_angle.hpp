/*
  Pitch/roll angle PID-control algorithm for real and simulated flight
  controllers
 
  Copyright (C) 2024 Simon D. Levy
 
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, in version 3.
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

namespace hf {

    class PitchRollAnglePid {

        private:

            static constexpr float KP = 6;
            static constexpr float KI = 3;
            static constexpr float ILIMIT = 20;


        public:

            void run(
                    const float dt, 
                    const bool reset,
                    const float rollDemand,
                    const float pitchDemand,
                    const float phi,
                    const float theta,
                    float & newRollDemand,
                    float & newPitchDemand)
            {

                runAxis(dt, reset, rollDemand, phi,
                        _roll_integral, newRollDemand);

                runAxis(dt, reset, pitchDemand, theta,
                        _pitch_integral, newPitchDemand);
            }

        private:

            float _roll_integral;
            float _pitch_integral;

            static void runAxis(
                    const float dt,
                    const bool reset,
                    const float demand,
                    const float angle, 
                    float & integral,
                    float & newDemand)
            {

                const auto error = demand - angle;

                integral = reset ? 0 :
                    Utils::fconstrain(integral + error * dt, ILIMIT);

                newDemand = KP * error + KI * integral;
            }
    };

}
