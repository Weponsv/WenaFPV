// Freestyle park: gates, towers with a dive gap, a "bando" frame, trees, containers, slalom poles.
// Abandoned-base ("заброшка") training map: ruined concrete industrial/military base for AIM, SPEED, REACTION, NOTICING OBJECTS.
#pragma once
#include "sim.hpp"
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdio>
#include <cstdint>

namespace fpv {

// Level file format structures
struct LevelBox {
    V3 mn, mx;
    V3 col;
    int mat = 0;
};

struct LevelThin {
    V3 p1, p2;
    float radius;
    V3 col;
    int mat = 0;
};

struct LevelSpawn {
    V3 pos;
    float yawDeg = 0.f;
};

struct LevelMarker {
    V3 pos;
    int type = 0; // 0=checkpoint, 1=gate
    int index = -1;
    float radius = 2.0f;
    V3 color = {1.f, 0.5f, 0.1f};
};

struct LevelData {
    std::vector<LevelBox> boxes;
    std::vector<LevelThin> thins;
    LevelSpawn spawn{};
    std::vector<LevelMarker> markers;
    bool valid = false;
};

// Parse a level file from path. Returns LevelData with valid=false on error.
inline LevelData loadLevelFile(const char* path) {
    LevelData data;
    data.spawn.pos = {0, 0.12f, 14.f};
    data.spawn.yawDeg = 0.f;
    
    std::ifstream file(path);
    if (!file.is_open()) {
        std::printf("[Level] Failed to open: %s\n", path);
        return data;
    }
    
    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream iss(line);
        std::string type;
        iss >> type;
        
        try {
            if (type == "box") {
                LevelBox b;
                iss >> b.mn.x >> b.mn.y >> b.mn.z >> b.mx.x >> b.mx.y >> b.mx.z >> b.col.x >> b.col.y >> b.col.z >> b.mat;
                if (iss.fail()) throw std::runtime_error("invalid box format");
                data.boxes.push_back(b);
            } else if (type == "thin") {
                LevelThin t;
                iss >> t.p1.x >> t.p1.y >> t.p1.z >> t.p2.x >> t.p2.y >> t.p2.z >> t.radius >> t.col.x >> t.col.y >> t.col.z >> t.mat;
                if (iss.fail()) throw std::runtime_error("invalid thin format");
                data.thins.push_back(t);
            } else if (type == "spawn") {
                iss >> data.spawn.pos.x >> data.spawn.pos.y >> data.spawn.pos.z >> data.spawn.yawDeg;
                if (iss.fail()) throw std::runtime_error("invalid spawn format");
            } else if (type == "marker") {
                LevelMarker m;
                iss >> m.pos.x >> m.pos.y >> m.pos.z >> m.type >> m.index >> m.radius >> m.color.x >> m.color.y >> m.color.z;
                if (iss.fail()) throw std::runtime_error("invalid marker format");
                data.markers.push_back(m);
            } else {
                std::printf("[Level] Line %d: unknown type '%s'\n", lineNum, type.c_str());
            }
        } catch (const std::exception& e) {
            std::printf("[Level] Line %d: parse error: %s\n", lineNum, e.what());
        }
    }
    
    data.valid = true;
    std::printf("[Level] Loaded: %zu boxes, %zu thins, %zu markers, spawn=(%.1f,%.1f,%.1f)\n",
                data.boxes.size(), data.thins.size(), data.markers.size(),
                data.spawn.pos.x, data.spawn.pos.y, data.spawn.pos.z);
    return data;
}

// Convert LevelData to world boxes (for rendering and collision)
// Thin cylinders are approximated as boxes for rendering; collision handles them separately
inline std::vector<Box> levelDataToBoxes(const LevelData& data) {
    std::vector<Box> w;
    for (const auto& b : data.boxes) {
        w.push_back({b.mn, b.mx, b.col, b.mat});
    }
    // Render thin objects as thin boxes for visibility
    for (const auto& t : data.thins) {
        V3 dir = t.p2 - t.p1;
        float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
        if (len < 1e-6f) continue;
        dir = dir / len;
        // Create a thin box along the segment
        V3 perp1, perp2;
        if (std::abs(dir.x) > 0.1f || std::abs(dir.z) > 0.1f) {
            perp1 = { -dir.z, 0, dir.x };
        } else {
            perp1 = { 1, 0, 0 };
        }
        float pl = std::sqrt(perp1.x*perp1.x + perp1.y*perp1.y + perp1.z*perp1.z);
        perp1 = perp1 / pl * t.radius;
        perp2 = { -dir.y * perp1.z + dir.z * perp1.y,
                  dir.x * perp1.z - dir.z * perp1.x,
                  -dir.x * perp1.y + dir.y * perp1.x };
        perp2 = perp2 / std::sqrt(perp2.x*perp2.x + perp2.y*perp2.y + perp2.z*perp2.z) * t.radius;
        
        V3 c = (t.p1 + t.p2) * 0.5f;
        V3 half = dir * (len * 0.5f) + perp1 + perp2;
        w.push_back({c - half, c + half, t.col, t.mat});
        half = dir * (len * 0.5f) + perp1 - perp2;
        w.push_back({c - half, c + half, t.col, t.mat});
        half = dir * (len * 0.5f) - perp1 + perp2;
        w.push_back({c - half, c + half, t.col, t.mat});
        half = dir * (len * 0.5f) - perp1 - perp2;
        w.push_back({c - half, c + half, t.col, t.mat});
    }
    return w;
}

// Save level data to file
inline void saveLevelFile(const char* path, const LevelData& data) {
    std::ofstream file(path);
    if (!file.is_open()) {
        std::printf("[Level] Failed to save: %s\n", path);
        return;
    }
    file << "# wenafpv level file\n";
    file << "# box x y z  x2 y2 z2  r g b  mat\n";
    file << "# thin x1 y1 z1  x2 y2 z2  radius  r g b  mat\n";
    file << "# spawn x y z  yawDeg\n";
    file << "# marker x y z  type  index  radius  r g b\n";
    file << "# type: 0=checkpoint, 1=gate\n\n";
    
    for (const auto& b : data.boxes) {
        file << "box " << b.mn.x << " " << b.mn.y << " " << b.mn.z << " "
             << b.mx.x << " " << b.mx.y << " " << b.mx.z << " "
             << b.col.x << " " << b.col.y << " " << b.col.z << " " << b.mat << "\n";
    }
    for (const auto& t : data.thins) {
        file << "thin " << t.p1.x << " " << t.p1.y << " " << t.p1.z << " "
             << t.p2.x << " " << t.p2.y << " " << t.p2.z << " "
             << t.radius << " " << t.col.x << " " << t.col.y << " " << t.col.z << " " << t.mat << "\n";
    }
    file << "spawn " << data.spawn.pos.x << " " << data.spawn.pos.y << " " << data.spawn.pos.z << " " << data.spawn.yawDeg << "\n";
    for (const auto& m : data.markers) {
        file << "marker " << m.pos.x << " " << m.pos.y << " " << m.pos.z << " "
             << m.type << " " << m.index << " " << m.radius << " "
             << m.color.x << " " << m.color.y << " " << m.color.z << "\n";
    }
    std::printf("[Level] Saved to: %s\n", path);
}

inline void addBox(std::vector<Box>& w, V3 mn, V3 mx, V3 col, int mat = 0) {
    w.push_back({mn, mx, col, mat});
}

// Helper: jitter a color slightly for natural variation
inline V3 jitterColor(V3 base, float amount, uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    float r1 = float((seed >> 8) & 0xFFFF) / 65535.f * 2.f - 1.f;
    seed = seed * 1664525u + 1013904223u;
    float r2 = float((seed >> 8) & 0xFFFF) / 65535.f * 2.f - 1.f;
    seed = seed * 1664525u + 1013904223u;
    float r3 = float((seed >> 8) & 0xFFFF) / 65535.f * 2.f - 1.f;
    return {base.x + r1 * amount, base.y + r2 * amount, base.z + r3 * amount};
}

// Helper: pick from a color palette
inline V3 pickColor(const V3* palette, int count, uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return palette[(seed >> 8) % count];
}

// Ground texture variations for more natural look
inline V3 groundColor(uint32_t& seed) {
    static const V3 palette[] = {
        {0.38f, 0.52f, 0.30f},  // standard grass
        {0.35f, 0.48f, 0.28f},  // darker grass
        {0.42f, 0.55f, 0.33f},  // lighter grass
        {0.33f, 0.45f, 0.25f},  // shadowed grass
        {0.40f, 0.50f, 0.28f},  // yellowish grass
    };
    return pickColor(palette, 5, seed);
}

inline V3 dirtColor(uint32_t& seed) {
    static const V3 palette[] = {
        {0.35f, 0.30f, 0.25f},  // standard dirt
        {0.40f, 0.33f, 0.28f},  // lighter dirt
        {0.30f, 0.26f, 0.22f},  // darker dirt
        {0.38f, 0.32f, 0.24f},  // reddish dirt
        {0.32f, 0.30f, 0.27f},  // grayish dirt
    };
    return pickColor(palette, 5, seed);
}

inline V3 concreteColor(uint32_t& seed, bool worn = false) {
    if (worn) {
        static const V3 palette[] = {
            {0.55f, 0.55f, 0.53f},
            {0.52f, 0.51f, 0.49f},
            {0.58f, 0.57f, 0.55f},
            {0.50f, 0.49f, 0.47f},
            {0.53f, 0.52f, 0.50f},
        };
        return pickColor(palette, 5, seed);
    }
    static const V3 palette[] = {
        {0.43f, 0.45f, 0.43f},
        {0.45f, 0.46f, 0.44f},
        {0.40f, 0.42f, 0.40f},
        {0.47f, 0.48f, 0.46f},
        {0.42f, 0.44f, 0.42f},
    };
    return pickColor(palette, 5, seed);
}

inline V3 rustColor(uint32_t& seed) {
    static const V3 palette[] = {
        {0.42f, 0.28f, 0.18f},
        {0.48f, 0.32f, 0.20f},
        {0.38f, 0.24f, 0.16f},
        {0.45f, 0.30f, 0.18f},
        {0.40f, 0.26f, 0.17f},
    };
    return pickColor(palette, 5, seed);
}

inline V3 steelColor(uint32_t& seed) {
    static const V3 palette[] = {
        {0.48f, 0.53f, 0.56f},
        {0.50f, 0.55f, 0.58f},
        {0.45f, 0.50f, 0.53f},
        {0.52f, 0.57f, 0.60f},
        {0.47f, 0.52f, 0.55f},
    };
    return pickColor(palette, 5, seed);
}

// Add detail props: small objects that add visual interest
// Offset by 1cm (0.01f) along Y to prevent z-fighting with ground plane
inline void addDetailProp(std::vector<Box>& w, float x, float z, uint32_t& seed) {
    int type = (seed * 1664525u + 1013904223u) >> 8 & 7;
    seed = seed * 1664525u + 1013904223u;
    const float yOffset = 0.01f;  // 1cm offset to prevent z-fighting with ground
    
    switch (type) {
        case 0: { // Small rock
            V3 c = dirtColor(seed);
            float s = 0.2f + (float((seed >> 8) & 0xFF) / 255.f) * 0.3f;
            addBox(w, {x - s, yOffset, z - s}, {x + s, s * 1.5f + yOffset, z + s}, c);
            break;
        }
        case 1: { // Debris pile
            V3 c = concreteColor(seed, true);
            for (int i = 0; i < 3; ++i) {
                seed = seed * 1664525u + 1013904223u;
                float dx = (float((seed >> 8) & 0xFF) / 255.f - 0.5f) * 0.8f;
                seed = seed * 1664525u + 1013904223u;
                float dz = (float((seed >> 8) & 0xFF) / 255.f - 0.5f) * 0.8f;
                float h = 0.15f + (float((seed >> 8) & 0xFF) / 255.f) * 0.4f;
                addBox(w, {x + dx - 0.15f, yOffset, z + dz - 0.15f}, {x + dx + 0.15f, h + yOffset, z + dz + 0.15f}, c);
            }
            break;
        }
        case 2: { // Metal scrap
            V3 c = rustColor(seed);
            float h = 0.3f + (float((seed >> 8) & 0xFF) / 255.f) * 0.5f;
            addBox(w, {x - 0.1f, yOffset, z - 0.1f}, {x + 0.1f, h + yOffset, z + 0.1f}, c);
            break;
        }
        case 3: { // Tire
            V3 c = {0.08f, 0.08f, 0.08f};
            addBox(w, {x - 0.3f, yOffset, z - 0.3f}, {x + 0.3f, 0.5f + yOffset, z + 0.3f}, c);
            break;
        }
        case 4: { // Wooden pallet
            V3 c = {0.35f, 0.28f, 0.18f};
            addBox(w, {x - 0.6f, yOffset, z - 0.5f}, {x + 0.6f, 0.15f + yOffset, z + 0.5f}, c);
            addBox(w, {x - 0.6f, 0.15f + yOffset, z - 0.5f}, {x - 0.4f, 0.3f + yOffset, z + 0.5f}, c);
            addBox(w, {x + 0.4f, 0.15f + yOffset, z - 0.5f}, {x + 0.6f, 0.3f + yOffset, z + 0.5f}, c);
            break;
        }
        case 5: { // Concrete pipe
            V3 c = concreteColor(seed, true);
            addBox(w, {x - 0.5f, yOffset, z - 0.5f}, {x + 0.5f, 1.0f + yOffset, z + 0.5f}, c);
            break;
        }
        case 6: { // Traffic cone
            V3 c = {0.95f, 0.45f, 0.05f};
            addBox(w, {x - 0.2f, yOffset, z - 0.2f}, {x + 0.2f, 0.6f + yOffset, z + 0.2f}, c);
            break;
        }
        case 7: { // Weeds/tall grass clump
            V3 c = {0.15f, 0.35f, 0.10f};
            for (int i = 0; i < 3; ++i) {
                seed = seed * 1664525u + 1013904223u;
                float dx = (float((seed >> 8) & 0xFF) / 255.f - 0.5f) * 0.3f;
                seed = seed * 1664525u + 1013904223u;
                float dz = (float((seed >> 8) & 0xFF) / 255.f - 0.5f) * 0.3f;
                float h = 0.3f + (float((seed >> 8) & 0xFF) / 255.f) * 0.5f;
                addBox(w, {x + dx - 0.05f, yOffset, z + dz - 0.05f}, {x + dx + 0.05f, h + yOffset, z + dz + 0.05f}, c);
            }
            break;
        }
    }
}

// Square gate. Centre (cx, cy, cz), opening `s` wide/tall. facingX: gate plane spans Z instead of X.
inline void addGate(std::vector<Box>& w, float cx, float cy, float cz, float s, bool facingX = false,
                    V3 col = {1.0f, 0.45f, 0.08f}) {
    const float t = 0.14f;  // frame thickness
    const float dp = 0.10f; // frame depth
    const float h = s * 0.5f;
    auto put = [&](float u0, float y0, float u1, float y1) {
        // u is the coordinate along the gate plane (relative to centre)
        if (facingX) addBox(w, {cx - dp, y0, cz + u0}, {cx + dp, y1, cz + u1}, col);
        else addBox(w, {cx + u0, y0, cz - dp}, {cx + u1, y1, cz + dp}, col);
    };
    put(-h - t, cy - h - t, -h, cy + h + t);          // left post
    put(h, cy - h - t, h + t, cy + h + t);            // right post
    put(-h - t, cy + h, h + t, cy + h + t);           // top bar
    put(-h - t, cy - h - t, h + t, cy - h);           // bottom bar
    // stand under the gate so it is not floating
    if (cy - h - t > 0.05f) put(-0.05f, 0.f, 0.05f, cy - h - t);
}

inline void addPole(std::vector<Box>& w, float x, float z, float height, float width = 0.12f,
                    V3 col = {0.9f, 0.9f, 0.9f}) {
    addBox(w, {x - width, 0, z - width}, {x + width, height, z + width}, col);
}

inline void addTree(std::vector<Box>& w, float x, float z, float scale) {
    V3 bark{0.35f, 0.22f, 0.12f};
    addBox(w, {x - 0.22f * scale, 0, z - 0.22f * scale}, {x + 0.22f * scale, 3.5f * scale, z + 0.22f * scale}, bark);
    // Low-poly branching and layered crowns add a little shape without expensive meshes.
    addBox(w, {x - 1.0f * scale, 2.2f * scale, z - 0.12f * scale},
           {x + 1.0f * scale, 2.55f * scale, z + 0.12f * scale}, bark);
    addBox(w, {x - 0.12f * scale, 2.35f * scale, z - 1.0f * scale},
           {x + 0.12f * scale, 2.7f * scale, z + 1.0f * scale}, bark);
    addBox(w, {x - 1.8f * scale, 2.0f * scale, z - 1.8f * scale},
           {x + 1.8f * scale, 4.5f * scale, z + 1.8f * scale}, {0.10f, 0.32f, 0.12f});
    addBox(w, {x - 1.35f * scale, 3.8f * scale, z - 1.35f * scale},
           {x + 1.35f * scale, 5.8f * scale, z + 1.35f * scale}, {0.15f, 0.42f, 0.16f});
    addBox(w, {x - 0.82f * scale, 5.1f * scale, z - 0.82f * scale},
           {x + 0.82f * scale, 6.8f * scale, z + 0.82f * scale}, {0.20f, 0.48f, 0.19f});
}

inline void addWillow(std::vector<Box>& w, float x, float z, float height = 7.f) {
    const float k = height / 7.f;
    addBox(w, {x - 0.22f, 0, z - 0.22f}, {x + 0.22f, height, z + 0.22f}, {0.34f, 0.22f, 0.12f});
    addBox(w, {x - 2.4f * k, 4.8f * k, z - 0.28f}, {x + 2.4f * k, 5.2f * k, z + 0.28f},
           {0.13f, 0.36f, 0.14f});
    addBox(w, {x - 0.28f, 4.7f * k, z - 2.4f * k}, {x + 0.28f, 5.1f * k, z + 2.4f * k},
           {0.13f, 0.36f, 0.14f});
    // Hanging foliage creates a broken, orbitable canopy instead of a solid trunk-like block.
    for (int i = -2; i <= 2; ++i) {
        float p = i * 0.82f * k;
        float length = (2.1f - 0.22f * std::abs(i)) * k;
        addBox(w, {x + p - 0.13f * k, 4.85f * k - length, z - 0.13f * k},
               {x + p + 0.13f * k, 4.95f * k, z + 0.13f * k}, {0.17f, 0.42f, 0.18f});
        addBox(w, {x - 0.13f * k, 4.85f * k - length, z + p - 0.13f * k},
               {x + 0.13f * k, 4.95f * k, z + p + 0.13f * k}, {0.17f, 0.42f, 0.18f});
    }
    addBox(w, {x - 1.5f * k, 5.7f * k, z - 1.5f * k},
           {x + 1.5f * k, height, z + 1.5f * k}, {0.20f, 0.47f, 0.19f});
}

inline void addSwingSet(std::vector<Box>& w, float cx, float cz, float width = 6.f, float height = 6.5f) {
    const V3 steel{0.48f, 0.53f, 0.56f};
    const float x0 = cx - width * 0.5f, x1 = cx + width * 0.5f;
    for (float x : {x0, x1}) {
        addBox(w, {x - 0.10f, 0, cz - 1.5f}, {x + 0.10f, height, cz - 1.3f}, steel);
        addBox(w, {x - 0.10f, 0, cz + 1.3f}, {x + 0.10f, height, cz + 1.5f}, steel);
    }
    addBox(w, {x0 - 0.1f, height - 0.18f, cz - 1.5f}, {x1 + 0.1f, height, cz + 1.5f}, steel);
    for (float x : {cx - width * 0.28f, cx, cx + width * 0.28f}) {
        addBox(w, {x - 0.035f, 2.2f, cz - 1.0f}, {x + 0.035f, height - 0.2f, cz - 0.94f}, {0.20f, 0.22f, 0.24f});
        addBox(w, {x - 0.035f, 2.2f, cz + 0.94f}, {x + 0.035f, height - 0.2f, cz + 1.0f}, {0.20f, 0.22f, 0.24f});
        addBox(w, {x - 0.48f, 2.1f, cz - 0.65f}, {x + 0.48f, 2.35f, cz + 0.65f}, {0.85f, 0.42f, 0.12f});
    }
}

inline void addScaffoldRow(std::vector<Box>& w, float x0, float x1, float z, float height = 6.f) {
    const V3 metal{0.50f, 0.56f, 0.59f};
    const float bay = 2.4f, depth = 1.8f, post = 0.09f;
    int bays = std::max(1, (int)std::ceil((x1 - x0) / bay));
    for (int i = 0; i <= bays; ++i) {
        float x = x0 + (x1 - x0) * i / bays;
        for (float zz : {z - depth * 0.5f, z + depth * 0.5f})
            addBox(w, {x - post, 0, zz - post}, {x + post, height, zz + post}, metal);
    }
    for (int level = 1; level <= 3; ++level) {
        float y = height * level / 4.f;
        for (int i = 0; i < bays; ++i) {
            float xa = x0 + (x1 - x0) * i / bays;
            float xb = x0 + (x1 - x0) * (i + 1) / bays;
            for (float zz : {z - depth * 0.5f, z + depth * 0.5f})
                addBox(w, {xa, y - 0.06f, zz - 0.06f}, {xb, y + 0.06f, zz + 0.06f}, metal);
        }
    }
    // Short transverse ledgers tie the two continuous frames together at each bay.
    for (int i = 0; i <= bays; ++i) {
        float x = x0 + (x1 - x0) * i / bays;
        for (int level = 1; level <= 3; ++level) {
            float y = height * level / 4.f;
            addBox(w, {x - 0.06f, y - 0.06f, z - depth * 0.5f}, {x + 0.06f, y + 0.06f, z + depth * 0.5f}, metal);
        }
    }
}

inline int trafficCarCount(int quality) {
    static const int counts[] = {1, 2, 3};
    return counts[std::max(0, std::min(2, quality))];
}

inline std::vector<Box> buildTrafficCars(float elapsed, int count) {
    std::vector<Box> cars;
    if (count <= 0) return cars;
    const V3 path[] = {{-47, 0, -82}, {47, 0, -82}, {47, 0, -146}, {-47, 0, -146}};
    const float lengths[] = {94.f, 64.f, 94.f, 64.f};
    const float perimeter = 316.f;
    const V3 paints[] = {{0.85f, 0.12f, 0.08f}, {0.10f, 0.34f, 0.85f}, {0.92f, 0.72f, 0.08f}};
    for (int i = 0; i < count; ++i) {
        float distance = std::fmod(elapsed * 12.f + perimeter * i / count, perimeter);
        int segment = 0;
        while (segment < 3 && distance >= lengths[segment]) {
            distance -= lengths[segment++];
        }
        const V3 a = path[segment], b = path[(segment + 1) % 4];
        float t = distance / lengths[segment];
        float x = a.x + (b.x - a.x) * t;
        float z = a.z + (b.z - a.z) * t;
        bool alongX = segment == 0 || segment == 2;
        float hx = alongX ? 2.1f : 1.0f, hz = alongX ? 1.0f : 2.1f;
        V3 paint = paints[i % 3];
        addBox(cars, {x - hx, 0.35f, z - hz}, {x + hx, 1.15f, z + hz}, paint);
        addBox(cars, {x - hx * 0.52f, 1.15f, z - hz * 0.68f},
               {x + hx * 0.52f, 1.85f, z + hz * 0.68f}, {0.16f, 0.20f, 0.24f});
        // Dark wheels are visible on both sides of the compact, box-built cars.
        for (int side : {-1, 1}) {
            float wx = x + (alongX ? side * 1.15f : side * 0.95f);
            float wz = z + (alongX ? side * 0.80f : side * 1.15f);
            addBox(cars, {wx - 0.28f, 0.12f, wz - 0.28f}, {wx + 0.28f, 0.55f, wz + 0.28f},
                   {0.045f, 0.05f, 0.06f});
        }
    }
    return cars;
}

inline std::vector<Box> buildFreestylePark() {
    std::vector<Box> w;
    uint32_t seed = 12345u;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return float((seed >> 8) & 0xFFFF) / 65535.f;
    };
    auto rndRange = [&](float min, float max) { return min + rnd() * (max - min); };

    // Ground with texture variation - split into patches for natural look
    const int groundPatch = 50;
    for (int gx = -30; gx < 30; ++gx) {
        for (int gz = -30; gz < 30; ++gz) {
            V3 gc = groundColor(seed);
            addBox(w, {gx * groundPatch, -5, gz * groundPatch}, {(gx + 1) * groundPatch, 0, (gz + 1) * groundPatch}, gc, 1);
        }
    }

    // Launch pad.
    addBox(w, {-2.5f, 0, 11.5f}, {2.5f, 0.02f, 16.5f}, {0.55f, 0.55f, 0.58f});

    // --- Gate line down the middle (-Z), varying heights.
    const float gz[] = {6, -2, -10, -18, -26};
    const float gy[] = {1.5f, 3.2f, 1.4f, 4.5f, 2.2f};
    const float gx[] = {0, 2.0f, -2.0f, 0, 1.0f};
    for (int i = 0; i < 5; ++i) addGate(w, gx[i], gy[i], gz[i], 2.2f);
    addGate(w, 0, 3.6f, -36, 4.5f, false, {0.95f, 0.15f, 0.25f});  // big freestyle gate

    // Fly-through gantry crane. The open bay is about 24 m wide and 10 m tall.
    const V3 crane{0.92f, 0.48f, 0.08f};
    for (float x : {-14.f, 12.f})
        for (float z : {-72.f, -64.f})
            addBox(w, {x, 0, z}, {x + 2.f, 11.f, z + 2.f}, steelColor(seed));
    addBox(w, {-16.f, 11.f, -73.f}, {16.f, 13.5f, -69.f}, crane);
    addBox(w, {-16.f, 11.f, -63.f}, {16.f, 13.5f, -59.f}, crane);
    addBox(w, {-16.f, 13.5f, -73.f}, {16.f, 14.5f, -59.f}, {0.72f, 0.38f, 0.08f});
    // Trolley, hanging cable, and a simple hook beneath the overhead girder.
    addBox(w, {-2.2f, 14.5f, -69.5f}, {2.2f, 16.f, -66.5f}, steelColor(seed));
    addBox(w, {-0.12f, 8.f, -68.2f}, {0.12f, 14.5f, -67.8f}, {0.08f, 0.08f, 0.08f});
    addBox(w, {-0.65f, 7.5f, -68.55f}, {0.65f, 8.f, -67.45f}, {0.95f, 0.72f, 0.12f});

    // --- Dive towers with a 6 m gap between them (classic gap dive).
    addBox(w, {16, 0, -30}, {32, 30, -10}, concreteColor(seed), 2);
    addBox(w, {38, 0, -30}, {54, 30, -10}, concreteColor(seed), 2);
    // Lower connecting block with a window gap between roof levels.
    addBox(w, {32, 0, -30}, {38, 9, -10}, concreteColor(seed), 2);

    // A tall solo tower on the other side for orbits.
    addBox(w, {-36, 0, -34}, {-24, 22, -22}, concreteColor(seed), 2);

    // --- Bando: 3-floor open concrete frame to fly through / around.
    {
        const float x0 = -30, x1 = -10, z0 = 4, z1 = 24;
        V3 conc = concreteColor(seed);
        for (float px : {x0, x1 - 1.0f})
            for (float pz : {z0, z1 - 1.0f}) addBox(w, {px, 0, pz}, {px + 1.0f, 10.0f, pz + 1.0f}, conc);
        // extra mid pillars
        addBox(w, {(x0 + x1) * 0.5f, 0, (z0 + z1) * 0.5f}, {(x0 + x1) * 0.5f + 1, 10, (z0 + z1) * 0.5f + 1}, conc);
        for (float fy : {3.2f, 6.4f, 9.6f}) {
            // slab with a 5 x 5 hole near the middle-left to dive through
            const float hx0 = x0 + 4, hx1 = x0 + 9, hz0 = z0 + 4, hz1 = z0 + 9;
            addBox(w, {x0, fy, z0}, {x1, fy + 0.4f, hz0}, concreteColor(seed));
            addBox(w, {x0, fy, hz1}, {x1, fy + 0.4f, z1}, concreteColor(seed));
            addBox(w, {x0, fy, hz0}, {hx0, fy + 0.4f, hz1}, concreteColor(seed));
            addBox(w, {hx1, fy, hz0}, {x1, fy + 0.4f, hz1}, concreteColor(seed));
        }
    }

    // --- Stacked shipping containers.
    addBox(w, {-6, 0, -56}, {6, 2.6f, -53.4f}, {0.75f, 0.25f, 0.18f});
    addBox(w, {-6, 0, -52.4f}, {6, 2.6f, -49.8f}, {0.20f, 0.35f, 0.65f});
    addBox(w, {-3, 2.6f, -54.5f}, {9, 5.2f, -51.9f}, {0.85f, 0.65f, 0.15f});

    // --- Slalom poles (zig-zag, 5 m tall).
    for (int i = 0; i < 10; ++i) addPole(w, 9.0f + 3.0f * (i % 2), 18.0f - 4.0f * i, 5.0f, 0.10f, {0.95f, 0.95f, 0.2f});

    // --- Stair-step kicker.
    for (int i = 0; i < 6; ++i)
        addBox(w, {22.f, 0, 14.f - float(i)}, {28.f, 0.8f * float(i + 1), 15.f - float(i)}, concreteColor(seed));

    // Asphalt loop behind the park, with dashed lane markings for the chase line.
    V3 asphalt = jitterColor({0.12f, 0.14f, 0.16f}, 0.02f, seed);
    addBox(w, {-51.f, 0.02f, -87.f}, {51.f, 0.09f, -78.f}, asphalt);
    addBox(w, {-51.f, 0.02f, -151.f}, {51.f, 0.09f, -142.f}, asphalt);
    addBox(w, {-51.f, 0.02f, -142.f}, {-42.f, 0.09f, -87.f}, asphalt);
    addBox(w, {42.f, 0.02f, -142.f}, {51.f, 0.09f, -87.f}, asphalt);
    V3 paint = jitterColor({0.82f, 0.82f, 0.72f}, 0.05f, seed);
    for (float x = -43.f; x < 44.f; x += 12.f) {
        addBox(w, {x, 0.095f, -82.25f}, {x + 5.f, 0.11f, -81.9f}, paint);
        addBox(w, {x, 0.095f, -146.25f}, {x + 5.f, 0.11f, -145.9f}, paint);
    }
    for (float z = -138.f; z < -88.f; z += 12.f) {
        addBox(w, {-47.25f, 0.095f, z}, {-46.9f, 0.11f, z + 5.f}, paint);
        addBox(w, {46.9f, 0.095f, z}, {47.25f, 0.11f, z + 5.f}, paint);
    }

    // --- Tree cluster (deterministic LCG).
    for (int i = 0; i < 28; ++i) {
        float x = 42.f + rnd() * 40.f;
        float z = 8.f + rnd() * 50.f;
        addTree(w, x, z, 0.9f + rnd() * 0.8f);
    }
    for (int i = 0; i < 10; ++i) addTree(w, -60.f - rnd() * 25.f, -20.f + rnd() * 50.f, 1.0f + rnd() * 0.8f);

    // --- Detail props scattered around for natural feel
    for (int i = 0; i < 80; ++i) {
        float x = rndRange(-140.f, 140.f);
        float z = rndRange(-140.f, 140.f);
        // Don't place props on launch pad or inside structures
        if (x > -5 && x < 5 && z > 10 && z < 18) continue;
        addDetailProp(w, x, z, seed);
    }

    return w;
}

inline std::vector<Box> buildGateTraining() {
    std::vector<Box> w;
    uint32_t seed = 54321u;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return float((seed >> 8) & 0xFFFF) / 65535.f;
    };
    auto rndRange = [&](float min, float max) { return min + rnd() * (max - min); };

    // Ground with texture variation
    const int groundPatch = 50;
    for (int gx = -30; gx < 30; ++gx) {
        for (int gz = -30; gz < 30; ++gz) {
            V3 gc = groundColor(seed);
            addBox(w, {gx * groundPatch, -5, gz * groundPatch}, {(gx + 1) * groundPatch, 0, (gz + 1) * groundPatch}, gc, 1);
        }
    }

    addBox(w, {-2.5f, 0, 11.5f}, {2.5f, 0.02f, 16.5f}, {0.55f, 0.55f, 0.58f});
    // Progressive gate line: build consistency before tightening the gaps.
    for (int i = 0; i < 8; ++i) {
        float z = 4.f - 8.f * i;
        float x = (i % 2 ? -1.5f : 1.5f);
        float size = i < 4 ? 3.2f : 2.2f;
        addGate(w, x, 1.8f + (i % 3) * 0.8f, z, size, false, {0.15f, 0.8f, 0.95f});
    }

    // Detail props
    for (int i = 0; i < 40; ++i) {
        float x = rndRange(-140.f, 140.f);
        float z = rndRange(-140.f, 140.f);
        if (x > -5 && x < 5 && z > 10 && z < 18) continue;
        addDetailProp(w, x, z, seed);
    }

    return w;
}

inline std::vector<Box> buildSlalomTraining() {
    std::vector<Box> w;
    uint32_t seed = 98765u;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return float((seed >> 8) & 0xFFFF) / 65535.f;
    };
    auto rndRange = [&](float min, float max) { return min + rnd() * (max - min); };

    // Ground with texture variation
    const int groundPatch = 50;
    for (int gx = -30; gx < 30; ++gx) {
        for (int gz = -30; gz < 30; ++gz) {
            V3 gc = groundColor(seed);
            addBox(w, {gx * groundPatch, -5, gz * groundPatch}, {(gx + 1) * groundPatch, 0, (gz + 1) * groundPatch}, gc, 1);
        }
    }

    addBox(w, {-2.5f, 0, 11.5f}, {2.5f, 0.02f, 16.5f}, {0.55f, 0.55f, 0.58f});
    // A smooth S-bend line of gates, with alternating outside marker poles.
    for (int i = 0; i < 10; ++i) {
        float z = 2.f - 7.f * i;
        float centerX = 5.f * std::sin(i * 0.58f);
        float poleX = centerX + (i % 2 ? 4.f : -4.f);
        addPole(w, poleX, z, 5.f, 0.22f, {0.95f, 0.78f, 0.12f});
        addGate(w, centerX, 2.2f + (i % 2) * 0.8f, z - 3.2f, 3.f, false, {0.95f, 0.3f, 0.12f});
    }

    // Detail props
    for (int i = 0; i < 40; ++i) {
        float x = rndRange(-140.f, 140.f);
        float z = rndRange(-140.f, 140.f);
        if (x > -5 && x < 5 && z > 10 && z < 18) continue;
        addDetailProp(w, x, z, seed);
    }

    return w;
}

// Compact 3.5-inch skill park. The playable floor and every structure fit in a 100 x 100 m plot.
inline std::vector<Box> build35TechPark() {
    std::vector<Box> w;
    const V3 concrete{0.43f, 0.45f, 0.43f};
    const V3 darkConcrete{0.20f, 0.23f, 0.23f};
    const V3 steel{0.48f, 0.54f, 0.57f};
    const V3 rust{0.42f, 0.31f, 0.22f};

    // 100 x 100 m arena. Two sunken channels divide the surface into low-flight lanes.
    addBox(w, {-50.f, -2.f, -50.f}, {50.f, -0.50f, 50.f}, {0.27f, 0.30f, 0.29f}, 1);
    addBox(w, {-50.f, -0.50f, -50.f}, {-18.5f, 0.f, 50.f}, {0.36f, 0.43f, 0.31f}, 1);
    addBox(w, {-15.5f, -0.50f, -50.f}, {15.5f, 0.f, 50.f}, concrete, 1);
    addBox(w, {18.5f, -0.50f, -50.f}, {50.f, 0.f, 50.f}, {0.38f, 0.40f, 0.37f}, 1);
    for (float x : {-17.f, 17.f}) {
        addBox(w, {x - 1.5f, -0.50f, -50.f}, {x + 1.5f, -0.43f, 50.f}, darkConcrete);
        addBox(w, {x - 1.60f, -0.50f, -50.f}, {x - 1.48f, 0.f, 50.f}, concrete);
        addBox(w, {x + 1.48f, -0.50f, -50.f}, {x + 1.60f, 0.f, 50.f}, concrete);
        // Drainage crossbars are sparse enough to leave fly-through space.
        for (float z = -46.f; z <= 46.f; z += 12.f)
            addBox(w, {x - 1.5f, -0.41f, z - 0.08f}, {x + 1.5f, -0.30f, z + 0.08f}, steel);
    }

    // Lightweight perimeter rails mark the compact arena boundary without boxing in the sky.
    for (int i = 0; i <= 20; ++i) {
        float p = -48.f + 4.8f * i;
        addPole(w, -49.f, p, 2.6f, 0.08f, steel);
        addPole(w, 49.f, p, 2.6f, 0.08f, steel);
        addPole(w, p, -49.f, 2.6f, 0.08f, steel);
        addPole(w, p, 49.f, 2.6f, 0.08f, steel);
    }
    addBox(w, {-49.1f, 2.35f, -49.f}, {-48.9f, 2.55f, 49.f}, steel);
    addBox(w, {48.9f, 2.35f, -49.f}, {49.1f, 2.55f, 49.f}, steel);
    addBox(w, {-49.f, 2.35f, -49.1f}, {49.f, 2.55f, -48.9f}, steel);
    addBox(w, {-49.f, 2.35f, 48.9f}, {49.f, 2.55f, 49.1f}, steel);

    // Launch corner and a compact 3 m gate line that bends through the whole course.
    addBox(w, {-45.f, 0.f, 38.f}, {-39.f, 0.08f, 45.f}, {0.63f, 0.64f, 0.62f});
    const V3 gateBlue{0.12f, 0.65f, 0.82f}, gateOrange{0.92f, 0.32f, 0.10f};
    addGate(w, -34.f, 2.1f, 31.f, 3.f, false, gateBlue);
    addGate(w, -26.f, 2.5f, 18.f, 3.f, true, gateOrange);
    addGate(w, -12.f, 2.1f, 7.f, 3.f, false, gateBlue);
    addGate(w, 2.f, 2.6f, -3.f, 3.f, true, gateOrange);
    addGate(w, 15.f, 2.0f, -15.f, 3.f, false, gateBlue);
    addGate(w, 28.f, 2.6f, -28.f, 3.f, true, gateOrange);
    addGate(w, 39.f, 2.0f, -39.f, 3.f, false, gateBlue);

    // Orbit and Matty-flip pocket: medium pines and spreading willows, all 5-8 m tall.
    addWillow(w, -44.f, 22.f, 7.2f);
    addTree(w, -37.f, 29.f, 1.0f);
    addTree(w, -29.f, 24.f, 1.05f);
    addWillow(w, -42.f, 8.f, 6.3f);
    addTree(w, -34.f, 13.f, 0.95f);

    // Playground swing set and compact jungle gym with multiple orbit/flip lines.
    addSwingSet(w, -25.f, 30.f, 6.f, 6.6f);
    const V3 playColor{0.84f, 0.48f, 0.12f};
    for (float x : {-29.f, -23.f})
        for (float z : {7.f, 13.f})
            addBox(w, {x - 0.16f, 0.f, z - 0.16f}, {x + 0.16f, 5.2f, z + 0.16f}, playColor);
    for (float y : {2.6f, 5.0f}) {
        addBox(w, {-29.f, y, 6.8f}, {-23.f, y + 0.16f, 7.2f}, steel);
        addBox(w, {-29.f, y, 12.8f}, {-23.f, y + 0.16f, 13.2f}, steel);
        addBox(w, {-29.2f, y, 7.f}, {-28.8f, y + 0.16f, 13.f}, steel);
        addBox(w, {-23.2f, y, 7.f}, {-22.8f, y + 0.16f, 13.f}, steel);
    }
    for (float x : {-27.5f, -25.f, -24.f})
        addBox(w, {x - 0.07f, 2.7f, 9.8f}, {x + 0.07f, 4.9f, 10.2f}, playColor);

    // Concrete skate bowl: stepped bank/coping rings approximate a compact transition bowl.
    const float bx = 3.f, bz = 31.f;
    addBox(w, {bx - 7.f, 0.f, bz - 6.f}, {bx - 6.2f, 1.2f, bz + 6.f}, concrete);
    addBox(w, {bx + 6.2f, 0.f, bz - 6.f}, {bx + 7.f, 1.2f, bz + 6.f}, concrete);
    addBox(w, {bx - 6.2f, 0.f, bz - 6.f}, {bx + 6.2f, 1.2f, bz - 5.2f}, concrete);
    addBox(w, {bx - 6.2f, 0.f, bz + 5.2f}, {bx + 6.2f, 1.2f, bz + 6.f}, concrete);
    addBox(w, {bx - 5.1f, 0.f, bz - 4.2f}, {bx - 4.5f, 0.65f, bz + 4.2f}, {0.52f, 0.54f, 0.51f});
    addBox(w, {bx + 4.5f, 0.f, bz - 4.2f}, {bx + 5.1f, 0.65f, bz + 4.2f}, {0.52f, 0.54f, 0.51f});
    addBox(w, {bx - 4.5f, 0.f, bz - 4.2f}, {bx + 4.5f, 0.45f, bz - 3.6f}, {0.52f, 0.54f, 0.51f});
    addBox(w, {bx - 4.5f, 0.f, bz + 3.6f}, {bx + 4.5f, 0.45f, bz + 4.2f}, {0.52f, 0.54f, 0.51f});
    for (float x : {-4.f, 3.f, 10.f}) addPole(w, x, 22.f, 1.8f, 0.10f, {0.92f, 0.32f, 0.10f});

    // Pumping-station pipe rack, with low pipes, overhead crossovers, and 3 m metal arches.
    addGate(w, 23.f, 2.0f, 31.f, 3.f, false, steel);
    addGate(w, 34.f, 3.6f, 27.f, 3.f, true, {0.74f, 0.78f, 0.77f});
    for (float x : {22.f, 29.f, 36.f}) {
        addBox(w, {x - 0.18f, 0.f, 37.f}, {x + 0.18f, 5.8f, 37.4f}, steel);
        addBox(w, {x - 0.18f, 0.f, 40.f}, {x + 0.18f, 5.8f, 40.4f}, steel);
        addBox(w, {x - 0.45f, 2.3f, 36.8f}, {x + 0.45f, 2.75f, 40.6f}, {0.68f, 0.47f, 0.12f});
    }
    for (float y : {2.5f, 4.8f})
        addBox(w, {21.8f, y, 36.9f}, {36.2f, y + 0.42f, 37.35f}, {0.70f, 0.50f, 0.14f});
    addBox(w, {21.8f, 5.7f, 36.8f}, {36.2f, 6.1f, 40.6f}, steel);

    // Dense bando tree band and continuous construction scaffolding for low, linked gaps.
    for (float z : {-25.f, -19.f, -13.f, -7.f, -1.f})
        for (float x : {-45.f, -40.f, -35.f, -30.f})
            if (!(x == -35.f && z == -13.f)) addTree(w, x, z, 0.72f + 0.10f * ((int)(x - z) & 1));
    addScaffoldRow(w, -14.f, 13.f, -43.f, 6.f);
    addScaffoldRow(w, -14.f, 13.f, -40.8f, 4.5f);

    // 2-3 story abandoned concrete shell with a roof dive slot and wall gaps.
    const V3 oldConcrete{0.48f, 0.49f, 0.47f};
    addBox(w, {2.f, 0.f, -27.f}, {17.f, 7.8f, -26.f}, oldConcrete, 2);
    addBox(w, {2.f, 0.f, -13.f}, {17.f, 7.8f, -12.f}, oldConcrete, 2);
    addBox(w, {2.f, 0.f, -26.f}, {3.f, 7.8f, -13.f}, oldConcrete, 2);
    addBox(w, {16.f, 0.f, -26.f}, {17.f, 7.8f, -13.f}, oldConcrete, 2);
    // Broken floor plates leave a 4 x 4 m recovery/dive opening.
    for (float y : {3.3f, 6.5f}) {
        addBox(w, {3.f, y, -25.f}, {7.f, y + 0.35f, -14.f}, oldConcrete);
        addBox(w, {11.f, y, -25.f}, {16.f, y + 0.35f, -14.f}, oldConcrete);
        addBox(w, {7.f, y, -25.f}, {11.f, y + 0.35f, -21.f}, oldConcrete);
        addBox(w, {7.f, y, -17.f}, {11.f, y + 0.35f, -14.f}, oldConcrete);
    }
    // Abandoned silo: overlapping square panels form a low-poly cylindrical shell.
    const float siloX = 24.f, siloZ = -17.f;
    for (int i = 0; i < 8; ++i) {
        float a = 6.2831853f * i / 8.f;
        float x = siloX + 2.05f * std::cos(a), z = siloZ + 2.05f * std::sin(a);
        addBox(w, {x - 1.0f, 0.f, z - 1.0f}, {x + 1.0f, 9.f, z + 1.0f},
               (i & 1) ? rust : oldConcrete);
    }
    addBox(w, {siloX - 2.8f, 8.8f, siloZ - 2.8f}, {siloX + 2.8f, 9.2f, siloZ + 2.8f}, steel);
    addPole(w, siloX - 3.2f, siloZ, 8.7f, 0.08f, {0.30f, 0.34f, 0.35f});

    // Compact warehouse shell and roof vents; roof height stays under 10 m.
    addBox(w, {29.f, 0.f, -45.f}, {45.f, 7.2f, -44.f}, oldConcrete, 2);
    addBox(w, {29.f, 0.f, -29.f}, {45.f, 7.2f, -28.f}, oldConcrete, 2);
    addBox(w, {29.f, 0.f, -44.f}, {30.f, 7.2f, -29.f}, oldConcrete, 2);
    addBox(w, {44.f, 0.f, -44.f}, {45.f, 7.2f, -29.f}, oldConcrete, 2);
    addBox(w, {30.f, 7.0f, -44.f}, {44.f, 7.35f, -29.f}, {0.38f, 0.40f, 0.39f});
    for (float x : {33.f, 38.f, 42.f}) {
        addBox(w, {x - 0.7f, 7.35f, -40.f}, {x + 0.7f, 9.0f, -38.6f}, steel);
        addBox(w, {x - 1.0f, 8.9f, -40.3f}, {x + 1.0f, 9.15f, -38.3f}, {0.32f, 0.35f, 0.35f});
    }
    // High and low warehouse-wall arches provide close recovery lines around the dives.
    addGate(w, 27.f, 2.0f, -35.f, 3.f, true, gateOrange);
    addGate(w, 38.f, 4.2f, -27.f, 3.f, false, gateBlue);

    return w;
}

// Abandoned base ("заброшка") training map: ruined concrete industrial/military base ~120x120m
// Zones: SPEED (long corridor), AIM (gaps of decreasing size), REACTION (seeded obstacles), NOTICE (thin/low-contrast hazards), OUTDOOR (yard, container, tower)
inline std::vector<Box> buildAbandonedBase(uint32_t seed = 0xDEADBEEF) {
    std::vector<Box> w;
    
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return float((seed >> 8) & 0xFFFF) / 65535.f;
    };
    auto rndInt = [&](int max) { return int(rnd() * max); };
    auto rndRange = [&](float min, float max) { return min + rnd() * (max - min); };
    
    // Base material colors with natural variation
    auto concreteLight = [&]() { return concreteColor(seed); };
    auto concreteDark = [&]() { return jitterColor({0.35f, 0.36f, 0.35f}, 0.03f, seed); };
    auto concreteWorn = [&]() { return concreteColor(seed, true); };
    auto rustyMetal = [&]() { return rustColor(seed); };
    auto rebarGray = [&]() { return jitterColor({0.30f, 0.30f, 0.32f}, 0.02f, seed); };
    auto wireDark = [&]() { return jitterColor({0.10f, 0.10f, 0.10f}, 0.01f, seed); };
    auto mossGreen = [&]() { return jitterColor({0.20f, 0.28f, 0.15f}, 0.03f, seed); };
    auto steel = [&]() { return steelColor(seed); };
    auto dirtGround = [&]() { return dirtColor(seed); };
    auto grassGround = [&]() { return groundColor(seed); };
    auto hazardOrange = [&]() { return jitterColor({0.95f, 0.45f, 0.05f}, 0.05f, seed); };
    auto hazardYellow = [&]() { return jitterColor({0.95f, 0.85f, 0.15f}, 0.05f, seed); };
    
    // Ground: single consolidated non-overlapping layer at Y=0
    // Base ground covers the full 300x300m area with material variation by region
    const int basePatch = 100;
    for (int gx = -15; gx < 15; ++gx) {
        for (int gz = -15; gz < 15; ++gz) {
            float px = gx * basePatch;
            float pz = gz * basePatch;
            float px2 = (gx + 1) * basePatch;
            float pz2 = (gz + 1) * basePatch;
            
            V3 gc;
            // Main base area (-60 to 60): worn concrete
            if (px >= -60 && px2 <= 60 && pz >= -60 && pz2 <= 60) {
                gc = concreteWorn();
            }
            // Speed zone corridor (-3 to 3, -42 to 12): concrete worn (slightly raised handled separately)
            else if (px >= -3 && px2 <= 3 && pz >= -42 && pz2 <= 12) {
                gc = concreteWorn();
            }
            // Reaction zone courtyard (10 to 50, -40 to -10): concrete worn
            else if (px >= 10 && px2 <= 50 && pz >= -40 && pz2 <= -10) {
                gc = concreteWorn();
            }
            // Notice zone (-50 to -10, 10 to 50): concrete worn
            else if (px >= -50 && px2 <= -10 && pz >= 10 && pz2 <= 50) {
                gc = concreteWorn();
            }
            // Outdoor yard (60 to 120, -60 to 60): grass/dirt mix
            else if (px >= 60 && px2 <= 120 && pz >= -60 && pz2 <= 60) {
                gc = (rnd() < 0.5f) ? grassGround() : dirtGround();
            }
            // Outer yard areas: grass/dirt mix
            else {
                gc = (rnd() < 0.5f) ? grassGround() : dirtGround();
            }
            addBox(w, {px, -5, pz}, {px2, 0, pz2}, gc, 1);
        }
    }
    
    // ========== STRUCTURAL GRID ==========
    // 8m grid, columns at intersections, floors every 3.5m up to ~14m
    const float gridSize = 8.f;
    const float floorHeights[] = {0.f, 3.5f, 7.f, 10.5f, 14.f};
    const int gridRange = 7;  // -56 to +56 covers ~112m
    
    // Columns (some missing/broken)
    for (int gx = -gridRange; gx <= gridRange; ++gx) {
        for (int gz = -gridRange; gz <= gridRange; ++gz) {
            float x = gx * gridSize;
            float z = gz * gridSize;
            // Skip some columns for broken structure
            if (rnd() < 0.15f) continue;
            // Don't place columns in the center speed corridor (z near 0, x near 0)
            if (std::abs(x) < 12.f && std::abs(z) < 24.f && rnd() < 0.5f) continue;
            
            V3 colColor = (rnd() < 0.3f) ? concreteDark() : concreteLight();
            if (rnd() < 0.1f) colColor = rustyMetal();  // exposed rebar columns
            
            // Column base to roof
            addBox(w, {x - 0.4f, 0, z - 0.4f}, {x + 0.4f, 14.5f, z + 0.4f}, colColor);
            
            // Exposed rebar on some broken columns
            if (rnd() < 0.15f) {
                for (int i = 0; i < 4; ++i) {
                    float rx = x + (i % 2 == 0 ? -0.45f : 0.45f);
                    float rz = z + (i < 2 ? -0.45f : 0.45f);
                    addBox(w, {rx - 0.02f, 0, rz - 0.02f}, {rx + 0.02f, 14.5f, rz + 0.02f}, rebarGray());
                }
            }
        }
    }
    
    // Floor slabs with holes/gaps
    for (int fi = 1; fi < 5; ++fi) {
        float y = floorHeights[fi];
        for (int gx = -gridRange; gx <= gridRange; ++gx) {
            for (int gz = -gridRange; gz <= gridRange; ++gz) {
                float x = gx * gridSize;
                float z = gz * gridSize;
                // Skip floor in speed corridor
                if (std::abs(x) < 12.f && std::abs(z) < 24.f) continue;
                // Random holes in slabs
                if (rnd() < 0.12f) continue;
                // Random broken corners
                bool brokenCorner = rnd() < 0.08f;
                addBox(w, {x - 3.8f, y, z - 3.8f}, {x + 3.8f, y + 0.35f, z + 3.8f}, 
                       brokenCorner ? concreteDark() : concreteWorn());
            }
        }
    }
    
    // ========== ZONE 1: SPEED ZONE (long corridor along -Z axis, ~50m) ==========
    // Corridor from z=10 to z=-40, bounded by walls with columns every ~6m
    // Wide enough for 100+ km/h (6m wide, 8m tall)
    // Floor is part of consolidated ground layer

    // Left wall with irregular gaps (columns every 6m)
    for (float cz = 8.f; cz >= -40.f; cz -= 6.f) {
        float gapStart = rndRange(-1.5f, 1.5f);
        float gapSize = rndRange(1.5f, 3.5f);
        // Wall segments above and below gap
        addBox(w, {-6.f, 0, cz - 3.f}, {-3.f, gapStart, cz + 3.f}, concreteWorn());
        addBox(w, {-6.f, gapStart + gapSize, cz - 3.f}, {-3.f, 9.f, cz + 3.f}, concreteWorn());
        // Column at gap edge
        addBox(w, {-6.f, 0, cz - 0.4f}, {-5.2f, 9.f, cz + 0.4f}, concreteDark());
    }
    
    // Right wall with irregular gaps
    for (float cz = 8.f; cz >= -40.f; cz -= 6.f) {
        float gapStart = rndRange(-1.5f, 1.5f);
        float gapSize = rndRange(1.5f, 3.5f);
        addBox(w, {3.f, 0, cz - 3.f}, {6.f, gapStart, cz + 3.f}, concreteWorn());
        addBox(w, {3.f, gapStart + gapSize, cz - 3.f}, {6.f, 9.f, cz + 3.f}, concreteWorn());
        addBox(w, {5.2f, 0, cz - 0.4f}, {6.f, 9.f, cz + 0.4f}, concreteDark());
    }
    
    // Broken ceiling slabs with holes
    for (float cz = 6.f; cz >= -42.f; cz -= 6.f) {
        if (rnd() < 0.2f) continue;  // hole in ceiling
        addBox(w, {-6.f, 8.5f, cz - 3.f}, {6.f, 9.f, cz + 3.f}, concreteDark());
    }
    
    // Low beams to duck under (at ~2.5m height)
    for (float cz = 4.f; cz >= -38.f; cz -= 8.f) {
        addBox(w, {-5.f, 2.3f, cz - 0.15f}, {5.f, 2.6f, cz + 0.15f}, rustyMetal());
    }
    
    // Speed zone marker gates (checkpoints)
    const V3 speedGateCol{0.15f, 0.8f, 0.95f};
    addGate(w, 0, 3.5f, 8.f, 4.f, false, speedGateCol);   // entry
    addGate(w, 0, 2.5f, -10.f, 3.5f, false, speedGateCol);
    addGate(w, 0, 4.f, -25.f, 3.f, false, speedGateCol);
    addGate(w, 0, 3.f, -40.f, 4.f, false, speedGateCol);  // exit
    
    // ========== ZONE 2: AIM ZONE (walls with decreasing gaps) ==========
    // Located at x = -30 to -10, z = -40 to -20
    
    // Wall 1: 2.0m gap
    addBox(w, {-32.f, 0, -35.f}, {-8.f, 8.f, -34.f}, concreteWorn());
    addBox(w, {-32.f, 0, -35.f}, {-16.f - 1.f, 8.f, -34.f}, concreteWorn());  // left of gap
    addBox(w, {-16.f + 1.f, 0, -35.f}, {-8.f, 8.f, -34.f}, concreteWorn());    // right of gap
    
    // Wall 2: 1.5m gap (higher up)
    addBox(w, {-32.f, 2.f, -30.f}, {-8.f, 7.f, -29.f}, concreteWorn());
    addBox(w, {-32.f, 2.f, -30.f}, {-15.f - 0.75f, 7.f, -29.f}, concreteWorn());
    addBox(w, {-15.f + 0.75f, 2.f, -30.f}, {-8.f, 7.f, -29.f}, concreteWorn());
    
    // Wall 3: 1.0m gap
    addBox(w, {-32.f, 1.f, -25.f}, {-8.f, 6.f, -24.f}, concreteWorn());
    addBox(w, {-32.f, 1.f, -25.f}, {-14.f - 0.5f, 6.f, -24.f}, concreteWorn());
    addBox(w, {-14.f + 0.5f, 1.f, -25.f}, {-8.f, 6.f, -24.f}, concreteWorn());
    
    // Wall 4: 0.7m gap (tight)
    addBox(w, {-32.f, 0.5f, -20.f}, {-8.f, 5.f, -19.f}, concreteWorn());
    addBox(w, {-32.f, 0.5f, -20.f}, {-13.5f - 0.35f, 5.f, -19.f}, concreteWorn());
    addBox(w, {-13.5f + 0.35f, 0.5f, -20.f}, {-8.f, 5.f, -19.f}, concreteWorn());
    
    // Wall 5: 0.5m gap (very tight - challenge)
    addBox(w, {-32.f, 2.f, -15.f}, {-8.f, 4.5f, -14.f}, concreteWorn());
    addBox(w, {-32.f, 2.f, -15.f}, {-13.f - 0.25f, 4.5f, -14.f}, concreteWorn());
    addBox(w, {-13.f + 0.25f, 2.f, -15.f}, {-8.f, 4.5f, -14.f}, concreteWorn());
    
    // Vertical shaft/stairwell to dive down (at x=-20, z=-27)
    addBox(w, {-22.f, 0, -29.f}, {-18.f, 14.f, -25.f}, concreteDark());
    // Hole in floor at each level for vertical dive
    for (int fi = 1; fi < 4; ++fi) {
        float y = floorHeights[fi];
        // Already has hole from floor generation above
    }
    
    // Hole in slab to dive through (at x=-25, z=-27, y=7)
    // Created by floor hole above
    
    // Aim zone gates (markers)
    const V3 aimGateCol{0.95f, 0.3f, 0.12f};
    addGate(w, -16.f, 4.f, -34.5f, 2.2f, false, aimGateCol);  // 2.0m gap
    addGate(w, -15.f, 4.5f, -29.5f, 1.7f, false, aimGateCol); // 1.5m gap
    addGate(w, -14.f, 3.5f, -24.5f, 1.2f, false, aimGateCol); // 1.0m gap
    addGate(w, -13.5f, 2.5f, -19.5f, 0.9f, false, aimGateCol); // 0.7m gap
    addGate(w, -13.f, 3.f, -14.5f, 0.7f, false, aimGateCol);   // 0.5m gap
    
    // ========== ZONE 3: REACTION ZONE (seeded obstacles in courtyard) ==========
    // Courtyard at x = 10 to 50, z = -40 to -10
    // Obstacles placed from seed: hanging beams, fallen slabs, barrels, pipes, fences
    // Floor is part of consolidated ground layer

    // Perimeter walls (partial, ruined)
    addBox(w, {10.f, 0, -40.f}, {50.f, 6.f, -39.f}, concreteWorn());
    addBox(w, {10.f, 0, -10.f}, {50.f, 6.f, -11.f}, concreteWorn());
    addBox(w, {10.f, 0, -40.f}, {11.f, 6.f, -10.f}, concreteWorn());
    addBox(w, {49.f, 0, -40.f}, {50.f, 6.f, -10.f}, concreteWorn());
    
    // Seeded obstacles
    for (int i = 0; i < 35; ++i) {
        float ox = rndRange(13.f, 47.f);
        float oz = rndRange(-37.f, -13.f);
        float oy = 0.f;
        int type = rndInt(6);
        
        switch (type) {
            case 0: // Hanging beam
                addBox(w, {ox - 0.15f, rndRange(3.f, 7.f), oz - 2.f},
                       {ox + 0.15f, rndRange(3.2f, 7.2f), oz + 2.f}, rustyMetal());
                break;
            case 1: // Fallen slab
                addBox(w, {ox - rndRange(1.f, 2.5f), oy, oz - rndRange(1.f, 2.5f)},
                       {ox + rndRange(1.f, 2.5f), oy + 0.3f, oz + rndRange(1.f, 2.5f)}, concreteDark());
                break;
            case 2: // Barrel
                addBox(w, {ox - 0.3f, oy, oz - 0.3f}, {ox + 0.3f, 0.8f, oz + 0.3f}, rustyMetal());
                break;
            case 3: // Pipe cluster
                for (int p = 0; p < 3; ++p) {
                    float px = ox + rndRange(-0.5f, 0.5f);
                    float pz = oz + rndRange(-0.5f, 0.5f);
                    addBox(w, {px - 0.08f, oy, pz - 0.08f}, {px + 0.08f, rndRange(1.f, 4.f), pz + 0.08f}, rebarGray());
                }
                break;
            case 4: // Fence segment
                addBox(w, {ox - 2.f, oy, oz - 0.05f}, {ox + 2.f, rndRange(1.5f, 2.5f), oz + 0.05f}, wireDark());
                break;
            case 5: // Rubble pile
                for (int r = 0; r < 5; ++r) {
                    float rx = ox + rndRange(-1.f, 1.f);
                    float rz = oz + rndRange(-1.f, 1.f);
                    addBox(w, {rx - 0.3f, oy, rz - 0.3f}, {rx + 0.3f, rndRange(0.3f, 1.f), rz + 0.3f}, concreteDark());
                }
                break;
        }
    }
    
    // Reaction zone markers
    const V3 reactionCol{0.95f, 0.78f, 0.12f};
    addGate(w, 30.f, 2.f, -38.f, 3.f, false, reactionCol);  // entry
    addGate(w, 30.f, 2.f, -12.f, 3.f, false, reactionCol);  // exit
    
    // ========== ZONE 4: NOTICE ZONE (thin/low-contrast hazards) ==========
    // Located at x = -50 to -10, z = 10 to 50
    // Thin rebar sticking out, hanging cables, wire fences, thin poles in gaps, low-contrast beams
    // Floor is part of consolidated ground layer

    // Ruined walls with exposed rebar
    for (float wz = 10.f; wz <= 50.f; wz += 8.f) {
        // Wall segment
        addBox(w, {-50.f, 0, wz - 4.f}, {-10.f, 5.f, wz + 4.f}, concreteWorn());
        // Exposed rebar sticking out horizontally (hard to see)
        for (int r = 0; r < 4; ++r) {
            float ry = rndRange(0.5f, 4.5f);
            float rx = -10.f;  // sticking out from wall
            addBox(w, {rx, ry, wz - 4.2f}, {rx + 1.5f, ry + 0.02f, wz + 4.2f}, rebarGray());
        }
        // Vertical rebar sticking up from broken top
        for (int r = 0; r < 3; ++r) {
            float rx = rndRange(-48.f, -12.f);
            addBox(w, {rx - 0.015f, 5.f, wz - 4.2f}, {rx + 0.015f, 6.5f, wz + 4.2f}, rebarGray());
        }
    }
    
    // Hanging cables (thin, dark, hard to see against dark background)
    for (int i = 0; i < 12; ++i) {
        float cx = rndRange(-48.f, -12.f);
        float cz = rndRange(12.f, 48.f);
        float cy = rndRange(3.f, 7.f);
        // Cable sagging down
        addBox(w, {cx - 0.01f, cy - rndRange(1.f, 3.f), cz - 0.01f},
               {cx + 0.01f, cy, cz + 0.01f}, wireDark());
    }
    
    // Wire fence (thin, low contrast)
    for (float fx = -48.f; fx <= -12.f; fx += 4.f) {
        addBox(w, {fx - 0.02f, 0, 28.f - 0.02f}, {fx + 0.02f, 2.5f, 28.f + 0.02f}, wireDark());
    }
    for (float fz = 12.f; fz <= 48.f; fz += 4.f) {
        addBox(w, {-30.f - 0.02f, 0, fz - 0.02f}, {-30.f + 0.02f, 2.5f, fz + 0.02f}, wireDark());
    }
    
    // Thin pole in middle of a gap (at x=-30, z=30, gap in wall)
    addBox(w, {-30.f - 0.025f, 0, 30.f - 0.025f}, {-30.f + 0.025f, 4.f, 30.f + 0.025f}, rebarGray());
    
    // Low-contrast beam (same color as concrete, at mid-height)
    addBox(w, {-45.f, 2.4f, 20.f}, {-15.f, 2.6f, 20.3f}, {0.36f, 0.36f, 0.35f});
    
    // Graffiti-like color stripes (flat colored boxes)
    for (int g = 0; g < 8; ++g) {
        float gx = rndRange(-48.f, -12.f);
        float gz = rndRange(12.f, 48.f);
        float gy = rndRange(0.5f, 4.f);
        V3 grafCol;
        switch (rndInt(4)) {
            case 0: grafCol = {0.8f, 0.1f, 0.1f}; break;  // red
            case 1: grafCol = {0.1f, 0.6f, 0.1f}; break;  // green
            case 2: grafCol = {0.7f, 0.7f, 0.1f}; break;  // yellow
            case 3: grafCol = {0.6f, 0.2f, 0.7f}; break;  // purple
        }
        addBox(w, {gx - 1.5f, gy, gz - 0.02f}, {gx + 1.5f, gy + 0.3f, gz + 0.02f}, grafCol);
    }
    
    // Notice zone markers
    const V3 noticeCol{0.95f, 0.85f, 0.15f};
    addGate(w, -30.f, 2.f, 12.f, 3.f, false, noticeCol);  // entry
    addGate(w, -30.f, 2.f, 48.f, 3.f, false, noticeCol);  // exit
    
    // ========== ZONE 5: OUTDOOR PART ==========
    // x = 60 to 120, z = -60 to 60 (beyond base walls)
    // Broken outer wall, overgrown yard, barrels, rusted container, tower/chimney, roof dive
    // Ground is part of consolidated ground layer

    // Broken outer wall (north side of base, at z=60)
    for (float wx = -56.f; wx <= 56.f; wx += 8.f) {
        if (rnd() < 0.3f) continue;  // broken sections
        addBox(w, {wx - 3.8f, 0, 59.f}, {wx + 3.8f, rndRange(4.f, 9.f), 61.f}, concreteWorn());
    }
    // Gap in wall for exit
    addBox(w, {-4.f, 0, 59.f}, {4.f, 0.5f, 61.f}, concreteWorn());  // low sill
    
    // Overgrown yard: grass tufts (small green boxes)
    // Offset by 1cm (0.01f) to prevent z-fighting with ground plane
    const float grassOffset = 0.01f;
    for (int i = 0; i < 40; ++i) {
        float gx = rndRange(62.f, 118.f);
        float gz = rndRange(-58.f, 58.f);
        addBox(w, {gx - 0.15f, grassOffset, gz - 0.15f}, {gx + 0.15f, rndRange(0.1f, 0.4f) + grassOffset, gz + 0.15f}, mossGreen());
    }
    
    // Barrels
    for (int i = 0; i < 8; ++i) {
        float bx = rndRange(65.f, 115.f);
        float bz = rndRange(-55.f, 55.f);
        addBox(w, {bx - 0.35f, 0, bz - 0.35f}, {bx + 0.35f, 0.9f, bz + 0.35f}, rustyMetal());
    }
    
    // Rusted shipping container (at x=80, z=-20) - with color variation
    V3 containerBase = rustyMetal();
    V3 containerMid = jitterColor({0.40f, 0.28f, 0.20f}, 0.03f, seed);
    V3 containerRoof = jitterColor({0.30f, 0.22f, 0.16f}, 0.03f, seed);
    addBox(w, {75.f, 0, -25.f}, {95.f, 2.6f, -15.f}, containerBase);
    addBox(w, {75.f, 2.6f, -25.f}, {95.f, 5.2f, -15.f}, containerMid);
    // Container roof (can land on)
    addBox(w, {75.f, 5.2f, -25.f}, {95.f, 5.4f, -15.f}, containerRoof);
    
    // Tower/chimney to orbit around (at x=100, z=30)
    const float towerX = 100.f, towerZ = 30.f;
    for (int i = 0; i < 8; ++i) {
        float a = 6.2831853f * i / 8.f;
        float x = towerX + 1.8f * std::cos(a);
        float z = towerZ + 1.8f * std::sin(a);
        addBox(w, {x - 0.7f, 0, z - 0.7f}, {x + 0.7f, 18.f, z + 0.7f}, 
               (i & 1) ? concreteDark() : rustyMetal());
    }
    // Tower top
    addBox(w, {towerX - 2.5f, 17.8f, towerZ - 2.5f}, {towerX + 2.5f, 18.2f, towerZ + 2.5f}, steel());
    
    // Roof you can fly over and dive from (at x=-10 to 10, z=60 to 80, y=14)
    addBox(w, {-10.f, 14.f, 60.f}, {10.f, 14.4f, 80.f}, concreteWorn());
    // Roof access ramp
    for (int i = 0; i < 14; ++i) {
        float ry = i * 1.f;
        addBox(w, {-12.f, ry, 60.f}, {-10.f, ry + 0.3f, 80.f}, concreteWorn());
    }
    
    // Outdoor zone markers
    const V3 outdoorCol{0.15f, 0.95f, 0.35f};
    addGate(w, 0, 15.f, 60.f, 4.f, true, outdoorCol);   // roof entry
    addGate(w, 90.f, 3.f, 30.f, 3.f, false, outdoorCol); // tower orbit
    
    // ========== RUIN DETAILS (throughout base) ==========
    // Rubble piles
    // Offset by 1cm (0.01f) to prevent z-fighting with ground plane
    const float rubbleOffset = 0.01f;
    for (int i = 0; i < 20; ++i) {
        float rx = rndRange(-55.f, 55.f);
        float rz = rndRange(-55.f, 55.f);
        for (int r = 0; r < 4; ++r) {
            addBox(w, {rx + rndRange(-0.8f, 0.8f), rubbleOffset, rz + rndRange(-0.8f, 0.8f)},
                   {rx + rndRange(-0.3f, 0.3f), rndRange(0.2f, 0.8f) + rubbleOffset, rz + rndRange(-0.3f, 0.3f)}, concreteDark());
        }
    }
    
    // Holes in floors (already created by floor generation skipping)
    
    // Collapsed corners
    addBox(w, {-58.f, 0, -58.f}, {-50.f, 3.f, -50.f}, concreteDark());  // SW corner collapsed
    addBox(w, {50.f, 0, -58.f}, {58.f, 2.f, -50.f}, concreteDark());    // SE corner
    addBox(w, {-58.f, 0, 50.f}, {-50.f, 1.f, 58.f}, concreteDark());    // NW corner
    
    // Missing wall segments (gaps in perimeter)
    // Already handled by column/floor generation
    
    // Pillars with exposed rebar
    for (int i = 0; i < 6; ++i) {
        float px = rndRange(-50.f, 50.f);
        float pz = rndRange(-50.f, 50.f);
        addBox(w, {px - 0.5f, 0, pz - 0.5f}, {px + 0.5f, rndRange(4.f, 10.f), pz + 0.5f}, concreteWorn());
        // Exposed rebar
        for (int r = 0; r < 4; ++r) {
            float rx = px + (r % 2 == 0 ? -0.55f : 0.55f);
            float rz = pz + (r < 2 ? -0.55f : 0.55f);
            addBox(w, {rx - 0.02f, 0, rz - 0.02f}, {rx + 0.02f, 10.f, rz + 0.02f}, rebarGray());
        }
    }
    
    // Additional detail props scattered throughout
    for (int i = 0; i < 100; ++i) {
        float x = rndRange(-110.f, 110.f);
        float z = rndRange(-110.f, 110.f);
        addDetailProp(w, x, z, seed);
    }
    
    // Spawn point (at speed zone entry)
    // Note: spawn is handled by main.cpp startPos, but we could encode it here
    
    return w;
}

// Procedural generator: creates a LevelData from seed, saves to file
inline LevelData generateAbandonedBaseLevel(uint32_t seed = 0xDEADBEEF) {
    // Build the world first
    std::vector<Box> boxes = buildAbandonedBase(seed);
    
    LevelData data;
    // Convert boxes back to level format (simplified - in practice you'd track during generation)
    for (const auto& b : boxes) {
        data.boxes.push_back({b.mn, b.mx, b.color, b.mat});
    }
    
    // Add spawn point (speed zone entry)
    data.spawn.pos = {0.f, 1.5f, 10.f};
    data.spawn.yawDeg = 180.f;  // facing down the corridor (-Z)
    
    // Add markers/checkpoints for training
    // Speed zone gates
    data.markers.push_back({{0, 3.5f, 8.f}, 1, 0, 2.5f, {0.15f, 0.8f, 0.95f}});
    data.markers.push_back({{0, 2.5f, -10.f}, 1, 1, 2.5f, {0.15f, 0.8f, 0.95f}});
    data.markers.push_back({{0, 4.f, -25.f}, 1, 2, 2.5f, {0.15f, 0.8f, 0.95f}});
    data.markers.push_back({{0, 3.f, -40.f}, 1, 3, 2.5f, {0.15f, 0.8f, 0.95f}});
    // Aim zone gates
    data.markers.push_back({{-16.f, 4.f, -34.5f}, 1, 4, 1.5f, {0.95f, 0.3f, 0.12f}});
    data.markers.push_back({{-15.f, 4.5f, -29.5f}, 1, 5, 1.2f, {0.95f, 0.3f, 0.12f}});
    data.markers.push_back({{-14.f, 3.5f, -24.5f}, 1, 6, 1.0f, {0.95f, 0.3f, 0.12f}});
    data.markers.push_back({{-13.5f, 2.5f, -19.5f}, 1, 7, 0.8f, {0.95f, 0.3f, 0.12f}});
    data.markers.push_back({{-13.f, 3.f, -14.5f}, 1, 8, 0.6f, {0.95f, 0.3f, 0.12f}});
    // Reaction zone
    data.markers.push_back({{30.f, 2.f, -38.f}, 1, 9, 2.f, {0.95f, 0.78f, 0.12f}});
    data.markers.push_back({{30.f, 2.f, -12.f}, 1, 10, 2.f, {0.95f, 0.78f, 0.12f}});
    // Notice zone
    data.markers.push_back({{-30.f, 2.f, 12.f}, 1, 11, 2.f, {0.95f, 0.85f, 0.15f}});
    data.markers.push_back({{-30.f, 2.f, 48.f}, 1, 12, 2.f, {0.95f, 0.85f, 0.15f}});
    // Outdoor zone
    data.markers.push_back({{0, 15.f, 60.f}, 1, 13, 2.5f, {0.15f, 0.95f, 0.35f}});
    data.markers.push_back({{90.f, 3.f, 30.f}, 1, 14, 2.f, {0.15f, 0.95f, 0.35f}});
    
    data.valid = true;
    return data;
}

}  // namespace fpv
