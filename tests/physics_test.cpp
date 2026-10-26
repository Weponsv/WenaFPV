// Headless physics sanity tests. Build: make test
#include "../src/config.hpp"
#include "../src/world.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

using namespace fpv;

static int failures = 0;
#define CHECK(cond, ...)                         \
    do {                                         \
        if (!(cond)) {                           \
            ++failures;                          \
            std::printf("FAIL: ");               \
            std::printf(__VA_ARGS__);            \
            std::printf("\n");                   \
        } else {                                 \
            std::printf("ok:   ");               \
            std::printf(__VA_ARGS__);            \
            std::printf("\n");                   \
        }                                        \
    } while (0)

static Sim makeSim() {
    Sim s;
    s.world.push_back({{-1500, -5, -1500}, {1500, 0, 1500}, {0.4f, 0.5f, 0.3f}, 1});
    s.startPos = {0, 0.12f, 0};
    s.reset();
    return s;
}

static void run(Sim& s, float seconds, Input in) {
    const float dt = 0.002f;
    for (int i = 0; i < int(seconds / dt); ++i) s.step(dt, in);
}

int main() {
    CHECK(trafficCarCount(0) == 1 && trafficCarCount(1) == 2 && trafficCarCount(2) == 3,
          "Low/Medium/High presets map to one/two/three traffic cars");
    Config shadowDefaults;
    CHECK(shadowDefaults.shadowsEnabled && shadowDefaults.shadowResolution == 1024 &&
              shadowDefaults.shadowDistance == 120 && shadowDefaults.shadowMaxAltitude >= 50,
          "real-time shadows default enabled with a tight 1024px/120m volume");

    // The compact 3.5-inch training course must stay inside its 100 x 100 m plot.
    {
        const std::vector<Box> course = build35TechPark();
        bool inBounds = !course.empty();
        for (const Box& box : course) {
            inBounds = inBounds && box.mn.x >= -50.f && box.mx.x <= 50.f &&
                       box.mn.z >= -50.f && box.mx.z <= 50.f;
        }
        CHECK(course.size() > 200 && inBounds,
              "3.5 TECH PARK has dense geometry contained in 100 x 100 m (%zu boxes)", course.size());
    }

    // The user's config used this typo; it must not trigger the legacy-physics reset.
    {
        char path[] = "/tmp/wenafpv-config-test-XXXXXX";
        int fd = mkstemp(path);
        CHECK(fd >= 0, "create temporary config test file");
        if (fd >= 0) {
            close(fd);
            {
                std::ofstream f(path);
                f << "onfig_version = 3\nquality = 4\nmass_kg = 0.25\nthrust_to_weight = 5.7\ndrag_y = 0.032\n"
                     "video_enabled = 0\nvideo_noise = 0\nvideo_scanlines = 1\nvideo_chromatic_aberration = 0\n"
                     "video_dynamic_breakup = 1\nvideo_snow_strength = 2.3\nlanding_assist = 3\n"
                     "ground_damping = 0.9\ncollider_scale = 1.1\ncontact_skin_m = 0.008\n"
                     "show_hitbox = 1\nstraighten_yaw_on_landing = 1\nlanding_snap = 0\n"
                     "wind_enabled = 1\nwind_strength = 35\nwind_direction_deg = 270\n"
                     "wind_gust_strength = 40\nwind_turbulence = 15\nwind_hud = 1\n"
                     "shadows_enabled = 1\nshadow_resolution = 2048\nshadow_distance_m = 140\n"
                     "shadow_fade_distance_m = 30\nshadow_max_altitude_m = 120\nshadow_strength = 80\n"
                     "shadow_bias = 0.0015\nshadow_normal_offset_m = 0.04\nshadow_filter = 2\n"
                     "shadow_stable_snapping = 0\nshadow_debug = 1\n";
            }
            Config c;
            bool loaded = loadConfig(c, path);
            CHECK(loaded && c.version == 5 && c.quality == 2 && std::fabs(c.drone.mass - 0.25f) < 0.001f &&
                      std::fabs(c.drone.thrustRatio - 5.7f) < 0.001f &&
                      std::fabs(c.drone.dragK.y - 0.032f) < 0.001f &&
                      !c.videoEnabled && !c.videoNoise && c.videoScanlines && !c.videoChromatic &&
                      c.videoBreakup && std::fabs(c.videoSnowStrength - 2.3f) < 0.001f &&
                      c.drone.landingAssist == 3 && std::fabs(c.drone.groundDamping - 0.9f) < 0.001f &&
                      std::fabs(c.drone.colliderScale - 1.1f) < 0.001f && c.drone.showHitbox &&
                      c.drone.straightenYaw && !c.drone.landingSnap && c.windEnabled &&
                      c.windStrength == 35 && c.windDirectionDeg == 270 &&
                      c.windGustStrength == 40 && c.windTurbulence == 15 && c.windHud &&
                      c.shadowsEnabled && c.shadowResolution == 2048 && c.shadowDistance == 140 &&
                      c.shadowFadeDistance == 30 && c.shadowMaxAltitude == 120 &&
                      c.shadowStrength == 80 && std::fabs(c.shadowBias - 0.0015f) < 0.00001f &&
                      std::fabs(c.shadowNormalOffset - 0.04f) < 0.0001f &&
                      c.shadowFilter == 2 && !c.shadowStableSnapping && c.shadowDebug,
                  "load video, landing/collider, wind, and realtime-shadow settings from config.ini");
            c.videoEnabled = true;
            c.videoSnowStrength = 0.7f;
            c.drone.landingAssist = 1;
            c.drone.colliderScale = 0.95f;
            c.windStrength = 62;
            c.windDirectionDeg = 123;
            c.windHud = false;
            c.shadowResolution = 512;
            c.shadowDistance = 90;
            c.shadowDebug = false;
            bool saved = saveConfig(c, path);
            Config roundTrip;
            bool reloaded = loadConfig(roundTrip, path);
            std::remove(path);
            CHECK(saved && reloaded && roundTrip.videoEnabled &&
                      std::fabs(roundTrip.videoSnowStrength - 0.7f) < 0.001f &&
                      roundTrip.drone.landingAssist == 1 &&
                      std::fabs(roundTrip.drone.colliderScale - 0.95f) < 0.001f &&
                      roundTrip.windStrength == 62 && roundTrip.windDirectionDeg == 123 &&
                      !roundTrip.windHud && roundTrip.shadowResolution == 512 &&
                      roundTrip.shadowDistance == 90 && !roundTrip.shadowDebug,
                  "persist video, landing, wind, and realtime-shadow settings through config.ini");
        }
    }

    // Wind is opt-in and represented as smooth ambient-air velocity, not camera or input lag.
    {
        Sim baseline = makeSim(), disabled = makeSim();
        baseline.d.pos = disabled.d.pos = {0.f, 20.f, 0.f};
        baseline.d.vel = disabled.d.vel = {8.f, 0.f, -3.f};
        disabled.windStrength = 100;
        disabled.windGustStrength = disabled.windTurbulence = 100;
        disabled.windEnabled = false;
        const V3 a = baseline.windVelocity(), b = disabled.windVelocity();
        CHECK(len(a) == 0.f && len(b) == 0.f, "wind defaults off and disabled wind velocity is exactly zero");
        Input idle;
        baseline.step(0.002f, idle);
        disabled.step(0.002f, idle);
        CHECK(len(baseline.d.vel - disabled.d.vel) < 1e-6f,
              "wind-off physics is identical to the baseline");

        Sim calm = makeSim();
        calm.windEnabled = true;
        calm.windStrength = 15;
        calm.windDirectionDeg = 90;
        calm.windGustStrength = 60;
        calm.windTurbulence = 20;
        V3 first = calm.windVelocity();
        calm.windTime = 0.01f;
        V3 nearby = calm.windVelocity();
        CHECK(first.x > 0.f && len(nearby - first) < 0.2f,
              "wind points by configured bearing and gust/turbulence vary smoothly");

        Sim micro = makeSim(), five = makeSim();
        micro.P.mass = 0.065f;
        micro.P.dragK = {0.050f, 0.009f, 0.022f};
        micro.P.dragLin = {0.035f, 0.010f, 0.035f};
        five.P.mass = 0.650f;
        five.P.dragK = {0.010f, 0.006f, 0.008f};
        five.P.dragLin = {0.080f, 0.040f, 0.080f};
        for (Sim* s : {&micro, &five}) {
            s->d.pos = {0.f, 20.f, 0.f};
            s->windEnabled = true;
            s->windStrength = 30;
            s->windDirectionDeg = 0;
        }
        micro.step(0.002f, idle);
        five.step(0.002f, idle);
        CHECK(std::fabs(micro.d.vel.z) > std::fabs(five.d.vel.z) * 1.5f,
              "85mm profile reacts more strongly to wind than 5-inch profile (%.4f vs %.4f m/s)",
              std::fabs(micro.d.vel.z), std::fabs(five.d.vel.z));
        micro.reset();
        CHECK(micro.windTime == 0.f, "reset restarts deterministic wind variation phase");
    }

    // 1. Rate curve endpoints
    Rates r{120, 700, 0.25f};
    CHECK(std::fabs(rateCurve(r, 1.f) - 700.f) < 0.01f, "full stick = max rate (%.1f)", rateCurve(r, 1.f));
    CHECK(std::fabs(rateCurve(r, -1.f) + 700.f) < 0.01f, "full stick negative symmetric");
    CHECK(rateCurve(r, 0.1f) < 20.f && rateCurve(r, 0.1f) > 8.f, "centre sensitivity (%.1f deg/s @10%%)", rateCurve(r, 0.1f));

    // 2. Disarmed drone sits on the ground
    {
        Sim s = makeSim();
        run(s, 2.f, {});
        CHECK(s.d.pos.y > 0.05f && s.d.pos.y < 0.2f, "disarmed rests on ground (y=%.3f)", s.d.pos.y);
        CHECK(len(s.d.vel) < 0.05f, "disarmed at rest (|v|=%.3f)", len(s.d.vel));
    }

    // 2b. Grounded landing assist settles a tilted quad, but never levels it in flight.
    {
        Sim s = makeSim();
        s.P.landingAssist = 3;
        s.P.landingSnap = true;
        s.d.pos = {0.f, 0.8f, 0.f};
        s.d.q = fromAxisAngle({0, 0, 1}, rad(12.f));
        run(s, 2.5f, {});
        V3 landedUp = rotate(s.d.q, {0, 1, 0});
        CHECK(s.d.onGround && landedUp.y > 0.985f && len(s.d.omega) < 0.08f,
              "gentle flat landing settles upright and motionless (up.y=%.3f omega=%.3f)",
              landedUp.y, len(s.d.omega));
    }
    {
        Sim s = makeSim();
        s.P.landingAssist = 3;
        s.P.landingSnap = true;
        s.d.pos = {0.f, 10.f, 0.f};
        s.d.q = fromAxisAngle({0, 0, 1}, rad(20.f));
        float initialUpY = rotate(s.d.q, {0, 1, 0}).y;
        run(s, 0.5f, {});
        float airborneUpY = rotate(s.d.q, {0, 1, 0}).y;
        CHECK(!s.d.onGround && std::fabs(airborneUpY - initialUpY) < 0.002f,
              "landing assist does not rotate the drone in mid-air (%.3f -> %.3f)",
              initialUpY, airborneUpY);
    }
    {
        Sim s = makeSim();
        s.P.landingAssist = 3;
        s.P.landingSnap = true;
        s.d.pos = {0.f, 1.f, 0.f};
        s.d.vel = {0.f, -12.f, 0.f};
        s.d.q = fromAxisAngle({0, 0, 1}, rad(20.f));
        run(s, 0.12f, {});
        V3 impactUp = rotate(s.d.q, {0, 1, 0});
        CHECK(s.d.crashTimer > 0.f && impactUp.y < 0.99f,
              "hard impact remains a crash and is not auto-straightened (timer=%.2f up.y=%.3f)",
              s.d.crashTimer, impactUp.y);
    }

    // 3. Arm is blocked with high throttle, allowed with low
    {
        Sim s = makeSim();
        Input in; in.arm = true; in.throttle = 0.5f;
        run(s, 0.1f, in);
        CHECK(!s.d.armed && s.d.armBlocked, "arming blocked at 50%% throttle");
        in.throttle = 0.f;
        run(s, 0.1f, in);
        CHECK(s.d.armed, "arms at zero throttle");
    }

    // 4. Hover throttle is plausible (~20-30%)
    {
        Sim s = makeSim();
        Input in; in.arm = true;
        run(s, 0.1f, in);
        // find hover: binary search throttle where |vy| stays small at altitude
        float lo = 0.1f, hi = 0.5f;
        for (int k = 0; k < 20; ++k) {
            float mid = 0.5f * (lo + hi);
            Sim t = makeSim();
            t.d.pos = {0, 50, 0};
            Input a; a.arm = true;
            run(t, 0.05f, a);  // arm
            a.throttle = mid;
            run(t, 1.5f, a);
            float vy = t.d.vel.y;
            if (vy > 0) hi = mid; else lo = mid;
        }
        float hover = 0.5f * (lo + hi);
        CHECK(hover > 0.18f && hover < 0.35f, "hover stick = %.1f%%", hover * 100.f);
    }

    // 5. Full-throttle climb and punch-out acceleration
    {
        Sim s = makeSim();
        s.d.pos = {0, 1, 0};
        Input in; in.arm = true;
        run(s, 0.05f, in);
        in.throttle = 1.f;
        run(s, 0.5f, in);
        CHECK(s.d.vel.y > 15.f && s.d.vel.y < 40.f, "0.5s full throttle vy = %.1f m/s", s.d.vel.y);
    }

    // 6. Roll rate with full right stick ~ max rate, and direction (right wing goes down)
    {
        Sim s = makeSim();
        s.d.pos = {0, 100, 0};
        Input in; in.arm = true;
        run(s, 0.05f, in);
        in.throttle = 0.25f;
        in.roll = 1.f;
        run(s, 0.12f, in);  // ~80 deg of roll, before wrapping past vertical
        V3 right = rotate(s.d.q, {1, 0, 0});
        CHECK(right.y < -0.3f, "roll right drops right side (right.y=%.2f)", right.y);
        run(s, 0.1f, in);
        float rate = deg(-s.d.omega.z);
        CHECK(rate > 600.f && rate < 720.f, "steady roll rate %.0f deg/s", rate);
    }

    // 7. Pitch forward -> nose down; accelerates forward (-Z)
    {
        Sim s = makeSim();
        s.d.pos = {0, 100, 0};
        Input in; in.arm = true;
        run(s, 0.05f, in);
        in.throttle = 0.25f;
        in.pitch = 0.15f;
        run(s, 0.3f, in);
        in.pitch = 0;
        V3 fwd = rotate(s.d.q, {0, 0, -1});
        CHECK(fwd.y < -0.05f, "pitch forward noses down (fwd.y=%.2f)", fwd.y);
        run(s, 1.0f, in);
        CHECK(s.d.vel.z < -0.75f, "moves toward -Z under voltage-scaled thrust (vz=%.1f)", s.d.vel.z);
    }

    // 8. Yaw right -> nose swings toward +X
    {
        Sim s = makeSim();
        s.d.pos = {0, 100, 0};
        Input in; in.arm = true;
        run(s, 0.05f, in);
        in.throttle = 0.25f;
        in.yaw = 0.5f;
        run(s, 0.3f, in);
        V3 fwd = rotate(s.d.q, {0, 0, -1});
        CHECK(fwd.x > 0.2f, "yaw right swings nose to +X (fwd.x=%.2f)", fwd.x);
    }

    // 9. Top speed in a forward-flying tilt
    {
        Sim s = makeSim();
        s.d.pos = {0, 400, 0};
        Input in; in.arm = true;
        run(s, 0.05f, in);
        in.throttle = 1.f;
        in.pitch = 0.f;
        // tilt forward 70 deg using a pitch burst
        in.pitch = 1.f;
        run(s, rad(70.f) / rad(700.f), in);
        in.pitch = 0;
        run(s, 6.f, in);
        float kmh = len(s.d.vel) * 3.6f;
        CHECK(kmh > 90.f && kmh < 200.f, "top speed ~%.0f km/h", kmh);
    }

    // BabyApe II-like 3.5 inch profile: catches regressions in the user's reported setup.
    {
        Sim s = makeSim();
        s.P.mass = 0.25f;
        s.P.thrustRatio = 5.7f;
        s.P.throttleCurve = 1.7f;
        s.P.motorTau = 0.01f;
        s.P.rateTau = 0.018f;
        s.P.yawTau = 0.05f;
        s.P.propPitchSpeed = 70.f;
        s.P.dragK = {0.045f, 0.010f, 0.008f};
        s.P.dragLin = {0.08f, 0.04f, 0.08f};
        s.P.cells = 6;
        s.P.capacityMah = 650.f;
        s.reset();
        s.d.pos = {0, 400, 0};
        Input in; in.arm = true;
        run(s, 0.05f, in);
        in.throttle = 1.f;
        in.pitch = 1.f;
        run(s, rad(70.f) / rad(700.f), in);
        in.pitch = 0.f;
        run(s, 6.f, in);
        float kmh = len(s.d.vel) * 3.6f;
        CHECK(kmh > 90.f && kmh < 105.f, "3.5 inch profile top speed ~%.0f km/h", kmh);
    }

    // Low-voltage cutoff scales per cell and immediately removes motor thrust.
    {
        for (int cells : {1, 2, 4, 6}) {
            Sim s = makeSim();
            s.P.cells = cells;
            s.d.volts = cells * 3.0f - 0.01f;
            s.d.armed = true;
            s.d.thrust = 5.f;
            Input in; in.arm = true; in.throttle = 1.f;
            s.step(0.002f, in);
            CHECK(s.d.batteryEmpty && !s.d.armed && s.d.thrust == 0.f,
                  "%dS pack hard-cuts at %.2fV (%.2fV threshold)", cells,
                  cells * 3.0f - 0.01f, cells * 3.0f);
        }
    }

    // Small-pack current draw scales with cell count; 1S/2S micros must retain usable voltage.
    {
        for (int cells : {1, 2}) {
            Sim s = makeSim();
            s.P.cells = cells;
            s.P.capacityMah = 450.f;
            s.P.packResistance = cells == 1 ? 0.030f : 0.035f;
            s.reset();
            s.d.pos = {0, 100, 0};
            Input in; in.arm = true;
            run(s, 0.05f, in);
            in.throttle = 1.f;
            run(s, 0.2f, in);
            CHECK(!s.d.batteryEmpty && s.d.thrust > 0.f && s.d.volts > cells * 3.0f,
                  "%dS micro has usable loaded voltage (%.2fV)", cells, s.d.volts);
        }
    }

    // 75/85 mm profiles should accelerate into a believable free-fall speed, not float down.
    {
        const float masses[] = {0.035f, 0.065f};
        const float dragY[] = {0.006f, 0.009f};
        const float dragLinY[] = {0.008f, 0.010f};
        for (int i = 0; i < 2; ++i) {
            Sim s = makeSim();
            s.P.mass = masses[i];
            s.P.dragK.y = dragY[i];
            s.P.dragLin.y = dragLinY[i];
            s.reset();
            s.d.pos = {0, 100, 0};
            run(s, 2.f, {});
            CHECK(s.d.vel.y < -6.f, "%d mm free-fall reaches %.1f m/s", i == 0 ? 75 : 85, -s.d.vel.y);
        }
    }

    // 10. No tunnelling into the ground at high speed
    {
        Sim s = makeSim();
        s.d.pos = {0, 30, 0};
        Input in; in.arm = true;
        run(s, 0.05f, in);
        in.throttle = 0.f;
        in.arm = false;
        run(s, 6.f, in);
        CHECK(s.d.pos.y > -0.01f, "falls and stops on ground (y=%.3f)", s.d.pos.y);
        CHECK(len(s.d.vel) < 0.5f, "settles after impact (|v|=%.2f)", len(s.d.vel));
    }

    // 11. Battery: voltage sags and drains
    {
        Sim s = makeSim();
        s.d.pos = {0, 100, 0};
        Input in; in.arm = true;
        run(s, 0.05f, in);
        float v0 = s.d.volts;
        in.throttle = 1.f;
        run(s, 0.5f, in);
        float vload = s.d.volts;
        CHECK(vload < v0 - 1.f, "voltage sag under load (%.1fV -> %.1fV)", v0, vload);
        in.throttle = 0.25f;
        Sim t = makeSim();
        t.d.pos = {0, 1000, 0};
        Input a; a.arm = true;
        run(t, 0.05f, a);
        a.throttle = 0.32f;
        float tEmpty = 0;
        for (int i = 0; i < 500 * 600 && !t.d.batteryEmpty; ++i, tEmpty += 0.002f) t.step(0.002f, a);
        CHECK(t.d.batteryEmpty && tEmpty > 120.f && tEmpty < 600.f, "pack empties after %.0f s at hover", tEmpty);
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
