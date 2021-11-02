{--
  Haskell Copilot support for altimeters

  Copyright(C) 2021 Simon D.Levy

  MIT License
--}

{-# LANGUAGE RebindableSyntax #-}

module Altimeter

where

import Language.Copilot

import State
import Sensor
import Time(micros)

altimeter :: Sensor

altimeter state =

  State (x      state)
        (dx     state)
        (y      state)
        (dy     state)
        ((z     state) + altimeterZ)
        ((dz    state) + ((altimeterZ - z') / ((time - time') / time)))
        (phi    state)
        (dphi   state)
        (theta  state)
        (dtheta state)
        (psi    state)
        (dpsi   state)

  where 

    time = (unsafeCast micros) / 1000000.0 
    z' = [0] ++ altimeterZ
    time' = [0] ++ time

--------------------------------------------------------------------------

altimeterZ :: Stream Float
altimeterZ = extern "stream_altimeterZ" Nothing
