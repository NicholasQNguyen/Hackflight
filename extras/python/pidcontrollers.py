'''
   Classes for PID controllers

   Copyright (c) 2021 Simon D. Levy

   MIT License
'''

import numpy as np

from demands import DEMANDS_ROLL, DEMANDS_PITCH, DEMANDS_YAW
from state import STATE_PHI, STATE_DPHI, STATE_THETA, STATE_DTHETA, STATE_DPSI
from debugging import debug


def _constrainAbs(val, lim):

    return -lim if val < -lim else (+lim if val > +lim else val)


class _DofPid:
    '''
    PID controller for a single degree of freedom.  Because time differences
    (dt) appear more-or-less constant, we avoid incoroporating them into the
    code i.e., they are "absorbed" into tuning constants Ki and Kd.
    '''

    def __init__(self, Kp, Ki, Kd, windupMax=0.4):

        # Constants
        self.Kp = Kp
        self.Ki = Ki
        self.Kd = Kd
        self.windupMax = windupMax

        # Initialize state
        self.reset()

    def compute(self, target, actual):

        # Compute error as scaled target minus actual
        error = target - actual

        # Compute P term
        pterm = error * self.Kp

        # Compute I term
        errorI = _DofPid.constrainAbs(self.state['errorI'] + error,
                                      self.windupMax)
        iterm = errorI * self.Ki

        # Compute D term
        deltaError = error - self.state['lastError']
        dterm = ((self.state['deltaError1'] +
                  self.state['deltaError2'] + deltaError) * self.Kd)

        # Update state
        self.state = {'deltaError2': self.state['deltaError1'],
                      'deltaError1': deltaError,
                      'errorI': errorI,
                      'lastError': error}

        return pterm + iterm + dterm, self.state

    def reset(self):

        self.state = {'deltaError1': 0,
                      'deltaError2': 0,
                      'errorI': 0,
                      'lastError': 0}

    @staticmethod
    def constrainAbs(val, lim):

        return -lim if val < -lim else (+lim if val > +lim else val)


class LevelPid:

    MAX_ANGLE_DEGREES = 45

    def __init__(self, Kp):

        self.Kp = Kp

        # Maximum roll pitch demand is +/-0.5, so to convert demand to
        # angle for error computation, we multiply by the folling amount:
        self.dmdscale = 2 * np.radians(self.MAX_ANGLE_DEGREES)

    def modifyDemands(self, state, demands):

        newdemands = demands.copy()

        newdemands[DEMANDS_ROLL] = self.Kp * (demands[DEMANDS_ROLL] *
                                              self.dmdscale -
                                              state[STATE_PHI])

        # Pitch demand is nose-down positive, so we negate
        # pitch-forward (nose-down negative) to reconcile them
        newdemands[DEMANDS_PITCH] = self.Kp * (demands[DEMANDS_PITCH] *
                                               self.dmdscale +
                                               state[STATE_THETA])

        # LevelPid uses no state
        return newdemands, None


class _AngularVelocityPid(_DofPid):

    # Arbitrary constants
    BIG_DEGREES_PER_SECOND = 40.0
    WINDUP_MAX = 6.0

    def __init__(self, Kp, Ki, Kd):

        _DofPid.__init__(self, Kp, Ki, Kd, self.WINDUP_MAX)

        # Convert degree parameters to radians for use later
        self.bigAngularVelocity = np.radians(self.BIG_DEGREES_PER_SECOND)

    def compute(self, demand, angularVelocity):

        # Reset integral on quick angular velocity change
        if abs(angularVelocity) > self.bigAngularVelocity:
            self.reset()

        return _DofPid.compute(self, demand, angularVelocity)


class RatePid:

    def __init__(self, Kp, Ki, Kd):

        self.rollPid = _AngularVelocityPid(Kp, Ki, Kd)
        self.pitchPid = _AngularVelocityPid(Kp, Ki, Kd)

    def modifyDemands(self, state, demands):

        newdmnds = demands.copy()

        # Roll angle and roll demand are both positive for starboard right down
        roll, rstate = self.rollPid.compute(demands[DEMANDS_ROLL],
                                            state[STATE_DPHI])
        newdmnds[DEMANDS_ROLL] = roll

        # Pitch demand is postive for stick forward, but pitch angle is
        # positive for nose up.  So we negate pitch angle to compute demand
        pitch, pstate = self.pitchPid.compute(demands[DEMANDS_PITCH],
                                              -state[STATE_DTHETA])
        newdmnds[DEMANDS_PITCH] = pitch

        return newdmnds, (rstate, pstate)


class YawPid:

    def __init__(self, Kp, Ki, windupMax=0.4):

        self.Kp = Kp
        self.Ki = Ki
        self.windupMax = windupMax

        self.state = {'errorI': 0, 'lastError': 0}

    def modifyDemands(self, state, demands):

        # Compute error as scaled target minus actual
        error = demands[DEMANDS_YAW] - state[STATE_DPSI]

        # Accumualte error integral
        errorI = _constrainAbs(self.state['errorI'] + error, self.windupMax)

        # Update controller state
        self.state = {'errorI': errorI, 'lastError': error}

        # Update demands
        newdemands = demands.copy()
        newdemands[DEMANDS_YAW] = error * self.Kp + errorI * self.Ki

        return newdemands, self.state

