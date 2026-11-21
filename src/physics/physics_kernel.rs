// ============================================================================
// Open City C++ v1.2.0 — Native Rust Rigid-Body, Torque-Curve Gearbox,
// Pacejka Tire, 4-Corner Suspension & Tangential-Friction Collision Kernel
// ============================================================================
// Compiled via `rustc --crate-type=lib --emit=obj -C opt-level=3 -C panic=abort`
// Zero external crate dependencies (`#![no_std]`) for deterministic C-ABI FFI
// across Linux x64 (GCC/Clang) and Windows x64 (MSVC /MT).
// ============================================================================

#![no_std]

use core::panic::PanicInfo;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

extern "C" {
    fn sinf(x: f32) -> f32;
    fn cosf(x: f32) -> f32;
    fn sqrtf(x: f32) -> f32;
    fn expf(x: f32) -> f32;
    fn fmodf(x: f32, y: f32) -> f32;
}

const PI: f32 = 3.14159265358979323846;
const TWO_PI: f32 = 6.28318530717958647692;

#[inline]
fn clampf(v: f32, lo: f32, hi: f32) -> f32 {
    if v < lo {
        lo
    } else if v > hi {
        hi
    } else {
        v
    }
}

#[inline]
fn absf(v: f32) -> f32 {
    if v < 0.0 { -v } else { v }
}

#[inline]
fn maxf(a: f32, b: f32) -> f32 {
    if a > b { a } else { b }
}

#[inline]
fn minf(a: f32, b: f32) -> f32 {
    if a < b { a } else { b }
}

#[inline]
fn lerpf(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * clampf(t, 0.0, 1.0)
}

#[inline]
fn wrap_angle(angle: f32) -> f32 {
    let mut a = unsafe { fmodf(angle + PI, TWO_PI) };
    if a < 0.0 {
        a += TWO_PI;
    }
    a - PI
}

#[repr(C)]
pub struct RsVehicleSpecParams {
    pub mass: f32,
    pub max_forward_speed: f32,
    pub max_reverse_speed: f32,
    pub acceleration: f32,
    pub braking: f32,
    pub turn_rate: f32,
    pub normal_grip: f32,
    pub drift_grip: f32,
    pub half_length: f32,
    pub half_width: f32,
    pub chassis_height: f32,
}

#[repr(C)]
pub struct RsVehicleControlInput {
    pub throttle: f32,
    pub steer: f32,
    pub handbrake: i32,
    pub nitro: i32,
    pub upgrade_mult: f32,
    pub aero_grip_bonus: f32,
    pub nitro_recharge_rate: f32,
    pub ground_height: f32,
    pub ramp_slope_pitch: f32,
    pub wet_road_factor: f32,
    pub wheel_ground_h: [f32; 4], // FL, FR, RL, RR surface elevation for 4-corner curb suspension
}

#[repr(C)]
pub struct RsVehiclePhysicsState {
    pub pos_x: f32,
    pub pos_y: f32,
    pub pos_z: f32,
    pub vel_x: f32,
    pub vel_y: f32,
    pub vel_z: f32,
    pub heading: f32,
    pub angular_velocity: f32,
    pub steer_visual: f32,
    pub wheel_spin: f32,
    pub body_pitch: f32,
    pub body_roll: f32,
    pub pitch_vel: f32,
    pub roll_vel: f32,
    pub wheel_comp: [f32; 4],
    pub nitro_charge: f32,
    pub engine_rpm: f32,
    pub current_gear: i32,
    pub braking_visual: i32,
    pub nitro_active: i32,
    pub is_drifting: i32,
    pub airborne: i32,
    pub air_time: f32,
    pub landed_air_time: f32,
    pub landed_impact_speed: f32,
    pub turbo_spool: f32,
    pub wheel_slip: f32,
    pub shift_cut_timer: f32,
    pub shift_backfire: i32,
    pub brake_heat: f32,
}

#[repr(C)]
pub struct RsRigidBody2D {
    pub pos_x: f32,
    pub pos_z: f32,
    pub vel_x: f32,
    pub vel_z: f32,
    pub heading: f32,
    pub angular_velocity: f32,
    pub mass: f32,
    pub half_length: f32,
    pub half_width: f32,
    pub front_damage: f32,
    pub rear_damage: f32,
    pub left_damage: f32,
    pub right_damage: f32,
}

#[no_mangle]
pub extern "C" fn oc_rs_physics_version() -> i32 {
    2000 // v2.0.0 Realistic Steering Rack, Slope Gravity & Pacejka Kernel
}

#[no_mangle]
pub unsafe extern "C" fn oc_rs_step_vehicle_dynamics(
    dt: f32,
    state: *mut RsVehiclePhysicsState,
    spec: *const RsVehicleSpecParams,
    input: *const RsVehicleControlInput,
) {
    if state.is_null() || spec.is_null() || input.is_null() || dt <= 0.0 {
        return;
    }
    let st = &mut *state;
    let sp = &*spec;
    let inp = &*input;

    st.landed_air_time = 0.0;
    st.landed_impact_speed = 0.0;
    st.braking_visual = 0;
    st.nitro_active = 0;
    st.shift_backfire = 0;

    if st.shift_cut_timer > 0.0 {
        st.shift_cut_timer = maxf(0.0, st.shift_cut_timer - dt);
    }

    let throttle = clampf(inp.throttle, -1.0, 1.0);
    let steer = clampf(inp.steer, -1.0, 1.0);
    let handbrake = inp.handbrake != 0;
    let nitro = inp.nitro != 0;
    let wet_f = clampf(inp.wet_road_factor, 0.0, 1.0);
    let wet_grip_scale = 1.0 - 0.16 * wet_f;

    let mut fwd_x = cosf(st.heading);
    let mut fwd_z = sinf(st.heading);
    let mut right_x = -sinf(st.heading);
    let mut right_z = cosf(st.heading);

    let prev_fwd_spd = st.vel_x * fwd_x + st.vel_z * fwd_z;
    let mut forward_spd = prev_fwd_spd;
    let mut lateral_spd = st.vel_x * right_x + st.vel_z * right_z;

    let on_ground = st.airborne == 0;
    let top_ref = maxf(18.0, sp.max_forward_speed * inp.upgrade_mult);
    let nitro_ready = nitro && st.nitro_charge > 0.03;
    let max_fwd = top_ref * (if nitro_ready { 1.32 } else { 1.0 });

    // 1. Multi-Gear Torque Curve, Turbo Boost Spool, Launch Wheelspin & Continuous Aero Drag
    let cur_gear = if st.current_gear >= 1 && st.current_gear <= 6 {
        st.current_gear
    } else {
        1
    };
    let gear_torque_mul = match cur_gear {
        1 => 1.38, // Strong 1st-gear low-end launch punch
        2 => 1.18, // Brisk 2nd-gear mid-speed pull
        3 => 1.02,
        4 => 0.88,
        5 => 0.76,
        _ => 0.66, // Tall 6th-gear highway overdrive
    };

    let rpm_norm = clampf((st.engine_rpm - 880.0) / 7270.0, 0.0, 1.0);
    // Authentic bell-shaped engine torque curve peaking between 4,600 and 6,500 RPM
    let torque_curve = 0.78 + 0.34 * sinf(clampf(rpm_norm * 1.10, 0.0, 1.0) * PI);

    // Progressive turbo / supercharger spool under load (+ lift-off exhaust pop!)
    let prev_spool = st.turbo_spool;
    if throttle > 0.25 && forward_spd >= -0.4 {
        let spool_rate = 1.65 + 0.55 * inp.upgrade_mult + (if nitro_ready { 1.2 } else { 0.0 });
        let spool_target = clampf(throttle * (0.45 + 0.65 * rpm_norm), 0.0, 1.0);
        st.turbo_spool = lerpf(st.turbo_spool, spool_target, dt * spool_rate);
    } else {
        st.turbo_spool = maxf(0.0, st.turbo_spool - dt * 2.8);
        if prev_spool > 0.68 && st.engine_rpm > 4700.0 && throttle <= 0.05 && on_ground {
            st.shift_backfire = 1; // Turbo overrun lift-off exhaust pop!
        }
    }
    let boost_mul = 1.0 + 0.18 * st.turbo_spool;

    let mut target_slip: f32 = 0.0;
    if throttle > 0.05 {
        if forward_spd < -0.5 {
            // Forward pressed while rolling backward: progressive ABS hydraulic braking
            let brake_force = sp.braking * (1.06 + 0.006 * absf(forward_spd)) * (1.0 - 0.12 * wet_f);
            forward_spd = minf(0.0, forward_spd + brake_force * dt);
            st.braking_visual = 1;
        } else {
            let clutch_mul = if st.shift_cut_timer > 0.0 { 0.32 } else { 1.0 };
            let mut accel = sp.acceleration * inp.upgrade_mult * throttle * gear_torque_mul * torque_curve * boost_mul * clutch_mul;
            if nitro_ready {
                accel *= 1.58;
                st.nitro_charge = maxf(0.0, st.nitro_charge - dt * 0.36);
                st.nitro_active = 1;
            }
            // High-torque launch wheelspin & traction limit at low speed (especially on wet roads or muscle/supercars)
            if on_ground && forward_spd < 11.5 && throttle > 0.60 {
                let traction_limit = (20.5 + sp.normal_grip * 1.45) * wet_grip_scale;
                if accel > traction_limit {
                    let excess = (accel - traction_limit) / maxf(traction_limit, 1.0);
                    target_slip = clampf(excess * 1.35 + (if nitro_ready { 0.25 } else { 0.0 }), 0.0, 1.0);
                    // Slight traction loss during wheelspin while still surging strongly forward
                    accel = traction_limit + (accel - traction_limit) * 0.62;
                }
            }
            if !on_ground {
                accel *= 0.16;
            }
            // Continuous rolling resistance + quadratic aerodynamic drag even under full throttle
            let aero_drag = (0.55 + 0.00052 * forward_spd * forward_spd) * (if on_ground { 1.0 } else { 0.35 });
            let net_accel = accel - aero_drag;
            forward_spd = minf(max_fwd, maxf(0.0, forward_spd + net_accel * dt));
        }
    } else if throttle < -0.05 {
        if forward_spd > 1.2 {
            // Progressive high-speed hydraulic braking with ABS modulation
            let abs_pulse = 1.0 + 0.03 * sinf(st.wheel_spin * 9.0);
            let brake_force = sp.braking * (1.08 + 0.007 * forward_spd) * (-throttle) * (1.0 - 0.14 * wet_f) * abs_pulse;
            forward_spd = maxf(0.0, forward_spd - brake_force * dt);
            st.braking_visual = 1;
        } else {
            let rev_accel = sp.acceleration * 0.72 * throttle * (if on_ground { 1.0 } else { 0.16 });
            forward_spd = maxf(-sp.max_reverse_speed, forward_spd + rev_accel * dt);
        }
    } else if on_ground {
        // Natural engine braking + rolling resistance + aerodynamic quadratic drag
        let aero_drag = 0.0017 * forward_spd * absf(forward_spd);
        let drag = (3.4 + aero_drag) * dt;
        if absf(forward_spd) <= drag {
            forward_spd = 0.0;
        } else {
            forward_spd -= if forward_spd > 0.0 { drag } else { -drag };
        }
    }

    st.wheel_slip = lerpf(st.wheel_slip, target_slip, dt * 11.0);

    if st.nitro_active == 0 {
        st.nitro_charge = minf(1.0, st.nitro_charge + dt * inp.nitro_recharge_rate);
    }

    if handbrake && on_ground {
        st.braking_visual = 1;
        let hb_decel = (12.2 - 1.5 * wet_f) * dt;
        if absf(forward_spd) <= hb_decel {
            forward_spd = 0.0;
        } else {
            forward_spd -= if forward_spd > 0.0 { hb_decel } else { -hb_decel };
        }
    }

    // Brake rotor thermal glow buildup & airflow cooling
    if st.braking_visual != 0 && absf(prev_fwd_spd) > 9.0 && on_ground {
        let heat_add = clampf((absf(prev_fwd_spd) - 7.0) / 24.0, 0.20, 1.55) * dt * 1.45;
        st.brake_heat = minf(1.0, st.brake_heat + heat_add);
    } else {
        let cool_rate = 0.36 + 0.012 * absf(forward_spd);
        st.brake_heat *= expf(-cool_rate * dt);
        if st.brake_heat < 0.01 {
            st.brake_heat = 0.0;
        }
    }

    // 2. 6-Speed Dual-Clutch Sports Gearbox & Hysteresis RPM Simulation
    let abs_fwd = absf(forward_spd);
    let prev_gear = st.current_gear;
    if forward_spd < -0.55 {
        st.current_gear = 0; // Reverse ('R')
        let rev_ratio = clampf(abs_fwd / maxf(1.0, sp.max_reverse_speed), 0.0, 1.0);
        let target_rpm = 1050.0 + rev_ratio * 5200.0 + absf(throttle) * 680.0;
        st.engine_rpm = lerpf(st.engine_rpm, target_rpm, dt * 12.0);
    } else {
        let norm_spd = clampf(abs_fwd / top_ref, 0.0, 1.20);
        // Distinct 6-speed shift points with subtle downshift hysteresis to avoid gear hunting
        let up_thresholds: [f32; 5] = [0.18, 0.35, 0.53, 0.72, 0.90];
        let mut gear = if abs_fwd < 0.35 {
            1
        } else if norm_spd < up_thresholds[0] {
            1
        } else if norm_spd < up_thresholds[1] {
            2
        } else if norm_spd < up_thresholds[2] {
            3
        } else if norm_spd < up_thresholds[3] {
            4
        } else if norm_spd < up_thresholds[4] {
            5
        } else {
            6
        };
        if prev_gear >= 2 && prev_gear <= 6 && gear == prev_gear - 1 {
            let hyst = up_thresholds[(prev_gear - 2) as usize] - 0.030;
            if norm_spd > hyst {
                gear = prev_gear;
            }
        }
        st.current_gear = gear;

        let gear_low = (gear - 1) as f32 * 0.165;
        let gear_span = 0.21;
        let in_gear = clampf((norm_spd - gear_low) / gear_span, 0.0, 1.06);
        let idle_rpm = 880.0;
        let slip_rev_boost = st.wheel_slip * 1350.0;
        let target_rpm = idle_rpm
            + in_gear * 6100.0
            + maxf(0.0, throttle) * 760.0
            + slip_rev_boost
            + (if st.nitro_active != 0 { 480.0 } else { 0.0 });
        st.engine_rpm = lerpf(st.engine_rpm, clampf(target_rpm, idle_rpm, 8150.0), dt * 15.0);

        // Dual-clutch upshift kick + brief torque cut + twin exhaust backfire flash!
        if gear > prev_gear && prev_gear >= 1 && throttle > 0.40 {
            st.shift_cut_timer = 0.055;
            st.shift_backfire = 1;
            st.pitch_vel -= 0.26;
        }
    }

    // 3. Speed-Sensitive Power Steering Rack, Road Slope Gravity, Weight-Transfer & Pacejka Lateral Grip
    if on_ground && absf(inp.ramp_slope_pitch) > 0.002 {
        // Real longitudinal gravity component when climbing/descending Namsan hills or Han River bridge ramps
        let slope_g = -9.81 * sinf(inp.ramp_slope_pitch);
        forward_spd = clampf(forward_spd + slope_g * dt, -sp.max_reverse_speed, max_fwd * 1.06);
    }

    let long_accel = clampf((forward_spd - prev_fwd_spd) / maxf(dt, 0.004), -32.0, 32.0);
    // Trail-braking loads front axle (+22% turn-in bite); hard throttle shifts load rearward
    let front_weight_transfer = clampf(1.0 - long_accel * 0.013, 0.80, 1.24);
    // Speed-squared aerodynamic downforce plants sports/hypercars at highway speeds
    let downforce_grip = 1.0 + clampf(abs_fwd * abs_fwd * 0.00024 * inp.aero_grip_bonus, 0.0, 0.40);

    // Weighted speed-sensitive steering rack (crisp at city speeds, smooth & planted on Olympic-daero!)
    let steer_rate = if absf(steer) < 0.05 {
        11.5 // Natural caster self-centering when wheel released
    } else {
        lerpf(9.6, 5.2, clampf(abs_fwd / 44.0, 0.0, 1.0))
    };
    st.steer_visual = lerpf(st.steer_visual, steer, clampf(dt * steer_rate, 0.0, 1.0));
    let eff_steer = st.steer_visual;

    let mut applied_yaw_rate: f32 = 0.0;
    if abs_fwd > 0.35 && on_ground {
        let low_speed_factor = clampf(abs_fwd / 5.4, 0.0, 1.0);
        // Realistic Ackermann speed-sensitive steering reduction + tire understeer saturation at high speed
        let high_speed_damp = 1.0 / (1.0 + abs_fwd * 0.022 + abs_fwd * abs_fwd * 0.00022);
        let dir_sign = if forward_spd >= 0.0 { 1.0 } else { -1.0 };
        let drift_boost = if handbrake { 1.38 } else { 1.0 };
        let steer_yaw_rate = eff_steer
            * sp.turn_rate
            * low_speed_factor
            * high_speed_damp
            * drift_boost
            * front_weight_transfer
            * wet_grip_scale
            * dir_sign;
        applied_yaw_rate = steer_yaw_rate;
        st.heading = wrap_angle(st.heading + steer_yaw_rate * dt);
    }

    // Integrate external collision angular velocity (PIT maneuvers & off-center crashes)
    if absf(st.angular_velocity) > 0.001 {
        st.heading = wrap_angle(st.heading + st.angular_velocity * dt);
        let ang_damp = if on_ground { 4.8 * wet_grip_scale } else { 0.8 };
        st.angular_velocity *= expf(-ang_damp * dt);
        if absf(st.angular_velocity) < 0.01 {
            st.angular_velocity = 0.0;
        }
    }

    // Reproject velocity onto updated heading + Pacejka-inspired progressive cornering slip & speed scrub
    fwd_x = cosf(st.heading);
    fwd_z = sinf(st.heading);
    right_x = -sinf(st.heading);
    right_z = cosf(st.heading);

    let prev_world_vx = st.vel_x;
    let prev_world_vz = st.vel_z;
    let prev_speed_sq = prev_world_vx * prev_world_vx + prev_world_vz * prev_world_vz;
    if on_ground && prev_speed_sq > 0.2 && absf(eff_steer) > 0.03 {
        let rot_vx = fwd_x * forward_spd + right_x * lateral_spd;
        let rot_vz = fwd_z * forward_spd + right_z * lateral_spd;
        let speed_slip_bias = clampf((abs_fwd - 14.0) / 42.0, 0.0, 0.24);
        let slip_blend = if handbrake {
            0.74
        } else {
            clampf(0.18 + speed_slip_bias + st.wheel_slip * 0.24 + wet_f * 0.12, 0.16, 0.56)
        };
        let mix_vx = rot_vx * (1.0 - slip_blend) + prev_world_vx * slip_blend;
        let mix_vz = rot_vz * (1.0 - slip_blend) + prev_world_vz * slip_blend;
        forward_spd = mix_vx * fwd_x + mix_vz * fwd_z;
        lateral_spd = mix_vx * right_x + mix_vz * right_z;
    }

    if on_ground {
        let base_grip = if handbrake { sp.drift_grip } else { sp.normal_grip };
        // Non-linear Pacejka-style tire saturation + cornering tire scrub drag
        let slip_ratio = absf(lateral_spd) / maxf(abs_fwd, 3.0);
        let pacejka_sat = 1.0 / (1.0 + slip_ratio * (if handbrake { 2.35 } else { 1.05 }));
        let grip = base_grip * inp.aero_grip_bonus * downforce_grip * wet_grip_scale * (0.42 + 0.58 * pacejka_sat);
        lateral_spd *= expf(-grip * dt);
        // Natural tire cornering scrub resistance when sliding sideways
        if slip_ratio > 0.10 {
            let scrub = clampf((slip_ratio - 0.10) * 4.2, 0.0, 4.8) * dt;
            if absf(forward_spd) > scrub {
                forward_spd -= if forward_spd > 0.0 { scrub } else { -scrub };
            }
        }
        // Subtle self-aligning pneumatic trail stability when releasing steer out of a slide
        if !handbrake && absf(steer) < 0.08 && absf(lateral_spd) > 1.2 && abs_fwd > 4.0 {
            let align_yaw = clampf((lateral_spd / maxf(abs_fwd, 4.0)) * 0.65, -0.45, 0.45);
            st.heading = wrap_angle(st.heading + align_yaw * dt);
        }
    }

    st.vel_x = fwd_x * forward_spd + right_x * lateral_spd;
    st.vel_z = fwd_z * forward_spd + right_z * lateral_spd;
    st.pos_x += st.vel_x * dt;
    st.pos_z += st.vel_z * dt;
    let spin_rate = forward_spd * (1.0 + st.wheel_slip * 1.15);
    st.wheel_spin = fmodf(st.wheel_spin + spin_rate * dt * 2.4, TWO_PI);

    // 4. Planted 3D Seoul Hill, Bridge Arch & Drop Contact Physics
    let prev_y = st.pos_y;
    let ground_y = inp.ground_height;
    if st.airborne == 0 {
        if ground_y > prev_y + 0.002 && forward_spd > 2.5 {
            let climb_rate = (ground_y - prev_y) / maxf(dt, 0.004);
            st.vel_y = maxf(st.vel_y, minf(climb_rate, 12.0));
        }
        if prev_y > ground_y + 0.48 && st.vel_y > 1.0 {
            st.airborne = 1;
            st.air_time = 0.0;
        } else {
            st.pos_y = ground_y;
            if st.vel_y < 0.0 {
                st.vel_y = 0.0;
            }
        }
    }

    if st.airborne != 0 {
        st.air_time += dt;
        st.vel_y -= 17.8 * dt;
        st.pos_y += st.vel_y * dt;
        if st.pos_y <= ground_y {
            st.landed_air_time = st.air_time;
            st.landed_impact_speed = -st.vel_y;
            st.pos_y = ground_y;
            st.vel_y = 0.0;
            st.airborne = 0;
            st.air_time = 0.0;
            let impact_comp = clampf(st.landed_impact_speed * 0.017, 0.04, 0.14);
            st.wheel_comp[0] = impact_comp;
            st.wheel_comp[1] = impact_comp;
            st.wheel_comp[2] = impact_comp;
            st.wheel_comp[3] = impact_comp;
            st.pitch_vel -= clampf(st.landed_impact_speed * 0.065, 0.10, 0.58);
        }
    }

    // 5. 4-Corner Independent Curb/Bump Suspension + Under-Damped Spring-Damper Chassis Pitch & Roll
    let dh0 = clampf(inp.wheel_ground_h[0] - ground_y, -0.14, 0.14); // FL
    let dh1 = clampf(inp.wheel_ground_h[1] - ground_y, -0.14, 0.14); // FR
    let dh2 = clampf(inp.wheel_ground_h[2] - ground_y, -0.14, 0.14); // RL
    let dh3 = clampf(inp.wheel_ground_h[3] - ground_y, -0.14, 0.14); // RR
    let curb_roll = ((dh1 + dh3) - (dh0 + dh2)) * 0.24;
    let curb_pitch = ((dh0 + dh1) - (dh2 + dh3)) * 0.12;
    let road_ripple = sinf(st.pos_x * 0.72 + st.pos_z * 0.58) * 0.007 * clampf(abs_fwd / 26.0, 0.0, 1.0);
    let lat_g = clampf(forward_spd * applied_yaw_rate, -24.0, 24.0);

    // Renderer bodyPitch convention: +bodyPitch (+Z rotation) = NOSE UP / REAR SQUAT, -bodyPitch = NOSE DIVE!
    let brake_pitch = if st.braking_visual != 0 {
        if prev_fwd_spd >= 0.0 { -0.044 } else { 0.036 }
    } else {
        0.0
    };
    let target_pitch = if st.airborne != 0 {
        clampf(st.vel_y * 0.022, -0.22, 0.22)
    } else {
        inp.ramp_slope_pitch
            + curb_pitch
            + clampf(
                long_accel * 0.0028 + throttle * 0.024 + st.wheel_slip * 0.018 + brake_pitch,
                -0.11,
                0.11,
            )
    };
    let target_roll = if st.airborne != 0 {
        clampf(steer * 0.08, -0.16, 0.16)
    } else {
        clampf(-lat_g * 0.0046 - lateral_spd * 0.012 + curb_roll, -0.14, 0.14)
    };

    let spring_k = 60.0;
    let damper_c = 11.2;
    let pitch_acc = (target_pitch - st.body_pitch) * spring_k - st.pitch_vel * damper_c;
    let roll_acc = (target_roll - st.body_roll) * spring_k - st.roll_vel * damper_c;
    st.pitch_vel += pitch_acc * dt;
    st.roll_vel += roll_acc * dt;
    st.body_pitch = clampf(st.body_pitch + st.pitch_vel * dt, -0.28, 0.28);
    st.body_roll = clampf(st.body_roll + st.roll_vel * dt, -0.24, 0.24);

    // Individual 4-wheel strut deflection (FL=0, FR=1, RL=2, RR=3) combining pitch, roll, curb bump & road texture
    // Nose up (+body_pitch) extends front struts (-p_def) and compresses rear struts (+p_def)!
    if st.airborne != 0 {
        for i in 0..4 {
            st.wheel_comp[i] = lerpf(st.wheel_comp[i], -0.058, dt * 8.0);
        }
    } else {
        let p_def = st.body_pitch * 0.68;
        let r_def = st.body_roll * 0.68;
        let targets = [
            clampf(-p_def - r_def + dh0 * 0.65 + road_ripple, -0.075, 0.13), // FL
            clampf(-p_def + r_def + dh1 * 0.65 - road_ripple, -0.075, 0.13), // FR
            clampf( p_def - r_def + dh2 * 0.65 - road_ripple, -0.075, 0.13), // RL
            clampf( p_def + r_def + dh3 * 0.65 + road_ripple, -0.075, 0.13), // RR
        ];
        for i in 0..4 {
            st.wheel_comp[i] = lerpf(st.wheel_comp[i], targets[i], dt * 15.0);
        }
    }

    let speed = sqrtf(st.vel_x * st.vel_x + st.vel_z * st.vel_z);
    let drifting = on_ground
        && (speed > 7.2)
        && (absf(lateral_spd) > 2.9 || (handbrake && absf(steer) > 0.32 && speed > 8.5));
    st.is_drifting = if drifting { 1 } else { 0 };
}

#[no_mangle]
pub unsafe extern "C" fn oc_rs_resolve_rigid_collision_2d(
    body_a: *mut RsRigidBody2D,
    body_b: *mut RsRigidBody2D,
    normal_x: f32,
    normal_z: f32,
    penetration: f32,
    contact_x: f32,
    contact_z: f32,
    restitution: f32,
) -> f32 {
    if body_a.is_null() || body_b.is_null() {
        return 0.0;
    }
    let a = &mut *body_a;
    let b = &mut *body_b;

    let n_len = sqrtf(normal_x * normal_x + normal_z * normal_z);
    if n_len < 1e-5 {
        return 0.0;
    }
    let nx = normal_x / n_len;
    let nz = normal_z / n_len;

    let ma = maxf(250.0, a.mass);
    let mb = maxf(250.0, b.mass);
    let inv_ma = 1.0 / ma;
    let inv_mb = 1.0 / mb;
    let inv_total = 1.0 / (ma + mb);

    // 1. Positional MTV separation weighted by inverse mass (with subtle slop for multi-car stability)
    let corr = maxf(0.0, penetration - 0.004) * 0.92;
    a.pos_x += nx * corr * (mb * inv_total);
    a.pos_z += nz * corr * (mb * inv_total);
    b.pos_x -= nx * corr * (ma * inv_total);
    b.pos_z -= nz * corr * (ma * inv_total);

    // 2. Contact lever arms rA, rB and 2D rotational moments of inertia IA, IB
    let ra_x = contact_x - a.pos_x;
    let ra_z = contact_z - a.pos_z;
    let rb_x = contact_x - b.pos_x;
    let rb_z = contact_z - b.pos_z;

    let la = a.half_length * 2.0;
    let wa = a.half_width * 2.0;
    let lb = b.half_length * 2.0;
    let wb = b.half_width * 2.0;
    let inv_ia = 1.0 / maxf(200.0, (1.0 / 12.0) * ma * (la * la + wa * wa));
    let inv_ib = 1.0 / maxf(200.0, (1.0 / 12.0) * mb * (lb * lb + wb * wb));

    // Velocity at contact point including rotational velocity: v_p = v + omega x r
    let vap_x = a.vel_x - a.angular_velocity * ra_z;
    let vap_z = a.vel_z + a.angular_velocity * ra_x;
    let vbp_x = b.vel_x - b.angular_velocity * rb_z;
    let vbp_z = b.vel_z + b.angular_velocity * rb_x;

    let rel_vx = vap_x - vbp_x;
    let rel_vz = vap_z - vbp_z;
    let sep_vel = rel_vx * nx + rel_vz * nz;
    if sep_vel >= 0.0 {
        return 0.0;
    }

    let ra_cross_n = ra_x * nz - ra_z * nx;
    let rb_cross_n = rb_x * nz - rb_z * nx;
    let denom = inv_ma + inv_mb + (ra_cross_n * ra_cross_n) * inv_ia + (rb_cross_n * rb_cross_n) * inv_ib;
    let j_impulse = -(1.0 + clampf(restitution, 0.1, 0.8)) * sep_vel / maxf(denom, 1e-4);

    let mut ix = nx * j_impulse;
    let mut iz = nz * j_impulse;

    // 2.5 Coulomb Tangential Friction Impulse (realistic fender-rubbing resistance & PIT torque!)
    let tx = -nz;
    let tz = nx;
    let rel_vt = rel_vx * tx + rel_vz * tz;
    let ra_cross_t = ra_x * tz - ra_z * tx;
    let rb_cross_t = rb_x * tz - rb_z * tx;
    let denom_t = inv_ma + inv_mb + (ra_cross_t * ra_cross_t) * inv_ia + (rb_cross_t * rb_cross_t) * inv_ib;
    let raw_jt = -rel_vt / maxf(denom_t, 1e-4);
    let max_jt = j_impulse * 0.28; // Steel-on-steel Coulomb friction coefficient mu = 0.28
    let jt = clampf(raw_jt, -max_jt, max_jt);

    ix += tx * jt;
    iz += tz * jt;

    a.vel_x += ix * inv_ma;
    a.vel_z += iz * inv_ma;
    b.vel_x -= ix * inv_mb;
    b.vel_z -= iz * inv_mb;

    // Combined Normal + Friction Torque Impulse: r x (J_n + J_t)
    let torque_a = (ra_x * iz - ra_z * ix) * inv_ia;
    let torque_b = -(rb_x * iz - rb_z * ix) * inv_ib;
    a.angular_velocity = clampf(a.angular_velocity + torque_a * 0.68, -4.5, 4.5);
    b.angular_velocity = clampf(b.angular_velocity + torque_b * 0.68, -4.5, 4.5);

    // 3. Accumulate localized 4-quadrant chassis crumple deformation on heavy impacts
    let impact_speed = -sep_vel;
    if impact_speed > 5.5 {
        let base_deform = clampf((impact_speed - 5.0) * 0.028, 0.0, 0.45);
        apply_local_crumple(a, contact_x, contact_z, base_deform * (mb * inv_total * 1.8));
        apply_local_crumple(b, contact_x, contact_z, base_deform * (ma * inv_total * 1.8));
    }

    impact_speed
}

unsafe fn apply_local_crumple(body: &mut RsRigidBody2D, cx: f32, cz: f32, amount: f32) {
    let dx = cx - body.pos_x;
    let dz = cz - body.pos_z;
    let fwd_x = cosf(body.heading);
    let fwd_z = sinf(body.heading);
    let right_x = -sinf(body.heading);
    let right_z = cosf(body.heading);

    let local_x = dx * fwd_x + dz * fwd_z;
    let local_z = dx * right_x + dz * right_z;

    if local_x > body.half_length * 0.25 {
        body.front_damage = clampf(body.front_damage + amount, 0.0, 1.0);
    } else if local_x < -body.half_length * 0.25 {
        body.rear_damage = clampf(body.rear_damage + amount, 0.0, 1.0);
    }
    if local_z < -body.half_width * 0.25 {
        body.left_damage = clampf(body.left_damage + amount, 0.0, 1.0);
    } else if local_z > body.half_width * 0.25 {
        body.right_damage = clampf(body.right_damage + amount, 0.0, 1.0);
    }
}
