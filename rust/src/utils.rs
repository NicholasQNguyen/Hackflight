/*
   Utility functions

   Copyright (C) 2022 Simon D. Levy

   MIT License
 */

pub const DT: f32 = 100.0;

pub fn constrain(value: f32, lo: f32, hi: f32) -> f32 {
    if value  < lo {lo} else if value > hi {hi} else {value}
}

pub fn constrain_abs(value : f32, limit : f32) -> f32 {
    constrain(value, -limit, limit)
}

pub fn deg2rad(x: f32) -> f32 {
    3.1415928 * x / 180.0
}
