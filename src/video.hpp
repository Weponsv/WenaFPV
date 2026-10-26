// ============================================================ Analog Video Signal System
// Simulates authentic analog FPV camera interference based on signal conditions.
// Designed for low-end integrated graphics (Intel HD 520 class).
#pragma once
#include "sim.hpp"
#include "world.hpp"
#include "config.hpp"
#include <cmath>

namespace fpv {

// VideoSignalState is defined in sim.hpp (embedded in Sim by value).
// This file provides the update functions only.

// ------------------------------------------------------------------ helpers
static inline float vsrand() {
    // Fast deterministic per-frame randomish float [0,1)
    return (float)(rand() & 0x7FFF) / 32767.f;
}

// ------------------------------------------------------------------ signal quality computation
static float computeRawSignalQuality(const V3& dronePos, const V3& pilotPos,
                                      const std::vector<Box>& world,
                                      const DroneParams& params,
                                      const DroneState& state,
                                      const Config& cfg,
                                      VideoSignalState& vs) {
    float quality = 100.f;

    V3    delta      = pilotPos - dronePos;
    float distToPilot = len(delta);
    // 1. Distance-based signal loss — only meaningful past ~150 m on a real 5.8 GHz VTX
    if (cfg.videoDistanceLoss) {
        float distFactor = clampf((distToPilot - 30.f) / 400.f, 0.f, 1.f); // grace zone first 30 m
        quality -= distFactor * distFactor * 30.f;
    }

    // 2. Line-of-sight obstruction (raycast drone → pilot)
    if (cfg.videoOcclusionEnabled) {
        int   blockedCount    = 0;
        float totalThickness  = 0.f;

        for (const Box& b : world) {
            V3 size = b.mx - b.mn;
            if (size.x > 500.f || size.z > 500.f) continue; // skip ground plane

            float enter = 0.f, leave = 1.f;
            const float p[3]  = {dronePos.x, dronePos.y, dronePos.z};
            const float v[3]  = {delta.x, delta.y, delta.z};
            const float lo[3] = {b.mn.x, b.mn.y, b.mn.z};
            const float hi[3] = {b.mx.x, b.mx.y, b.mx.z};
            bool hit = true;

            for (int axis = 0; axis < 3; ++axis) {
                if (std::fabs(v[axis]) < 1e-5f) {
                    if (p[axis] < lo[axis] || p[axis] > hi[axis]) { hit = false; break; }
                    continue;
                }
                float a = (lo[axis] - p[axis]) / v[axis];
                float c = (hi[axis] - p[axis]) / v[axis];
                if (a > c) { float t = a; a = c; c = t; }
                enter = enter > a ? enter : a;
                leave = leave < c ? leave : c;
                if (enter > leave) { hit = false; break; }
            }

            if (hit && leave > 0.02f && enter < 0.98f) {
                blockedCount++;
                float thickness = (leave - enter) * distToPilot;
                totalThickness += thickness;
            }
        }

        float occlusionLoss = (float)std::min(blockedCount, 5) * 13.f * cfg.videoOcclusionSensitivity;
        occlusionLoss += std::min(totalThickness / 4.f, 28.f)  * cfg.videoOcclusionSensitivity;
        quality -= occlusionLoss;
    }

        // 3. Close wall proximity interference (multipath)
    if (cfg.videoCloseWallEnabled) {
        float minWallDist = 1000.f;

        for (const Box& b : world) {
            V3 size = b.mx - b.mn;
            if (size.x > 500.f || size.z > 500.f) continue;

            float dx = std::max(std::max(b.mn.x - dronePos.x, 0.f), dronePos.x - b.mx.x);
            float dy = std::max(std::max(b.mn.y - dronePos.y, 0.f), dronePos.y - b.mx.y);
            float dz = std::max(std::max(b.mn.z - dronePos.z, 0.f), dronePos.z - b.mx.z);
            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (dist < minWallDist) minWallDist = dist;
        }

        vs.closeWallDist = minWallDist;
        // Interference within 4 m; strong burst < 1 m
        if (minWallDist < 4.f) {
            float proximity = 1.f - clampf(minWallDist / 4.f, 0.f, 1.f);
            quality -= proximity * 28.f * cfg.videoCloseWallSensitivity;
            vs.closeWallProximity = proximity;

            // --- CÓDIGO NUEVO: Guardar interferencia por rebote (Multipath) ---
            vs.multipathInterference = proximity * 0.6f * (1.f - (quality / 100.f));

            // Trigger burst of static when very close
            if (minWallDist < 1.5f && vs.burstTimer <= 0.f && vsrand() < 0.04f) {
                vs.burstTimer    = 0.04f + vsrand() * 0.10f;
                vs.burstStrength = 0.5f + proximity * 0.5f;
            }
        } else {
            vs.closeWallProximity = 0.f;
            vs.multipathInterference = 0.f; // Limpiar si se aleja de los muros
        }
    }


    // 4. Low battery interference
    if (cfg.videoLowBatteryEnabled) {
        float vcell = state.volts / (float)params.cells;
        if (vcell < 3.6f) {
            float batteryLoss = (3.6f - vcell) / 0.6f * 22.f; // up to 22% at 3.0V/cell
            quality -= batteryLoss;
        }
    }

    // 5. High-speed / aggressive maneuver
    if (cfg.videoSpeedInterference) {
        float speed        = len(state.vel);
        float angularSpeed = len(state.omega);
        if (speed > 25.f)
            quality -= clampf((speed - 25.f) / 25.f, 0.f, 1.f) * 10.f;
        if (angularSpeed > 10.f)
            quality -= clampf((angularSpeed - 10.f) / 20.f, 0.f, 1.f) * 8.f;
    }

    return clampf(quality, 0.f, 100.f);
}

// ------------------------------------------------------------------ per-frame state update
static void updateVideoSignal(VideoSignalState& vs, float rawQuality, float dt,
                               int frame, const Config& cfg) {
    // --- CÓDIGO NUEVO: Histéresis avanzada con el estado previo ---
    float target = rawQuality;
    float alpha  = (target < vs.signalQuality) ? clampf(dt * 10.f, 0.f, 1.f)
                                                : clampf(dt * 1.8f, 0.f, 1.f);

    // Aplicamos una histéresis más marcada usando el prevSignalQuality solicitado
    vs.signalQuality    = (vs.prevSignalQuality * 0.7f) + (vs.signalQuality + (target - vs.signalQuality) * alpha) * 0.3f;
    vs.rawSignalQuality = target;
    vs.prevSignalQuality = vs.signalQuality;

    float sq   = vs.signalQuality;
    float loss = 1.f - sq * 0.01f;  // 0..1

    // --- CÓDIGO NUEVO: Timers de Sincronismo, Color Burst y Multipath Ghosting ---
    // Pérdida de Sincronismo (Sync Loss) si la señal cae demasiado (< 60%)
    if (sq < 60.f) {
        vs.syncLossTimer += dt * loss;
        vs.syncLossSeverity = loss * 1.5f;
        vs.syncLossPhase = std::fmod(vs.syncLossPhase + dt * 25.f, 6.28318f);
    } else {
        vs.syncLossTimer = std::max(0.f, vs.syncLossTimer - dt * 3.f);
        vs.syncLossSeverity = 0.f;
    }

    // Pérdida de subportadora de Color (Color Burst Loss) si la señal cae (< 75%)
    if (sq < 75.f) {
        vs.colorBurstLossTimer += dt * loss;
        vs.colorBurstLossSeverity = loss;
        vs.colorSubcarrierPhase = std::fmod(vs.colorSubcarrierPhase + dt * 15.f, 6.28318f);
    } else {
        vs.colorBurstLossTimer = std::max(0.f, vs.colorBurstLossTimer - dt * 5.f);
        vs.colorBurstLossSeverity = 0.f;
    }

    // Temporizador y desfase del fantasma multipath
    if (vs.multipathInterference > 0.02f) {
        vs.multipathGhostTimer += dt;
        vs.multipathGhostOffset = std::sin(vs.multipathGhostTimer * 8.f) * vs.multipathInterference * 0.04f;
    } else {
        vs.multipathGhostOffset = 0.f;
    }
    // -------------------------------------------------------------------------------

    // Rolling sync bar (speed increases with signal loss)
    float barSpeed = 0.f;
    if (cfg.videoRollingBar)
        barSpeed = 0.4f + loss * 12.f; // slow when clean, fast when broken
    vs.rollingBarPhase = std::fmod(vs.rollingBarPhase + dt * barSpeed, 1.f);

    // Chroma glitch phase
    vs.chromaGlitchPhase = std::fmod(vs.chromaGlitchPhase + dt * (2.f + loss * 14.f), 6.28318f);

    // Burst timer decay
    if (vs.burstTimer > 0.f) {
        vs.burstTimer -= dt;
        if (vs.burstTimer < 0.f) vs.burstTimer = 0.f;
    }

    // Horizontal tear: update occasionally when signal < 80%
    if (sq < 80.f && vsrand() < dt * (4.f + loss * 20.f)) {
        vs.tearOffset = (vsrand() - 0.5f) * 2.f * loss;
        vs.tearRow    = vsrand();
    }

    // Dropout flash timer
    if (vs.dropoutFlashTimer > 0.f) {
        vs.dropoutFlashTimer -= dt;
        if (vs.dropoutFlashTimer < 0.f) vs.dropoutFlashTimer = 0.f;
    }

    // Frame freeze / dropout logic
    if (vs.frameFreezeTimer > 0.f) {
        vs.frameFreezeTimer -= dt;
        vs.freezeFrame = true;
    } else {
        vs.freezeFrame = false;
    }

    // Random freeze triggers (scaled by dropout frequency setting)
    float dropoutChance = 0.f;
    if      (sq < 20.f) dropoutChance = 0.010f + loss * 0.025f;
    else if (sq < 50.f) dropoutChance = 0.003f * loss;
    dropoutChance *= clampf(cfg.videoDropoutFreq, 0.f, 3.f);

    if (dropoutChance > 0.f && vsrand() < dropoutChance * dt * 60.f) {
        vs.frameFreezeTimer  = 0.04f + vsrand() * 0.12f;
        vs.lastDropoutFrame  = frame;

        // Occasionally trigger a black or white flash
        if (sq < 40.f && vsrand() < 0.35f) {
            vs.dropoutFlashTimer = 0.025f + vsrand() * 0.05f;
            vs.dropoutFlashValue = vsrand() > 0.5f ? 1.f : 0.f;
        }
    }
}


} // namespace fpv
