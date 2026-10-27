// WenaFPV - physics core. No SDL/OpenGL dependencies so it can be tested headless.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace fpv {

// ---- Analog video signal state (defined here so Sim can embed it by value)
struct VideoSignalState {
    float signalQuality   = 100.f;
    float rawSignalQuality = 100.f;
    float closeWallProximity = 0.f;
    float closeWallDist   = 1000.f;
    float frameFreezeTimer  = 0.f;
    bool  freezeFrame       = false;
    int   lastDropoutFrame  = -1;
    float rollingBarPhase   = 0.f;
    float burstTimer        = 0.f;
    float burstStrength     = 0.f;
    float tearOffset        = 0.f;
    float tearRow           = 0.f;
    float dropoutFlashTimer = 0.f;
    float dropoutFlashValue = 0.f;
    float chromaGlitchPhase = 0.f;

    // --- Nuevas variables FPV Avanzadas ---
    float multipathInterference = 0.f;
    float multipathGhostTimer   = 0.f;
    float multipathGhostOffset  = 0.f;
    float syncLossPhase        = 0.f;
    float syncLossTimer        = 0.f;
    float syncLossSeverity     = 0.f;
    float colorSubcarrierPhase = 0.f;
    float colorBurstLossTimer  = 0.f;
    float colorBurstLossSeverity = 0.f;
    float prevSignalQuality    = 0.f;

    void reset() {
        signalQuality = rawSignalQuality = 100.f;
        closeWallProximity = 0.f;
        closeWallDist      = 1000.f;
        frameFreezeTimer   = 0.f;
        freezeFrame        = false;
        lastDropoutFrame   = -1;
        rollingBarPhase    = 0.f;
        burstTimer = burstStrength     = 0.f;
        tearOffset = tearRow           = 0.f;
        dropoutFlashTimer = dropoutFlashValue = 0.f;
        chromaGlitchPhase  = 0.f;

        // Reset de nuevas variables
        multipathInterference = 0.f;
        multipathGhostTimer   = 0.f;
        multipathGhostOffset  = 0.f;
        syncLossPhase        = 0.f;
        syncLossTimer        = 0.f;
        syncLossSeverity     = 0.f;
        colorSubcarrierPhase = 0.f;
        colorBurstLossTimer  = 0.f;
        colorBurstLossSeverity = 0.f;
        prevSignalQuality    = 0.f;
    }
};


constexpr float PI = 3.14159265358979f;
inline float rad(float d) { return d * PI / 180.f; }
inline float deg(float r) { return r * 180.f / PI; }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// ---------------------------------------------------------------- math
struct V3 {
    float x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator*(float s, V3 a) { return a * s; }
inline V3 operator/(V3 a, float s) { return a * (1.f / s); }
inline V3& operator+=(V3& a, V3 b) { a = a + b; return a; }
inline V3& operator-=(V3& a, V3 b) { a = a - b; return a; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float len(V3 a) { return std::sqrt(dot(a, a)); }

// Coordinate system: +X right, +Y up, -Z forward (OpenGL style).
struct Quat {
    float w = 1, x = 0, y = 0, z = 0;
};
inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quat conj(Quat q) { return {q.w, -q.x, -q.y, -q.z}; }
inline Quat normalize(Quat q) {
    float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n < 1e-12f) return {};
    float i = 1.f / n;
    return {q.w * i, q.x * i, q.y * i, q.z * i};
}
inline Quat fromAxisAngle(V3 axis, float ang) {
    float s = std::sin(ang * 0.5f);
    return {std::cos(ang * 0.5f), axis.x * s, axis.y * s, axis.z * s};
}
inline V3 rotate(Quat q, V3 v) {
    V3 u{q.x, q.y, q.z};
    V3 t = 2.f * cross(u, v);
    return v + q.w * t + cross(u, t);
}

// ---------------------------------------------------------------- rates
// Betaflight "Actual" rates: center sensitivity, max rate, expo. Output deg/s.
struct Rates {
    float center = 120.f;
    float max = 700.f;
    float expo = 0.25f;
};
inline float rateCurve(const Rates& r, float x) {
    float ax = std::fabs(x);
    float x5 = x * x * x * x * x;
    float expof = ax * (x5 * r.expo + x * (1.f - r.expo));
    float stick = std::max(0.f, r.max - r.center);
    return x * r.center + stick * expof;
}

// ---------------------------------------------------------------- world
struct Box {
    V3 mn, mx;
    V3 color;
    int mat = 0;  // 0 flat, 1 ground checker, 2 building facade
};

// Thin cylinder for wires, rebar, pipes - collision only (rendered as thin boxes)
struct ThinCylinder {
    V3 p1, p2;      // endpoints
    float radius;   // radius of cylinder
    V3 color;       // for rendering
    int mat = 0;    // material
};

inline float distPointSegment(const V3& p, const V3& a, const V3& b) {
    V3 ab = b - a;
    float len2 = ab.x*ab.x + ab.y*ab.y + ab.z*ab.z;
    if (len2 < 1e-8f) return std::sqrt((p.x-a.x)*(p.x-a.x) + (p.y-a.y)*(p.y-a.y) + (p.z-a.z)*(p.z-a.z));
    float t = clampf(((p.x-a.x)*ab.x + (p.y-a.y)*ab.y + (p.z-a.z)*ab.z) / len2, 0.f, 1.f);
    V3 closest = {a.x + ab.x*t, a.y + ab.y*t, a.z + ab.z*t};
    float dx = p.x - closest.x, dy = p.y - closest.y, dz = p.z - closest.z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

inline V3 closestPointSegment(const V3& p, const V3& a, const V3& b) {
    V3 ab = b - a;
    float len2 = ab.x*ab.x + ab.y*ab.y + ab.z*ab.z;
    if (len2 < 1e-8f) return a;
    float t = clampf(((p.x-a.x)*ab.x + (p.y-a.y)*ab.y + (p.z-a.z)*ab.z) / len2, 0.f, 1.f);
    return {a.x + ab.x*t, a.y + ab.y*t, a.z + ab.z*t};
}

// Swept sphere vs thin cylinder: check if sphere moving from pos to pos+vel*dt hits cylinder
inline bool sweptSphereCylinder(const V3& pos, const V3& vel, float dt, float radius,
                                const V3& p1, const V3& p2, float cylRadius,
                                V3& outNormal, float& outPenetration) {
    // Simplified: check at start and end positions, and midpoint
    // For proper swept test, we'd solve quadratic, but this is sufficient for small dt
    V3 pStart = pos;
    V3 pEnd = pos + vel * dt;
    V3 pMid = (pStart + pEnd) * 0.5f;
    
    float dStart = distPointSegment(pStart, p1, p2);
    float dEnd = distPointSegment(pEnd, p1, p2);
    float dMid = distPointSegment(pMid, p1, p2);
    
    float minDist = std::min(dStart, std::min(dEnd, dMid));
    float combinedRadius = radius + cylRadius;
    
    if (minDist < combinedRadius) {
        // Find collision point and normal
        V3 closest = closestPointSegment(pMid, p1, p2);
        V3 diff = pMid - closest;
        float dist = std::sqrt(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);
        if (dist > 1e-6f) {
            outNormal = {diff.x/dist, diff.y/dist, diff.z/dist};
        } else {
            // Sphere center on cylinder axis - push radially outward
            V3 axis = p2 - p1;
            float axisLen = std::sqrt(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
            if (axisLen > 1e-6f) {
                axis = {axis.x/axisLen, axis.y/axisLen, axis.z/axisLen};
                // Pick a perpendicular direction
                if (std::abs(axis.x) > 0.1f || std::abs(axis.z) > 0.1f) {
                    outNormal = {-axis.z, 0, axis.x};
                } else {
                    outNormal = {1, 0, 0};
                }
            } else {
                outNormal = {1, 0, 0};
            }
        }
        outPenetration = combinedRadius - minDist;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- drone
struct DroneParams {
    float mass = 0.65f;          // kg, 5" freestyle with 6S 1300mAh
    float thrustRatio = 7.0f;    // max thrust / weight
    float throttleCurve = 1.7f;  // thrust = throttle^curve (hover ~ 32% stick)
    float motorTau = 0.025f;     // s, motor/prop spool lag
    float maxMotorRPM = 30000.f; // max RPM at full thrust (5" ~30k, 3.5" ~40k)
    float rateTau = 0.022f;      // s, roll/pitch closed-loop rate response (PID + motors)
    float yawTau = 0.050f;       // s, yaw is slower (small reaction torque)
    float propPitchSpeed = 70.f; // m/s, axial airspeed where prop thrust reaches zero
    float radius = 0.12f;        // m, collision sphere
    float armLength = 0.105f;    // m, centre of mass to motor centre
    float colliderScale = 1.f;   // symmetric scale for the central body collider and four landing points
    float contactSkin = 0.006f;  // m, small contact offset to prevent resting-contact chatter
    int landingAssist = 2;       // 0 off, 1 low, 2 medium, 3 high
    float groundDamping = 0.75f;
    bool landingSnap = true;
    bool straightenYaw = false;
    bool showHitbox = false;
    bool airmode = true;                 // keep idle RPM when armed at zero throttle
    V3 dragK{0.010f, 0.006f, 0.008f};    // body-axis quadratic drag (x right, y up, z back)
    V3 dragLin{0.08f, 0.04f, 0.08f};     // body-axis linear drag: brakes the quad at low speed
    float bodyLiftFraction = 0.0f;       // fraction of drag magnitude acting as lift perpendicular to vrel (default 0 = no glide)
    float aeroPitchDamping = 0.0f;       // aerodynamic pitch damping (rad/s per rad/s air-relative angular velocity)
    float aeroRollDamping = 0.0f;        // aerodynamic roll damping
    float aeroYawDamping = 0.0f;         // aerodynamic yaw damping
    int throttleZeroMode = 0;            // 0 = airmode idle, 1 = motors off (free tumble)
    float restitution = 0.25f;
    float friction = 0.6f;
    int cells = 6;
    float capacityMah = 1300.f;
    float packResistance = 0.030f;  // ohm
    Rates roll{120.f, 700.f, 0.25f};
    Rates pitch{120.f, 700.f, 0.25f};
    Rates yaw{110.f, 600.f, 0.20f};
};

// Normalised stick input.
// roll: right +, pitch: forward (nose down) +, yaw: right +, throttle 0..1
struct Input {
    float roll = 0, pitch = 0, yaw = 0, throttle = 0;
    bool arm = false;
};

struct DroneState {
    V3 pos{0, 0.1f, 14.f};
    V3 vel;
    Quat q;
    V3 omega;  // body-frame angular velocity, rad/s
    float thrust = 0;  // total N, sum of motor thrusts
    std::array<float, 4> motorThrust{};
    std::array<float, 4> motorRPM{};  // per-motor RPM for audio
    bool armed = false;
    bool armBlocked = false;
    bool batteryEmpty = false;
    float usedMah = 0, volts = 25.2f, amps = 0;
    float flightTime = 0;
    float crashTimer = 0;
    float lastImpact = 0;
    bool onGround = false;
    V3 groundNormal{0, 1, 0};
};

constexpr float GRAVITY = 9.81f;

struct Sim {
    DroneParams P;
    DroneState d;
    std::vector<Box> world;
    std::vector<ThinCylinder> thinCylinders;
    V3 startPos{0, 0.1f, 14.f};
    bool windEnabled = false;
    int windStrength = 0;       // 0..100, approximately 0..18 m/s
    int windDirectionDeg = 0;   // 0 blows toward -Z, 90 toward +X
    int windGustStrength = 0;   // 0..100
    int windTurbulence = 0;     // 0..100
    float windTime = 0.f;

    // Analog video signal state
    VideoSignalState videoSignal;

    void reset() {
        d = DroneState{};
        d.pos = startPos;
        d.volts = P.cells * 4.2f;
        windTime = 0.f;
        videoSignal.reset();
    }

    float maxThrust() const { return P.thrustRatio * P.mass * GRAVITY; }

    V3 windVelocity() const {
        if (!windEnabled) return {};
        const float strength = clampf((float)windStrength, 0.f, 100.f) * 0.01f;
        const float gust = clampf((float)windGustStrength, 0.f, 100.f) * 0.01f;
        const float turbulence = clampf((float)windTurbulence, 0.f, 100.f) * 0.01f;
        const float t = windTime;
        const float direction = rad((float)windDirectionDeg);
        const V3 along{std::sin(direction), 0.f, -std::cos(direction)};
        const V3 across{std::cos(direction), 0.f, std::sin(direction)};
        const float gustWave = (std::sin(t * 0.37f) + 0.55f * std::sin(t * 0.23f + 1.4f) +
                                0.25f * std::sin(t * 0.61f + 2.3f)) / 1.8f;
        const float meanSpeed = strength * 18.f;
        const float speed = meanSpeed * (1.f + 0.45f * gust * gustWave);
        const float turbulenceSpeed = turbulence * 1.5f;
        const float crossFlow = turbulenceSpeed *
            (0.55f * std::sin(t * 1.6f + 0.7f) + 0.30f * std::sin(t * 2.4f + 2.1f));
        const float alongFlow = turbulenceSpeed * 0.25f * std::sin(t * 1.1f + 1.9f);
        float groundFactor = d.onGround ? 0.18f :
            clampf((d.pos.y - P.radius * 0.5f) / 0.75f, 0.18f, 1.f);
        return (along * (speed + alongFlow) + across * crossFlow +
                V3{0.f, turbulenceSpeed * 0.18f * std::sin(t * 1.8f + 0.3f), 0.f}) * groundFactor;
    }

    void step(float dt, const Input& in) {
        // ---- arming
        if (!in.arm) {
            d.armed = false;
            d.armBlocked = false;
        } else if (!d.armed) {
            if (in.throttle < 0.05f && !d.batteryEmpty) {
                d.armed = true;
                d.armBlocked = false;
            } else {
                d.armBlocked = true;
            }
        }
        bool power = d.armed && !d.batteryEmpty;
        const float cutoffVoltage = P.cells * 3.0f;
        if (d.volts <= cutoffVoltage) {
            d.batteryEmpty = true;
            d.armed = false;
            d.motorThrust.fill(0.f);
            d.motorRPM.fill(0.f);
            d.thrust = 0.f;
            power = false;
        }
        if (power) d.flightTime += dt;

        // ---- motor commands and rigid-body rate dynamics
        const float thr = clampf(in.throttle, 0.f, 1.f);
        const float nominalVoltage = P.cells * 4.2f;
        const float voltageFactor = clampf(d.volts / nominalVoltage, 0.f, 1.f);
        const float maxTotalThrust = maxThrust() * voltageFactor;
        const float maxMotorThrust = maxTotalThrust * 0.25f;
        
        // throttleZeroMode: 0 = airmode idle (default), 1 = motors off / disarmed (free tumble)
        bool motorsOff = (P.throttleZeroMode == 1 && thr <= 0.f);
        const float idleTotal = (power && P.airmode && !motorsOff) ? maxTotalThrust * 0.06f : 0.f;
        const float throttleTotal = power
            ? idleTotal + (maxTotalThrust - idleTotal) * std::pow(thr, P.throttleCurve)
            : 0.f;

        V3 rateTarget{};
        if (power && !motorsOff) {
            rateTarget = {-rad(rateCurve(P.pitch, in.pitch)),
                          -rad(rateCurve(P.yaw, in.yaw)),
                          -rad(rateCurve(P.roll, in.roll))};
        }

        // Improved moment of inertia based on quadcopter mass distribution
        // Assume ~35% of mass is at motor ends (motors, props, ESCs), rest is central body
        float motorMassFraction = 0.35f;
        float motorMass = P.mass * motorMassFraction * 0.25f;  // per motor
        float bodyMass = P.mass * (1.f - motorMassFraction);
        float arm = std::max(0.01f, P.armLength) * 0.70710678f;
        float arm2 = arm * arm;
        
        // Roll and pitch: motors at arm_length contribute (2 motors on axis + 2 at 45° = 4 × 0.5)
        float iMotor = 2.f * motorMass * arm2;  // per axis
        // Body: approximate as box with dimensions ~armLength × armLength × radius
        float iBody = bodyMass * (arm2 + P.radius * P.radius) / 6.f;
        
        const float ix = std::max(1e-6f, iMotor + iBody);  // roll
        const float iy = std::max(1e-6f, iMotor + iBody);  // pitch (symmetric)
        // Yaw: all 4 motors contribute fully at arm_length
        float iMotorZ = 4.f * motorMass * arm2;
        const float iz = std::max(1e-6f, iMotorZ + iBody * 2.f);  // yaw
        const float rpTau = std::max(0.006f, P.rateTau);
        const float yawTau = std::max(0.008f, P.yawTau);
        // Rate controller requests body torque; individual props must create it.
        V3 requestedTorque{0, 0, 0};
        if (!motorsOff) {
            requestedTorque = {ix * (rateTarget.x - d.omega.x) / rpTau,
                               iy * (rateTarget.y - d.omega.y) / yawTau,
                               iz * (rateTarget.z - d.omega.z) / rpTau};
        }
        const float yawLever = std::max(0.001f, arm * 0.12f);
        const float base = throttleTotal * 0.25f;
        // X is pitch, Z is roll; alternating prop reaction torque drives yaw.
        const float motorTarget[4] = {
            base + requestedTorque.x / (4.f * arm) + requestedTorque.z / (4.f * arm) + requestedTorque.y / (4.f * yawLever),
            base - requestedTorque.x / (4.f * arm) + requestedTorque.z / (4.f * arm) - requestedTorque.y / (4.f * yawLever),
            base - requestedTorque.x / (4.f * arm) - requestedTorque.z / (4.f * arm) + requestedTorque.y / (4.f * yawLever),
            base + requestedTorque.x / (4.f * arm) - requestedTorque.z / (4.f * arm) - requestedTorque.y / (4.f * yawLever)};

        const float motorUpTau = std::max(0.002f, P.motorTau);
        // Motor KV drops with voltage: scale max RPM by voltage factor (min 0.5)
        float motorVoltageFactor = clampf(voltageFactor, 0.5f, 1.f);
        float effectiveMaxRPM = P.maxMotorRPM * motorVoltageFactor;
        for (int i = 0; i < 4; ++i) {
            const float target = (power && !motorsOff) ? clampf(motorTarget[i], 0.f, maxMotorThrust) : 0.f;
            const float tau = target > d.motorThrust[i] ? motorUpTau : motorUpTau * 1.4f;
            d.motorThrust[i] += (target - d.motorThrust[i]) * (1.f - std::exp(-dt / tau));
            // Convert thrust to RPM (thrust ∝ RPM²), with KV voltage scaling
            d.motorRPM[i] = effectiveMaxRPM * std::sqrt(clampf(d.motorThrust[i] / maxMotorThrust, 0.f, 1.f));
        }
        d.thrust = d.motorThrust[0] + d.motorThrust[1] + d.motorThrust[2] + d.motorThrust[3];

        V3 motorTorque{arm * (d.motorThrust[0] + d.motorThrust[3] - d.motorThrust[1] - d.motorThrust[2]),
                       yawLever * (d.motorThrust[0] - d.motorThrust[1] + d.motorThrust[2] - d.motorThrust[3]),
                       arm * (d.motorThrust[0] + d.motorThrust[1] - d.motorThrust[2] - d.motorThrust[3])};
        V3 angularMomentum{ix * d.omega.x, iy * d.omega.y, iz * d.omega.z};
        V3 gyro = cross(d.omega, angularMomentum);
        
        // Aerodynamic angular damping: scales with airspeed
        V3 airVel = d.vel - windVelocity();
        float airSpeedForDamping = len(airVel);
        float dampingScale = clampf(airSpeedForDamping / 30.f, 0.f, 1.f);  // scales with airspeed, max at 30 m/s
        V3 angularDrag{
            (P.aeroRollDamping > 0 ? P.aeroRollDamping : 0.7f) * d.omega.x * dampingScale * ix,
            (P.aeroPitchDamping > 0 ? P.aeroPitchDamping : 0.7f) * d.omega.y * dampingScale * iy,
            (P.aeroYawDamping > 0 ? P.aeroYawDamping : 0.7f) * d.omega.z * dampingScale * iz
        };
        V3 alpha{(motorTorque.x - gyro.x - angularDrag.x) / ix,
                 (motorTorque.y - gyro.y - angularDrag.y) / iy,
                 (motorTorque.z - gyro.z - angularDrag.z) / iz};
        alpha.x = clampf(alpha.x, -5000.f, 5000.f);
        alpha.y = clampf(alpha.y, -5000.f, 5000.f);
        alpha.z = clampf(alpha.z, -5000.f, 5000.f);
        d.omega += alpha * dt;
        const float angularSpeed = len(d.omega);
        if (angularSpeed > 1e-6f)
            d.q = normalize(d.q * fromAxisAngle(d.omega / angularSpeed, angularSpeed * dt));

        // ---- translation
        V3 up = rotate(d.q, {0, 1, 0});
        V3 airVelocity = d.vel - windVelocity();
        V3 vb = rotate(conj(d.q), airVelocity);  // air-relative velocity in body axes
        
        // Prop thrust unloading: linear model (original, carefully tuned for tests)
        float unload = clampf(1.f - std::max(0.f, vb.y) / P.propPitchSpeed, 0.15f, 1.f);
        
        // Ground effect: thrust boost when close to ground (within ~1 prop diameter)
        // Based on momentum theory: induced velocity reduced near ground
        float groundEffect = 1.f;
        float heightAGL = d.pos.y - P.radius * 0.5f;  // approximate height above ground
        if (heightAGL > 0.f && heightAGL < P.armLength * 2.f) {
            float ratio = heightAGL / (P.armLength * 2.f);
            // Smooth interpolation: 1.0 at 2*armLength, up to 1.15 at ground
            groundEffect = 1.f + 0.15f * (1.f - ratio) * (1.f - ratio);
        }
        unload *= groundEffect;
        // Drag force always parallel to -vrel with ellipsoid-projected area:
        // vrel = vel - wind; v_hat = vrel / |vrel|; l = v_hat in body frame (direction cosines)
        // A(v_hat) = sqrt((Ax*lx)^2 + (Ay*ly)^2 + (Az*lz)^2);  // smooth area
        // F_drag = -0.5 * rho * Cd * A(v_hat) * |vrel| * vrel
        // Since dragK = 0.5 * rho * Cd * A_axis, effective k_quad = sqrt((dragK.x*lx)^2 + (dragK.y*ly)^2 + (dragK.z*lz)^2)
        float airSpeed = len(airVelocity);
        float k_quad = 0.f, k_lin = 0.f;
        V3 n_body{};  // direction of -vrel in body frame
        if (airSpeed > 1e-6f) {
            n_body = {-vb.x / airSpeed, -vb.y / airSpeed, -vb.z / airSpeed};
            // Ellipsoid projection: sqrt(sum((dragK_i * l_i)^2))
            k_quad = std::sqrt((P.dragK.x * n_body.x) * (P.dragK.x * n_body.x) +
                              (P.dragK.y * n_body.y) * (P.dragK.y * n_body.y) +
                              (P.dragK.z * n_body.z) * (P.dragK.z * n_body.z));
            k_lin = std::sqrt((P.dragLin.x * n_body.x) * (P.dragLin.x * n_body.x) +
                             (P.dragLin.y * n_body.y) * (P.dragLin.y * n_body.y) +
                             (P.dragLin.z * n_body.z) * (P.dragLin.z * n_body.z));
        }
        V3 dragW = -(k_quad * airSpeed + k_lin) * airVelocity;
        
        // Optional lift component perpendicular to vrel (bodyLiftFraction = 0 by default)
        if (P.bodyLiftFraction > 0.f && airSpeed > 1e-6f) {
            // Lift direction: perpendicular to vrel in the plane formed by vrel and body -Z (thrust axis)
            V3 bodyForward = rotate(d.q, {0, 0, -1});  // body -Z in world frame
            V3 vrelHat = airVelocity / airSpeed;
            // Component of bodyForward perpendicular to vrel
            V3 liftDir = bodyForward - vrelHat * dot(bodyForward, vrelHat);
            float liftDirLen = len(liftDir);
            if (liftDirLen > 1e-6f) {
                liftDir = liftDir / liftDirLen;
                float dragMag = len(dragW);
                dragW += liftDir * (dragMag * P.bodyLiftFraction);
            }
        }
        
        V3 acc = up * (d.thrust * unload / P.mass) + dragW / P.mass + V3{0, -GRAVITY, 0};
        d.vel += acc * dt;
        d.pos += d.vel * dt;
        if (windEnabled) windTime += dt;

        collide(dt, in);

        // ---- battery
        float tf = clampf(d.thrust / maxThrust(), 0.f, 1.2f);
        const float maxCurrentA = 100.f * clampf(P.cells / 6.f, 0.15f, 1.f);
        d.amps = power ? 2.f + maxCurrentA * std::pow(tf, 1.2f) : 0.2f;
        d.usedMah += d.amps * dt / 3.6f;
        float frac = d.usedMah / P.capacityMah;
        float ocv = 4.2f - 0.9f * frac;
        d.volts = P.cells * ocv - d.amps * P.packResistance;
        if (frac >= 1.f || d.volts <= cutoffVoltage) {
            d.batteryEmpty = true;
            d.armed = false;
            d.motorThrust.fill(0.f);
            d.motorRPM.fill(0.f);
            d.thrust = 0.f; // hard low-voltage cutoff, no motor lag below minimum voltage
        }

        if (d.crashTimer > 0) d.crashTimer = std::max(0.f, d.crashTimer - dt);
    }

private:
    void collide(float dt, const Input& in) {
        d.onGround = false;
        d.groundNormal = {0, 1, 0};
        d.lastImpact = 0.f;
        V3 supportNormal{};
        int supportCount = 0;
        const float scale = clampf(P.colliderScale, 0.7f, 1.3f);
        const float skin = clampf(P.contactSkin, 0.001f, 0.03f);
        const float bodyRadius = std::max(0.018f, P.radius * 0.42f) * scale + skin;

        auto recordContact = [&](V3 n, float impact, bool support) {
            if (support && n.y > 0.45f) {
                supportNormal += n;
                ++supportCount;
                d.onGround = true;
            }
            d.lastImpact = std::max(d.lastImpact, impact);
        };
        auto collideSphere = [&](V3 center, V3 localOffset, float radius, bool support) {
            for (const Box& b : world) {
                // The COM collider is for frame/object hits; the four underside
                // contacts, not a big central sphere, carry the drone on ground.
                if (!support && b.mat == 1) continue;
                V3 c{clampf(center.x, b.mn.x, b.mx.x), clampf(center.y, b.mn.y, b.mx.y),
                     clampf(center.z, b.mn.z, b.mx.z)};
                V3 diff = center - c;
                float dist = len(diff);
                V3 n{};
                float pen = 0.f;
                if (dist > 1e-6f) {
                    n = diff / dist;
                    if (dist >= radius) {
                        // Retain a stable grounded state at the contact skin boundary.
                        if (support && dist <= radius + 0.002f && n.y > 0.45f)
                            recordContact(n, 0.f, true);
                        continue;
                    }
                    pen = radius - dist;
                } else {  // sphere centre inside box: exit by the closest face
                    float dx0 = center.x - b.mn.x, dx1 = b.mx.x - center.x;
                    float dy0 = center.y - b.mn.y, dy1 = b.mx.y - center.y;
                    float dz0 = center.z - b.mn.z, dz1 = b.mx.z - center.z;
                    float m = std::min({dx0, dx1, dy0, dy1, dz0, dz1});
                    if (m == dx0) n = {-1, 0, 0};
                    else if (m == dx1) n = {1, 0, 0};
                    else if (m == dy0) n = {0, -1, 0};
                    else if (m == dy1) n = {0, 1, 0};
                    else if (m == dz0) n = {0, 0, -1};
                    else n = {0, 0, 1};
                    pen = m + radius;
                }
                d.pos += n * pen;
                V3 pointVelocity = d.vel + rotate(d.q, cross(d.omega, localOffset));
                float vn = dot(pointVelocity, n);
                float impact = std::max(0.f, -vn);
                if (vn < 0.f) {
                    float j = (1.f + P.restitution) * impact;
                    if (impact < 0.3f) j = impact;
                    d.vel += n * j;
                    float jn = j + (n.y > 0.45f ? GRAVITY * dt : 0.f);
                    V3 vt = d.vel - n * dot(d.vel, n);
                    float vtl = len(vt);
                    if (vtl > 1e-6f) d.vel -= vt * (std::min(vtl, P.friction * jn) / vtl);
                    // Tiny normal settling taps must not invent a tumbling kick.
                    if (impact > (support && n.y > 0.45f ? 3.0f : 2.0f)) {
                        float kick = std::min(impact * 0.8f, 12.f);
                        V3 axis = cross(rotate(conj(d.q), n), {0, 1, 0});
                        float al = len(axis);
                        if (al > 1e-4f) d.omega += axis / al * kick;
                    }
                    if (impact > 7.0f) d.crashTimer = 1.5f;
                }
                recordContact(n, impact, support);
            }
            
            // Thin cylinder collision (wires, rebar, pipes) - swept test to avoid tunneling
            for (const ThinCylinder& tc : thinCylinders) {
                // Thin cylinders are never support contacts
                V3 normal;
                float penetration;
                if (sweptSphereCylinder(center, d.vel, dt, radius, tc.p1, tc.p2, tc.radius, normal, penetration)) {
                    d.pos += normal * penetration;
                    V3 pointVelocity = d.vel + rotate(d.q, cross(d.omega, localOffset));
                    float vn = dot(pointVelocity, normal);
                    float impact = std::max(0.f, -vn);
                    if (vn < 0.f) {
                        float j = (1.f + P.restitution) * impact;
                        if (impact < 0.3f) j = impact;
                        d.vel += normal * j;
                        float jn = j;
                        V3 vt = d.vel - normal * dot(d.vel, normal);
                        float vtl = len(vt);
                        if (vtl > 1e-6f) d.vel -= vt * (std::min(vtl, P.friction * jn) / vtl);
                        if (impact > 2.0f) {
                            float kick = std::min(impact * 0.8f, 12.f);
                            V3 axis = cross(rotate(conj(d.q), normal), {0, 1, 0});
                            float al = len(axis);
                            if (al > 1e-4f) d.omega += axis / al * kick;
                        }
                        if (impact > 7.0f) d.crashTimer = 1.5f;
                    }
                    recordContact(normal, impact, false);
                }
            }
        };

        // Compact, centered body collider for walls and props.
        collideSphere(d.pos, {}, bodyRadius, false);

        // Four matching landing/contact points, at the motor-arm corners underneath
        // the centre of mass. Their symmetric footprint gives stable flat-ground support.
        const float arm = std::max(0.025f, P.armLength) * 0.70710678f * scale;
        const float footY = -std::max(0.012f, P.radius * 0.45f) * scale;
        const float footRadius = std::max(0.010f, P.radius * 0.14f) * scale + skin;
        for (float sx : {-1.f, 1.f}) for (float sz : {-1.f, 1.f}) {
            V3 local{sx * arm, footY, sz * arm};
            collideSphere(d.pos + rotate(d.q, local), local, footRadius, true);
        }
        if (supportCount > 0 && len(supportNormal) > 1e-5f)
            d.groundNormal = supportNormal / len(supportNormal);

        // Landing correction is deliberately contact-gated: no in-air attitude
        // stabilization and no assistance during a crash or fast impact.
        float normalY = clampf(d.groundNormal.y, -1.f, 1.f);
        const bool safeSlope = normalY >= std::cos(rad(35.f));
        const bool gentleLanding = d.onGround && safeSlope && in.throttle < 0.10f &&
                                   len(d.vel) < 1.5f && d.lastImpact < 3.0f && d.crashTimer <= 0.f;
        if (d.onGround && d.crashTimer <= 0.f) {
            float damping = clampf(P.groundDamping, 0.f, 1.f);
            d.omega = d.omega * std::exp(-dt * (3.f + 10.f * damping));
            if (in.throttle < 0.10f) {
                V3 normalVelocity = d.groundNormal * dot(d.vel, d.groundNormal);
                V3 tangentVelocity = d.vel - normalVelocity;
                d.vel -= tangentVelocity * (1.f - std::exp(-dt * (2.f + 10.f * damping)));
            }
        }
        if (gentleLanding) {
            V3 upNow = rotate(d.q, {0, 1, 0});
            V3 axis = cross(upNow, d.groundNormal);
            float axisLen = len(axis);
            float tilt = std::acos(clampf(dot(upNow, d.groundNormal), -1.f, 1.f));
            float assistRate[] = {0.f, 1.2f, 2.5f, 4.5f};
            int mode = std::max(0, std::min(3, P.landingAssist));
            float rate = assistRate[mode];
            if (P.landingSnap && len(d.vel) < 0.18f && tilt < rad(15.f))
                rate = std::max(rate, 18.f);  // settles to the support normal in about 0.15 s
            if (axisLen > 1e-5f && rate > 0.f) {
                float fraction = 1.f - std::exp(-rate * dt);
                d.q = normalize(fromAxisAngle(axis / axisLen, tilt * fraction) * d.q);
            }
            if (P.straightenYaw && normalY > 0.95f && len(d.vel) < 0.18f) {
                V3 forward = rotate(d.q, {0, 0, -1});
                float yaw = std::atan2(-forward.x, -forward.z);
                float targetYaw = std::round(yaw / (PI * 0.5f)) * (PI * 0.5f);
                float yawError = std::atan2(std::sin(targetYaw - yaw), std::cos(targetYaw - yaw));
                d.q = normalize(fromAxisAngle(d.groundNormal, yawError * (1.f - std::exp(-8.f * dt))) * d.q);
            }
        }
    }
};

}  // namespace fpv
