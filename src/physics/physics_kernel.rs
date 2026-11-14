// ============================================================================
// Open City C++ v0.9.0 — Native Rust Rigid-Body, Gearbox & Suspension Kernel
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
    900 // v0.9.0 Rust Physics Kernel
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

    let throttle = clampf(inp.throttle, -1.0, 1.0);
    let steer = clampf(inp.steer, -1.0, 1.0);
    let handbrake = inp.handbrake != 0;
    let nitro = inp.nitro != 0;
    let wet_grip_scale = 1.0 - 0.14 * clampf(inp.wet_road_factor, 0.0, 1.0);

    let mut fwd_x = cosf(st.heading);
    let mut fwd_z = sinf(st.heading);
    let mut right_x = -sinf(st.heading);
    let mut right_z = cosf(st.heading);

    let prev_fwd_spd = st.vel_x * fwd_x + st.vel_z * fwd_z;
    let mut forward_spd = prev_fwd_spd;
    let mut lateral_spd = st.vel_x * right_x + st.vel_z * right_z;

    let max_fwd = sp.max_forward_speed * inp.upgrade_mult * (if nitro && st.nitro_charge > 0.03 { 1.32 } else { 1.0 });

    // 1. Longitudinal Drivetrain Force (only when tires have ground contact or slight air control)
    let on_ground = st.airborne == 0;
    if throttle > 0.05 {
        if forward_spd < -0.5 {
            forward_spd = minf(0.0, forward_spd + sp.braking * dt);
            st.braking_visual = 1;
        } else {
            let mut accel = sp.acceleration * inp.upgrade_mult * throttle;
            if nitro && st.nitro_charge > 0.03 {
                accel *= 1.65;
                st.nitro_charge = maxf(0.0, st.nitro_charge - dt * 0.38);
                st.nitro_active = 1;
            }
            if !on_ground {
                accel *= 0.18;
            }
            forward_spd = minf(max_fwd, forward_spd + accel * dt);
        }
    } else if throttle < -0.05 {
        if forward_spd > 1.2 {
            forward_spd = maxf(0.0, forward_spd - sp.braking * (-throttle) * dt);
            st.braking_visual = 1;
        } else {
            let rev_accel = sp.acceleration * 0.68 * throttle * (if on_ground { 1.0 } else { 0.18 });
            forward_spd = maxf(-sp.max_reverse_speed, forward_spd + rev_accel * dt);
        }
    } else if on_ground {
        // Natural rolling resistance + aerodynamic quadratic drag
        let aero_drag = 0.0018 * forward_spd * absf(forward_spd);
        let drag = (4.2 + aero_drag) * dt;
        if absf(forward_spd) <= drag {
            forward_spd = 0.0;
        } else {
            forward_spd -= if forward_spd > 0.0 { drag } else { -drag };
        }
    }

    if st.nitro_active == 0 {
        st.nitro_charge = minf(1.0, st.nitro_charge + dt * inp.nitro_recharge_rate);
    }

    if handbrake && on_ground {
        st.braking_visual = 1;
        let hb_decel = 11.5 * dt;
        if absf(forward_spd) <= hb_decel {
            forward_spd = 0.0;
        } else {
            forward_spd -= if forward_spd > 0.0 { hb_decel } else { -hb_decel };
        }
    }

    // 2. 6-Speed Automatic Sports Gearbox & Engine RPM Simulation
    let abs_fwd = absf(forward_spd);
    let prev_gear = st.current_gear;
    if forward_spd < -0.55 {
        st.current_gear = 0; // Reverse ('R')
        let rev_ratio = clampf(abs_fwd / maxf(1.0, sp.max_reverse_speed), 0.0, 1.0);
        let target_rpm = 1050.0 + rev_ratio * 5200.0 + absf(throttle) * 650.0;
        st.engine_rpm = lerpf(st.engine_rpm, target_rpm, dt * 12.0);
    } else {
        let top_ref = maxf(18.0, sp.max_forward_speed * inp.upgrade_mult);
        let norm_spd = clampf(abs_fwd / top_ref, 0.0, 1.15);
        let gear = if abs_fwd < 0.35 {
            1
        } else if norm_spd < 0.17 {
            1
        } else if norm_spd < 0.34 {
            2
        } else if norm_spd < 0.52 {
            3
        } else if norm_spd < 0.71 {
            4
        } else if norm_spd < 0.90 {
            5
        } else {
            6
        };
        st.current_gear = gear;
        let gear_low = (gear - 1) as f32 * 0.165;
        let gear_span = 0.21;
        let in_gear = clampf((norm_spd - gear_low) / gear_span, 0.0, 1.05);
        let idle_rpm = 880.0;
        let target_rpm = idle_rpm + in_gear * 6150.0 + maxf(0.0, throttle) * 780.0 + (if st.nitro_active != 0 { 450.0 } else { 0.0 });
        st.engine_rpm = lerpf(st.engine_rpm, clampf(target_rpm, idle_rpm, 8150.0), dt * 14.0);
        // Subtle transmission shift-kick pitch impulse on upshift under throttle!
        if gear > prev_gear && prev_gear >= 1 && throttle > 0.4 {
            st.pitch_vel -= 0.18;
        }
    }

    // 3. Speed-Sensitive Ackermann Steering + Angular Momentum Integration
    st.steer_visual = lerpf(st.steer_visual, steer, dt * 10.0);
    if abs_fwd > 0.45 && on_ground {
        let low_speed_factor = clampf(abs_fwd / 6.5, 0.0, 1.0);
        let high_speed_damp = 1.0 / (1.0 + abs_fwd * 0.018);
        let dir_sign = if forward_spd >= 0.0 { 1.0 } else { -1.0 };
        let drift_boost = if handbrake { 1.38 } else { 1.0 };
        let steer_yaw_rate = steer * sp.turn_rate * low_speed_factor * high_speed_damp * drift_boost * dir_sign;
        st.heading = wrap_angle(st.heading + steer_yaw_rate * dt);
    }

    // Integrate external collision angular velocity (PIT maneuvers & off-center crashes)
    if absf(st.angular_velocity) > 0.001 {
        st.heading = wrap_angle(st.heading + st.angular_velocity * dt);
        let ang_damp = if on_ground { 4.8 } else { 0.8 };
        st.angular_velocity *= expf(-ang_damp * dt);
        if absf(st.angular_velocity) < 0.01 {
            st.angular_velocity = 0.0;
        }
    }

    // Reproject velocity onto updated heading + Pacejka-inspired cornering slip
    fwd_x = cosf(st.heading);
    fwd_z = sinf(st.heading);
    right_x = -sinf(st.heading);
    right_z = cosf(st.heading);

    let prev_world_vx = st.vel_x;
    let prev_world_vz = st.vel_z;
    let prev_speed_sq = prev_world_vx * prev_world_vx + prev_world_vz * prev_world_vz;
    if on_ground && prev_speed_sq > 0.2 && absf(steer) > 0.05 {
        let rot_vx = fwd_x * forward_spd + right_x * lateral_spd;
        let rot_vz = fwd_z * forward_spd + right_z * lateral_spd;
        let slip_blend = if handbrake { 0.68 } else { 0.24 };
        let mix_vx = rot_vx * (1.0 - slip_blend) + prev_world_vx * slip_blend;
        let mix_vz = rot_vz * (1.0 - slip_blend) + prev_world_vz * slip_blend;
        forward_spd = mix_vx * fwd_x + mix_vz * fwd_z;
        lateral_spd = mix_vx * right_x + mix_vz * right_z;
    }

    if on_ground {
        let base_grip = if handbrake { sp.drift_grip } else { sp.normal_grip };
        let grip = base_grip * inp.aero_grip_bonus * wet_grip_scale;
        lateral_spd *= maxf(0.0, 1.0 - grip * dt);
    }

    st.vel_x = fwd_x * forward_spd + right_x * lateral_spd;
    st.vel_z = fwd_z * forward_spd + right_z * lateral_spd;
    st.pos_x += st.vel_x * dt;
    st.pos_z += st.vel_z * dt;
    st.wheel_spin = fmodf(st.wheel_spin + forward_spd * dt * 2.4, TWO_PI);

    // 4. 3D Vertical Ramp Launch, Parabolic Flight & Ground Contact Physics
    let prev_y = st.pos_y;
    let ground_y = inp.ground_height;
    if st.airborne == 0 {
        if ground_y > prev_y + 0.002 && forward_spd > 2.5 {
            let climb_rate = (ground_y - prev_y) / maxf(dt, 0.004);
            st.vel_y = maxf(st.vel_y, minf(climb_rate, 16.5));
        }
        // Launch into airborne state when dropping off a ramp lip or crest!
        if prev_y > ground_y + 0.16 && st.vel_y > -2.5 {
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
        st.vel_y -= 17.8 * dt; // Realistic gravity
        st.pos_y += st.vel_y * dt;
        // Landing check
        if st.pos_y <= ground_y {
            st.landed_air_time = st.air_time;
            st.landed_impact_speed = -st.vel_y;
            st.pos_y = ground_y;
            st.vel_y = 0.0;
            st.airborne = 0;
            st.air_time = 0.0;
            // Compress all 4 suspension struts & excite pitch spring on touchdown!
            let impact_comp = clampf(st.landed_impact_speed * 0.016, 0.04, 0.14);
            st.wheel_comp[0] = impact_comp;
            st.wheel_comp[1] = impact_comp;
            st.wheel_comp[2] = impact_comp;
            st.wheel_comp[3] = impact_comp;
            st.pitch_vel += clampf(st.landed_impact_speed * 0.06, 0.10, 0.55);
        }
    }

    // 5. Under-Damped Spring-Damper Chassis Pitch, Roll & 4-Wheel Suspension Compression
    let long_accel = clampf((forward_spd - prev_fwd_spd) / maxf(dt, 0.004), -28.0, 28.0);
    let target_pitch = if st.airborne != 0 {
        clampf(-st.vel_y * 0.022, -0.22, 0.22)
    } else {
        inp.ramp_slope_pitch + clampf(-long_accel * 0.0024 - throttle * 0.022 + (if st.braking_visual != 0 { 0.036 } else { 0.0 }), -0.09, 0.09)
    };
    let target_roll = if st.airborne != 0 {
        clampf(steer * 0.08, -0.16, 0.16)
    } else {
        clampf(-lateral_spd * 0.0155, -0.12, 0.12)
    };

    let spring_k = 58.0;
    let damper_c = 11.5;
    let pitch_acc = (target_pitch - st.body_pitch) * spring_k - st.pitch_vel * damper_c;
    let roll_acc = (target_roll - st.body_roll) * spring_k - st.roll_vel * damper_c;
    st.pitch_vel += pitch_acc * dt;
    st.roll_vel += roll_acc * dt;
    st.body_pitch = clampf(st.body_pitch + st.pitch_vel * dt, -0.28, 0.28);
    st.body_roll = clampf(st.body_roll + st.roll_vel * dt, -0.22, 0.22);

    // Individual 4-wheel strut deflection (FL=0, FR=1, RL=2, RR=3)
    if st.airborne != 0 {
        // Wheels droop downward when airborne
        for i in 0..4 {
            st.wheel_comp[i] = lerpf(st.wheel_comp[i], -0.055, dt * 8.0);
        }
    } else {
        let p_def = st.body_pitch * 0.65;
        let r_def = st.body_roll * 0.65;
        let targets = [
            clampf( p_def - r_def, -0.07, 0.12), // FL
            clampf( p_def + r_def, -0.07, 0.12), // FR
            clampf(-p_def - r_def, -0.07, 0.12), // RL
            clampf(-p_def + r_def, -0.07, 0.12), // RR
        ];
        for i in 0..4 {
            st.wheel_comp[i] = lerpf(st.wheel_comp[i], targets[i], dt * 14.0);
        }
    }

    let speed = sqrtf(st.vel_x * st.vel_x + st.vel_z * st.vel_z);
    let drifting = on_ground && (speed > 7.5) && (absf(lateral_spd) > 3.1 || (handbrake && absf(steer) > 0.35 && speed > 9.0));
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

    // 1. Positional MTV separation weighted by inverse mass
    a.pos_x += nx * penetration * (mb * inv_total);
    a.pos_z += nz * penetration * (mb * inv_total);
    b.pos_x -= nx * penetration * (ma * inv_total);
    b.pos_z -= nz * penetration * (ma * inv_total);

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

    let ix = nx * j_impulse;
    let iz = nz * j_impulse;

    a.vel_x += ix * inv_ma;
    a.vel_z += iz * inv_ma;
    b.vel_x -= ix * inv_mb;
    b.vel_z -= iz * inv_mb;

    // Torque impulse: r x J (induces authentic PIT maneuver spin-outs!)
    let torque_a = (ra_x * iz - ra_z * ix) * inv_ia;
    let torque_b = -(rb_x * iz - rb_z * ix) * inv_ib;
    a.angular_velocity = clampf(a.angular_velocity + torque_a * 0.65, -4.2, 4.2);
    b.angular_velocity = clampf(b.angular_velocity + torque_b * 0.65, -4.2, 4.2);

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
