/*
   Hackflight PID controller support

   Copyright (C) 2022 Simon D. Levy

   MIT License
*/


pub mod newpids {

    use crate::datatypes::Demands;
    use crate::datatypes::VehicleState;

    #[derive(Debug,Clone)]
    enum PidController {

        AnglePid {
            k_rate_p: f32,
            k_rate_i: f32,
            k_rate_d: f32,
            k_rate_f: f32,
            k_level_p: f32,
            dyn_lpf_previous_quantized_throttle: i32,  
            feedforward_lpf_initialized: bool,
            sum: f32,
        },

        AltitudePid {
            k_p: f32,
            k_i: f32,
            in_band_prev: bool,
            error_integral: f32,
            altitude_target: f32 
        },
    }

    fn get_demands(
        pid: &PidController,
        d_usec: &u32,
        demands: &Demands,
        vstate: &VehicleState,
        reset: &bool) -> Demands {

        match pid {

            PidController::AnglePid {
                k_rate_p,
                k_rate_i,
                k_rate_d,
                k_rate_f,
                k_level_p,
                dyn_lpf_previous_quantized_throttle,  
                feedforward_lpf_initialized,
                sum,
            } => { 
                get_angle_demands(
                    d_usec,
                    demands,
                    vstate,
                    reset,
                    k_rate_p,
                    k_rate_i,
                    k_rate_d,
                    k_rate_f,
                    k_level_p,
                    dyn_lpf_previous_quantized_throttle,  
                    feedforward_lpf_initialized,
                    sum) 
            },

            PidController::AltitudePid {
                k_p,
                k_i,
                in_band_prev,  
                error_integral,
                altitude_target,
            } => { 
                get_alt_hold_demands(
                    demands,
                    vstate,
                    reset,
                    k_p,
                    k_i,
                    in_band_prev,  
                    error_integral,
                    altitude_target) 
            },
        }
    }

    fn get_angle_demands(
        d_usec: &u32,
        demands: &Demands,
        vstate: &VehicleState,
        reset: &bool,
        k_rate_p: &f32,
        k_rate_i: &f32,
        k_rate_d: &f32,
        k_rate_f: &f32,
        k_level_p: &f32,
        dyn_lpf_previous_quantized_throttle: &i32,  
        feedforward_lpf_initialized: &bool,
        sum: &f32) -> Demands  {

        // minimum of 5ms between updates
        const DYN_LPF_THROTTLE_UPDATE_DELAY_US: u16 = 5000; 

        const DYN_LPF_THROTTLE_STEPS: u16 = 100;

        // Full iterm suppression in setpoint mode at high-passed setpoint rate > 40deg/sec
        const ITERM_RELAX_SETPOINT_THRESHOLD: u8 = 40;
        const ITERM_RELAX_CUTOFF: u8 = 15;

        const DTERM_LPF1_DYN_MIN_HZ: u16 = 75;
        const DTERM_LPF1_DYN_MAX_HZ: u16 = 150;
        const DTERM_LPF2_HZ: u16 = 150;

        const YAW_LOWPASS_HZ: u16 = 100;

        const ITERM_WINDUP_POINT_PERCENT: u8 = 85;        

        const D_MIN: u8 = 30;
        const D_MIN_GAIN: u8 = 37;
        const D_MIN_ADVANCE: u8 = 20;

        const FEEDFORWARD_MAX_RATE_LIMIT: u8 = 90;

        const DYN_LPF_CURVE_EXPO: u8 = 5;

        // PT2 lowpass input cutoff to peak D around propwash frequencies
        const D_MIN_RANGE_HZ: f32 = 85.0;  

        // PT2 lowpass cutoff to smooth the boost effect
        const D_MIN_LOWPASS_HZ: f32 = 35.0;  
        const D_MIN_GAIN_FACTOR: f32  = 0.00008;
        const D_MIN_SETPOINT_GAIN_FACTOR: f32 = 0.00008;

        const RATE_ACCEL_LIMIT: u16 = 0;
        const YAW_RATE_ACCEL_LIMIT: u16 = 0;
        const ITERM_LIMIT: u16 = 400;

        const LEVEL_ANGLE_LIMIT: f32 = 45.0;

        const OUTPUT_SCALING: f32 = 1000.0;
        const  LIMIT_YAW: u16 = 400;
        const  LIMIT: u16 = 500;

        Demands { 
            throttle : 0.0,
            roll : 0.0,
            pitch : 0.0,
            yaw : 0.0
        }
    }

    fn get_alt_hold_demands(
        demands: &Demands,
        vstate: &VehicleState,
        reset: &bool,
        k_p: &f32,
        k_i: &f32,
        in_band_prev: &bool,
        error_integral: &f32,
        altitude_target: &f32) -> Demands  {
        Demands { 
            throttle : 0.0,
            roll : 0.0,
            pitch : 0.0,
            yaw : 0.0
        }
    }
}

