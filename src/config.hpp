// Config: simple "key = value" ini, stored in ~/.config/wenafpv/config.ini
#pragma once
#include "sim.hpp"
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace fpv {

struct Config {
    // window / render
    int width = 1280, height = 720;
    bool fullscreen = false;
    bool vsync = true;
    int msaa = 0;          // 0, 2, 4
    float fogDensity = 0.0025f;
    int quality = 1;       // 0 low, 1 medium, 2 high
    bool muteAudio = false;
    bool videoEnabled = true;
    bool videoNoise = true;
    bool videoScanlines = true;
    bool videoChromatic = true;
    bool videoBreakup = true;
    float videoSnowStrength = 1.0f;   // legacy field, no longer used as master scale
    // Analog video signal settings
    bool videoOcclusionEnabled = true;        // signal loss behind obstacles
    float videoOcclusionSensitivity = 1.0f;  // 0.0 - 2.0
    bool videoCloseWallEnabled = true;        // multipath interference near walls
    float videoCloseWallSensitivity = 1.0f;  // 0.0 - 2.0
    bool videoDistanceLoss = true;            // signal loss over distance
    bool videoLowBatteryEnabled = true;       // interference when battery low
    bool videoSpeedInterference = false;      // interference at high speed/aggression
    bool videoRollingBar = true;              // rolling sync bar effect
    int  videoPerformanceMode = 0;            // 0 off, 1 fast, 2 ultra low
    bool videoShowRSSI = false;               // show video link quality HUD
    // per-effect intensity knobs (0.0..2.0)
    float videoNoiseIntensity    = 0.5f;   // grain: 0=off, 1=normal, 2=heavy
    float videoStaticIntensity   = 0.4f;   // salt/pepper/burst: 0=off
    float videoColorGlitch       = 0.6f;   // chroma split & hue corruption
    float videoScanlineIntensity = 0.3f;   // barely visible scanlines
    float videoTearIntensity     = 0.8f;   // horizontal tearing on signal drop
    float videoDropoutFreq       = 0.8f;   // multiplier on dropout probability
    bool videoDotCrawl          = true;   // NTSC dot-crawl luma/chroma interference
    bool videoSyncLossEffect    = true;   // horizontal displacement waves on sync loss
    bool videoColorBurstEffect  = true;   // rainbow hue bands on color burst loss
    bool videoMultipathGhost    = true;   // ghost image from multipath reflections
    int droneSize = 4;     // -1 custom, 0 75mm, 1 85mm, 2 3in, 3 3.5in, 4 5in
    bool airmode = true;   // motors keep idle RPM when armed at zero throttle (like Betaflight)
    bool windEnabled = false;
    int windStrength = 10;       // 0..100, maps to 0..18 m/s
    int windDirectionDeg = 0;    // 0 blows toward -Z, 90 toward +X
    int windGustStrength = 0;    // 0..100
    int windTurbulence = 0;      // 0..100
    bool windHud = false;
    bool shadowsEnabled = true;
    int shadowResolution = 1024; // 512, 1024, 2048
    int shadowDistance = 120;    // metres from shadow focus
    int shadowFadeDistance = 25; // metres before the distance limit
    int shadowMaxAltitude = 100;
    int shadowStrength = 65;     // 0..100
    float shadowBias = 0.001f; // normalized depth bias
    float shadowNormalOffset = 0.05f; // world-space metres
    int shadowFilter = 1;        // 0 off, 1 2x2, 2 3x3 PCF
    bool shadowStableSnapping = true;
    bool shadowDebug = false;
    bool droneShadow = true;  // planar projected drone shadow

    // camera (5" freestyle typical: 25-35 deg tilt, wide lens)
    float camTilt = 30.f;  // degrees up
    bool propsInView = true;  // propeller overlay in FPV view
    float propsOpacity = 85.f;  // propeller overlay opacity 0-100%
    float propsLowering = 0.f;  // propeller lowering 0-100%
    float fovH = 110.f;    // horizontal degrees

    // radio mapping (SDL joystick axis indices; EdgeTX channel N -> axis N-1 for CH1..CH6)
    std::string joystickName = "";  // substring filter, e.g. "RadioMaster" (empty = first usable)
    int axRoll = 0, axPitch = 1, axThrottle = 2, axYaw = 3, axArm = 4, axReset = -1;
    bool invRoll = false, invPitch = true, invThrottle = false, invYaw = false;
    bool invArm = false, invReset = false;
    float deadband = 0.02f;
    float armThreshold = 0.5f;

    int version = 5;  // bumped when flight-model/config semantics change

    // Abandoned base procedural generation seed
    uint32_t abandonedBaseSeed = 0xDEADBEEF;

    // flight model (see sim.hpp for meaning)
    DroneParams drone;
};

struct Entry {
    enum Type { INT, FLOAT, BOOL, STR } type;
    const char* key;
    void* ptr;
};

inline std::vector<Entry> entries(Config& c) {
    return {
        {Entry::INT, "config_version", &c.version},
        {Entry::INT, "window_width", &c.width},
        {Entry::INT, "window_height", &c.height},
        {Entry::BOOL, "fullscreen", &c.fullscreen},
        {Entry::BOOL, "vsync", &c.vsync},
        {Entry::INT, "msaa", &c.msaa},
        {Entry::INT, "quality", &c.quality},
        {Entry::INT, "abandoned_base_seed", &c.abandonedBaseSeed},
        {Entry::BOOL, "mute_audio", &c.muteAudio},
        {Entry::BOOL, "video_enabled", &c.videoEnabled},
        {Entry::BOOL, "video_noise", &c.videoNoise},
        {Entry::BOOL, "video_scanlines", &c.videoScanlines},
        {Entry::BOOL, "video_chromatic_aberration", &c.videoChromatic},
        {Entry::BOOL, "video_dynamic_breakup", &c.videoBreakup},
        {Entry::FLOAT, "video_snow_strength", &c.videoSnowStrength},
        {Entry::BOOL, "video_occlusion_enabled", &c.videoOcclusionEnabled},
        {Entry::FLOAT, "video_occlusion_sensitivity", &c.videoOcclusionSensitivity},
        {Entry::BOOL, "video_close_wall_enabled", &c.videoCloseWallEnabled},
        {Entry::FLOAT, "video_close_wall_sensitivity", &c.videoCloseWallSensitivity},
        {Entry::BOOL, "video_distance_loss", &c.videoDistanceLoss},
        {Entry::BOOL, "video_low_battery_interference", &c.videoLowBatteryEnabled},
        {Entry::BOOL, "video_speed_interference", &c.videoSpeedInterference},
        {Entry::BOOL,  "video_rolling_bar",        &c.videoRollingBar},
        {Entry::INT,   "video_performance_mode",   &c.videoPerformanceMode},
        {Entry::BOOL,  "video_show_rssi",          &c.videoShowRSSI},
        {Entry::FLOAT, "video_noise_intensity",    &c.videoNoiseIntensity},
        {Entry::FLOAT, "video_static_intensity",   &c.videoStaticIntensity},
        {Entry::FLOAT, "video_color_glitch",       &c.videoColorGlitch},
        {Entry::FLOAT, "video_scanline_intensity", &c.videoScanlineIntensity},
        {Entry::FLOAT, "video_tear_intensity",     &c.videoTearIntensity},
        {Entry::FLOAT, "video_dropout_freq",       &c.videoDropoutFreq},
        {Entry::BOOL, "video_dot_crawl",           &c.videoDotCrawl},
        {Entry::BOOL, "video_sync_loss_effect",    &c.videoSyncLossEffect},
        {Entry::BOOL, "video_color_burst_effect",  &c.videoColorBurstEffect},
        {Entry::BOOL, "video_multipath_ghost",     &c.videoMultipathGhost},
        {Entry::INT, "drone_size", &c.droneSize},
        {Entry::BOOL, "airmode", &c.airmode},
        {Entry::BOOL, "wind_enabled", &c.windEnabled},
        {Entry::INT, "wind_strength", &c.windStrength},
        {Entry::INT, "wind_direction_deg", &c.windDirectionDeg},
        {Entry::INT, "wind_gust_strength", &c.windGustStrength},
        {Entry::INT, "wind_turbulence", &c.windTurbulence},
        {Entry::BOOL, "wind_hud", &c.windHud},
        {Entry::BOOL, "shadows_enabled", &c.shadowsEnabled},
        {Entry::INT, "shadow_resolution", &c.shadowResolution},
        {Entry::INT, "shadow_distance_m", &c.shadowDistance},
        {Entry::INT, "shadow_fade_distance_m", &c.shadowFadeDistance},
        {Entry::INT, "shadow_max_altitude_m", &c.shadowMaxAltitude},
        {Entry::INT, "shadow_strength", &c.shadowStrength},
        {Entry::FLOAT, "shadow_bias", &c.shadowBias},
        {Entry::FLOAT, "shadow_normal_offset_m", &c.shadowNormalOffset},
        {Entry::INT, "shadow_filter", &c.shadowFilter},
        {Entry::BOOL, "shadow_stable_snapping", &c.shadowStableSnapping},
        {Entry::BOOL, "shadow_debug", &c.shadowDebug},
        {Entry::BOOL, "drone_shadow", &c.droneShadow},
        {Entry::FLOAT, "fog_density", &c.fogDensity},
        {Entry::FLOAT, "cam_tilt_deg", &c.camTilt},
        {Entry::FLOAT, "fov_horizontal_deg", &c.fovH},
        {Entry::BOOL, "props_in_view", &c.propsInView},
        {Entry::FLOAT, "props_opacity", &c.propsOpacity},
        {Entry::FLOAT, "props_lowering", &c.propsLowering},
        {Entry::STR, "joystick_name_contains", &c.joystickName},
        {Entry::INT, "axis_roll", &c.axRoll},
        {Entry::INT, "axis_pitch", &c.axPitch},
        {Entry::INT, "axis_throttle", &c.axThrottle},
        {Entry::INT, "axis_yaw", &c.axYaw},
        {Entry::INT, "axis_arm", &c.axArm},
        {Entry::INT, "axis_reset", &c.axReset},
        {Entry::BOOL, "invert_roll", &c.invRoll},
        {Entry::BOOL, "invert_pitch", &c.invPitch},
        {Entry::BOOL, "invert_throttle", &c.invThrottle},
        {Entry::BOOL, "invert_yaw", &c.invYaw},
        {Entry::BOOL, "invert_arm", &c.invArm},
        {Entry::BOOL, "invert_reset", &c.invReset},
        {Entry::FLOAT, "deadband", &c.deadband},
        {Entry::FLOAT, "arm_threshold", &c.armThreshold},
        {Entry::FLOAT, "roll_center_rate", &c.drone.roll.center},
        {Entry::FLOAT, "roll_max_rate", &c.drone.roll.max},
        {Entry::FLOAT, "roll_expo", &c.drone.roll.expo},
        {Entry::FLOAT, "pitch_center_rate", &c.drone.pitch.center},
        {Entry::FLOAT, "pitch_max_rate", &c.drone.pitch.max},
        {Entry::FLOAT, "pitch_expo", &c.drone.pitch.expo},
        {Entry::FLOAT, "yaw_center_rate", &c.drone.yaw.center},
        {Entry::FLOAT, "yaw_max_rate", &c.drone.yaw.max},
        {Entry::FLOAT, "yaw_expo", &c.drone.yaw.expo},
        {Entry::FLOAT, "mass_kg", &c.drone.mass},
        {Entry::FLOAT, "arm_length_m", &c.drone.armLength},
        {Entry::FLOAT, "thrust_to_weight", &c.drone.thrustRatio},
        {Entry::FLOAT, "throttle_curve", &c.drone.throttleCurve},
        {Entry::FLOAT, "motor_lag_s", &c.drone.motorTau},
        {Entry::FLOAT, "rate_lag_s", &c.drone.rateTau},
        {Entry::FLOAT, "yaw_lag_s", &c.drone.yawTau},
        {Entry::FLOAT, "prop_pitch_speed", &c.drone.propPitchSpeed},
        {Entry::FLOAT, "drag_x", &c.drone.dragK.x},
        {Entry::FLOAT, "drag_y", &c.drone.dragK.y},
        {Entry::FLOAT, "drag_z", &c.drone.dragK.z},
        {Entry::FLOAT, "drag_linear_x", &c.drone.dragLin.x},
        {Entry::FLOAT, "drag_linear_y", &c.drone.dragLin.y},
        {Entry::FLOAT, "drag_linear_z", &c.drone.dragLin.z},
        {Entry::INT, "battery_cells", &c.drone.cells},
        {Entry::FLOAT, "battery_mah", &c.drone.capacityMah},
        {Entry::INT, "landing_assist", &c.drone.landingAssist},
        {Entry::FLOAT, "ground_damping", &c.drone.groundDamping},
        {Entry::FLOAT, "collider_scale", &c.drone.colliderScale},
        {Entry::FLOAT, "contact_skin_m", &c.drone.contactSkin},
        {Entry::BOOL, "show_hitbox", &c.drone.showHitbox},
        {Entry::BOOL, "straighten_yaw_on_landing", &c.drone.straightenYaw},
        {Entry::BOOL, "landing_snap", &c.drone.landingSnap},
    };
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline std::string defaultConfigPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::string dir = xdg && *xdg ? std::string(xdg) : (home ? std::string(home) + "/.config" : ".");
    return dir + "/wenafpv/config.ini";
}

inline void makeDirs(const std::string& file) {
    for (size_t i = 1; i < file.size(); ++i)
        if (file[i] == '/') mkdir(file.substr(0, i).c_str(), 0755);
}

inline bool loadConfig(Config& c, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    auto es = entries(c);
    c.version = 1;  // files written before versioning have no config_version key
    bool sawDroneSize = false;
    bool sawQuality = false;
    std::string line;
    while (std::getline(f, line)) {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        // Accept a common typo so it cannot trigger the legacy-physics reset below.
        if (k == "onfig_version") k = "config_version";
        for (auto& e : es) {
            if (k != e.key) continue;
            if (k == "drone_size") sawDroneSize = true;
            if (k == "quality") sawQuality = true;
            switch (e.type) {
                case Entry::INT: *(int*)e.ptr = std::atoi(v.c_str()); break;
                case Entry::FLOAT: *(float*)e.ptr = (float)std::atof(v.c_str()); break;
                case Entry::BOOL: *(bool*)e.ptr = (v == "1" || v == "true" || v == "yes" || v == "on"); break;
                case Entry::STR: *(std::string*)e.ptr = v; break;
            }
        }
    }
    if (!sawDroneSize) c.droneSize = -1;  // preserve hand-tuned legacy configs as Custom
    if (c.version < 3) {  // old flight-model numbers feel wrong with the new model: reset them, keep the rest
        DroneParams fresh;
        c.drone.mass = fresh.mass;
        c.drone.thrustRatio = fresh.thrustRatio;
        c.drone.throttleCurve = fresh.throttleCurve;
        c.drone.motorTau = fresh.motorTau;
        c.drone.rateTau = fresh.rateTau;
        c.drone.yawTau = fresh.yawTau;
        c.drone.propPitchSpeed = fresh.propPitchSpeed;
        c.drone.dragK = fresh.dragK;
        c.drone.dragLin = fresh.dragLin;
        c.version = 3;
    }
    if (c.version < 4) {  // collapse Minimal/Low/Medium/High/Ultra into Low/Medium/High
        if (sawQuality) c.quality = c.quality <= 1 ? 0 : (c.quality == 2 ? 1 : 2);
        c.version = 4;
    }
    if (c.version < 5) c.version = 5;
    return true;
}

inline bool saveConfig(Config& c, const std::string& path) {
    makeDirs(path);
    std::ofstream f(path);
    if (!f) return false;
    f << "# wenafpv configuration. Edit freely; unknown keys are ignored.\n"
         "#\n"
         "# Radio: RadioMaster Pocket in USB Joystick (HID) mode. Axis index = EdgeTX channel - 1\n"
         "# (CH1..CH6 -> 0..5). Default order is AETR: roll=0 pitch=1 throttle=2 yaw=3, arm switch on CH5.\n"
         "# Press F1 in the sim for the live axis monitor; every bar should move RIGHT when you push\n"
         "# the stick right / forward / up. If one moves the wrong way, flip its invert_* flag.\n"
         "# Set axis_reset to a channel (e.g. 5 for CH6) to reset the quad with a switch; -1 = off.\n"
         "# Rates are Betaflight 'Actual' rates: centre sensitivity / max rate (deg/s) / expo (0..1).\n"
         "# drone_size selects an approximate physics profile (-1 = custom, 0..4 = 75mm..5in).\n"
         "# Camera tilt and FOV can be changed live with [ ] and - = and are saved on exit.\n\n";
    for (auto& e : entries(c)) {
        f << e.key << " = ";
        switch (e.type) {
            case Entry::INT: f << *(int*)e.ptr; break;
            case Entry::FLOAT: f << *(float*)e.ptr; break;
            case Entry::BOOL: f << (*(bool*)e.ptr ? 1 : 0); break;
            case Entry::STR: f << *(std::string*)e.ptr; break;
        }
        f << "\n";
    }
    return true;
}

}  // namespace fpv
