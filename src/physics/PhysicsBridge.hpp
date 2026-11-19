#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

extern "C" {

struct RsVehicleSpecParams {
    float mass;
    float max_forward_speed;
    float max_reverse_speed;
    float acceleration;
    float braking;
    float turn_rate;
    float normal_grip;
    float drift_grip;
    float half_length;
    float half_width;
    float chassis_height;
};

struct RsVehicleControlInput {
    float throttle;
    float steer;
    std::int32_t handbrake;
    std::int32_t nitro;
    float upgrade_mult;
    float aero_grip_bonus;
    float nitro_recharge_rate;
    float ground_height;
    float ramp_slope_pitch;
    float wet_road_factor;
    float wheel_ground_h[4]; // FL, FR, RL, RR surface elevation for 4-corner curb suspension
};

struct RsVehiclePhysicsState {
    float pos_x;
    float pos_y;
    float pos_z;
    float vel_x;
    float vel_y;
    float vel_z;
    float heading;
    float angular_velocity;
    float steer_visual;
    float wheel_spin;
    float body_pitch;
    float body_roll;
    float pitch_vel;
    float roll_vel;
    float wheel_comp[4];
    float nitro_charge;
    float engine_rpm;
    std::int32_t current_gear;
    std::int32_t braking_visual;
    std::int32_t nitro_active;
    std::int32_t is_drifting;
    std::int32_t airborne;
    float air_time;
    float landed_air_time;
    float landed_impact_speed;
    float turbo_spool;
    float wheel_slip;
    float shift_cut_timer;
    std::int32_t shift_backfire;
    float brake_heat;
};

struct RsRigidBody2D {
    float pos_x;
    float pos_z;
    float vel_x;
    float vel_z;
    float heading;
    float angular_velocity;
    float mass;
    float half_length;
    float half_width;
    float front_damage;
    float rear_damage;
    float left_damage;
    float right_damage;
};

#if defined(OC_USE_RUST_PHYSICS)
std::int32_t oc_rs_physics_version();
void oc_rs_step_vehicle_dynamics(float dt,
                                 RsVehiclePhysicsState* state,
                                 const RsVehicleSpecParams* spec,
                                 const RsVehicleControlInput* input);
float oc_rs_resolve_rigid_collision_2d(RsRigidBody2D* body_a,
                                       RsRigidBody2D* body_b,
                                       float normal_x,
                                       float normal_z,
                                       float penetration,
                                       float contact_x,
                                       float contact_z,
                                       float restitution);
#else
inline std::int32_t oc_rs_physics_version() { return 1200; }

inline void oc_rs_step_vehicle_dynamics(float dt,
                                        RsVehiclePhysicsState* st,
                                        const RsVehicleSpecParams* sp,
                                        const RsVehicleControlInput* inp) {
    if (!st || !sp || !inp || dt <= 0.0f) return;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    auto clampf = [](float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); };
    auto lerpf = [&](float a, float b, float t) { return a + (b - a) * clampf(t, 0.0f, 1.0f); };
    auto wrapAngle = [&](float a) {
        a = std::fmod(a + kPi, kTwoPi);
        if (a < 0.0f) a += kTwoPi;
        return a - kPi;
    };

    st->landed_air_time = 0.0f;
    st->landed_impact_speed = 0.0f;
    st->braking_visual = 0;
    st->nitro_active = 0;
    st->shift_backfire = 0;
    if (st->shift_cut_timer > 0.0f) {
        st->shift_cut_timer = std::max(0.0f, st->shift_cut_timer - dt);
    }

    const float throttle = clampf(inp->throttle, -1.0f, 1.0f);
    const float steer = clampf(inp->steer, -1.0f, 1.0f);
    const bool handbrake = (inp->handbrake != 0);
    const bool nitro = (inp->nitro != 0);
    const float wetF = clampf(inp->wet_road_factor, 0.0f, 1.0f);
    const float wetGripScale = 1.0f - 0.16f * wetF;

    float fwdX = std::cos(st->heading), fwdZ = std::sin(st->heading);
    float rightX = -std::sin(st->heading), rightZ = std::cos(st->heading);

    const float prevFwdSpd = st->vel_x * fwdX + st->vel_z * fwdZ;
    float forwardSpd = prevFwdSpd;
    float lateralSpd = st->vel_x * rightX + st->vel_z * rightZ;

    const bool onGround = (st->airborne == 0);
    const float topRef = std::max(18.0f, sp->max_forward_speed * inp->upgrade_mult);
    const bool nitroReady = (nitro && st->nitro_charge > 0.03f);
    const float maxFwd = topRef * (nitroReady ? 1.32f : 1.0f);

    const int curGear = (st->current_gear >= 1 && st->current_gear <= 6) ? st->current_gear : 1;
    static constexpr float kGearTorque[6] = {1.38f, 1.18f, 1.02f, 0.88f, 0.76f, 0.66f};
    const float gearTorqueMul = kGearTorque[curGear - 1];

    const float rpmNorm = clampf((st->engine_rpm - 880.0f) / 7270.0f, 0.0f, 1.0f);
    const float torqueCurve = 0.78f + 0.34f * std::sin(clampf(rpmNorm * 1.10f, 0.0f, 1.0f) * kPi);

    const float prevSpool = st->turbo_spool;
    if (throttle > 0.25f && forwardSpd >= -0.4f) {
        const float spoolRate = 1.65f + 0.55f * inp->upgrade_mult + (nitroReady ? 1.2f : 0.0f);
        const float spoolTarget = clampf(throttle * (0.45f + 0.65f * rpmNorm), 0.0f, 1.0f);
        st->turbo_spool = lerpf(st->turbo_spool, spoolTarget, dt * spoolRate);
    } else {
        st->turbo_spool = std::max(0.0f, st->turbo_spool - dt * 2.8f);
        if (prevSpool > 0.68f && st->engine_rpm > 4700.0f && throttle <= 0.05f && onGround) {
            st->shift_backfire = 1;
        }
    }
    const float boostMul = 1.0f + 0.18f * st->turbo_spool;

    float targetSlip = 0.0f;
    if (throttle > 0.05f) {
        if (forwardSpd < -0.5f) {
            const float brakeForce = sp->braking * (1.06f + 0.006f * std::abs(forwardSpd)) * (1.0f - 0.12f * wetF);
            forwardSpd = std::min(0.0f, forwardSpd + brakeForce * dt);
            st->braking_visual = 1;
        } else {
            const float clutchMul = (st->shift_cut_timer > 0.0f) ? 0.32f : 1.0f;
            float accel = sp->acceleration * inp->upgrade_mult * throttle * gearTorqueMul * torqueCurve * boostMul * clutchMul;
            if (nitroReady) {
                accel *= 1.58f;
                st->nitro_charge = std::max(0.0f, st->nitro_charge - dt * 0.36f);
                st->nitro_active = 1;
            }
            if (onGround && forwardSpd < 11.5f && throttle > 0.60f) {
                const float tractionLimit = (20.5f + sp->normal_grip * 1.45f) * wetGripScale;
                if (accel > tractionLimit) {
                    const float excess = (accel - tractionLimit) / std::max(tractionLimit, 1.0f);
                    targetSlip = clampf(excess * 1.35f + (nitroReady ? 0.25f : 0.0f), 0.0f, 1.0f);
                    accel = tractionLimit + (accel - tractionLimit) * 0.62f;
                }
            }
            if (!onGround) accel *= 0.16f;
            const float aeroDrag = (0.55f + 0.00052f * forwardSpd * forwardSpd) * (onGround ? 1.0f : 0.35f);
            forwardSpd = std::min(maxFwd, std::max(0.0f, forwardSpd + (accel - aeroDrag) * dt));
        }
    } else if (throttle < -0.05f) {
        if (forwardSpd > 1.2f) {
            const float absPulse = 1.0f + 0.03f * std::sin(st->wheel_spin * 9.0f);
            const float brakeForce = sp->braking * (1.08f + 0.007f * forwardSpd) * (-throttle) * (1.0f - 0.14f * wetF) * absPulse;
            forwardSpd = std::max(0.0f, forwardSpd - brakeForce * dt);
            st->braking_visual = 1;
        } else {
            const float revAccel = sp->acceleration * 0.72f * throttle * (onGround ? 1.0f : 0.16f);
            forwardSpd = std::max(-sp->max_reverse_speed, forwardSpd + revAccel * dt);
        }
    } else if (onGround) {
        const float aeroDrag = 0.0017f * forwardSpd * std::abs(forwardSpd);
        const float drag = (3.4f + aeroDrag) * dt;
        if (std::abs(forwardSpd) <= drag) forwardSpd = 0.0f;
        else forwardSpd -= (forwardSpd > 0.0f ? drag : -drag);
    }

    st->wheel_slip = lerpf(st->wheel_slip, targetSlip, dt * 11.0f);

    if (st->nitro_active == 0) {
        st->nitro_charge = std::min(1.0f, st->nitro_charge + dt * inp->nitro_recharge_rate);
    }
    if (handbrake && onGround) {
        st->braking_visual = 1;
        const float hbDecel = (12.2f - 1.5f * wetF) * dt;
        if (std::abs(forwardSpd) <= hbDecel) forwardSpd = 0.0f;
        else forwardSpd -= (forwardSpd > 0.0f ? hbDecel : -hbDecel);
    }

    if (st->braking_visual != 0 && std::abs(prevFwdSpd) > 9.0f && onGround) {
        const float heatAdd = clampf((std::abs(prevFwdSpd) - 7.0f) / 24.0f, 0.20f, 1.55f) * dt * 1.45f;
        st->brake_heat = std::min(1.0f, st->brake_heat + heatAdd);
    } else {
        const float coolRate = 0.36f + 0.012f * std::abs(forwardSpd);
        st->brake_heat *= std::exp(-coolRate * dt);
        if (st->brake_heat < 0.01f) st->brake_heat = 0.0f;
    }

    const float absFwd = std::abs(forwardSpd);
    const int prevGear = st->current_gear;
    if (forwardSpd < -0.55f) {
        st->current_gear = 0;
        const float revRatio = clampf(absFwd / std::max(1.0f, sp->max_reverse_speed), 0.0f, 1.0f);
        st->engine_rpm = lerpf(st->engine_rpm, 1050.0f + revRatio * 5200.0f + std::abs(throttle) * 680.0f, dt * 12.0f);
    } else {
        const float normSpd = clampf(absFwd / topRef, 0.0f, 1.20f);
        static constexpr float kUpThresh[5] = {0.18f, 0.35f, 0.53f, 0.72f, 0.90f};
        int gear = (absFwd < 0.35f || normSpd < kUpThresh[0]) ? 1
                 : (normSpd < kUpThresh[1]) ? 2
                 : (normSpd < kUpThresh[2]) ? 3
                 : (normSpd < kUpThresh[3]) ? 4
                 : (normSpd < kUpThresh[4]) ? 5 : 6;
        if (prevGear >= 2 && prevGear <= 6 && gear == prevGear - 1) {
            if (normSpd > kUpThresh[prevGear - 2] - 0.030f) gear = prevGear;
        }
        st->current_gear = gear;
        const float gearLow = static_cast<float>(gear - 1) * 0.165f;
        const float inGear = clampf((normSpd - gearLow) / 0.21f, 0.0f, 1.06f);
        const float targetRpm = 880.0f + inGear * 6100.0f + std::max(0.0f, throttle) * 760.0f
                              + st->wheel_slip * 1350.0f + (st->nitro_active ? 480.0f : 0.0f);
        st->engine_rpm = lerpf(st->engine_rpm, clampf(targetRpm, 880.0f, 8150.0f), dt * 15.0f);
        if (gear > prevGear && prevGear >= 1 && throttle > 0.40f) {
            st->shift_cut_timer = 0.055f;
            st->shift_backfire = 1;
            st->pitch_vel -= 0.26f;
        }
    }

    const float longAccel = clampf((forwardSpd - prevFwdSpd) / std::max(dt, 0.004f), -32.0f, 32.0f);
    const float frontWeightTransfer = clampf(1.0f - longAccel * 0.011f, 0.84f, 1.22f);
    const float downforceGrip = 1.0f + clampf(absFwd * absFwd * 0.00022f * inp->aero_grip_bonus, 0.0f, 0.36f);

    st->steer_visual = lerpf(st->steer_visual, steer, dt * 11.5f);
    float appliedYawRate = 0.0f;
    if (absFwd > 0.40f && onGround) {
        const float lowSpeedFactor = clampf(absFwd / 5.8f, 0.0f, 1.0f);
        const float highSpeedDamp = 1.0f / (1.0f + absFwd * 0.017f);
        const float dirSign = (forwardSpd >= 0.0f) ? 1.0f : -1.0f;
        const float driftBoost = handbrake ? 1.42f : 1.0f;
        appliedYawRate = steer * sp->turn_rate * lowSpeedFactor * highSpeedDamp * driftBoost * frontWeightTransfer * dirSign;
        st->heading = wrapAngle(st->heading + appliedYawRate * dt);
    }

    if (std::abs(st->angular_velocity) > 0.001f) {
        st->heading = wrapAngle(st->heading + st->angular_velocity * dt);
        st->angular_velocity *= std::exp(-(onGround ? (4.6f * wetGripScale) : 0.8f) * dt);
        if (std::abs(st->angular_velocity) < 0.01f) st->angular_velocity = 0.0f;
    }

    fwdX = std::cos(st->heading); fwdZ = std::sin(st->heading);
    rightX = -std::sin(st->heading); rightZ = std::cos(st->heading);

    const float prevVx = st->vel_x, prevVz = st->vel_z;
    if (onGround && (prevVx * prevVx + prevVz * prevVz) > 0.2f && std::abs(steer) > 0.04f) {
        const float rotVx = fwdX * forwardSpd + rightX * lateralSpd;
        const float rotVz = fwdZ * forwardSpd + rightZ * lateralSpd;
        const float slipBlend = handbrake ? 0.70f : clampf(0.20f + st->wheel_slip * 0.22f + wetF * 0.08f, 0.18f, 0.48f);
        const float mixVx = rotVx * (1.0f - slipBlend) + prevVx * slipBlend;
        const float mixVz = rotVz * (1.0f - slipBlend) + prevVz * slipBlend;
        forwardSpd = mixVx * fwdX + mixVz * fwdZ;
        lateralSpd = mixVx * rightX + mixVz * rightZ;
    }

    if (onGround) {
        const float baseGrip = handbrake ? sp->drift_grip : sp->normal_grip;
        const float slipRatio = std::abs(lateralSpd) / std::max(absFwd, 3.0f);
        const float pacejkaSat = 1.0f / (1.0f + slipRatio * (handbrake ? 2.2f : 0.95f));
        const float grip = baseGrip * inp->aero_grip_bonus * downforceGrip * wetGripScale * (0.45f + 0.55f * pacejkaSat);
        lateralSpd *= std::exp(-grip * dt);
    }

    st->vel_x = fwdX * forwardSpd + rightX * lateralSpd;
    st->vel_z = fwdZ * forwardSpd + rightZ * lateralSpd;
    st->pos_x += st->vel_x * dt;
    st->pos_z += st->vel_z * dt;
    st->wheel_spin = std::fmod(st->wheel_spin + forwardSpd * (1.0f + st->wheel_slip * 1.15f) * dt * 2.4f, kTwoPi);

    const float prevY = st->pos_y;
    const float groundY = inp->ground_height;
    if (st->airborne == 0) {
        if (groundY > prevY + 0.002f && forwardSpd > 2.5f) {
            const float climbRate = (groundY - prevY) / std::max(dt, 0.004f);
            st->vel_y = std::max(st->vel_y, std::min(climbRate, 16.5f));
        }
        if (prevY > groundY + 0.16f && st->vel_y > -2.5f) {
            st->airborne = 1;
            st->air_time = 0.0f;
        } else {
            st->pos_y = groundY;
            if (st->vel_y < 0.0f) st->vel_y = 0.0f;
        }
    }

    if (st->airborne != 0) {
        st->air_time += dt;
        st->vel_y -= 17.8f * dt;
        st->pos_y += st->vel_y * dt;
        if (st->pos_y <= groundY) {
            st->landed_air_time = st->air_time;
            st->landed_impact_speed = -st->vel_y;
            st->pos_y = groundY;
            st->vel_y = 0.0f;
            st->airborne = 0;
            st->air_time = 0.0f;
            const float comp = clampf(st->landed_impact_speed * 0.017f, 0.04f, 0.14f);
            for (int i = 0; i < 4; ++i) st->wheel_comp[i] = comp;
            st->pitch_vel += clampf(st->landed_impact_speed * 0.065f, 0.10f, 0.58f);
        }
    }

    const float dh0 = clampf(inp->wheel_ground_h[0] - groundY, -0.14f, 0.14f);
    const float dh1 = clampf(inp->wheel_ground_h[1] - groundY, -0.14f, 0.14f);
    const float dh2 = clampf(inp->wheel_ground_h[2] - groundY, -0.14f, 0.14f);
    const float dh3 = clampf(inp->wheel_ground_h[3] - groundY, -0.14f, 0.14f);
    const float curbRoll = ((dh1 + dh3) - (dh0 + dh2)) * 0.24f;
    const float curbPitch = ((dh0 + dh1) - (dh2 + dh3)) * 0.12f;
    const float roadRipple = std::sin(st->pos_x * 0.72f + st->pos_z * 0.58f) * 0.007f * clampf(absFwd / 26.0f, 0.0f, 1.0f);
    const float latG = clampf(forwardSpd * appliedYawRate, -24.0f, 24.0f);

    const float targetPitch = (st->airborne != 0)
        ? clampf(-st->vel_y * 0.022f, -0.22f, 0.22f)
        : (inp->ramp_slope_pitch + curbPitch +
           clampf(-longAccel * 0.0028f - throttle * 0.026f - st->wheel_slip * 0.018f + (st->braking_visual ? 0.044f : 0.0f), -0.11f, 0.11f));
    const float targetRoll = (st->airborne != 0)
        ? clampf(steer * 0.08f, -0.16f, 0.16f)
        : clampf(-latG * 0.0046f - lateralSpd * 0.012f + curbRoll, -0.14f, 0.14f);

    st->pitch_vel += ((targetPitch - st->body_pitch) * 60.0f - st->pitch_vel * 11.2f) * dt;
    st->roll_vel  += ((targetRoll  - st->body_roll)  * 60.0f - st->roll_vel  * 11.2f) * dt;
    st->body_pitch = clampf(st->body_pitch + st->pitch_vel * dt, -0.28f, 0.28f);
    st->body_roll  = clampf(st->body_roll  + st->roll_vel  * dt, -0.24f, 0.24f);

    if (st->airborne != 0) {
        for (int i = 0; i < 4; ++i) st->wheel_comp[i] = lerpf(st->wheel_comp[i], -0.058f, dt * 8.0f);
    } else {
        const float pDef = st->body_pitch * 0.68f, rDef = st->body_roll * 0.68f;
        const float targets[4] = {
            clampf( pDef - rDef + dh0 * 0.65f + roadRipple, -0.075f, 0.13f),
            clampf( pDef + rDef + dh1 * 0.65f - roadRipple, -0.075f, 0.13f),
            clampf(-pDef - rDef + dh2 * 0.65f - roadRipple, -0.075f, 0.13f),
            clampf(-pDef + rDef + dh3 * 0.65f + roadRipple, -0.075f, 0.13f)
        };
        for (int i = 0; i < 4; ++i) st->wheel_comp[i] = lerpf(st->wheel_comp[i], targets[i], dt * 15.0f);
    }

    const float speed = std::sqrt(st->vel_x * st->vel_x + st->vel_z * st->vel_z);
    st->is_drifting = (onGround && speed > 7.2f && (std::abs(lateralSpd) > 2.9f || (handbrake && std::abs(steer) > 0.32f && speed > 8.5f))) ? 1 : 0;
}

inline float oc_rs_resolve_rigid_collision_2d(RsRigidBody2D* a,
                                              RsRigidBody2D* b,
                                              float normal_x,
                                              float normal_z,
                                              float penetration,
                                              float contact_x,
                                              float contact_z,
                                              float restitution) {
    if (!a || !b) return 0.0f;
    auto clampf = [](float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); };
    const float nLen = std::sqrt(normal_x * normal_x + normal_z * normal_z);
    if (nLen < 1e-5f) return 0.0f;
    const float nx = normal_x / nLen, nz = normal_z / nLen;

    const float ma = std::max(250.0f, a->mass), mb = std::max(250.0f, b->mass);
    const float invMa = 1.0f / ma, invMb = 1.0f / mb, invTotal = 1.0f / (ma + mb);

    const float corr = std::max(0.0f, penetration - 0.004f) * 0.92f;
    a->pos_x += nx * corr * (mb * invTotal);
    a->pos_z += nz * corr * (mb * invTotal);
    b->pos_x -= nx * corr * (ma * invTotal);
    b->pos_z -= nz * corr * (ma * invTotal);

    const float raX = contact_x - a->pos_x, raZ = contact_z - a->pos_z;
    const float rbX = contact_x - b->pos_x, rbZ = contact_z - b->pos_z;
    const float la = a->half_length * 2.0f, wa = a->half_width * 2.0f;
    const float lb = b->half_length * 2.0f, wb = b->half_width * 2.0f;
    const float invIa = 1.0f / std::max(200.0f, (1.0f / 12.0f) * ma * (la * la + wa * wa));
    const float invIb = 1.0f / std::max(200.0f, (1.0f / 12.0f) * mb * (lb * lb + wb * wb));

    const float vapX = a->vel_x - a->angular_velocity * raZ;
    const float vapZ = a->vel_z + a->angular_velocity * raX;
    const float vbpX = b->vel_x - b->angular_velocity * rbZ;
    const float vbpZ = b->vel_z + b->angular_velocity * rbX;

    const float relVx = vapX - vbpX;
    const float relVz = vapZ - vbpZ;
    const float sepVel = relVx * nx + relVz * nz;
    if (sepVel >= 0.0f) return 0.0f;

    const float raCrossN = raX * nz - raZ * nx;
    const float rbCrossN = rbX * nz - rbZ * nx;
    const float denom = invMa + invMb + (raCrossN * raCrossN) * invIa + (rbCrossN * rbCrossN) * invIb;
    const float jImpulse = -(1.0f + clampf(restitution, 0.1f, 0.8f)) * sepVel / std::max(denom, 1e-4f);

    float ix = nx * jImpulse, iz = nz * jImpulse;

    // Coulomb Tangential Friction Impulse (mu = 0.28)
    const float tx = -nz, tz = nx;
    const float relVt = relVx * tx + relVz * tz;
    const float raCrossT = raX * tz - raZ * tx;
    const float rbCrossT = rbX * tz - rbZ * tx;
    const float denomT = invMa + invMb + (raCrossT * raCrossT) * invIa + (rbCrossT * rbCrossT) * invIb;
    const float maxJt = jImpulse * 0.28f;
    const float jt = clampf(-relVt / std::max(denomT, 1e-4f), -maxJt, maxJt);
    ix += tx * jt;
    iz += tz * jt;

    a->vel_x += ix * invMa; a->vel_z += iz * invMa;
    b->vel_x -= ix * invMb; b->vel_z -= iz * invMb;

    a->angular_velocity = clampf(a->angular_velocity + (raX * iz - raZ * ix) * invIa * 0.68f, -4.5f, 4.5f);
    b->angular_velocity = clampf(b->angular_velocity - (rbX * iz - rbZ * ix) * invIb * 0.68f, -4.5f, 4.5f);

    const float impactSpeed = -sepVel;
    if (impactSpeed > 5.5f) {
        const float baseDeform = clampf((impactSpeed - 5.0f) * 0.028f, 0.0f, 0.45f);
        auto applyCrumple = [&](RsRigidBody2D* body, float amt) {
            const float dx = contact_x - body->pos_x, dz = contact_z - body->pos_z;
            const float fx = std::cos(body->heading), fz = std::sin(body->heading);
            const float rx = -std::sin(body->heading), rz = std::cos(body->heading);
            const float lx = dx * fx + dz * fz, lz = dx * rx + dz * rz;
            if (lx > body->half_length * 0.25f) body->front_damage = clampf(body->front_damage + amt, 0.0f, 1.0f);
            else if (lx < -body->half_length * 0.25f) body->rear_damage = clampf(body->rear_damage + amt, 0.0f, 1.0f);
            if (lz < -body->half_width * 0.25f) body->left_damage = clampf(body->left_damage + amt, 0.0f, 1.0f);
            else if (lz > body->half_width * 0.25f) body->right_damage = clampf(body->right_damage + amt, 0.0f, 1.0f);
        };
        applyCrumple(a, baseDeform * (mb * invTotal * 1.8f));
        applyCrumple(b, baseDeform * (ma * invTotal * 1.8f));
    }
    return impactSpeed;
}
#endif

} // extern "C"
