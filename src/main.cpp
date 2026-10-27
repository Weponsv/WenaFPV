// WenaFPV - 5" FPV freestyle drone simulator. SDL2 + OpenGL 3.3 core.
// Tuned for low-end Intel iGPUs (single static mesh, one draw call, cheap shaders).
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <cctype>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.hpp"
#include "sim.hpp"
#include "world.hpp"
#include "video.hpp"

using namespace fpv;

// ============================================================ GL loader
// Define missing PFN types for texture functions (core OpenGL 1.1+)
typedef void (APIENTRYP PFNGLGENTEXTURESPROC) (GLsizei n, GLuint *textures);
typedef void (APIENTRYP PFNGLBINDTEXTUREPROC) (GLenum target, GLuint texture);
typedef void (APIENTRYP PFNGLTEXIMAGE2DPROC) (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
typedef void (APIENTRYP PFNGLTEXPARAMETERIPROC) (GLenum target, GLenum pname, GLint param);
typedef void (APIENTRYP PFNGLDELETETEXTURESPROC) (GLsizei n, const GLuint *textures);
typedef void (APIENTRYP PFNGLDELETEFRAMEBUFFERSPROC) (GLsizei n, const GLuint *framebuffers);
typedef void (APIENTRYP PFNGLDELETERENDERBUFFERSPROC) (GLsizei n, const GLuint *renderbuffers);
typedef void (APIENTRYP PFNGLDELETEPROGRAMPROC) (GLuint program);

#define GLFUNCS(X)                                                        \
    X(PFNGLCREATESHADERPROC, glCreateShader)                              \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                              \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                            \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                      \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                              \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                            \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                              \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                              \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                    \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                  \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                  \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                      \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                    \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                    \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                                    \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                                    \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                            \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                        \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                        \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                  \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                  \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                  \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)        \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                        \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                        \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)              \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)          \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                      \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                      \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                        \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)        \
    X(PFNGLGENTEXTURESPROC, glGenTextures)                                \
    X(PFNGLBINDTEXTUREPROC, glBindTexture)                                \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D)                                  \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri)                            \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                          \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                  \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)

struct GLF {
#define X(T, N) T N = nullptr;
    GLFUNCS(X)
#undef X
};
static GLF gl;

static bool loadGL() {
    bool ok = true;
#define X(T, N)                                         \
    gl.N = (T)SDL_GL_GetProcAddress(#N);                \
    if (!gl.N) {                                        \
        std::fprintf(stderr, "missing GL function " #N "\n"); \
        ok = false;                                     \
    }
    GLFUNCS(X)
#undef X
    return ok;
}

static GLuint buildProgram(const char* vs, const char* fs, const char* name) {
    auto compile = [&](GLenum type, const char* src) -> GLuint {
        GLuint s = gl.glCreateShader(type);
        gl.glShaderSource(s, 1, &src, nullptr);
        gl.glCompileShader(s);
        GLint ok = 0;
        gl.glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            gl.glGetShaderInfoLog(s, sizeof log, nullptr, log);
            std::fprintf(stderr, "shader error (%s):\n%s\n", name, log);
            std::exit(1);
        }
        return s;
    };
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    GLuint p = gl.glCreateProgram();
    gl.glAttachShader(p, v);
    gl.glAttachShader(p, f);
    gl.glLinkProgram(p);
    GLint ok = 0;
    gl.glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl.glGetProgramInfoLog(p, sizeof log, nullptr, log);
        std::fprintf(stderr, "link error (%s):\n%s\n", name, log);
        std::exit(1);
    }
    gl.glDeleteShader(v);
    gl.glDeleteShader(f);
    return p;
}

// ============================================================ shaders
static const char* WORLD_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec3 aCol;
layout(location=3) in float aMat;
uniform mat4 uVP;
uniform mat4 uLightVP;
out vec3 vWorld; out vec3 vNrm; out vec3 vCol; out float vMat;
void main(){
    vWorld = aPos; vNrm = aNrm; vCol = aCol; vMat = aMat;
    gl_Position = uVP * vec4(aPos, 1.0);
}
)";

static const char* WORLD_FS = R"WFS_DELIM_2024(#version 330 core
in vec3 vWorld; in vec3 vNrm; in vec3 vCol; in float vMat;
uniform vec3 uCam;
uniform float uFog;
uniform float uDetail;
uniform mat4 uLightVP;
uniform sampler2D uShadowMap;
uniform vec3 uShadowFocus;
uniform vec2 uShadowTexel;
uniform float uShadowDistance;
uniform float uShadowFadeDistance;
uniform float uShadowAltitudeFade;
uniform float uShadowStrength;
uniform float uShadowBias;
uniform float uShadowNormalOffset;
uniform int uShadowFilter;
uniform int uShadowsEnabled;
// Debug uniforms for flickering investigation
uniform int uDbgDisableHash21;
uniform int uDbgDisableGrain;
uniform int uDbgDisableScratch;
uniform int uDbgDisableDetail;
out vec4 oColor;
const vec3 HORIZON = vec3(0.74, 0.83, 0.92);
// Stable hash for large integer coordinates (from floor())
// Uses small multiplier (0.1031) to keep values in good range for fract()
float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.x, p.y, p.x + p.y) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float shadowOcclusion(vec3 n) {
    if (uShadowsEnabled == 0 || uShadowStrength <= 0.0 || uShadowAltitudeFade <= 0.0) return 0.0;
    float worldDistance = length(vWorld.xz - uShadowFocus.xz);
    float fadeStart = max(0.0, uShadowDistance - uShadowFadeDistance);
    float distanceFade = 1.0 - smoothstep(fadeStart, uShadowDistance, worldDistance);
    if (distanceFade <= 0.0) return 0.0;

    vec3 lightDir = normalize(vec3(0.45, 0.85, 0.30));
    vec4 clip = uLightVP * vec4(vWorld + n * uShadowNormalOffset, 1.0);
    vec3 coord = clip.xyz / clip.w * 0.5 + 0.5;
    if (coord.x <= 0.0 || coord.x >= 1.0 || coord.y <= 0.0 || coord.y >= 1.0 ||
        coord.z <= 0.0 || coord.z >= 1.0) return 0.0;
    float bias = max(uShadowBias * (1.0 - max(dot(n, lightDir), 0.0)), uShadowBias * 0.25);
    float blocked = 0.0;
    float samples = 0.0;
    if (uShadowFilter == 0) {
        blocked = coord.z - bias > texture(uShadowMap, coord.xy).r ? 1.0 : 0.0;
        samples = 1.0;
    } else if (uShadowFilter == 1) {
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
            vec2 offset = (vec2(float(x), float(y)) - 0.5) * uShadowTexel;
            blocked += coord.z - bias > texture(uShadowMap, coord.xy + offset).r ? 1.0 : 0.0;
            samples += 1.0;
        }
    } else {
        for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(float(x), float(y)) * uShadowTexel;
            blocked += coord.z - bias > texture(uShadowMap, coord.xy + offset).r ? 1.0 : 0.0;
            samples += 1.0;
        }
    }
    return (blocked / samples) * uShadowStrength * distanceFade * uShadowAltitudeFade;
}
void main(){
    vec3 n = normalize(vNrm);
    vec3 col = vCol;
    if (vMat > 0.5 && vMat < 1.5) {                       // ground checker, filtered
        vec2 p = vWorld.xz * 0.5;
        vec2 w = fwidth(p);
        float c = mod(floor(p.x) + floor(p.y), 2.0);
        float fade = clamp(max(w.x, w.y) * 1.5, 0.0, 1.0);
        c = mix(c, 0.5, fade);
        col *= mix(1.0, mix(0.86, 1.0, c), uDetail);
    } else if (vMat > 1.5) {                              // building facade windows
        if (abs(n.y) < 0.5) {
            float h = abs(n.x) > 0.5 ? vWorld.z : vWorld.x;
            vec2 cell = vec2(fract(h / 3.0), fract(vWorld.y / 3.5));
            float win = step(0.25, cell.x) * step(cell.x, 0.8) * step(0.30, cell.y) * step(cell.y, 0.8);
            float fade = clamp(fwidth(h / 3.0) * 3.0, 0.0, 1.0);
            col = mix(col, vec3(0.12, 0.17, 0.25), win * 0.85 * (1.0 - fade) * uDetail);
            if (uDetail > 0.2 && uDbgDisableHash21 == 0) {
                float seam = max(1.0 - smoothstep(0.0, 0.035, min(cell.x, 1.0 - cell.x)),
                                 1.0 - smoothstep(0.0, 0.035, min(cell.y, 1.0 - cell.y)));
                float plaster = hash21(floor(vec2(h, vWorld.y) * 1.7));
                col *= 1.0 + (0.94 + plaster * 0.10 - 1.0) * uDetail;
                col *= 1.0 - seam * 0.12 * uDetail * (1.0 - fade);
            }
        }
    }
    if (!(vMat > 0.5 && vMat < 1.5)) {
        // World-aligned triplanar-like grain gives every prop a material surface:
        // mottled foliage, painted metal, timber, and weathered concrete all pick
        // up subtle detail without textures or extra draw calls.
        vec2 surface = abs(n.y) > 0.5 ? vWorld.xz :
                       (abs(n.x) > 0.5 ? vWorld.zy : vWorld.xy);
        float broad = uDbgDisableHash21 == 0 ? hash21(floor(surface * 1.35)) : 0.5;
        float greenMaterial = smoothstep(0.015, 0.16, col.g - max(col.r, col.b) * 0.58);
        float foliageDapple = smoothstep(0.34, 0.72, broad) * greenMaterial;
        float detailMul = uDbgDisableDetail == 0 ? uDetail : 0.0;
        col *= 1.0 + ((broad - 0.5) * 0.11 +
                       (foliageDapple - 0.35 * greenMaterial) * 0.18) * detailMul;
        if (uDetail > 0.45 && uDbgDisableDetail == 0) {
            float grain = uDbgDisableGrain == 0 ? hash21(floor(surface * 17.0)) : 0.5;
            float neutral = 1.0 - smoothstep(0.025, 0.22, max(max(col.r, col.g), col.b) -
                                                               min(min(col.r, col.g), col.b));
            float scratch = uDbgDisableScratch == 0 ? step(0.992, hash21(floor(surface * vec2(5.0, 31.0)))) : 0.0;
            col *= 1.0 + (grain - 0.5) * 0.07 * uDetail - scratch * neutral * 0.08 * uDetail;
        }
    }
    float diff = max(dot(n, normalize(vec3(0.45, 0.85, 0.30))), 0.0);
    float hemi = 0.5 + 0.5 * n.y;
    vec3 lit = col * (0.30 + 0.25 * hemi + 0.55 * diff);
    vec3 lightDir = normalize(vec3(0.45, 0.85, 0.30));
    vec3 viewDir = normalize(uCam - vWorld);
    float metallic = 1.0 - smoothstep(0.04, 0.28, max(max(col.r, col.g), col.b) -
                                                       min(min(col.r, col.g), col.b));
    float specular = pow(max(dot(reflect(-lightDir, n), viewDir), 0.0), 36.0);
    lit += vec3(0.72, 0.82, 0.9) * specular * metallic * 0.20;
    lit *= 1.0 - shadowOcclusion(n);
    float d = length(vWorld - uCam) * uFog;
    float fog = 1.0 - exp(-d * d);
    oColor = vec4(mix(lit, HORIZON, fog), 1.0);
}
)WFS_DELIM_2024";

static const char* SHADOW_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uShadowVP;
void main(){ gl_Position = uShadowVP * vec4(aPos, 1.0); }
)";
static const char* SHADOW_FS = R"(#version 330 core
void main(){}
)";

// Planar drone shadow shader - projects drone shape onto ground plane
static const char* DRONE_SHADOW_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec3 aCol;
layout(location=3) in float aMat;
uniform mat4 uVP;
uniform mat4 uModel;
uniform vec3 uSunDir;
uniform float uShadowHeight;
uniform vec3 uPlaneNormal;
out float vFade;
void main(){
    vec3 worldPos = (uModel * vec4(aPos, 1.0)).xyz;
    // Project onto plane along sun direction
    float denom = dot(uSunDir, uPlaneNormal);
    float t = (uShadowHeight - dot(worldPos, uPlaneNormal)) / denom;
    vec3 shadowPos = worldPos + uSunDir * t;
    // Fade based on height above plane
    float height = dot(worldPos, uPlaneNormal) - uShadowHeight;
    vFade = 1.0 - smoothstep(0.0, 20.0, height);
    gl_Position = uVP * vec4(shadowPos, 1.0);
}
)";

static const char* DRONE_SHADOW_FS = R"(#version 330 core
in float vFade;
out vec4 oColor;
void main(){
    // Dark semi-transparent shadow (40% black), faded by height
    float alpha = 0.4 * vFade;
    oColor = vec4(0.0, 0.0, 0.0, alpha);
}
)";

// Propeller overlay shader - renders spinning props in FPV view with motion blur
static const char* PROP_OVERLAY_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec3 aCol;
layout(location=3) in float aMat;
layout(location=4) in float aPropIdx;
uniform mat4 uVP;
uniform mat4 uView;
uniform float uTime;
uniform float uRPM[4];
uniform float uOpacity;
uniform int uBlades;
uniform vec3 uSunDir;
out vec3 vWorld;
out vec3 vNrm;
out vec3 vCol;
out float vPropIdx;
out float vRPM;
out float vBlades;
void main(){
    vWorld = aPos;
    vNrm = aNrm;
    vCol = aCol;
    vPropIdx = aPropIdx;
    vRPM = uRPM[int(aPropIdx + 0.5)];
    vBlades = float(uBlades);
    
    vec3 pos = aPos;
    // Spin propellers (indices 0-3)
    if (aPropIdx >= 0.0 && aPropIdx <= 3.0) {
        float rpm = uRPM[int(aPropIdx + 0.5)];
        // Alternate direction: 0,3 = CW; 1,2 = CCW (or vice versa)
        float dir = (aPropIdx == 0.0 || aPropIdx == 3.0) ? 1.0 : -1.0;
        float angle = rpm * (2.0 * 3.14159 / 60.0) * uTime * dir;
        // Rotate around Y axis
        float ca = cos(angle);
        float sa = sin(angle);
        pos = vec3(pos.x * ca + pos.z * sa, pos.y, -pos.x * sa + pos.z * ca);
    }
    // Apply view transform (camera tilt, no translation)
    vec4 viewPos = uView * vec4(pos, 1.0);
    gl_Position = uVP * viewPos;
}
)";

static const char* PROP_OVERLAY_FS = R"(#version 330 core
in vec3 vWorld;
in vec3 vNrm;
in vec3 vCol;
in float vPropIdx;
in float vRPM;
in float vBlades;
uniform float uOpacity;
uniform vec3 uSunDir;
uniform float uTime;
out vec4 oColor;
void main(){
    vec3 n = normalize(vNrm);
    vec3 col = vCol;
    
    // Calculate lighting
    float diff = max(dot(n, uSunDir), 0.0);
    float amb = 0.3;
    col = col * (amb + diff * 0.7);
    
    // Propeller motion blur: at high RPM, fade blades and show disc
    float maxRPM = 30000.0;
    float rpmNorm = clamp(vRPM / maxRPM, 0.0, 1.0);
    
    // Alpha: blades fade out, disc fades in
    float bladeAlpha = 1.0 - smoothstep(0.3, 0.8, rpmNorm);
    float discAlpha = smoothstep(0.3, 0.8, rpmNorm);
    
    // Determine if this is a blade or hub/disc based on propIdx
    // propIdx 0-3 = propellers, 4 = motors, 5 = front arms
    float alpha;
    if (vPropIdx >= 0.0 && vPropIdx <= 3.0) {
        // Propeller: blend between blades and disc
        // Use a subtle wobble on disc brightness at high RPM
        float wobble = 1.0 + 0.05 * sin(uTime * vRPM * 0.001 * vBlades) * discAlpha;
        // Increased disc alpha from 0.3 to 0.5 for better visibility
        alpha = bladeAlpha * wobble + discAlpha * 0.5 * wobble;
    } else {
        // Motors and arms: fully opaque
        alpha = 1.0;
    }
    
    alpha *= uOpacity / 100.0;
    oColor = vec4(col, alpha);
}
)";

static const char* SKY_VS = R"(#version 330 core
out vec2 vNdc;
void main(){
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vNdc = p * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 0.999, 1.0);
}
)";

static const char* SKY_FS = R"(#version 330 core
in vec2 vNdc;
uniform vec3 uRight; uniform vec3 uUp; uniform vec3 uBack;
uniform vec2 uTan;
out vec4 oColor;
void main(){
    vec3 dir = normalize(uRight * (vNdc.x * uTan.x) + uUp * (vNdc.y * uTan.y) - uBack);
    float t = clamp(dir.y, 0.0, 1.0);
    vec3 horizon = vec3(0.74, 0.83, 0.92);
    vec3 zenith = vec3(0.22, 0.46, 0.84);
    vec3 col = mix(horizon, zenith, pow(t, 0.55));
    float sun = pow(max(dot(dir, normalize(vec3(0.45, 0.85, 0.30))), 0.0), 400.0);
    col += vec3(1.0, 0.95, 0.8) * sun;
    oColor = vec4(col, 1.0);
}
)";

// Analog FPV video shader — designed for Intel HD 520 class hardware.
// Single pass, no blur, no extra render targets.
static const char* VIDEO_FS = R"(#version 330 core
in vec2 vNdc;
uniform sampler2D uScene;
uniform vec2  uResolution;
uniform float uTime;

// Signal state uniforms
uniform float uSignalLoss;      // 0..1  (1 = total loss)
uniform float uBurstStrength;   // 0..1  close-wall burst

// Effect toggles / intensities
uniform float uNoiseOn;           // 0 or 1
uniform float uNoiseIntensity;    // 0..2
uniform float uStaticIntensity;   // 0..2
uniform float uSnowStrength;      // legacy compat, maps to static
uniform float uScanlinesOn;       // 0 or 1
uniform float uScanlineIntensity; // 0..2
uniform float uChromaOn;          // 0 or 1
uniform float uColorGlitch;       // 0..2
uniform float uBreakupOn;         // 0 or 1
uniform float uTearIntensity;     // 0..2
uniform float uRollingBarOn;      // 0 or 1
uniform float uRollingBarPhase;   // 0..1
uniform float uChromaGlitchPhase; // radians

// Tear band (CPU-driven)
uniform float uTearRow;    // 0..1 UV Y of the thin tear band
uniform float uTearOffset; // -1..1 horizontal shift in pixels

// Dropout flash (black/white frame overlay)
uniform float uDropoutFlash;      // 0 = no flash, 0..1 = partial flash brightness
uniform float uDropoutFlashActive;// 0 or 1

// Performance mode (0=full, 1=fast, 2=ultralow)
uniform int uPerfMode;

// New analog artifact uniforms
uniform float uDotCrawlOn;          // 0 or 1
uniform float uSyncLossOn;          // 0 or 1
uniform float uSyncLossTimer;       // accumulated s (>0 when active)
uniform float uSyncLossSeverity;    // 0..1.5
uniform float uSyncLossPhase;       // radians
uniform float uColorBurstOn;        // 0 or 1
uniform float uColorBurstTimer;     // accumulated s (>0 when active)
uniform float uColorBurstSeverity;  // 0..1
uniform float uMultipathGhostOn;    // 0 or 1
uniform float uMultipathGhostOffset;// UV horizontal offset for ghost

out vec4 oColor;

// ---- fast hash (Blum-Blum-Shub variant, cheap on ALU)
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float hash12f(vec2 p, float frame) {
    return hash12(floor(p) + vec2(frame * 17.0, frame * 3.7));
}

void main() {
    vec2 uv  = vNdc * 0.5 + 0.5;
    vec2 px  = 1.0 / max(uResolution, vec2(1.0));
    float loss    = clamp(uSignalLoss, 0.0, 1.0);
    float burst   = clamp(uBurstStrength, 0.0, 1.0);
    float effectiveLoss = clamp(loss + burst * 0.5, 0.0, 1.0);

    // Quantise time so noise updates at ~30 fps regardless of render rate (cheap)
    float frame = floor(uTime * 30.0);

    // ---- Horizontal image shift (signal displacement)
    // Low-quality: skip sub-pixel UV distortion entirely
    vec2 sampleUV = uv;
    if (uPerfMode < 2 && uBreakupOn > 0.5) {
        // Coarse horizontal shift from tear band
        float onTear = step(abs(uv.y - uTearRow), 4.0 * px.y);
        sampleUV.x  += onTear * uTearOffset * 0.06 * uTearIntensity;
        sampleUV     = clamp(sampleUV, vec2(0.0), vec2(1.0));

        // Image-wide horizontal jitter when signal is bad
        float jitter = (hash12f(vec2(frame, 7.0), uv.y * 80.0) - 0.5)
                       * effectiveLoss * effectiveLoss * 0.012 * uTearIntensity;
        if (effectiveLoss > 0.4) sampleUV.x = clamp(sampleUV.x + jitter, 0.0, 1.0);
    }

    // ---- Chromatic aberration / RGB channel split
    // At full signal: very slight edge-only fringing (analog lens effect).
    // Scales up strongly with signal loss.
    vec4 scene;
    if (uChromaOn > 0.5 && uPerfMode < 2) {
        float edge  = smoothstep(0.25, 0.9,
            length((uv - 0.5) * vec2(1.0, uResolution.y / uResolution.x)));
        // Base fringe is tiny (0.3px at edges at full signal), grows with loss
        float chroma = edge * (0.08 + effectiveLoss * 0.9);
        float cg    = uColorGlitch * (1.0 + 0.4 * sin(uChromaGlitchPhase));
        vec2 shift  = vec2(px.x * (0.5 + effectiveLoss * 2.5) * chroma * cg, 0.0);
        scene.r = texture(uScene, clamp(sampleUV + shift, vec2(0.0), vec2(1.0))).r;
        scene.g = texture(uScene, sampleUV).g;
        scene.b = texture(uScene, clamp(sampleUV - shift, vec2(0.0), vec2(1.0))).b;
        scene.a = 1.0;
    } else {
        scene = texture(uScene, sampleUV);
    }

    // ---- Hue / color corruption (glitch bands)
    if (uBreakupOn > 0.5 && uColorGlitch > 0.01 && uPerfMode < 2) {
        float glitchBand = hash12(vec2(floor(uv.y * uResolution.y * 0.25), frame * 2.0 + 1.0));
        if (glitchBand > 0.95 && effectiveLoss > 0.35) {
            // Swap R and B in glitch band
            float swapAmt = (glitchBand - 0.95) / 0.05 * effectiveLoss * uColorGlitch;
            float tmp = scene.r;
            scene.r = mix(scene.r, scene.b, swapAmt * 0.6);
            scene.b = mix(scene.b, tmp,     swapAmt * 0.6);
            scene.g = mix(scene.g, 1.0 - scene.g, swapAmt * 0.25);
        }
    }

    // ---- Chroma bleed (horizontal smear of colour)
    if (uChromaOn > 0.5 && effectiveLoss > 0.3 && uPerfMode == 0) {
        float bleed = effectiveLoss * effectiveLoss * 0.008 * uColorGlitch;
        scene.r += texture(uScene, clamp(sampleUV + vec2(-bleed, 0.0), vec2(0.0), vec2(1.0))).r * 0.15;
        scene.b += texture(uScene, clamp(sampleUV + vec2( bleed, 0.0), vec2(0.0), vec2(1.0))).b * 0.15;
        scene.rgb = clamp(scene.rgb, 0.0, 1.0);
    }

    // ---- Desaturation (analog loses chroma first)
    if (uBreakupOn > 0.5) {
        float luma = dot(scene.rgb, vec3(0.299, 0.587, 0.114));
        scene.rgb = mix(scene.rgb, vec3(luma), clamp(effectiveLoss * 1.1, 0.0, 1.0));
    }

    // ---- Snow / grain / salt-pepper
    // All three knobs multiply their respective effect to zero when set to 0.
    // uSnowStrength is the master scale (maps to old "snow strength" slider).
    if (uNoiseOn > 0.5) {
        float noiseScale = (uPerfMode >= 2) ? 0.5 : 1.0;
        float rnd = hash12f(gl_FragCoord.xy * noiseScale, frame);

        // Fine grain — nearly invisible at full signal (loss≈0), grows with degradation.
        // Formula: amplitude = noiseIntensity * snowStrength * (base + loss_component)
        // loss=0, intensity=1, snow=1 → ±0.004 (invisible)
        // loss=1, intensity=1, snow=1 → ±0.050 (heavy grain)
        float grainAmp = uNoiseIntensity * uSnowStrength * (0.008 + 0.092 * effectiveLoss);
        float grain    = (rnd - 0.5) * grainAmp;

        // Salt & pepper — start appearing only below ~85% signal quality (loss > 0.15)
        float saltThresh   = 0.9998 - max(effectiveLoss - 0.15, 0.0) * 0.055;
        float pepperThresh = 0.0001 + max(effectiveLoss - 0.15, 0.0) * 0.035;
        float salt   = step(saltThresh,   rnd) * uStaticIntensity * uSnowStrength;
        float pepper = step(rnd, pepperThresh) * uStaticIntensity * uSnowStrength;

        // Heavy static blocks — only below 50% signal quality
        float bigBlock = 0.0;
        if (effectiveLoss > 0.5 && uPerfMode < 2) {
            float rnd2 = hash12(floor(gl_FragCoord.xy * 0.125) + vec2(frame * 5.0, 0.0));
            bigBlock   = step(0.94 - (effectiveLoss - 0.5) * 0.7, rnd2)
                         * (effectiveLoss - 0.5) * 2.0 * uStaticIntensity * uSnowStrength;
        }

        scene.rgb = clamp(scene.rgb + grain + salt * 0.40 - pepper * 0.40
                          + bigBlock * (rnd - 0.5) * 0.8, 0.0, 1.0);
    }

    // ---- Scanlines
    // At intensity=1.0: very faint modulation (±1.5%). At 2.0: noticeable (±3%).
    if (uScanlinesOn > 0.5) {
        float scan = 1.0 - 0.015 * uScanlineIntensity
                     * (0.5 - 0.5 * sin(gl_FragCoord.y * 3.14159 + uTime * 2.0));
        scene.rgb *= scan;
    }

    // ---- Rolling sync bar (only appears with meaningful signal loss)
    if (uRollingBarOn > 0.5 && effectiveLoss > 0.15) {
        float barY  = mod(uv.y - uRollingBarPhase, 1.0);
        float bar   = smoothstep(0.04, 0.0, abs(barY - 0.5) - 0.46)
                      * (effectiveLoss - 0.15) * 0.4;
        scene.rgb = clamp(scene.rgb + bar - bar * 0.5, 0.0, 1.0);
    }

    // ---- Close-wall burst interference (gated by static intensity knob)
    if (burst > 0.01 && uStaticIntensity > 0.01) {
        float scaledBurst = burst * uStaticIntensity;
        float rndB = hash12f(gl_FragCoord.xy, frame + 100.0);
        float burstNoise = (rndB - 0.5) * scaledBurst * 1.4;
        scene.rgb = clamp(scene.rgb + burstNoise, 0.0, 1.0);
        // White/black flicker bands
        float bandRnd = hash12(vec2(floor(uv.y * uResolution.y * 0.2), frame * 7.0));
        if (bandRnd > 0.7) scene.rgb = mix(scene.rgb, vec3(bandRnd), scaledBurst * 0.6);
    }

    // ---- Tearing — hard horizontal rip (dark band)
    if (uBreakupOn > 0.5 && uTearIntensity > 0.01) {
        float rndT = hash12f(vec2(floor(uv.y * uResolution.y), 0.0), frame);
        float tearLine = step(0.993 - effectiveLoss * 0.08, rndT) * uTearIntensity;
        scene.rgb = mix(scene.rgb, vec3(0.008, 0.012, 0.016), tearLine * 0.85);
    }

    // ---- Full-frame dropout flash (black or white)
    if (uDropoutFlashActive > 0.5) {
        scene.rgb = mix(scene.rgb, vec3(uDropoutFlash), 0.92);
    }

    // ---- NTSC dot-crawl (luma/chroma subcarrier phase crawl)
    if (uDotCrawlOn > 0.5 && loss > 0.05 && uPerfMode < 2) {
        float crawl = sin(uTime * 22600.0 + uv.x * 500.0) * loss * 0.05;
        scene.r = clamp(scene.r + crawl,        0.0, 1.0);
        scene.b = clamp(scene.b - crawl * 0.5,  0.0, 1.0);
    }

    // ---- Sync loss: horizontal displacement waves
    if (uSyncLossOn > 0.5 && uSyncLossTimer > 0.0) {
        float syncDisplace = sin(uv.y * 80.0 + uSyncLossPhase) * uSyncLossSeverity * 0.018;
        vec2 syncUV = clamp(sampleUV + vec2(syncDisplace, 0.0), vec2(0.0), vec2(1.0));
        float blend = clamp(uSyncLossSeverity * 0.7, 0.0, 1.0);
        scene = mix(scene, texture(uScene, syncUV), blend);
    }

    // ---- Color burst loss: rainbow hue bands
    if (uColorBurstOn > 0.5 && uColorBurstTimer > 0.0) {
        float bandPhase = uv.y * 50.0 + uSyncLossPhase;
        vec3 rainbow = vec3(
            0.5 + 0.5 * sin(bandPhase + 0.000),
            0.5 + 0.5 * sin(bandPhase + 2.094),
            0.5 + 0.5 * sin(bandPhase + 4.189)
        );
        float bandMask = abs(sin(uv.y * 25.0 + uSyncLossPhase * 0.5)) * uColorBurstSeverity;
        scene.rgb = mix(scene.rgb, rainbow, clamp(bandMask * 0.55, 0.0, 0.5));
    }

    // ---- Multipath ghost image
    if (uMultipathGhostOn > 0.5 && abs(uMultipathGhostOffset) > 0.0005) {
        vec2 ghostUV = clamp(sampleUV + uMultipathGhostOffset * vec2(0.01, 0.0),
                             vec2(0.0), vec2(1.0));
        vec4 ghost = texture(uScene, ghostUV);
        float ghostAlpha = clamp(abs(uMultipathGhostOffset) * 18.0, 0.0, 0.28);
        scene = mix(scene, ghost, ghostAlpha);
    }

    oColor = scene;
}
)";

static const char* OSD_VS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;
uniform vec2 uRes;
out vec4 vCol;
void main(){
    vCol = aCol;
    gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
}
)";

static const char* OSD_FS = R"(#version 330 core
in vec4 vCol; out vec4 oColor;
void main(){ oColor = vCol; }
)";

// ============================================================ Draw Order Logging
static void print_draw_order() {
    std::printf("\n=== DRAW ORDER (one frame) ===\n");
    std::printf("1. Shadow depth pass -> shadowFbo (shadowVP)\n");
    std::printf("2. Camera view -> sceneFbo:\n");
    std::printf("   2a. Sky pass (progSky, vaoSky, fullscreen triangle)\n");
    std::printf("   2b. World pass (progWorld, vaoWorld, VP matrix)\n");
    std::printf("   2c. Traffic cars (progWorld, vaoTraffic, VP matrix)\n");
    std::printf("   2d. Drone planar shadow (progDroneShadow, vaoDrone, VP matrix, stencil)\n");
    std::printf("   2e. Propeller overlay (progPropOverlay, vaoPropOverlay, P_overlay+view, blend, no cull)\n");
    std::printf("3. MSAA resolve (sceneFbo -> resolveFbo) if MSAA > 1\n");
    std::printf("4. Video post-process -> default framebuffer (progVideo, vaoSky, sceneColor texture)\n");
    std::printf("5. OSD/UI -> default framebuffer (progOsd, vaoOsd, orthographic uRes)\n");
    std::printf("   5a. Battery, altitude, speed, timer, ARMED status\n");
    std::printf("   5b. Settings menu (if open)\n");
    std::printf("6. Swap buffers\n");
    std::printf("=== END DRAW ORDER ===\n\n");
}

// ============================================================ GL State Diagnostic
static void dump_gl_state(const char* label) {
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    
    int scissor[4];
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    GLboolean scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLint depthFunc;
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    GLboolean depthMask;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    
    GLboolean blend = glIsEnabled(GL_BLEND);
    GLint blendSrcRGB, blendDstRGB, blendSrcAlpha, blendDstAlpha;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
    
    GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
    GLint cullMode;
    glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
    
    GLboolean colorMask[4];
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    
    GLboolean stencilTest = glIsEnabled(GL_STENCIL_TEST);
    
    GLboolean polygonOffsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    float polyOffsetFactor, polyOffsetUnits;
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &polyOffsetFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &polyOffsetUnits);
    
    GLint currentProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    
    GLint boundVAO;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);
    
    GLint activeTexture;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    
    GLint boundTex2D[4];
    for (int i = 0; i < 4; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex2D[i]);
    }
    glActiveTexture(GL_TEXTURE0);
    
    GLint boundFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &boundFBO);
    
    float clearDepth;
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clearDepth);
    
    const char* depthFuncStr = "UNKNOWN";
    switch (depthFunc) {
        case GL_NEVER: depthFuncStr = "NEVER"; break;
        case GL_LESS: depthFuncStr = "LESS"; break;
        case GL_EQUAL: depthFuncStr = "EQUAL"; break;
        case GL_LEQUAL: depthFuncStr = "LEQUAL"; break;
        case GL_GREATER: depthFuncStr = "GREATER"; break;
        case GL_NOTEQUAL: depthFuncStr = "NOTEQUAL"; break;
        case GL_GEQUAL: depthFuncStr = "GEQUAL"; break;
        case GL_ALWAYS: depthFuncStr = "ALWAYS"; break;
    }
    
    const char* cullModeStr = "UNKNOWN";
    switch (cullMode) {
        case GL_FRONT: cullModeStr = "FRONT"; break;
        case GL_BACK: cullModeStr = "BACK"; break;
        case GL_FRONT_AND_BACK: cullModeStr = "FRONT_AND_BACK"; break;
    }
    
    const char* blendSrcStr = "UNKNOWN";
    switch (blendSrcRGB) {
        case GL_ZERO: blendSrcStr = "ZERO"; break;
        case GL_ONE: blendSrcStr = "ONE"; break;
        case GL_SRC_COLOR: blendSrcStr = "SRC_COLOR"; break;
        case GL_ONE_MINUS_SRC_COLOR: blendSrcStr = "ONE_MINUS_SRC_COLOR"; break;
        case GL_DST_COLOR: blendSrcStr = "DST_COLOR"; break;
        case GL_ONE_MINUS_DST_COLOR: blendSrcStr = "ONE_MINUS_DST_COLOR"; break;
        case GL_SRC_ALPHA: blendSrcStr = "SRC_ALPHA"; break;
        case GL_ONE_MINUS_SRC_ALPHA: blendSrcStr = "ONE_MINUS_SRC_ALPHA"; break;
        case GL_DST_ALPHA: blendSrcStr = "DST_ALPHA"; break;
        case GL_ONE_MINUS_DST_ALPHA: blendSrcStr = "ONE_MINUS_DST_ALPHA"; break;
        case GL_CONSTANT_COLOR: blendSrcStr = "CONSTANT_COLOR"; break;
        case GL_ONE_MINUS_CONSTANT_COLOR: blendSrcStr = "ONE_MINUS_CONSTANT_COLOR"; break;
        case GL_CONSTANT_ALPHA: blendSrcStr = "CONSTANT_ALPHA"; break;
        case GL_ONE_MINUS_CONSTANT_ALPHA: blendSrcStr = "ONE_MINUS_CONSTANT_ALPHA"; break;
        case GL_SRC_ALPHA_SATURATE: blendSrcStr = "SRC_ALPHA_SATURATE"; break;
    }
    
    const char* blendDstStr = "UNKNOWN";
    switch (blendDstRGB) {
        case GL_ZERO: blendDstStr = "ZERO"; break;
        case GL_ONE: blendDstStr = "ONE"; break;
        case GL_SRC_COLOR: blendDstStr = "SRC_COLOR"; break;
        case GL_ONE_MINUS_SRC_COLOR: blendDstStr = "ONE_MINUS_SRC_COLOR"; break;
        case GL_DST_COLOR: blendDstStr = "DST_COLOR"; break;
        case GL_ONE_MINUS_DST_COLOR: blendDstStr = "ONE_MINUS_DST_COLOR"; break;
        case GL_SRC_ALPHA: blendDstStr = "SRC_ALPHA"; break;
        case GL_ONE_MINUS_SRC_ALPHA: blendDstStr = "ONE_MINUS_SRC_ALPHA"; break;
        case GL_DST_ALPHA: blendDstStr = "DST_ALPHA"; break;
        case GL_ONE_MINUS_DST_ALPHA: blendDstStr = "ONE_MINUS_DST_ALPHA"; break;
    }
    
    std::printf("\n=== GL STATE DUMP: %s ===\n", label);
    std::printf("  Viewport: %d %d %d %d\n", viewport[0], viewport[1], viewport[2], viewport[3]);
    std::printf("  Scissor: %d %d %d %d  Enabled: %s\n", scissor[0], scissor[1], scissor[2], scissor[3], scissorTest ? "YES" : "NO");
    std::printf("  DepthTest: %s  Func: %s  Mask: %s\n", depthTest ? "ON" : "OFF", depthFuncStr, depthMask ? "TRUE" : "FALSE");
    std::printf("  Blend: %s  Src: %s  Dst: %s\n", blend ? "ON" : "OFF", blendSrcStr, blendDstStr);
    std::printf("  CullFace: %s  Mode: %s\n", cullFace ? "ON" : "OFF", cullModeStr);
    std::printf("  ColorMask: R=%s G=%s B=%s A=%s\n", 
                colorMask[0] ? "T" : "F", colorMask[1] ? "T" : "F", colorMask[2] ? "T" : "F", colorMask[3] ? "T" : "F");
    std::printf("  StencilTest: %s\n", stencilTest ? "ON" : "OFF");
    std::printf("  PolygonOffset: %s  Factor: %.3f  Units: %.3f\n", polygonOffsetFill ? "ON" : "OFF", polyOffsetFactor, polyOffsetUnits);
    std::printf("  CurrentProgram: %u\n", (unsigned)currentProgram);
    std::printf("  BoundVAO: %u\n", (unsigned)boundVAO);
    std::printf("  ActiveTexture: GL_TEXTURE%d\n", activeTexture - GL_TEXTURE0);
    std::printf("  BoundTex2D[0-3]: %u %u %u %u\n", (unsigned)boundTex2D[0], (unsigned)boundTex2D[1], (unsigned)boundTex2D[2], (unsigned)boundTex2D[3]);
    std::printf("  BoundFBO: %u\n", (unsigned)boundFBO);
    std::printf("  ClearDepth: %.6f\n", clearDepth);
    std::printf("=== END GL STATE DUMP ===\n\n");
}

// ============================================================ matrices (column-major)
static void mat4mul(const float* a, const float* b, float* o) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            o[c * 4 + r] = s;
        }
}

static V3 normalize3(V3 v) {
    const float l = len(v);
    return l > 1e-6f ? v / l : V3{0.f, 1.f, 0.f};
}

static void lookAtMatrix(V3 eye, V3 target, V3 up, float out[16]) {
    const V3 f = normalize3(target - eye);
    const V3 s = normalize3(cross(f, up));
    const V3 u = cross(s, f);
    const float m[16] = {
        s.x, u.x, -f.x, 0.f,
        s.y, u.y, -f.y, 0.f,
        s.z, u.z, -f.z, 0.f,
        -dot(s, eye), -dot(u, eye), dot(f, eye), 1.f
    };
    std::memcpy(out, m, sizeof m);
}

static void orthoMatrix(float left, float right, float bottom, float top,
                        float nearPlane, float farPlane, float out[16]) {
    std::memset(out, 0, 16 * sizeof(float));
    out[0] = 2.f / (right - left);
    out[5] = 2.f / (top - bottom);
    out[10] = -2.f / (farPlane - nearPlane);
    out[12] = -(right + left) / (right - left);
    out[13] = -(top + bottom) / (top - bottom);
    out[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    out[15] = 1.f;
}

// ============================================================ Ground Diagnostics
struct GroundLayer {
    std::string name;
    V3 mn, mx;  // world bounds
    float yMin, yMax;  // Y height range
    V3 color;
    int mat;
    int triCount;
    int boxCount;
};

static void analyzeGroundLayers(const std::vector<Box>& boxes, std::vector<GroundLayer>& layers) {
    // Collect all boxes that could be ground (y <= 0.2, normal facing up)
    struct GroundBox { V3 mn, mx; V3 color; int mat; };
    std::vector<GroundBox> groundBoxes;
    
    for (const Box& b : boxes) {
        // Ground boxes: top face at or near y=0, or boxes with very small height at ground level
        float height = b.mx.y - b.mn.y;
        float topY = b.mx.y;
        float bottomY = b.mn.y;
        
        // Check if this is a ground-like box (horizontal surface at ground level)
        if (topY <= 0.5f && height <= 1.0f) {
            // Further check: does it have a top face normal of (0,1,0)?
            // In buildMesh, +Y face uses normal (0,1,0) - that's the top face
            groundBoxes.push_back({b.mn, b.mx, b.color, b.mat});
        }
    }
    
    // Group by material and approximate Y height
    for (const auto& gb : groundBoxes) {
        bool found = false;
        for (auto& layer : layers) {
            if (layer.mat == gb.mat && std::fabs(layer.yMax - gb.mx.y) < 0.02f) {
                // Merge into existing layer
                layer.mn.x = std::min(layer.mn.x, gb.mn.x);
                layer.mn.z = std::min(layer.mn.z, gb.mn.z);
                layer.mx.x = std::max(layer.mx.x, gb.mx.x);
                layer.mx.z = std::max(layer.mx.z, gb.mx.z);
                layer.yMin = std::min(layer.yMin, gb.mn.y);
                layer.yMax = std::max(layer.yMax, gb.mx.y);
                layer.boxCount++;
                // Each box has 2 triangles for top face
                layer.triCount += 2;
                found = true;
                break;
            }
        }
        if (!found) {
            GroundLayer layer;
            layer.name = "Layer_" + std::to_string(layers.size());
            layer.mn = gb.mn;
            layer.mx = gb.mx;
            layer.yMin = gb.mn.y;
            layer.yMax = gb.mx.y;
            layer.color = gb.color;
            layer.mat = gb.mat;
            layer.triCount = 2;  // top face = 2 triangles
            layer.boxCount = 1;
            layers.push_back(layer);
        }
    }
    
    // Refine layer names based on properties
    for (auto& layer : layers) {
        std::string matStr = layer.mat == 0 ? "DEF" : (layer.mat == 1 ? "GRD" : (layer.mat == 2 ? "BLD" : "UNK"));
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s_Y%.2fto%.2f_X%.0fto%.0f_Z%.0fto%.0f_C%.2f%.2f%.2f_B%d",
                      matStr.c_str(), layer.yMin, layer.yMax, 
                      layer.mn.x, layer.mx.x, layer.mn.z, layer.mx.z,
                      layer.color.x, layer.color.y, layer.color.z, layer.boxCount);
        layer.name = buf;
    }
}

static void printGroundLayerInfo(const std::vector<GroundLayer>& layers) {
    std::printf("\n=== GROUND LAYER ANALYSIS ===\n");
    std::printf("Found %zu ground layers:\n", layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        const auto& L = layers[i];
        std::printf("  [%zu] %s\n", i, L.name.c_str());
        std::printf("       Bounds: X[%.1f,%.1f] Y[%.3f,%.3f] Z[%.1f,%.1f]  Tris:%d  Boxes:%d\n",
                    L.mn.x, L.mx.x, L.yMin, L.yMax, L.mn.z, L.mx.z, L.triCount, L.boxCount);
    }
    
    // Check for overlaps
    std::printf("\n=== OVERLAPPING GROUND LAYERS (same Y, overlapping XZ) ===\n");
    for (size_t i = 0; i < layers.size(); ++i) {
        for (size_t j = i + 1; j < layers.size(); ++j) {
            const auto& A = layers[i];
            const auto& B = layers[j];
            
            // Check Y overlap (within 1cm)
            float yOverlap = std::min(A.yMax, B.yMax) - std::max(A.yMin, B.yMin);
            if (yOverlap > -0.01f) {  // overlapping or very close in Y
                // Check XZ overlap
                float xOverlap = std::min(A.mx.x, B.mx.x) - std::max(A.mn.x, B.mn.x);
                float zOverlap = std::min(A.mx.z, B.mx.z) - std::max(A.mn.z, B.mn.z);
                if (xOverlap > 0.0f && zOverlap > 0.0f) {
                    float overlapArea = xOverlap * zOverlap;
                    std::printf("  OVERLAP: [%zu] vs [%zu]  Y-overlap: %.4f  X-overlap: %.1f  Z-overlap: %.1f  Area: %.1f m^2\n",
                                i, j, yOverlap, xOverlap, zOverlap, overlapArea);
                }
            }
        }
    }
}

static void printProjectionInfo(float zn, float zf, const float* P, int dw, int dh, float fovH) {
    std::printf("\n=== PROJECTION / DEPTH INFO ===\n");
    std::printf("  FOV: %.1f deg  Aspect: %.3f  Near: %.3f  Far: %.3f\n", fovH, (float)dw/(float)dh, zn, zf);
    std::printf("  Depth buffer: 24-bit\n");
    std::printf("  Projection type: Standard perspective (not reversed-Z)\n");
    std::printf("  P matrix:\n");
    for (int r = 0; r < 4; ++r) {
        std::printf("    ");
        for (int c = 0; c < 4; ++c) {
            std::printf("%10.4f ", P[c*4 + r]);
        }
        std::printf("\n");
    }
    // Depth precision at various distances
    std::printf("  Depth precision (z / far * 2^24):\n");
    for (float z : {0.1f, 0.5f, 1.0f, 5.0f, 10.0f, 50.0f, 100.0f, 500.0f, 1000.0f}) {
        if (z < zf) {
            float ndc = (zf + zn) / (zn - zf) + (2.0f * zf * zn / (zn - zf)) / z;
            float depth = (ndc + 1.0f) * 0.5f;
            std::printf("    z=%.1f -> depth=%.6f\n", z, depth);
        }
    }
}

// Forward declaration
static std::vector<float> buildMesh(const std::vector<Box>& boxes);

// Rebuild mesh with certain ground layers disabled
static std::vector<float> buildMeshWithGroundToggles(const std::vector<Box>& boxes, 
                                                      const bool disableGroundLayer[8]) {
    // First analyze ground layers
    std::vector<GroundLayer> layers;
    analyzeGroundLayers(boxes, layers);
    
    // Build a set of boxes to exclude
    std::vector<Box> filteredBoxes;
    filteredBoxes.reserve(boxes.size());
    
    for (const Box& b : boxes) {
        float height = b.mx.y - b.mn.y;
        float topY = b.mx.y;
        
        // Check if this is a ground box
        bool isGround = (topY <= 0.5f && height <= 1.0f);
        bool exclude = false;
        
        if (isGround) {
            // Find which layer this box belongs to
            for (size_t i = 0; i < layers.size() && i < 8; ++i) {
                const auto& L = layers[i];
                if (L.mat == b.mat && std::fabs(L.yMax - b.mx.y) < 0.02f &&
                    b.mn.x >= L.mn.x - 0.1f && b.mx.x <= L.mx.x + 0.1f &&
                    b.mn.z >= L.mn.z - 0.1f && b.mx.z <= L.mx.z + 0.1f) {
                    if (disableGroundLayer[i]) {
                        exclude = true;
                    }
                    break;
                }
            }
        }
        
        if (!exclude) {
            filteredBoxes.push_back(b);
        }
    }
    
    return buildMesh(filteredBoxes);
}

// PPM image writing
static void writePPM(const char* filename, const std::vector<uint8_t>& pixels, int w, int h) {
    FILE* f = std::fopen(filename, "wb");
    if (!f) { std::fprintf(stderr, "Failed to open %s\n", filename); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(pixels.data(), 1, pixels.size(), f);
    std::fclose(f);
    std::printf("  Wrote %s (%dx%d)\n", filename, w, h);
}

// Color census in lower half of screen
static void colorCensus(const std::vector<uint8_t>& pixels, int w, int h, 
                        int& greenCount, int& grayCount, int& brownCount, int& totalCount) {
    greenCount = grayCount = brownCount = totalCount = 0;
    int startY = h / 2;  // lower half
    for (int y = startY; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * 4;
            float r = pixels[idx] / 255.0f;
            float g = pixels[idx+1] / 255.0f;
            float b = pixels[idx+2] / 255.0f;
            
            // Classify
            float maxC = std::max({r, g, b});
            float minC = std::min({r, g, b});
            float saturation = maxC > 0.001f ? (maxC - minC) / maxC : 0.0f;
            float hue = 0.0f;
            if (maxC != minC) {
                if (maxC == r) hue = 60.0f * fmodf((g - b) / (maxC - minC), 6.0f);
                else if (maxC == g) hue = 60.0f * ((b - r) / (maxC - minC) + 2.0f);
                else hue = 60.0f * ((r - g) / (maxC - minC) + 4.0f);
                if (hue < 0) hue += 360.0f;
            }
            
            // Green-ish: hue 80-160, saturation > 0.15
            // Brown-ish: hue 20-50, saturation > 0.2, value < 0.7
            // Gray-ish: saturation < 0.2
            if (saturation < 0.2f) {
                grayCount++;
            } else if (hue >= 80.0f && hue <= 160.0f && saturation > 0.15f) {
                greenCount++;
            } else if (hue >= 20.0f && hue <= 50.0f && saturation > 0.2f && maxC < 0.7f) {
                brownCount++;
            }
            totalCount++;
        }
    }
}

// ============================================================ static world mesh
static void pushQuad(std::vector<float>& v, V3 a, V3 b, V3 c, V3 d, V3 n, V3 col, float mat) {
    auto vert = [&](V3 p) {
        float f[10] = {p.x, p.y, p.z, n.x, n.y, n.z, col.x, col.y, col.z, mat};
        v.insert(v.end(), f, f + 10);
    };
    vert(a); vert(b); vert(c);
    vert(a); vert(c); vert(d);
}

static void pushTri(std::vector<float>& v, V3 a, V3 b, V3 c, V3 n, V3 col, float mat) {
    auto vert = [&](V3 p) {
        float f[10] = {p.x, p.y, p.z, n.x, n.y, n.z, col.x, col.y, col.z, mat};
        v.insert(v.end(), f, f + 10);
    };
    vert(a); vert(b); vert(c);
}

// 11-float versions for propeller overlay (with propIdx attribute)
static void pushQuad11(std::vector<float>& v, V3 a, V3 b, V3 c, V3 d, V3 n, V3 col, float mat, float propIdx) {
    auto vert = [&](V3 p) {
        float f[11] = {p.x, p.y, p.z, n.x, n.y, n.z, col.x, col.y, col.z, mat, propIdx};
        v.insert(v.end(), f, f + 11);
    };
    vert(a); vert(b); vert(c);
    vert(a); vert(c); vert(d);
}

static void pushTri11(std::vector<float>& v, V3 a, V3 b, V3 c, V3 n, V3 col, float mat, float propIdx) {
    auto vert = [&](V3 p) {
        float f[11] = {p.x, p.y, p.z, n.x, n.y, n.z, col.x, col.y, col.z, mat, propIdx};
        v.insert(v.end(), f, f + 11);
    };
    vert(a); vert(b); vert(c);
}

static std::vector<float> buildMesh(const std::vector<Box>& boxes) {
    std::vector<float> v;
    v.reserve(boxes.size() * 36 * 10);
    for (const Box& b : boxes) {
        const V3 &lo = b.mn, &hi = b.mx;
        float m = (float)b.mat;
        // +X / -X
        pushQuad(v, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}, {1, 0, 0}, b.color, m);
        pushQuad(v, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z}, {lo.x, lo.y, lo.z}, {-1, 0, 0}, b.color, m);
        // +Y / -Y
        pushQuad(v, {lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}, {0, 1, 0}, b.color, m);
        pushQuad(v, {lo.x, lo.y, hi.z}, {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {0, -1, 0}, b.color, m);
        // +Z / -Z
        pushQuad(v, {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}, {0, 0, 1}, b.color, m);
        pushQuad(v, {hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {0, 0, -1}, b.color, m);
    }
    return v;
}

// Build drone mesh from primitives (boxes/cylinders)
// Returns vertex data: pos(3), normal(3), color(3), material(1) = 10 floats per vertex
static std::vector<float> buildDroneMesh(const DroneParams& P) {
    std::vector<float> v;
    v.reserve(500 * 3 * 10); // target under 500 triangles
    
    // Helper lambda to add a box
    auto addBox = [&](const V3& lo, const V3& hi, const V3& color, float mat) {
        pushQuad(v, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}, {1, 0, 0}, color, mat);
        pushQuad(v, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z}, {lo.x, lo.y, lo.z}, {-1, 0, 0}, color, mat);
        pushQuad(v, {lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}, {0, 1, 0}, color, mat);
        pushQuad(v, {lo.x, lo.y, hi.z}, {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {0, -1, 0}, color, mat);
        pushQuad(v, {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}, {0, 0, 1}, color, mat);
        pushQuad(v, {hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {0, 0, -1}, color, mat);
    };
    
    // Helper lambda to add a cylinder (approximated as 8-sided prism)
    auto addCylinder = [&](const V3& base, float radius, float height, const V3& color, float mat, const V3& axis) {
        const int sides = 8;
        V3 up = axis;
        V3 right, forward;
        if (std::fabs(up.y) > 0.9f) {
            right = {1, 0, 0};
            forward = {0, 0, 1};
        } else {
            right = normalize3(cross({0, 1, 0}, up));
            forward = cross(up, right);
        }
        
        std::vector<V3> bottomVerts, topVerts;
        for (int i = 0; i < sides; ++i) {
            float a = 2.f * PI * i / sides;
            V3 offset = right * (radius * std::cos(a)) + forward * (radius * std::sin(a));
            bottomVerts.push_back(base + offset);
            topVerts.push_back(base + up * height + offset);
        }
        
        // Side quads
        for (int i = 0; i < sides; ++i) {
            int ni = (i + 1) % sides;
            V3 b0 = bottomVerts[i], b1 = bottomVerts[ni];
            V3 t0 = topVerts[i], t1 = topVerts[ni];
            V3 normal = normalize3(cross(t1 - b1, b0 - b1));
            pushQuad(v, b0, b1, t1, t0, normal, color, mat);
        }
        
        // Bottom cap
        for (int i = 1; i < sides - 1; ++i) {
            V3 normal = {-up.x, -up.y, -up.z};
            pushTri(v, bottomVerts[0], bottomVerts[i+1], bottomVerts[i], normal, color, mat);
        }
        
        // Top cap
        for (int i = 1; i < sides - 1; ++i) {
            pushTri(v, topVerts[0], topVerts[i], topVerts[i+1], up, color, mat);
        }
    };
    
    // Helper to add a thin disc (for propellers)
    auto addDisc = [&](const V3& center, float radius, const V3& normal, const V3& color, float mat) {
        const int sides = 6;
        V3 up = normal;
        V3 right, forward;
        if (std::fabs(up.y) > 0.9f) {
            right = {1, 0, 0};
            forward = {0, 0, 1};
        } else {
            right = normalize3(cross({0, 1, 0}, up));
            forward = cross(up, right);
        }
        
        // Two triangles for a thin disc (top and bottom)
        for (int pass = 0; pass < 2; ++pass) {
            V3 n = pass == 0 ? up : V3{-up.x, -up.y, -up.z};
            for (int i = 1; i < sides - 1; ++i) {
                float a0 = 2.f * PI * i / sides;
                float a1 = 2.f * PI * (i + 1) / sides;
                V3 v0 = center + right * (radius * std::cos(2.f * PI * 0 / sides)) + forward * (radius * std::sin(2.f * PI * 0 / sides));
                V3 v1 = center + right * (radius * std::cos(a0)) + forward * (radius * std::sin(a0));
                V3 v2 = center + right * (radius * std::cos(a1)) + forward * (radius * std::sin(a1));
                pushTri(v, v0, v1, v2, n, color, mat);
            }
        }
    };
    
    // Helper to add an oriented box (for arms at 45 degrees)
    auto addOrientedBox = [&](const V3& center, const V3& halfExtents, const V3& xAxis, const V3& yAxis, const V3& zAxis, const V3& color, float mat) {
        // 8 corners of the box
        V3 corners[8];
        for (int i = 0; i < 8; ++i) {
            float sx = (i & 1) ? halfExtents.x : -halfExtents.x;
            float sy = (i & 2) ? halfExtents.y : -halfExtents.y;
            float sz = (i & 4) ? halfExtents.z : -halfExtents.z;
            corners[i] = center + xAxis * sx + yAxis * sy + zAxis * sz;
        }
        // 6 faces, each as 2 triangles
        // +X face: corners 1,3,7,5
        pushQuad(v, corners[1], corners[3], corners[7], corners[5], xAxis, color, mat);
        // -X face: corners 0,2,6,4
        pushQuad(v, corners[0], corners[4], corners[6], corners[2], V3{-xAxis.x, -xAxis.y, -xAxis.z}, color, mat);
        // +Y face: corners 2,3,7,6
        pushQuad(v, corners[2], corners[3], corners[7], corners[6], yAxis, color, mat);
        // -Y face: corners 0,1,5,4
        pushQuad(v, corners[0], corners[4], corners[5], corners[1], V3{-yAxis.x, -yAxis.y, -yAxis.z}, color, mat);
        // +Z face: corners 4,5,7,6
        pushQuad(v, corners[4], corners[5], corners[7], corners[6], zAxis, color, mat);
        // -Z face: corners 0,1,3,2
        pushQuad(v, corners[0], corners[2], corners[3], corners[1], V3{-zAxis.x, -zAxis.y, -zAxis.z}, color, mat);
    };
    
    // Colors
    V3 bodyColor = {0.15f, 0.15f, 0.18f};
    V3 armColor = {0.20f, 0.20f, 0.22f};
    V3 motorColor = {0.10f, 0.10f, 0.12f};
    V3 propColor = {0.05f, 0.05f, 0.07f};
    V3 cameraColor = {0.08f, 0.08f, 0.10f};
    V3 batteryColor = {0.12f, 0.12f, 0.14f};
    float mat = 1.0f; // standard material
    
    // Sizes based on drone preset (5" or 3.5")
    float armLen = P.armLength; // distance from center to motor
    float armCoord = armLen * 0.70710678f; // X/Z coordinate of motors
    float bodyWidth = armLen * 0.35f;
    float bodyDepth = armLen * 0.25f;
    float bodyHeight = 0.025f;
    float armWidth = armLen * 0.10f;
    float armHeight = 0.010f;
    float armLength3D = armLen * 0.95f; // arm length from center to near motor
    float motorRadius = armLen * 0.06f;
    float motorHeight = 0.02f;
    float propRadius = armLen * 0.45f;
    float cameraSize = armLen * 0.15f;
    float batteryWidth = bodyWidth * 0.7f;
    float batteryDepth = bodyDepth * 0.5f;
    float batteryHeight = 0.03f;
    
    // Central body (flat box)
    addBox({-bodyWidth, -bodyHeight * 0.5f, -bodyDepth},
           {bodyWidth, bodyHeight * 0.5f, bodyDepth}, bodyColor, mat);
    
    // Battery on top
    addBox({-batteryWidth, bodyHeight * 0.5f, -batteryDepth},
           {batteryWidth, bodyHeight * 0.5f + batteryHeight, batteryDepth}, batteryColor, mat);
    
    // 4 arms in X layout - oriented along diagonals
    // Arm direction vectors (from center to motor)
    V3 armDirs[4] = {
        { armCoord, 0, -armCoord }, // front-right
        {-armCoord, 0, -armCoord }, // front-left
        {-armCoord, 0,  armCoord }, // back-left
        { armCoord, 0,  armCoord }, // back-right
    };
    
    for (int i = 0; i < 4; ++i) {
        V3 dir = normalize3(armDirs[i]);
        V3 up = {0, 1, 0};
        V3 right = normalize3(cross(up, dir));
        V3 center = dir * (armLength3D * 0.5f);
        V3 halfExtents = {armLength3D * 0.5f, armHeight * 0.5f, armWidth * 0.5f};
        addOrientedBox(center, halfExtents, dir, up, right, armColor, mat);
    }
    
    // 4 motors at arm ends (cylinders pointing up)
    for (int i = 0; i < 4; ++i) {
        V3 motorPos = armDirs[i];
        motorPos.y = motorHeight * 0.5f;
        addCylinder(motorPos - V3{0, motorHeight * 0.5f, 0}, motorRadius, motorHeight, motorColor, mat, {0, 1, 0});
    }
    
    // 4 propellers at motor positions (thin discs, horizontal)
    for (int i = 0; i < 4; ++i) {
        V3 propPos = armDirs[i];
        propPos.y = motorHeight + 0.002f; // just above motor
        addDisc(propPos, propRadius, {0, 1, 0}, propColor, mat);
    }
    
    // Camera at front (small box tilted with camera)
    // Front is -Z direction
    V3 camPos = {0, bodyHeight * 0.5f + cameraSize * 0.5f, -bodyDepth - cameraSize * 0.5f};
    addBox({-cameraSize * 0.5f, camPos.y - cameraSize * 0.5f, camPos.z - cameraSize * 0.5f},
           {cameraSize * 0.5f, camPos.y + cameraSize * 0.5f, camPos.z + cameraSize * 0.5f}, cameraColor, mat);
    
    return v;
}

// Build propeller overlay mesh (only parts in front of camera: props, motors, front arms)
// Returns vertex data: pos(3), normal(3), color(3), material(1), propIndex(1) = 11 floats per vertex
// propIndex: 0-3 for each prop, 4 for motors, 5 for front arms, -1 for other
static std::vector<float> buildPropOverlayMesh(const DroneParams& P) {
    std::vector<float> v;
    v.reserve(300 * 3 * 11); // target under 300 triangles
    
    // Helper lambda to add a cylinder (approximated as 8-sided prism)
    auto addCylinder = [&](const V3& base, float radius, float height, const V3& color, float mat, const V3& axis, float propIdx) {
        const int sides = 8;
        V3 up = axis;
        V3 right, forward;
        if (std::fabs(up.y) > 0.9f) {
            right = {1, 0, 0};
            forward = {0, 0, 1};
        } else {
            right = normalize3(cross({0, 1, 0}, up));
            forward = cross(up, right);
        }
        
        std::vector<V3> bottomVerts, topVerts;
        for (int i = 0; i < sides; ++i) {
            float a = 2.f * PI * i / sides;
            V3 offset = right * (radius * std::cos(a)) + forward * (radius * std::sin(a));
            bottomVerts.push_back(base + offset);
            topVerts.push_back(base + up * height + offset);
        }
        
        // Side quads
        for (int i = 0; i < sides; ++i) {
            int ni = (i + 1) % sides;
            V3 b0 = bottomVerts[i], b1 = bottomVerts[ni];
            V3 t0 = topVerts[i], t1 = topVerts[ni];
            V3 normal = normalize3(cross(t1 - b1, b0 - b1));
            pushQuad11(v, b0, b1, t1, t0, normal, color, mat, propIdx);
        }
        
        // Bottom cap
        for (int i = 1; i < sides - 1; ++i) {
            V3 normal = {-up.x, -up.y, -up.z};
            pushTri11(v, bottomVerts[0], bottomVerts[i+1], bottomVerts[i], normal, color, mat, propIdx);
        }
        
        // Top cap
        for (int i = 1; i < sides - 1; ++i) {
            pushTri11(v, topVerts[0], topVerts[i], topVerts[i+1], up, color, mat, propIdx);
        }
    };
    
    // Helper to add a thin disc (for propellers) with 2 or 3 blades
    auto addPropDisc = [&](const V3& center, float radius, const V3& normal, const V3& color, float mat, float propIdx, int blades = 3) {
        V3 up = normal;
        V3 right, forward;
        if (std::fabs(up.y) > 0.9f) {
            right = {1, 0, 0};
            forward = {0, 0, 1};
        } else {
            right = normalize3(cross({0, 1, 0}, up));
            forward = cross(up, right);
        }
        
        // Create individual blades as thin triangles
        for (int b = 0; b < blades; ++b) {
            float bladeAngle = 2.f * PI * b / blades;
            V3 bladeDir = right * std::cos(bladeAngle) + forward * std::sin(bladeAngle);
            V3 bladePerp = right * (-std::sin(bladeAngle)) + forward * std::cos(bladeAngle);
            
            // Blade dimensions: length = radius, width at root = radius * 0.25, width at tip = radius * 0.1
            float bladeLen = radius;
            float rootWidth = radius * 0.25f;
            float tipWidth = radius * 0.1f;
            
            V3 rootL = center - bladePerp * rootWidth;
            V3 rootR = center + bladePerp * rootWidth;
            V3 tipL = center + bladeDir * bladeLen - bladePerp * tipWidth;
            V3 tipR = center + bladeDir * bladeLen + bladePerp * tipWidth;
            
            // Two triangles per blade (top and bottom)
            pushQuad11(v, rootL, rootR, tipR, tipL, up, color, mat, propIdx);
            pushQuad11(v, rootL, rootR, tipR, tipL, V3{-up.x, -up.y, -up.z}, color, mat, propIdx);
        }
        
        // Hub (small disc at center)
        float hubRadius = radius * 0.2f;
        const int hubSides = 6;
        for (int pass = 0; pass < 2; ++pass) {
            V3 n = pass == 0 ? up : V3{-up.x, -up.y, -up.z};
            for (int i = 1; i < hubSides - 1; ++i) {
                float a0 = 2.f * PI * i / hubSides;
                float a1 = 2.f * PI * (i + 1) / hubSides;
                V3 v0 = center + right * (hubRadius * std::cos(0)) + forward * (hubRadius * std::sin(0));
                V3 v1 = center + right * (hubRadius * std::cos(a0)) + forward * (hubRadius * std::sin(a0));
                V3 v2 = center + right * (hubRadius * std::cos(a1)) + forward * (hubRadius * std::sin(a1));
                pushTri11(v, v0, v1, v2, n, color, mat, propIdx);
            }
        }
    };
    
    // Helper to add an oriented box (for arms at 45 degrees)
    auto addOrientedBox = [&](const V3& center, const V3& halfExtents, const V3& xAxis, const V3& yAxis, const V3& zAxis, const V3& color, float mat, float propIdx) {
        V3 corners[8];
        for (int i = 0; i < 8; ++i) {
            float sx = (i & 1) ? halfExtents.x : -halfExtents.x;
            float sy = (i & 2) ? halfExtents.y : -halfExtents.y;
            float sz = (i & 4) ? halfExtents.z : -halfExtents.z;
            corners[i] = center + xAxis * sx + yAxis * sy + zAxis * sz;
        }
        pushQuad11(v, corners[1], corners[3], corners[7], corners[5], xAxis, color, mat, propIdx);
        pushQuad11(v, corners[0], corners[4], corners[6], corners[2], V3{-xAxis.x, -xAxis.y, -xAxis.z}, color, mat, propIdx);
        pushQuad11(v, corners[2], corners[3], corners[7], corners[6], yAxis, color, mat, propIdx);
        pushQuad11(v, corners[0], corners[4], corners[5], corners[1], V3{-yAxis.x, -yAxis.y, -yAxis.z}, color, mat, propIdx);
        pushQuad11(v, corners[4], corners[5], corners[7], corners[6], zAxis, color, mat, propIdx);
        pushQuad11(v, corners[0], corners[2], corners[3], corners[1], V3{-zAxis.x, -zAxis.y, -zAxis.z}, color, mat, propIdx);
    };
    
    // Colors
    V3 armColor = {0.20f, 0.20f, 0.22f};
    V3 motorColor = {0.10f, 0.10f, 0.12f};
    V3 propColor = {0.05f, 0.05f, 0.07f};
    float mat = 1.0f;
    
    // Sizes based on drone preset (5" or 3.5")
    float armLen = P.armLength;
    float armCoord = armLen * 0.70710678f;
    float bodyDepth = armLen * 0.25f;
    float bodyHeight = 0.025f;
    float armWidth = armLen * 0.10f;
    float armHeight = 0.010f;
    float armLength3D = armLen * 0.95f;
    float motorRadius = armLen * 0.06f;
    float motorHeight = 0.02f;
    float propRadius = armLen * 0.45f;
    float cameraSize = armLen * 0.15f;
    
    // Camera position in drone-local frame - MATCH MAIN CAMERA: {0, 0.03f, -0.07f}
    V3 camLocalPos = {0, 0.03f, -0.07f};
    
    // 4 arms in X layout - front two arms are indices 0, 1 (front-right, front-left)
    V3 armDirs[4] = {
        { armCoord, 0, -armCoord }, // front-right (index 0)
        {-armCoord, 0, -armCoord }, // front-left (index 1)
        {-armCoord, 0,  armCoord }, // back-left (index 2)
        { armCoord, 0,  armCoord }, // back-right (index 3)
    };
    
    // Ensure front props are IN FRONT of camera (more negative Z) for visibility
    // Camera is at camLocalPos.z = -0.07. Front props should be at z < -0.07.
    // Use actual arm Z or 3cm in front of camera, whichever is more forward.
    float propZ = std::min(-armCoord, camLocalPos.z - 0.03f); // 3cm in front of camera
    
    // Add front arm portions (only front two arms: indices 0, 1)
    // Extend arms to propZ position
    for (int i = 0; i < 2; ++i) {
        V3 dir = normalize3({armDirs[i].x, 0, propZ}); // direction to prop position
        V3 up = {0, 1, 0};
        V3 right = normalize3(cross(up, dir));
        float armLenToProp = std::sqrt(armDirs[i].x * armDirs[i].x + propZ * propZ);
        V3 center = {armDirs[i].x * 0.5f, 0, propZ * 0.5f};
        V3 halfExtents = {armLenToProp * 0.5f, armHeight * 0.5f, armWidth * 0.5f};
        addOrientedBox(center, halfExtents, dir, up, right, armColor, mat, 5.f); // propIdx = 5 for front arms
    }
    
    // 4 motors at arm ends - only front two (indices 0, 1)
    for (int i = 0; i < 2; ++i) {
        V3 motorPos = {armDirs[i].x, motorHeight * 0.5f, propZ};
        addCylinder(motorPos - V3{0, motorHeight * 0.5f, 0}, motorRadius, motorHeight, motorColor, mat, {0, 1, 0}, 4.f); // propIdx = 4 for motors
    }
    
    // 4 propellers at motor positions - only front two (indices 0, 1)
    for (int i = 0; i < 2; ++i) {
        V3 propPos = {armDirs[i].x, motorHeight + 0.002f, propZ};
        addPropDisc(propPos, propRadius, {0, 1, 0}, propColor, mat, (float)i, 3); // 3 blades per prop
    }
    
    return v;
}

// Raycast downward from a point to find the nearest surface in the world
// Returns true if hit, sets outHitY to the Y coordinate of the hit surface,
// and outNormal to the surface normal
static bool raycastDown(const V3& origin, const std::vector<Box>& world, const std::vector<ThinCylinder>& thinCylinders, float maxDist, float& outHitY, V3& outNormal) {
    float bestT = maxDist;
    bool hit = false;
    V3 rayDir = {0, -1, 0}; // straight down
    
    // Check boxes
    for (const Box& b : world) {
        // Ray-box intersection (slab method)
        // Ray: origin + t * rayDir, t >= 0
        float tmin = -1e30f, tmax = 1e30f;
        
        // X slab
        if (rayDir.x != 0) {
            float tx1 = (b.mn.x - origin.x) / rayDir.x;
            float tx2 = (b.mx.x - origin.x) / rayDir.x;
            if (tx1 > tx2) std::swap(tx1, tx2);
            tmin = std::max(tmin, tx1);
            tmax = std::min(tmax, tx2);
        } else if (origin.x < b.mn.x || origin.x > b.mx.x) {
            continue; // ray parallel to slab and outside
        }
        
        // Y slab
        if (rayDir.y != 0) {
            float ty1 = (b.mn.y - origin.y) / rayDir.y;
            float ty2 = (b.mx.y - origin.y) / rayDir.y;
            if (ty1 > ty2) std::swap(ty1, ty2);
            tmin = std::max(tmin, ty1);
            tmax = std::min(tmax, ty2);
        } else if (origin.y < b.mn.y || origin.y > b.mx.y) {
            continue;
        }
        
        // Z slab
        if (rayDir.z != 0) {
            float tz1 = (b.mn.z - origin.z) / rayDir.z;
            float tz2 = (b.mx.z - origin.z) / rayDir.z;
            if (tz1 > tz2) std::swap(tz1, tz2);
            tmin = std::max(tmin, tz1);
            tmax = std::min(tmax, tz2);
        } else if (origin.z < b.mn.z || origin.z > b.mx.z) {
            continue;
        }
        
        if (tmin <= tmax && tmax >= 0 && tmin < bestT) {
            // Hit! The entry point is at tmin (or tmax if origin inside)
            float t = tmin >= 0 ? tmin : tmax;
            if (t >= 0 && t < bestT) {
                bestT = t;
                hit = true;
                // For downward ray, we hit the top face (y = b.mx.y)
                outHitY = b.mx.y;
                outNormal = {0, 1, 0}; // top face normal
            }
        }
    }
    
    // Check thin cylinders (wires, rebar, pipes)
    for (const ThinCylinder& tc : thinCylinders) {
        // For vertical downward ray, check if ray passes through cylinder horizontally
        // Cylinder axis is line segment from tc.p1 to tc.p2
        // Ray is at (origin.x, origin.z) going down
        
        // Find the point on cylinder axis closest to ray's x-z position
        V3 a = tc.p1;
        V3 b = tc.p2;
        V3 ab = {b.x - a.x, b.y - a.y, b.z - a.z};
        float len2 = ab.x*ab.x + ab.y*ab.y + ab.z*ab.z;
        
        if (len2 > 1e-8f) {
            // Project ray's x-z onto cylinder axis in x-z plane
            float t = clampf(((origin.x - a.x)*ab.x + (origin.z - a.z)*ab.z) / len2, 0.f, 1.f);
            // The y coordinate at this point on the axis
            float axisY = a.y + ab.y * t;
            // Horizontal distance from ray to axis at this point
            float dx = origin.x - (a.x + ab.x * t);
            float dz = origin.z - (a.z + ab.z * t);
            float horizDist = std::sqrt(dx*dx + dz*dz);
            
            if (horizDist <= tc.radius && axisY < origin.y && (origin.y - axisY) < bestT) {
                // Ray hits the cylinder
                bestT = origin.y - axisY;
                hit = true;
                outHitY = axisY;
                // Normal points horizontally away from cylinder axis
                float invDist = horizDist > 1e-6f ? 1.f / horizDist : 0.f;
                outNormal = {dx * invDist, 0, dz * invDist};
            }
        }
    }
    
    return hit;
}

// VideoSignalState is updated every frame; kept as a persistent variable in main().
// The old videoSignalLoss() stub is replaced by the video.hpp system.

// ============================================================ 5x7 bitmap font for the OSD
static const char* FONT_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:.-+%/()!=,_ ";
static const unsigned char FONT[][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},  // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},  // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},  // 9
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // A
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},  // B
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},  // C
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},  // D
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},  // E
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},  // F
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},  // G
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // H
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},  // I
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},  // J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},  // K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},  // L
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},  // M
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},  // N
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // O
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},  // P
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},  // Q
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},  // R
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},  // S
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},  // T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // U
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},  // V
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},  // W
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},  // X
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},  // Y
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},  // Z
    {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00},  // :
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C},  // .
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},  // -
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},  // +
    {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03},  // %
    {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10},  // /
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02},  // (
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08},  // )
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04},  // !
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00},  // =
    {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08},  // ,
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F},  // _
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // space
};

static int glyphIndex(char ch) {
    ch = (char)std::toupper((unsigned char)ch);
    const char* p = std::strchr(FONT_CHARS, ch);
    if (!p || ch == 0) return (int)std::strlen(FONT_CHARS) - 1;
    return (int)(p - FONT_CHARS);
}

static void fontTest() {
    for (size_t i = 0; i < std::strlen(FONT_CHARS); ++i) {
        std::printf("'%c'\n", FONT_CHARS[i]);
        for (int r = 0; r < 7; ++r) {
            for (int c = 4; c >= 0; --c) std::printf("%s", (FONT[i][r] >> c) & 1 ? "#" : ".");
            std::printf("\n");
        }
    }
}

// ============================================================ 2D batch (OSD)
struct Batch {
    std::vector<float> v;
    void rect(float x, float y, float w, float h, float r, float g, float b, float a) {
        float q[6][6] = {{x, y, r, g, b, a},         {x + w, y, r, g, b, a}, {x + w, y + h, r, g, b, a},
                         {x, y, r, g, b, a},         {x + w, y + h, r, g, b, a}, {x, y + h, r, g, b, a}};
        for (auto& p : q) v.insert(v.end(), p, p + 6);
    }
    void triangle(float ax, float ay, float bx, float by, float cx, float cy,
                  float r, float g, float b, float a) {
        const float q[3][6] = {{ax, ay, r, g, b, a}, {bx, by, r, g, b, a}, {cx, cy, r, g, b, a}};
        for (const auto& p : q) v.insert(v.end(), p, p + 6);
    }
    void line(float x0, float y0, float x1, float y1, float width,
              float r, float g, float b, float a) {
        float dx = x1 - x0, dy = y1 - y0;
        float l = std::sqrt(dx * dx + dy * dy);
        if (l < 1e-4f) return;
        float nx = -dy * width * 0.5f / l, ny = dx * width * 0.5f / l;
        triangle(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, r, g, b, a);
        triangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, r, g, b, a);
    }
    void circle(float cx, float cy, float radius, float r, float g, float b, float a) {
        constexpr int sides = 16;
        for (int i = 0; i < sides; ++i) {
            float a0 = 2.f * PI * i / sides, a1 = 2.f * PI * (i + 1) / sides;
            triangle(cx, cy, cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
                     cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, r, g, b, a);
        }
    }
    static float textWidth(const std::string& s, float sc) { return s.size() * 6.f * sc - sc; }
    void text(float x, float y, float sc, const std::string& s, float r, float g, float b, float a = 1.f) {
        for (int pass = 0; pass < 2; ++pass) {
            float ox = pass == 0 ? sc : 0, oy = pass == 0 ? sc : 0;  // drop shadow first
            float cx = x;
            for (char ch : s) {
                const unsigned char* gph = FONT[glyphIndex(ch)];
                for (int row = 0; row < 7; ++row) {
                    int bits = gph[row], col = 0;
                    while (col < 5) {  // merge horizontal runs
                        if (!((bits >> (4 - col)) & 1)) { ++col; continue; }
                        int start = col;
                        while (col < 5 && ((bits >> (4 - col)) & 1)) ++col;
                        if (pass == 0) rect(cx + ox + start * sc, y + oy + row * sc, (col - start) * sc, sc, 0, 0, 0, 0.75f * a);
                        else rect(cx + start * sc, y + row * sc, (col - start) * sc, sc, r, g, b, a);
                    }
                }
                cx += 6.f * sc;
            }
        }
    }
    void textRight(float xr, float y, float sc, const std::string& s, float r, float g, float b, float a = 1.f) {
        text(xr - textWidth(s, sc), y, sc, s, r, g, b, a);
    }
    void textCenter(float xc, float y, float sc, const std::string& s, float r, float g, float b, float a = 1.f) {
        text(xc - textWidth(s, sc) * 0.5f, y, sc, s, r, g, b, a);
    }
};

static const char* SETTING_TABS[] = {"QUALITY", "DISPLAY", "CAMERA", "RADIO", "RATES", "DRONE", "BATTERY", "SOUND", "VIDEO", "LANDING", "WIND", "SHADOW"};
static constexpr int SETTING_TAB_COUNT = 12;

static int settingTab(const Entry& e) {
    std::string k = e.key;
    if (k == "quality" || k == "abandoned_base_seed") return 0;
    if (k == "mute_audio") return 7;
    if (k.find("video_") == 0) return 8;
    if (k.find("wind_") == 0) return 10;
    if (k.find("shadow") == 0 || k.find("shadows_") == 0) return 11;
    if (k == "landing_assist" || k == "ground_damping" || k == "collider_scale" ||
        k == "contact_skin_m" || k == "show_hitbox" || k == "straighten_yaw_on_landing" ||
        k == "landing_snap") return 9;
    if (k.find("window_") == 0 || k == "fullscreen" || k == "vsync" || k == "msaa" || k == "fog_density") return 1;
    if (k.find("cam_") == 0 || k.find("fov_") == 0 || k == "props_in_view" || k == "props_opacity" || k == "props_lowering") return 2;
    if (k.find("axis_") == 0 || k.find("invert_") == 0 || k == "joystick_name_contains" ||
        k == "deadband" || k == "arm_threshold") return 3;
    if (k.find("_rate") != std::string::npos || k.find("_expo") != std::string::npos) return 4;
    if (k.find("battery_") == 0) return 6;
    return 5;
}

static std::string settingLabel(const char* key) {
    std::string s = key;
    if (s == "mass_kg") return "MASS KG";
    if (s == "arm_length_m") return "MOTOR ARM LENGTH M";
    if (s == "thrust_to_weight") return "THRUST / WEIGHT";
    if (s == "throttle_curve") return "THROTTLE CURVE";
    if (s == "motor_lag_s") return "MOTOR RESPONSE S";
    if (s == "rate_lag_s") return "RATE RESPONSE S";
    if (s == "yaw_lag_s") return "YAW RESPONSE S";
    if (s == "prop_pitch_speed") return "PROP PITCH SPEED";
    if (s == "fog_density") return "FOG DENSITY";
    if (s == "abandoned_base_seed") return "ABANDONED BASE SEED";
    if (s == "cam_tilt_deg") return "CAMERA TILT DEG";
    if (s == "fov_horizontal_deg") return "HORIZONTAL FOV";
    if (s == "props_in_view") return "PROPS IN VIEW";
    if (s == "props_opacity") return "PROPS OPACITY";
    if (s == "props_lowering") return "PROPS LOWERING";
    if (s == "video_enabled")              return "ANALOG VIDEO EFFECTS";
    if (s == "video_noise")                return "GRAIN / SNOW";
    if (s == "video_snow_strength")        return "SNOW STRENGTH";
    if (s == "video_scanlines")            return "SCANLINES";
    if (s == "video_chromatic_aberration") return "RGB CHANNEL SPLIT";
    if (s == "video_dynamic_breakup")      return "SIGNAL BREAKUP";
    if (s == "video_occlusion_enabled")    return "OBSTACLE SIGNAL LOSS";
    if (s == "video_occlusion_sensitivity")return "OCCLUSION SENSITIVITY";
    if (s == "video_close_wall_enabled")   return "CLOSE WALL INTERFERENCE";
    if (s == "video_close_wall_sensitivity")return "WALL INTERFERENCE SENSITIVITY";
    if (s == "video_distance_loss")        return "DISTANCE SIGNAL LOSS";
    if (s == "video_low_battery_interference") return "LOW BATTERY INTERFERENCE";
    if (s == "video_speed_interference")   return "HIGH SPEED INTERFERENCE";
    if (s == "video_rolling_bar")          return "ROLLING SYNC BAR";
    if (s == "video_performance_mode")     return "EFFECT PERFORMANCE MODE";
    if (s == "video_show_rssi")            return "SHOW RSSI / LINK QUALITY";
    if (s == "video_noise_intensity")      return "NOISE INTENSITY";
    if (s == "video_static_intensity")     return "STATIC INTENSITY";
    if (s == "video_color_glitch")         return "COLOR GLITCH INTENSITY";
    if (s == "video_scanline_intensity")   return "SCANLINE INTENSITY";
    if (s == "video_tear_intensity")       return "TEARING INTENSITY";
    if (s == "video_dropout_freq")         return "DROPOUT FREQUENCY";
    if (s == "video_dot_crawl")            return "NTSC DOT-CRAWL";
    if (s == "video_sync_loss_effect")     return "SYNC LOSS WAVES";
    if (s == "video_color_burst_effect")   return "COLOR BURST ERRORS";
    if (s == "video_multipath_ghost")      return "MULTIPATH GHOST IMAGE";
    if (s == "landing_assist") return "LANDING ASSIST";
    if (s == "ground_damping") return "GROUND DAMPING";
    if (s == "collider_scale") return "HITBOX SCALE";
    if (s == "contact_skin_m") return "CONTACT SKIN WIDTH M";
    if (s == "show_hitbox") return "SHOW DEBUG HITBOX";
    if (s == "straighten_yaw_on_landing") return "STRAIGHTEN YAW ON LANDING";
    if (s == "landing_snap") return "LANDING SNAP";
    if (s == "wind_enabled") return "WIND";
    if (s == "wind_strength") return "WIND STRENGTH (0-100)";
    if (s == "wind_direction_deg") return "WIND DIRECTION DEG";
    if (s == "wind_gust_strength") return "GUST STRENGTH (0-100)";
    if (s == "wind_turbulence") return "TURBULENCE (0-100)";
    if (s == "wind_hud") return "WIND HUD INDICATOR";
    if (s == "shadows_enabled") return "REAL-TIME SHADOWS";
    if (s == "shadow_resolution") return "SHADOW MAP RESOLUTION";
    if (s == "shadow_distance_m") return "SHADOW DISTANCE M";
    if (s == "shadow_fade_distance_m") return "SHADOW FADE DISTANCE M";
    if (s == "shadow_max_altitude_m") return "MAX SHADOW ALTITUDE M";
    if (s == "shadow_strength") return "SHADOW STRENGTH";
    if (s == "shadow_bias") return "SHADOW BIAS";
    if (s == "shadow_normal_offset_m") return "SHADOW NORMAL OFFSET M";
    if (s == "shadow_filter") return "SHADOW FILTERING";
    if (s == "shadow_stable_snapping") return "STABLE SHADOW SNAPPING";
    if (s == "shadow_debug") return "SHADOW DEBUG HUD";
    if (s == "drone_shadow") return "DRONE SHADOW";
    if (s == "joystick_name_contains") return "JOYSTICK NAME FILTER";
    if (s == "battery_cells") return "BATTERY CELLS";
    if (s == "battery_mah") return "BATTERY CAPACITY MAH";
    for (char& c : s) c = c == '_' ? ' ' : (char)std::toupper((unsigned char)c);
    return s;
}

static std::string settingValue(const Entry& e) {
    char b[64];
    if (std::string(e.key) == "drone_size") {
        static const char* names[] = {"75 MM", "85 MM", "3 INCH", "3.5 INCH", "5 INCH"};
        int size = *(int*)e.ptr;
        return size < 0 || size > 4 ? "CUSTOM" : names[size];
    }
    if (std::string(e.key) == "quality") {
        static const char* names[] = {"LOW", "MEDIUM", "HIGH"};
        int q = std::max(0, std::min(2, *(int*)e.ptr));
        return names[q];
    }
    if (std::string(e.key) == "landing_assist") {
        static const char* names[] = {"OFF", "LOW", "MEDIUM", "HIGH"};
        int mode = std::max(0, std::min(3, *(int*)e.ptr));
        return names[mode];
    }
    if (std::string(e.key) == "shadow_resolution") {
        int resolution = *(int*)e.ptr;
        std::snprintf(b, sizeof b, "%d", resolution);
        return b;
    }
    if (std::string(e.key) == "shadow_bias") {
        std::snprintf(b, sizeof b, "%.5f", *(float*)e.ptr);
        return b;
    }
    if (std::string(e.key) == "shadow_filter") {
        static const char* names[] = {"OFF", "2X2", "3X3"};
        int filter = std::max(0, std::min(2, *(int*)e.ptr));
        return names[filter];
    }
    if (std::string(e.key) == "video_performance_mode") {
        static const char* names[] = {"FULL", "FAST", "ULTRA LOW"};
        int m = std::max(0, std::min(2, *(int*)e.ptr));
        return names[m];
    }
    switch (e.type) {
        case Entry::INT:
            std::snprintf(b, sizeof b, "%d", *(int*)e.ptr);
            return b;
        case Entry::FLOAT:
            std::snprintf(b, sizeof b, "%.3f", *(float*)e.ptr);
            return b;
        case Entry::BOOL:
            return *(bool*)e.ptr ? "ON" : "OFF";
        case Entry::STR:
            return *(std::string*)e.ptr;
    }
    return "";
}

static void applyQuality(Config& c) {
    static const int widths[] = {960, 1280, 1600};
    static const int heights[] = {540, 720, 900};
    static const int msaa[] = {0, 0, 2};
    static const float fog[] = {0.008f, 0.0015f, 0.0005f};
    c.quality = std::max(0, std::min(2, c.quality));
    c.width = widths[c.quality];
    c.height = heights[c.quality];
    c.msaa = msaa[c.quality];
    c.fogDensity = fog[c.quality];
}

static void applyWindConfig(Sim& sim, const Config& c) {
    sim.windEnabled = c.windEnabled;
    sim.windStrength = std::max(0, std::min(100, c.windStrength));
    sim.windDirectionDeg = ((c.windDirectionDeg % 360) + 360) % 360;
    sim.windGustStrength = std::max(0, std::min(100, c.windGustStrength));
    sim.windTurbulence = std::max(0, std::min(100, c.windTurbulence));
}

static void applyDroneProfile(Config& c) {
    DroneParams p = c.drone;
    switch (c.droneSize) {
        case 0:  // 75 mm 1S micro/whoop - ducted props, compact frame
            p.mass = 0.035f; p.thrustRatio = 3.5f; p.cells = 1; p.capacityMah = 450.f;
            p.packResistance = 0.030f; p.radius = 0.055f; p.armLength = 0.045f; p.motorTau = 0.008f; p.maxMotorRPM = 50000.f; p.rateTau = 0.018f;
            p.yawTau = 0.040f; p.propPitchSpeed = 35.f; p.dragK = {0.075f, 0.006f, 0.030f};
            p.dragLin = {0.025f, 0.008f, 0.030f};
            p.colliderScale = 0.90f;  // Ducts make effective collision radius smaller
            break;
        case 1:  // 85 mm 2S micro - open props, slightly larger
            p.mass = 0.065f; p.thrustRatio = 4.5f; p.cells = 2; p.capacityMah = 450.f;
            p.packResistance = 0.035f; p.radius = 0.065f; p.armLength = 0.050f; p.motorTau = 0.010f; p.maxMotorRPM = 45000.f; p.rateTau = 0.020f;
            p.yawTau = 0.045f; p.propPitchSpeed = 45.f; p.dragK = {0.050f, 0.009f, 0.022f};
            p.dragLin = {0.035f, 0.010f, 0.035f};
            p.colliderScale = 0.95f;
            break;
        case 2:  // 3 inch freestyle - standard open frame
            p.mass = 0.180f; p.thrustRatio = 5.5f; p.cells = 4; p.capacityMah = 850.f;
            p.packResistance = 0.040f; p.radius = 0.090f; p.armLength = 0.065f; p.motorTau = 0.014f; p.maxMotorRPM = 38000.f; p.rateTau = 0.021f;
            p.yawTau = 0.048f; p.propPitchSpeed = 58.f; p.dragK = {0.030f, 0.020f, 0.012f};
            p.dragLin = {0.055f, 0.035f, 0.050f};
            p.colliderScale = 1.00f;
            break;
        case 3:  // 3.5 inch, BabyApe II 6S-style baseline
            p.mass = 0.250f; p.thrustRatio = 5.7f; p.cells = 6; p.capacityMah = 650.f;
            p.packResistance = 0.030f; p.radius = 0.100f; p.armLength = 0.075f; p.motorTau = 0.010f; p.maxMotorRPM = 40000.f; p.rateTau = 0.018f;
            p.yawTau = 0.050f; p.propPitchSpeed = 70.f; p.dragK = {0.010f, 0.010f, 0.008f};
            p.dragLin = {0.040f, 0.040f, 0.040f};
            p.colliderScale = 1.00f;
            break;
        case 4:  // standard 5 inch freestyle
            p.mass = 0.650f; p.thrustRatio = 7.0f; p.cells = 6; p.capacityMah = 1300.f;
            p.packResistance = 0.030f; p.radius = 0.120f; p.armLength = 0.105f; p.motorTau = 0.025f; p.maxMotorRPM = 30000.f; p.rateTau = 0.022f;
            p.yawTau = 0.050f; p.propPitchSpeed = 70.f; p.dragK = {0.010f, 0.006f, 0.008f};
            p.dragLin = {0.080f, 0.040f, 0.080f};
            p.colliderScale = 1.00f;
            break;
        default: return;  // Custom leaves every flight-model value untouched.
    }
    p.throttleCurve = 1.7f;
    p.airmode = true;
    c.drone = p;
}

static void markCustomDrone(Config& c, const Entry& e) {
    if ((settingTab(e) == 5 || settingTab(e) == 6) && std::string(e.key) != "drone_size")
        c.droneSize = -1;
}

static void changeSetting(const Entry& e, int direction) {
    if (e.type == Entry::BOOL) {
        *(bool*)e.ptr = !*(bool*)e.ptr;
    } else if (e.type == Entry::INT) {
        int& v = *(int*)e.ptr;
        int step = (std::string(e.key).find("window_") == 0) ? 100 : 1;
        if (std::string(e.key) == "msaa") {
            v = direction > 0 ? (v == 0 ? 2 : 4) : (v == 4 ? 2 : 0);
        } else if (std::string(e.key) == "axis_reset") v = std::max(-1, std::min(15, v + direction));
        else if (std::string(e.key) == "landing_assist") v = std::max(0, std::min(3, v + direction));
        else if (std::string(e.key) == "drone_size") v = std::max(-1, std::min(4, v + direction));
        else if (std::string(e.key) == "quality") v = std::max(0, std::min(2, v + direction));
        else if (std::string(e.key) == "abandoned_base_seed") {
            // Allow full 32-bit range, step by 1 for fine control, larger steps with modifiers would be nice but keep simple
            v = (int)((uint32_t)v + (direction > 0 ? 1u : 0xFFFFFFFFu));
        }
        else if (std::string(e.key) == "shadow_resolution") {
            const int resolutions[] = {512, 1024, 2048};
            int index = v <= 512 ? 0 : (v <= 1024 ? 1 : 2);
            index = (index + (direction > 0 ? 1 : 2)) % 3;
            v = resolutions[index];
        } else if (std::string(e.key) == "shadow_filter") v = std::max(0, std::min(2, v + direction));
        else if (std::string(e.key) == "shadow_distance_m") v = std::max(40, std::min(250, v + direction * 10));
        else if (std::string(e.key) == "shadow_fade_distance_m") v = std::max(5, std::min(100, v + direction * 5));
        else if (std::string(e.key) == "shadow_max_altitude_m") v = std::max(20, std::min(250, v + direction * 10));
        else if (std::string(e.key) == "shadow_strength") v = std::max(0, std::min(100, v + direction * 5));
        else if (std::string(e.key) == "wind_direction_deg") v = (v + direction + 360) % 360;
        else if (std::string(e.key) == "wind_strength" || std::string(e.key) == "wind_gust_strength" ||
                 std::string(e.key) == "wind_turbulence") v = std::max(0, std::min(100, v + direction));
        else if (std::string(e.key) == "window_width") v = std::max(640, std::min(3840, v + direction * step));
        else if (std::string(e.key) == "window_height") v = std::max(480, std::min(2160, v + direction * step));
        else v = std::max(0, std::min(64, v + direction));
    } else if (e.type == Entry::FLOAT) {
        float step = 0.01f, lo = 0.f, hi = 1.f;
        std::string k = e.key;
        if (k.find("rate") != std::string::npos) { step = 10.f; hi = 2000.f; }
        else if (k.find("expo") != std::string::npos || k == "deadband" || k == "arm_threshold") step = 0.01f;
        else if (k == "cam_tilt_deg") { step = 1.f; lo = -10.f; hi = 80.f; }
        else if (k == "fov_horizontal_deg") { step = 5.f; lo = 60.f; hi = 150.f; }
        else if (k == "props_opacity") { step = 1.f; lo = 0.f; hi = 100.f; }
        else if (k == "props_lowering") { step = 5.f; lo = 0.f; hi = 100.f; }
        else if (k == "video_snow_strength")        { step = 0.1f; lo = 0.f; hi = 3.f; }
        else if (k == "video_occlusion_sensitivity"){ step = 0.1f; lo = 0.f; hi = 2.f; }
        else if (k == "video_close_wall_sensitivity"){ step = 0.1f; lo = 0.f; hi = 2.f; }
        else if (k == "video_noise_intensity")      { step = 0.1f; lo = 0.f; hi = 2.f; }
        else if (k == "video_static_intensity")     { step = 0.1f; lo = 0.f; hi = 2.f; }
        else if (k == "video_color_glitch")         { step = 0.1f; lo = 0.f; hi = 2.f; }
        else if (k == "video_scanline_intensity")   { step = 0.1f; lo = 0.f; hi = 2.f; }
        else if (k == "video_tear_intensity")       { step = 0.1f; lo = 0.f; hi = 2.f; }
        else if (k == "video_dropout_freq")         { step = 0.1f; lo = 0.f; hi = 3.f; }
        else if (k == "shadow_bias") { step = 0.00005f; hi = 0.003f; }
        else if (k == "shadow_normal_offset_m") { step = 0.005f; hi = 0.2f; }
        else if (k == "ground_damping") { step = 0.1f; hi = 1.f; }
        else if (k == "collider_scale") { step = 0.05f; lo = 0.75f; hi = 1.25f; }
        else if (k == "contact_skin_m") { step = 0.002f; lo = 0.002f; hi = 0.02f; }
        else if (k == "fog_density") { step = 0.0005f; hi = 0.02f; }
        else if (k == "mass_kg") { step = 0.05f; lo = 0.1f; hi = 10.f; }
        else if (k == "arm_length_m") { step = 0.005f; lo = 0.025f; hi = 0.25f; }
        else if (k == "thrust_to_weight") { step = 0.1f; lo = 1.f; hi = 20.f; }
        else if (k == "battery_mah") { step = 100.f; hi = 10000.f; }
        else if (k.find("drag") != std::string::npos) { step = 0.001f; hi = 1.f; }
        else if (k.find("_s") != std::string::npos) { step = 0.005f; hi = 1.f; }
        else if (k == "prop_pitch_speed") { step = 1.f; hi = 200.f; }
        float& v = *(float*)e.ptr;
        v = clampf(v + direction * step, lo, hi);
    }
}

// ============================================================ radio
struct Radio {
    SDL_Joystick* joy = nullptr;
    SDL_JoystickID id = -1;
    std::string name;
    int naxes = 0;
};

// ============================================================ audio synthesis

struct AudioParams {
    // Drone geometry
    int numBlades = 3;
    int polePairs = 7;           // typical 14-pole motor = 7 pole pairs
    float maxMotorRPM = 30000.f; // max RPM at full thrust (5" ~30k, 3.5" ~40k)
    
    // Harmonic gains (relative to fundamental) - at full RPM
    float harm2Gain = 0.25f;
    float harm3Gain = 0.08f;
    float harm4Gain = 0.04f;
    
    // Harmonic RPM scaling - harmonics grow with RPM (less at idle)
    float harmRpmExponent = 1.5f;
    
    // ESC whine (electrical frequency = RPM/60 * polePairs)
    // More prominent at low RPM (commutation noise)
    float escGain = 0.08f;
    float escHarmGain = 0.03f;
    float escRpmExponent = 0.5f;  // ESC whine relatively louder at low RPM
    
    // Broadband noise (prop wash / turbulence)
    float noiseGain = 0.06f;
    float noiseRpmExponent = 3.0f;  // noise ~ RPM^3
    float noiseFloor = 0.002f;       // minimum noise at idle
    
    // Filtering
    float noiseLowHz = 200.f;
    float noiseHighHz = 6000.f;
    
    // Reverb (for indoor/structures)
    float reverbLevel = 0.03f;
    float reverbDelay = 0.03f;  // seconds
    
    // Soft limiter
    float limiterThreshold = 0.95f;
    float limiterKnee = 0.1f;
    
    // Master volume
    float masterVolume = 0.15f;
    
    // Presets
    static AudioParams preset5inch() {
        AudioParams p;
        p.numBlades = 3;
        p.polePairs = 7;
        p.maxMotorRPM = 30000.f;
        p.harm2Gain = 0.25f;
        p.harm3Gain = 0.08f;
        p.harm4Gain = 0.04f;
        p.harmRpmExponent = 1.5f;
        p.escGain = 0.08f;
        p.escHarmGain = 0.03f;
        p.escRpmExponent = 0.5f;
        p.noiseGain = 0.06f;
        p.noiseRpmExponent = 3.0f;
        p.noiseFloor = 0.002f;
        p.noiseLowHz = 200.f;
        p.noiseHighHz = 6000.f;
        p.reverbLevel = 0.03f;
        p.reverbDelay = 0.03f;
        p.masterVolume = 0.15f;
        return p;
    }
    
    static AudioParams preset35inch() {
        AudioParams p;
        p.numBlades = 3;
        p.polePairs = 7;
        p.maxMotorRPM = 40000.f;
        p.harm2Gain = 0.22f;
        p.harm3Gain = 0.07f;
        p.harm4Gain = 0.03f;
        p.harmRpmExponent = 1.5f;
        p.escGain = 0.10f;
        p.escHarmGain = 0.04f;
        p.escRpmExponent = 0.5f;
        p.noiseGain = 0.08f;
        p.noiseRpmExponent = 3.0f;
        p.noiseFloor = 0.003f;
        p.noiseLowHz = 250.f;
        p.noiseHighHz = 8000.f;
        p.reverbLevel = 0.03f;
        p.reverbDelay = 0.03f;
        p.masterVolume = 0.15f;
        return p;
    }
};

struct AudioState {
    std::atomic<float> motorRPM[4] = {0.f, 0.f, 0.f, 0.f};
    std::atomic<bool> muted{false};
    std::atomic<int> droneSize{4};  // 0-4, matches drone_size config
    
    // Phase accumulators per motor per oscillator
    float phaseBpf[4] = {0.f, 0.7f, 1.8f, 2.6f};      // fundamental (blade pass)
    float phaseHarm2[4] = {0.2f, 0.9f, 2.0f, 2.8f};   // 2nd harmonic
    float phaseHarm3[4] = {0.4f, 1.1f, 2.2f, 3.0f};   // 3rd harmonic
    float phaseHarm4[4] = {0.6f, 1.3f, 2.4f, 3.2f};   // 4th harmonic
    float phaseEsc[4] = {0.1f, 0.8f, 1.9f, 2.7f};     // ESC whine
    float phaseEsc2[4] = {0.3f, 1.0f, 2.1f, 2.9f};    // ESC 2nd harmonic
    
    // Smoothed RPM for audio (1-pole filtered)
    float smoothRPM[4] = {0.f, 0.f, 0.f, 0.f};
    
    // Noise state (simple LFSR for broadband noise)
    uint32_t noiseState = 0x12345678;
    
    // Noise filter states (per motor) - HPF + LPF for bandpass
    float noiseHpState[4] = {0.f, 0.f, 0.f, 0.f};
    float noiseLpState[4] = {0.f, 0.f, 0.f, 0.f};
    
    // Reverb delay line (simple stereo)
    static constexpr int REVERB_MAX_SAMPLES = 48000;  // 1 second at 48kHz
    float reverbLeft[REVERB_MAX_SAMPLES] = {};
    float reverbRight[REVERB_MAX_SAMPLES] = {};
    int reverbWritePos = 0;
    int reverbDelaySamples = 1440;  // 30ms default
    
    // Limiter state
    float limiterEnvelope = 0.f;
    
    AudioParams params = AudioParams::preset5inch();
    int sampleRate = 48000;
};

// Fast random for noise (xorshift32)
static inline float fastRand(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (float)state * (1.f / 4294967296.f) * 2.f - 1.f;  // -1 to 1
}

// One-pole low-pass filter coefficient from time constant (seconds)
static inline float onePoleCoeff(float tau, float sampleRate) {
    return 1.f - std::exp(-1.f / (tau * sampleRate));
}

// Soft limiter
static inline float softLimit(float x, float threshold, float knee) {
    float absx = std::fabs(x);
    if (absx <= threshold) return x;
    float over = absx - threshold;
    float limited = threshold + knee * std::log1p(over / knee);
    return x >= 0 ? limited : -limited;
}

static void audioCallback(void* userdata, Uint8* bytes, int length) {
    AudioState* a = (AudioState*)userdata;
    float* out = (float*)bytes;
    int frames = length / (2 * (int)sizeof(float));
    
    bool muted = a->muted.load(std::memory_order_relaxed);
    int currentDroneSize = a->droneSize.load(std::memory_order_relaxed);
    
    // Update audio params based on drone size (only when changed)
    static int lastDroneSize = -1;
    if (currentDroneSize != lastDroneSize) {
        if (currentDroneSize == 3) {  // 3.5 inch
            a->params = AudioParams::preset35inch();
        } else {  // 5 inch or others
            a->params = AudioParams::preset5inch();
        }
        lastDroneSize = currentDroneSize;
        // Update reverb delay
        a->reverbDelaySamples = (int)(a->params.reverbDelay * a->sampleRate);
        if (a->reverbDelaySamples >= AudioState::REVERB_MAX_SAMPLES) 
            a->reverbDelaySamples = AudioState::REVERB_MAX_SAMPLES - 1;
    }
    
    const AudioParams& p = a->params;
    
    // Soft limiter with envelope follower (fast attack ~1ms)
    // Limiter coefficient (time constant ~1ms for fast attack)
    float limiterAlpha = onePoleCoeff(0.001f, a->sampleRate);
    
    for (int i = 0; i < frames; ++i) {
        float left = 0.f, right = 0.f;
        
        // Generate per-motor sound
        for (int m = 0; m < 4; ++m) {
            // Get target RPM and smooth it (1-pole filter, tau = 2ms)
            float targetRPM = a->motorRPM[m].load(std::memory_order_relaxed);
            float alpha = onePoleCoeff(0.002f, a->sampleRate);
            a->smoothRPM[m] += alpha * (targetRPM - a->smoothRPM[m]);
            float rpm = a->smoothRPM[m];
            
            if (rpm < 100.f) continue;  // Below idle, silent
            
            float rps = rpm / 60.f;
            float bpf = rps * p.numBlades;           // Blade pass frequency
            float escFreq = rps * p.polePairs;       // ESC electrical frequency
            
            // Amplitude scales with RPM (roughly sqrt for perceived loudness)
            float rpmNorm = clampf(rpm / p.maxMotorRPM, 0.f, 1.f);
            
            // Blade pass fundamental + harmonics
            float bpfInc = 6.28318530718f * bpf / a->sampleRate;
            a->phaseBpf[m] += bpfInc;
            if (a->phaseBpf[m] >= 6.28318530718f) a->phaseBpf[m] -= 6.28318530718f;
            
            float harm2Inc = bpfInc * 2.f;
            a->phaseHarm2[m] += harm2Inc;
            if (a->phaseHarm2[m] >= 6.28318530718f) a->phaseHarm2[m] -= 6.28318530718f;
            
            float harm3Inc = bpfInc * 3.f;
            a->phaseHarm3[m] += harm3Inc;
            if (a->phaseHarm3[m] >= 6.28318530718f) a->phaseHarm3[m] -= 6.28318530718f;
            
            float harm4Inc = bpfInc * 4.f;
            a->phaseHarm4[m] += harm4Inc;
            if (a->phaseHarm4[m] >= 6.28318530718f) a->phaseHarm4[m] -= 6.28318530718f;
            
            // ESC whine
            float escInc = 6.28318530718f * escFreq / a->sampleRate;
            a->phaseEsc[m] += escInc;
            if (a->phaseEsc[m] >= 6.28318530718f) a->phaseEsc[m] -= 6.28318530718f;
            a->phaseEsc2[m] += escInc * 2.f;
            if (a->phaseEsc2[m] >= 6.28318530718f) a->phaseEsc2[m] -= 6.28318530718f;
            
            // Sum oscillators with RPM-scaled harmonics
            float motorSound = 0.f;
            
            // Harmonic amplitude scales with RPM (less harmonics at idle)
            float harmScale = std::pow(rpmNorm, p.harmRpmExponent);
            motorSound += std::sin(a->phaseBpf[m]) * 1.0f;
            motorSound += std::sin(a->phaseHarm2[m]) * p.harm2Gain * harmScale;
            motorSound += std::sin(a->phaseHarm3[m]) * p.harm3Gain * harmScale;
            motorSound += std::sin(a->phaseHarm4[m]) * p.harm4Gain * harmScale;
            
            // ESC whine - relatively louder at low RPM (commutation noise)
            float escScale = std::pow(rpmNorm, p.escRpmExponent);
            motorSound += std::sin(a->phaseEsc[m]) * p.escGain * escScale;
            motorSound += std::sin(a->phaseEsc2[m]) * p.escHarmGain * escScale;
            
            // Broadband noise (prop wash / turbulence) - bandpass filtered via simple 1-pole HPF + LPF
            float noise = fastRand(a->noiseState);
            float noiseAmp = p.noiseGain * std::pow(rpmNorm, p.noiseRpmExponent) + p.noiseFloor;
            
            // Simple high-pass (remove low freq rumble) - use per-instance state
            float hpAlpha = onePoleCoeff(1.f / (2.f * 3.14159f * p.noiseLowHz), a->sampleRate);
            a->noiseHpState[m] = noise - a->noiseHpState[m] * (1.f - hpAlpha);
            
            // Simple low-pass
            float lpAlpha = onePoleCoeff(1.f / (2.f * 3.14159f * p.noiseHighHz), a->sampleRate);
            a->noiseLpState[m] += lpAlpha * (a->noiseHpState[m] - a->noiseLpState[m]);
            
            motorSound += a->noiseLpState[m] * noiseAmp;
            
            // Pan: motors 0,3 left; motors 1,2 right (X configuration)
            // Slight crossfeed for realism
            float pan = (m == 0 || m == 3) ? -0.7f : 0.7f;
            left += motorSound * (0.5f - pan * 0.5f);
            right += motorSound * (0.5f + pan * 0.5f);
        }
        
        // Apply master volume
        left *= p.masterVolume;
        right *= p.masterVolume;
        
        // Simple reverb (feedback delay)
        if (p.reverbLevel > 0.f) {
            int readPos = a->reverbWritePos - a->reverbDelaySamples;
            if (readPos < 0) readPos += AudioState::REVERB_MAX_SAMPLES;
            
            float revL = a->reverbLeft[readPos];
            float revR = a->reverbRight[readPos];
            
            // Write current output to delay line (with feedback)
            a->reverbLeft[a->reverbWritePos] = left + revL * 0.3f;
            a->reverbRight[a->reverbWritePos] = right + revR * 0.3f;
            a->reverbWritePos++;
            if (a->reverbWritePos >= AudioState::REVERB_MAX_SAMPLES) a->reverbWritePos = 0;
            
            // Mix reverb
            left += revL * p.reverbLevel;
            right += revR * p.reverbLevel;
        }
        
        // Mute
        if (muted) {
            left = right = 0.f;
        }
        
        // Soft limiter with envelope follower (fast attack)
        float peak = std::max(std::fabs(left), std::fabs(right));
        a->limiterEnvelope += limiterAlpha * (peak - a->limiterEnvelope);
        float gain = 1.f;
        if (a->limiterEnvelope > p.limiterThreshold) {
            gain = p.limiterThreshold / a->limiterEnvelope;
        }
        
        // Hard clamp as safety, then soft limit
        left = clampf(left * gain, -1.f, 1.f);
        right = clampf(right * gain, -1.f, 1.f);
        
        out[i * 2] = softLimit(left, p.limiterThreshold, p.limiterKnee);
        out[i * 2 + 1] = softLimit(right, p.limiterThreshold, p.limiterKnee);
    }
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static void tryOpen(Radio& r, int index, const Config& cfg) {
    if (r.joy) return;
    const char* nm = SDL_JoystickNameForIndex(index);
    std::string name = nm ? nm : "";
    if (!cfg.joystickName.empty() && lower(name).find(lower(cfg.joystickName)) == std::string::npos) return;
    SDL_Joystick* j = SDL_JoystickOpen(index);
    if (!j) return;
    if (SDL_JoystickNumAxes(j) < 4) {  // not a radio/stick device (mouse, keyboard, etc.)
        SDL_JoystickClose(j);
        return;
    }
    r.joy = j;
    r.id = SDL_JoystickInstanceID(j);
    r.name = name;
    r.naxes = SDL_JoystickNumAxes(j);
    std::printf("radio: %s (%d axes)\n", name.c_str(), r.naxes);
}

static float readAxis(const Radio& r, int idx, bool inv) {
    if (!r.joy || idx < 0 || idx >= r.naxes) return 0.f;
    float v = clampf(SDL_JoystickGetAxis(r.joy, idx) / 32767.f, -1.f, 1.f);
    return inv ? -v : v;
}

static float applyDeadband(float x, float db) {
    float a = std::fabs(x);
    if (a <= db) return 0.f;
    return (x < 0 ? -1.f : 1.f) * (a - db) / (1.f - db);
}

// ============================================================ Debug Overlay Shader and Functions

// Simple debug shader for prop overlay - flat color per prop, no blending, no culling
static const char* DEBUG_PROP_OVERLAY_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec3 aCol;
layout(location=3) in float aMat;
layout(location=4) in float aPropIdx;
uniform mat4 uVP;
uniform mat4 uView;
out vec3 vCol;
out float vPropIdx;
void main(){
    vCol = aCol;
    vPropIdx = aPropIdx;
    vec4 viewPos = uView * vec4(aPos, 1.0);
    gl_Position = uVP * viewPos;
}
)";

static const char* DEBUG_PROP_OVERLAY_FS = R"(#version 330 core
in vec3 vCol;
in float vPropIdx;
out vec4 oColor;
void main(){
    // Bright distinct colors per prop: 0=red, 1=green, 2=blue, 3=yellow, 4=cyan, 5=magenta
    int idx = int(vPropIdx + 0.5);
    vec3 color;
    if (idx == 0) color = vec3(1.0, 0.0, 0.0);        // prop 0 - red
    else if (idx == 1) color = vec3(0.0, 1.0, 0.0);    // prop 1 - green
    else if (idx == 2) color = vec3(0.0, 0.0, 1.0);    // prop 2 - blue
    else if (idx == 3) color = vec3(1.0, 1.0, 0.0);    // prop 3 - yellow
    else if (idx == 4) color = vec3(0.0, 1.0, 1.0);    // motors - cyan
    else if (idx == 5) color = vec3(1.0, 0.0, 1.0);    // arms - magenta
    else color = vCol;
    oColor = vec4(color, 1.0);
}
)";

static GLuint buildDebugPropOverlayProgram() {
    return buildProgram(DEBUG_PROP_OVERLAY_VS, DEBUG_PROP_OVERLAY_FS, "debug-prop-overlay");
}

// Write PPM file
static void writePPM(const char* filename, int w, int h, const std::vector<uint8_t>& pixels) {
    FILE* f = std::fopen(filename, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(pixels.data(), 1, pixels.size(), f);
    std::fclose(f);
}

// Print ASCII art of framebuffer
static void printASCIIArt(const std::vector<uint8_t>& pixels, int w, int h, int outW, int outH) {
    std::printf("\n+");
    for (int i = 0; i < outW; ++i) std::printf("-");
    std::printf("+\n");
    
    for (int row = 0; row < outH; ++row) {
        std::printf("|");
        for (int col = 0; col < outW; ++col) {
            // Sample multiple pixels in the source region
            int srcX = (col * w) / outW;
            int srcY = (row * h) / outH;
            int srcW = std::max(1, w / outW);
            int srcH = std::max(1, h / outH);
            
            int rSum = 0, gSum = 0, bSum = 0, count = 0;
            for (int sy = 0; sy < srcH && srcY + sy < h; ++sy) {
                for (int sx = 0; sx < srcW && srcX + sx < w; ++sx) {
                    int idx = ((srcY + sy) * w + (srcX + sx)) * 3;
                    rSum += pixels[idx];
                    gSum += pixels[idx + 1];
                    bSum += pixels[idx + 2];
                    count++;
                }
            }
            if (count > 0) {
                int r = rSum / count;
                int g = gSum / count;
                int b = bSum / count;
                int luminance = (r * 299 + g * 587 + b * 114) / 1000;
                std::printf("%c", luminance > 32 ? '#' : '.');
            } else {
                std::printf(".");
            }
        }
        std::printf("|\n");
    }
    
    std::printf("+");
    for (int i = 0; i < outW; ++i) std::printf("-");
    std::printf("+\n");
}

// Count lit pixels in a region
static int countLitPixels(const std::vector<uint8_t>& pixels, int w, int h, int x0, int y0, int x1, int y1, int threshold = 32) {
    int count = 0;
    for (int y = y0; y < y1 && y < h; ++y) {
        for (int x = x0; x < x1 && x < w; ++x) {
            int idx = (y * w + x) * 3;
            int r = pixels[idx];
            int g = pixels[idx + 1];
            int b = pixels[idx + 2];
            int luminance = (r * 299 + g * 587 + b * 114) / 1000;
            if (luminance > threshold) count++;
        }
    }
    return count;
}

// UI Visibility Test: read back framebuffer after OSD and count non-background pixels in UI regions
static void testUIVisibility(int dw, int dh, const char* label, bool withOverlay, size_t osdVertexCount, int frameCounter) {
    std::vector<uint8_t> pixels(dw * dh * 4);
    glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    
    // Define UI regions (in window coordinates, origin bottom-left)
    // Top-left: throttle, ARMED/DISARMED
    int tl_x0 = 0, tl_y0 = dh * 3 / 4, tl_x1 = dw / 3, tl_y1 = dh;
    // Top-center: timer
    int tc_x0 = dw / 3, tc_y0 = dh * 3 / 4, tc_x1 = dw * 2 / 3, tc_y1 = dh;
    // Top-right: FPS, tilt, FOV
    int tr_x0 = dw * 2 / 3, tr_y0 = dh * 3 / 4, tr_x1 = dw, tr_y1 = dh;
    // Bottom-left: battery voltage, cell voltage, MAH
    int bl_x0 = 0, bl_y0 = 0, bl_x1 = dw / 3, bl_y1 = dh / 4;
    // Bottom-right: speed, altitude
    int br_x0 = dw * 2 / 3, br_y0 = 0, br_x1 = dw, br_y1 = dh / 4;
    
    auto countBright = [&](int x0, int y0, int x1, int y1) {
        int count = 0;
        for (int y = y0; y < y1 && y < dh; ++y) {
            for (int x = x0; x < x1 && x < dw; ++x) {
                int idx = (y * dw + x) * 4;
                // OSD text is white/bright (r,g,b > 200). Scene is darker.
                if (pixels[idx] > 200 && pixels[idx+1] > 200 && pixels[idx+2] > 200) {
                    count++;
                }
            }
        }
        return count;
    };
    
    int tl_count = countBright(tl_x0, tl_y0, tl_x1, tl_y1);
    int tc_count = countBright(tc_x0, tc_y0, tc_x1, tc_y1);
    int tr_count = countBright(tr_x0, tr_y0, tr_x1, tr_y1);
    int bl_count = countBright(bl_x0, bl_y0, bl_x1, bl_y1);
    int br_count = countBright(br_x0, br_y0, br_x1, br_y1);
    
    std::printf("\n=== UI VISIBILITY TEST: %s (overlay=%s, osd_verts=%zu) ===\n", label, withOverlay ? "ON" : "OFF", osdVertexCount);
    std::printf("  Top-left (throttle):    %d bright pixels\n", tl_count);
    std::printf("  Top-center (timer):     %d bright pixels\n", tc_count);
    std::printf("  Top-right (FPS/tilt):   %d bright pixels\n", tr_count);
    std::printf("  Bottom-left (battery):  %d bright pixels\n", bl_count);
    std::printf("  Bottom-right (speed):   %d bright pixels\n", br_count);
    std::printf("=== END UI VISIBILITY TEST ===\n\n");
}

// Debug overlay render and analysis
static void debugRenderPropOverlay(
    const Sim& sim,
    const Config& cfg,
    GLuint vaoPropOverlay,
    GLsizei propOverlayMeshVerts,
    int windowWidth,
    int windowHeight,
    GLuint debugFbo,
    GLuint debugTex,
    GLuint debugDepth
) {
    // Test cases: FOV 160/tilt 33, FOV 120/tilt 33, FOV 90/tilt 20
    struct TestCase { float fov; float tilt; const char* name; };
    TestCase testCases[] = {
        {160.f, 33.f, "FOV160_Tilt33"},
    };
    
    // Build debug shader program
    GLuint debugProg = buildDebugPropOverlayProgram();
    GLint uDebugVP = gl.glGetUniformLocation(debugProg, "uVP");
    GLint uDebugView = gl.glGetUniformLocation(debugProg, "uView");
    
    // Camera mount geometry (from drone preset) - MATCH MAIN CAMERA: {0, 0.03f, -0.07f}
    float armLen = sim.P.armLength;
    float armCoord = armLen * 0.70710678f;
    float bodyHeight = 0.025f;
    float bodyDepth = armLen * 0.25f;
    float motorHeight = 0.02f;
    float cameraSize = armLen * 0.15f;
    V3 camLocalPos = {0, 0.03f, -0.07f};
    
    // Ensure front props are IN FRONT of camera for visibility (same logic as buildPropOverlayMesh)
    float propZ = std::min(-armCoord, camLocalPos.z - 0.03f); // 3cm in front of camera
    V3 propPositions[4] = {
        { armCoord, motorHeight + 0.002f, propZ }, // front-right
        {-armCoord, motorHeight + 0.002f, propZ }, // front-left
        {-armCoord, motorHeight + 0.002f,  armCoord }, // back-left
        { armCoord, motorHeight + 0.002f,  armCoord }, // back-right
    };
    
    // Prop radius from preset
    float propRadius = armLen * 0.45f; // 5" = ~63mm, 3.5" = ~45mm
    
    std::printf("\n========== DEBUG PROP OVERLAY ANALYSIS ==========\n");
    std::printf("Camera mount: offset=(%.3f, %.3f, %.3f) tilt=%.1f deg\n", 
                camLocalPos.x, camLocalPos.y, camLocalPos.z, cfg.camTilt);
    std::printf("Arm length: %.3f m, Arm coord: %.3f m\n", armLen, armCoord);
    std::printf("Prop radius: %.3f m (%.1f mm)\n", propRadius, propRadius * 1000.f);
    std::printf("Motor height: %.3f m\n", motorHeight);
    std::printf("Body height: %.3f m, Body depth: %.3f m, Camera size: %.3f m\n", bodyHeight, bodyDepth, cameraSize);
    std::printf("\nProp positions relative to body (drone-local):\n");
    for (int i = 0; i < 4; ++i) {
        std::printf("  Prop %d: (%.3f, %.3f, %.3f)\n", i, propPositions[i].x, propPositions[i].y, propPositions[i].z);
    }
    std::printf("Camera position (drone-local): (%.3f, %.3f, %.3f)\n", camLocalPos.x, camLocalPos.y, camLocalPos.z);
    
    for (const auto& tc : testCases) {
        std::printf("\n--- Test Case: %s (FOV=%.0f, Tilt=%.0f) ---\n", tc.name, tc.fov, tc.tilt);
        
        // Overlay projection: same FOV/aspect, near=0.002, far=1.0
        float aspect = (float)windowWidth / (float)windowHeight;
        float tanH = std::tan(rad(tc.fov) * 0.5f);
        float tanV = tanH / aspect;
        float overlayNear = 0.002f;
        float overlayFar = 1.0f;
        float P_overlay[16] = {0};
        P_overlay[0] = 1.f / tanH;
        P_overlay[5] = 1.f / tanV;
        P_overlay[10] = (overlayFar + overlayNear) / (overlayNear - overlayFar);
        P_overlay[11] = -1.f;
        P_overlay[14] = 2.f * overlayFar * overlayNear / (overlayNear - overlayFar);
        
        // Main projection (for comparison) - computed but not used in this debug function
        float mainNear = 0.05f;
        float mainFar = 210.f; // qualityFar[0]
        float P_main[16] = {0};
        (void)P_main;
        P_main[0] = 1.f / tanH;
        P_main[5] = 1.f / tanV;
        P_main[10] = (mainFar + mainNear) / (mainNear - mainFar);
        P_main[11] = -1.f;
        P_main[14] = 2.f * mainFar * mainNear / (mainNear - mainFar);
        
        // View matrix (camera tilt only, no translation)
        float tiltRad = rad(tc.tilt);
        float cosT = cosf(tiltRad);
        float sinT = sinf(tiltRad);
        V3 overlayRight = {1, 0, 0};
        V3 overlayUp = {0, cosT, sinT};
        V3 overlayBack = {0, -sinT, cosT};
        float view[16] = {
            overlayRight.x, overlayUp.x, overlayBack.x, 0,
            overlayRight.y, overlayUp.y, overlayBack.y, 0,
            overlayRight.z, overlayUp.z, overlayBack.z, 0,
            -dot(overlayRight, camLocalPos), -dot(overlayUp, camLocalPos), -dot(overlayBack, camLocalPos), 1
        };
        
        std::printf("Overlay projection: near=%.3f, far=%.3f, FOV=%.0f, aspect=%.3f\n", overlayNear, overlayFar, tc.fov, aspect);
        std::printf("Main projection:    near=%.3f, far=%.3f, FOV=%.0f, aspect=%.3f\n", mainNear, mainFar, tc.fov, aspect);
        
        // Transform prop positions to camera space and NDC
        std::printf("\nProp analysis:\n");
        for (int i = 0; i < 4; ++i) {
            V3 relPos = {propPositions[i].x - camLocalPos.x, propPositions[i].y - camLocalPos.y, propPositions[i].z - camLocalPos.z};
            float cx = dot(overlayRight, relPos);
            float cy = dot(overlayUp, relPos);
            float cz = dot(overlayBack, relPos);
            float dist = std::sqrt(cx*cx + cy*cy + cz*cz);
            
            // NDC with overlay projection
            float ndcX_overlay = cx / (-cz * tanH);
            float ndcY_overlay = cy / (-cz * tanV);
            
            // NDC with main projection
            float ndcX_main = cx / (-cz * tanH);
            float ndcY_main = cy / (-cz * tanV);
            
            // Check near plane clipping
            bool clipped_overlay = (-cz) < overlayNear;
            bool clipped_main = (-cz) < mainNear;
            
            std::printf("  Prop %d: cam=(%.3f, %.3f, %.3f) dist=%.3f NDC_overlay=(%.3f, %.3f) NDC_main=(%.3f, %.3f) clip_overlay=%s clip_main=%s\n",
                        i, cx, cy, cz, dist, ndcX_overlay, ndcY_overlay, ndcX_main, ndcY_main,
                        clipped_overlay ? "YES" : "NO", clipped_main ? "YES" : "NO");
        }
        
        // Resize debug framebuffer to window size
        glBindTexture(GL_TEXTURE_2D, debugTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, windowWidth, windowHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        
        gl.glBindRenderbuffer(GL_RENDERBUFFER, debugDepth);
        gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, windowWidth, windowHeight);
        
        gl.glBindFramebuffer(GL_FRAMEBUFFER, debugFbo);
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, debugTex, 0);
        gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, debugDepth);
        
        GLenum status = gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::printf("ERROR: Debug framebuffer incomplete: 0x%X\n", status);
        }
        
        // Render to debug framebuffer
        glViewport(0, 0, windowWidth, windowHeight);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, debugFbo);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Disable culling, blending for debug
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        
        // Use debug shader
        gl.glUseProgram(debugProg);
        gl.glUniformMatrix4fv(uDebugVP, 1, GL_FALSE, P_overlay);
        gl.glUniformMatrix4fv(uDebugView, 1, GL_FALSE, view);
        
        // Draw the geometry
        gl.glBindVertexArray(vaoPropOverlay);
        glDrawArrays(GL_TRIANGLES, 0, propOverlayMeshVerts);
        
        // Read back framebuffer
        std::vector<uint8_t> pixels(windowWidth * windowHeight * 3);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, windowWidth, windowHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        
        // Flip vertically (OpenGL origin is bottom-left)
        for (int y = 0; y < windowHeight / 2; ++y) {
            int y2 = windowHeight - 1 - y;
            for (int x = 0; x < windowWidth; ++x) {
                int idx1 = (y * windowWidth + x) * 3;
                int idx2 = (y2 * windowWidth + x) * 3;
                std::swap(pixels[idx1], pixels[idx2]);
                std::swap(pixels[idx1 + 1], pixels[idx2 + 1]);
                std::swap(pixels[idx1 + 2], pixels[idx2 + 2]);
            }
        }
        
        // Save PPM
        char ppmName[64];
        std::snprintf(ppmName, sizeof(ppmName), "overlay_debug_%s.ppm", tc.name);
        writePPM(ppmName, windowWidth, windowHeight, pixels);
        std::printf("Saved %s\n", ppmName);
        
        // Print ASCII art (downsampled to 100x40)
        std::printf("\nASCII art (100x40):\n");
        printASCIIArt(pixels, windowWidth, windowHeight, 100, 40);
        
        // Count lit pixels in regions
        int lowerHalf = countLitPixels(pixels, windowWidth, windowHeight, 0, windowHeight/2, windowWidth, windowHeight);
        int middleRegion = countLitPixels(pixels, windowWidth, windowHeight, 
                                          windowWidth*4/10, windowHeight*4/10, 
                                          windowWidth*6/10, windowHeight*6/10);
        int lowerLeft = countLitPixels(pixels, windowWidth, windowHeight, 0, windowHeight/2, windowWidth/2, windowHeight);
        int lowerRight = countLitPixels(pixels, windowWidth, windowHeight, windowWidth/2, windowHeight/2, windowWidth, windowHeight);
        
        std::printf("\nLit pixel counts:\n");
        std::printf("  Lower half: %d\n", lowerHalf);
        std::printf("  Middle region (center 20%%): %d\n", middleRegion);
        std::printf("  Lower-left corner: %d\n", lowerLeft);
        std::printf("  Lower-right corner: %d\n", lowerRight);
        std::printf("  Expected: many in lower half, almost none in middle\n");
        
        // Cleanup - don't delete framebuffer handles (managed by main)
        gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl.glDeleteProgram(debugProg);
    }
    
    std::printf("\n========== END DEBUG PROP OVERLAY ANALYSIS ==========\n\n");
}

// ============================================================ Props Lowering Verification
static void verifyPropsLowering(
    const Sim& sim,
    GLuint vaoPropOverlay,
    GLsizei propOverlayMeshVerts,
    int windowWidth,
    int windowHeight,
    GLuint debugFbo,
    GLuint debugTex,
    GLuint debugDepth
) {
    std::printf("\n========== PROPS LOWERING VERIFICATION ==========\n");
    
    // Build debug shader program for flat color rendering
    GLuint debugProg = buildDebugPropOverlayProgram();
    GLint uDebugVP = gl.glGetUniformLocation(debugProg, "uVP");
    GLint uDebugView = gl.glGetUniformLocation(debugProg, "uView");
    
    // Camera mount geometry
    float armLen = sim.P.armLength;
    (void)armLen;  // unused in this function but kept for reference
    V3 camLocalPos = {0, 0.03f, -0.07f};
    
    // Test cases: FOV 90, 120, 160 with tilt 20, 33
    struct TestConfig { float fov; float tilt; const char* name; };
    TestConfig testConfigs[] = {
        {90.f, 20.f, "FOV90_Tilt20"},
        {90.f, 33.f, "FOV90_Tilt33"},
        {120.f, 20.f, "FOV120_Tilt20"},
        {120.f, 33.f, "FOV120_Tilt33"},
        {160.f, 20.f, "FOV160_Tilt20"},
        {160.f, 33.f, "FOV160_Tilt33"},
    };
    
    // Lowering percentages to test
    float loweringPercents[] = {0.f, 25.f, 50.f, 75.f, 100.f};
    
    // Results storage
    int results[6][5][2] = {}; // [config][pct][0=LL, 1=LR]
    
    // Resize debug framebuffer to window size
    glBindTexture(GL_TEXTURE_2D, debugTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, windowWidth, windowHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    gl.glBindRenderbuffer(GL_RENDERBUFFER, debugDepth);
    gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, windowWidth, windowHeight);
    
    gl.glBindFramebuffer(GL_FRAMEBUFFER, debugFbo);
    gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, debugTex, 0);
    gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, debugDepth);
    
    GLenum status = gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::printf("ERROR: Debug framebuffer incomplete: 0x%X\n", status);
    }
    
    // Print table header
    std::printf("\n=== LIT PIXEL COUNTS IN LOWER CORNERS ===\n");
    std::printf("%-20s | ", "Config");
    for (float pct : loweringPercents) {
        std::printf(" %3.0f%%    ", pct);
    }
    std::printf("\n");
    std::printf("%-20s | ", "");
    for (size_t i = 0; i < 5; ++i) std::printf("LL     LR     ");
    std::printf("\n");
    std::printf("-------------------------------------------------------------\n");
    
    // For ASCII dumps at FOV 160 / tilt 33
    const float asciiFov = 160.f;
    const float asciiTilt = 33.f;
    float asciiPercents[] = {0.f, 50.f, 100.f};
    
    for (int ci = 0; ci < 6; ++ci) {
        const auto& tc = testConfigs[ci];
        float aspect = (float)windowWidth / (float)windowHeight;
        float tanH = std::tan(rad(tc.fov) * 0.5f);
        float tanV = tanH / aspect;
        float verticalHalfFOV = std::atan(tanV);  // radians
        
        // Print computed angles for this config
        std::printf("\nConfig: %s (FOV=%.0f, Tilt=%.0f, vertHalfFOV=%.1f deg)\n", 
                    tc.name, tc.fov, tc.tilt, verticalHalfFOV * 180.f / M_PI);
        std::printf("  Lowering%% -> Angle(deg) = (pct/100) * (%.1f + 20.0)\n", 
                    verticalHalfFOV * 180.f / M_PI);
        
        for (int pi = 0; pi < 5; ++pi) {
            float pct = loweringPercents[pi];
            float loweringAngleRad = (pct / 100.0f) * (verticalHalfFOV + rad(20.0f));
            float loweringAngleDeg = loweringAngleRad * 180.f / M_PI;
            
            // Build view matrix with lowering
            float tiltRad = rad(tc.tilt);
            float totalTiltRad = tiltRad + loweringAngleRad;
            float cosT = cosf(totalTiltRad);
            float sinT = sinf(totalTiltRad);
            V3 overlayRight = {1, 0, 0};
            V3 overlayUp = {0, cosT, sinT};
            V3 overlayBack = {0, -sinT, cosT};
            float view[16] = {
                overlayRight.x, overlayUp.x, overlayBack.x, 0,
                overlayRight.y, overlayUp.y, overlayBack.y, 0,
                overlayRight.z, overlayUp.z, overlayBack.z, 0,
                -dot(overlayRight, camLocalPos), -dot(overlayUp, camLocalPos), -dot(overlayBack, camLocalPos), 1
            };
            
            // Overlay projection
            float overlayNear = 0.002f;
            float overlayFar = 1.0f;
            float P_overlay[16] = {0};
            P_overlay[0] = 1.f / tanH;
            P_overlay[5] = 1.f / tanV;
            P_overlay[10] = (overlayFar + overlayNear) / (overlayNear - overlayFar);
            P_overlay[11] = -1.f;
            P_overlay[14] = 2.f * overlayFar * overlayNear / (overlayNear - overlayFar);
            
            // Render to debug framebuffer
            glViewport(0, 0, windowWidth, windowHeight);
            gl.glBindFramebuffer(GL_FRAMEBUFFER, debugFbo);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            
            gl.glUseProgram(debugProg);
            gl.glUniformMatrix4fv(uDebugVP, 1, GL_FALSE, P_overlay);
            gl.glUniformMatrix4fv(uDebugView, 1, GL_FALSE, view);
            
            gl.glBindVertexArray(vaoPropOverlay);
            glDrawArrays(GL_TRIANGLES, 0, propOverlayMeshVerts);
            
            // Read back framebuffer
            std::vector<uint8_t> pixels(windowWidth * windowHeight * 3);
            glFinish();  // Ensure rendering is complete
            glPixelStorei(GL_PACK_ALIGNMENT, 1);  // Ensure proper pixel packing
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(0, 0, windowWidth, windowHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            
            // Flip vertically (OpenGL origin is bottom-left)
            for (int y = 0; y < windowHeight / 2; ++y) {
                int y2 = windowHeight - 1 - y;
                for (int x = 0; x < windowWidth; ++x) {
                    int idx1 = (y * windowWidth + x) * 3;
                    int idx2 = (y2 * windowWidth + x) * 3;
                    std::swap(pixels[idx1], pixels[idx2]);
                    std::swap(pixels[idx1 + 1], pixels[idx2 + 1]);
                    std::swap(pixels[idx1 + 2], pixels[idx2 + 2]);
                }
            }
            
            // Count lit pixels in lower-left and lower-right corners (bottom 25% of screen)
            int cornerHeight = windowHeight / 4;
            int cornerWidth = windowWidth / 4;
            int lowerLeft = countLitPixels(pixels, windowWidth, windowHeight, 
                                           0, 0, cornerWidth, cornerHeight);
            int lowerRight = countLitPixels(pixels, windowWidth, windowHeight, 
                                            windowWidth - cornerWidth, 0, windowWidth, cornerHeight);
            
            // Store for table
            results[ci][pi][0] = lowerLeft;
            results[ci][pi][1] = lowerRight;
            
            // Print computed angle for this percentage
            std::printf("  %3.0f%% -> %.2f deg, LL=%d, LR=%d\n", pct, loweringAngleDeg, lowerLeft, lowerRight);
            
            // ASCII dump for specific configs (FOV 160 / tilt 33 at 0%, 50%, 100%)
            if (tc.fov == asciiFov && tc.tilt == asciiTilt) {
                for (float apct : asciiPercents) {
                    if (std::fabs(pct - apct) < 0.01f) {
                        std::printf("\n--- ASCII DUMP: %s, Lowering=%.0f%%, Angle=%.2f deg ---\n", 
                                    tc.name, pct, loweringAngleDeg);
                        printASCIIArt(pixels, windowWidth, windowHeight, 100, 40);
                    }
                }
            }
        }
    }
    
    // Print summary table
    std::printf("\n=== SUMMARY TABLE: Lower-Left / Lower-Right Lit Pixels ===\n");
    for (int ci = 0; ci < 6; ++ci) {
        std::printf("%-20s | ", testConfigs[ci].name);
        for (int pi = 0; pi < 5; ++pi) {
            std::printf("%4d %4d  ", results[ci][pi][0], results[ci][pi][1]);
        }
        std::printf("\n");
    }
    
    // Verify monotonic decrease and zero at 100%
    std::printf("\n=== VERIFICATION: Monotonic decrease to zero at 100%% ===\n");
    bool allOk = true;
    for (int ci = 0; ci < 6; ++ci) {
        for (int corner = 0; corner < 2; ++corner) {
            const char* cornerName = corner == 0 ? "LL" : "LR";
            int prev = results[ci][0][corner];
            bool ok = true;
            for (int pi = 1; pi < 5; ++pi) {
                int curr = results[ci][pi][corner];
                if (curr > prev) {
                    ok = false;
                    allOk = false;
                    std::printf("  FAIL: %s %s: %.0f%% (%d) > %.0f%% (%d)\n", 
                                testConfigs[ci].name, cornerName, 
                                loweringPercents[pi], curr, 
                                loweringPercents[pi-1], prev);
                }
                prev = curr;
            }
            // Check zero at 100%
            if (results[ci][4][corner] != 0) {
                ok = false;
                allOk = false;
                std::printf("  FAIL: %s %s at 100%%: %d != 0\n", 
                            testConfigs[ci].name, cornerName, results[ci][4][corner]);
            }
            if (ok) {
                std::printf("  OK: %s %s monotonically decreases to 0\n", 
                            testConfigs[ci].name, cornerName);
            }
        }
    }
    if (allOk) {
        std::printf("\nALL VERIFICATIONS PASSED\n");
    } else {
        std::printf("\nSOME VERIFICATIONS FAILED\n");
    }
    
    // Cleanup - don't delete framebuffer handles (managed by main)
    gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (debugProg) {
        gl.glDeleteProgram(debugProg);
    }
    
    std::printf("\n========== END PROPS LOWERING VERIFICATION ==========\n\n");
}

// ============================================================ main
int main(int argc, char** argv) {
    Config cfg;
    std::string cfgPath = defaultConfigPath();
    bool listJoy = false;
    bool generateLevel = false;
    uint32_t generateSeed = 0xDEADBEEF;
    std::string generateOutput = "abandoned_base.level";
    // Debug flags for flickering investigation
    bool freezeTest = false;
    bool frameDiffTest = false;
    bool drawCallAudit = false;
    bool groundAudit = false;
    bool colorCensusTest = false;
    int freezeTestFrames = 60;
    int frameDiffFrames = 60;
    bool dbgDisableHash21 = false;
    bool dbgDisableGrain = false;
    bool dbgDisableScratch = false;
    bool dbgDisableDetail = false;
    bool dbgDisableShadows = false;
    bool disableVideo = false;
    int startMapIndex = -1;
    bool debugMode = false;
    bool debugOverlay = false;
    bool debugGlState = false;
    bool debugPropsLowering = false;
    // Ground layer toggles
    bool disableGroundLayer0 = false;
    bool disableGroundLayer1 = false;
    bool disableGroundLayer2 = false;
    bool disableGroundLayer3 = false;
    bool disableGroundLayer4 = false;
    bool disableGroundLayer5 = false;
    bool disableGroundLayer6 = false;
    bool disableGroundLayer7 = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) cfgPath = argv[++i];
        else if (a == "--list-joysticks") listJoy = true;
        else if (a == "--fonttest") { fontTest(); return 0; }
        else if (a == "--generate") {
            generateLevel = true;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                generateSeed = std::stoul(argv[++i]);
            }
            if (i + 1 < argc && argv[i+1][0] != '-') {
                generateOutput = argv[++i];
            }
        }
        else if (a == "--freeze-test") {
            freezeTest = true;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                freezeTestFrames = std::stoi(argv[++i]);
            }
        }
        else if (a == "--frame-diff-test") {
            frameDiffTest = true;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                frameDiffFrames = std::stoi(argv[++i]);
            }
        }
        else if (a == "--draw-call-audit") {
            drawCallAudit = true;
        }
        else if (a == "--dbg-disable-hash21") {
            dbgDisableHash21 = true;
        }
        else if (a == "--dbg-disable-grain") {
            dbgDisableGrain = true;
        }
        else if (a == "--dbg-disable-scratch") {
            dbgDisableScratch = true;
        }
        else if (a == "--dbg-disable-detail") {
            dbgDisableDetail = true;
        }
        else if (a == "--dbg-disable-shadows") {
            dbgDisableShadows = true;
        }
        else if (a == "--disable-video") {
            disableVideo = true;
        }
        else if (a == "--debug-gl-state") {
            debugGlState = true;
        }
        else if (a == "--map" && i + 1 < argc) {
            startMapIndex = std::stoi(argv[++i]);
        }
        else if (a == "--ground-audit") {
            groundAudit = true;
        }
        else if (a == "--color-census") {
            colorCensusTest = true;
        }
        else if (a == "--disable-ground-layer" && i + 1 < argc) {
            int layer = std::stoi(argv[++i]);
            switch (layer) {
                case 0: disableGroundLayer0 = true; break;
                case 1: disableGroundLayer1 = true; break;
                case 2: disableGroundLayer2 = true; break;
                case 3: disableGroundLayer3 = true; break;
                case 4: disableGroundLayer4 = true; break;
                case 5: disableGroundLayer5 = true; break;
                case 6: disableGroundLayer6 = true; break;
                case 7: disableGroundLayer7 = true; break;
            }
        }
        else if (a == "--debug") {
            debugMode = true;
        }
        else if (a == "--debug-overlay") {
            debugOverlay = true;
        }
        else if (a == "--debug-props-lowering") {
            debugPropsLowering = true;
        }
        else if (a == "--help" || a == "-h") {
            std::printf("wenafpv [--config FILE] [--list-joysticks] [--fonttest] [--generate [SEED] [OUTPUT]]\n"
                        "                   [--freeze-test [FRAMES]] [--frame-diff-test [FRAMES]] [--draw-call-audit]\n"
                        "                   [--ground-audit] [--color-census] [--disable-ground-layer N] [--debug] [--debug-overlay]\n"
                        "                   [--debug-props-lowering] [--debug-gl-state]\n"
                        "Config: %s\n"
                        "Keys: F1 RC input visualizer | C analog video on/off | F3/F4 previous/next map | F5 settings | R reset | [ ] tilt | - = FOV | F11 fullscreen\n"
                        "      F2 force keyboard | ESC quit | keyboard: W/S throttle, A/D yaw, arrows pitch/roll, SPACE arm\n"
                        "      F5 reload level (abandoned base) | F6 reseed reaction | F7 start/stop timer\n"
                        "Debug: F6 disable hash21 | F7 disable grain | F8 disable scratch | F9 disable detail | F10 disable shadows | F11 wireframe\n"
                        "       G ground audit | H color census | J-K disable ground layer 0-7\n",
                        defaultConfigPath().c_str());
            return 0;
        }
    }
    if (generateLevel) {
        LevelData data = generateAbandonedBaseLevel(generateSeed);
        saveLevelFile(generateOutput.c_str(), data);
        std::printf("Generated level file: %s (seed=%u)\n", generateOutput.c_str(), generateSeed);
        return 0;
    }
    if (!loadConfig(cfg, cfgPath)) {
        saveConfig(cfg, cfgPath);
        std::printf("wrote default config: %s\n", cfgPath.c_str());
    }
    // Refresh built-in profile geometry (including newly introduced motor-arm lengths).
    // Custom configs use droneSize=-1 and retain their hand-tuned values.
    if (cfg.droneSize >= 0) applyDroneProfile(cfg);
    Config startupCfg = cfg;
    std::vector<Entry> settingEntries = entries(cfg);

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    AudioState audio;
    audio.muted.store(cfg.muteAudio);
    SDL_AudioDeviceID audioDevice = 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want{};
        want.freq = audio.sampleRate;
        want.format = AUDIO_F32SYS;
        want.channels = 2;
        want.samples = 512;
        want.callback = audioCallback;
        want.userdata = &audio;
        audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        if (audioDevice) SDL_PauseAudioDevice(audioDevice, 0);
        else std::fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    } else {
        std::fprintf(stderr, "audio subsystem unavailable: %s\n", SDL_GetError());
    }
    if (listJoy) {
        int n = SDL_NumJoysticks();
        std::printf("%d joystick(s)\n", n);
        for (int i = 0; i < n; ++i) {
            SDL_Joystick* j = SDL_JoystickOpen(i);
            std::printf("  [%d] %s  axes=%d buttons=%d\n", i, SDL_JoystickNameForIndex(i),
                        j ? SDL_JoystickNumAxes(j) : -1, j ? SDL_JoystickNumButtons(j) : -1);
            if (j) SDL_JoystickClose(j);
        }
        SDL_Quit();
        return 0;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    if (cfg.msaa > 1) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, cfg.msaa);
    }
    Uint32 wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (cfg.fullscreen) wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    // Wayland/Hyprland fix: set hints before window creation
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "0");
    
    SDL_Window* win = SDL_CreateWindow("WenaFPV", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cfg.width, cfg.height, wflags);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    // Ensure window gets keyboard focus on Wayland
    SDL_RaiseWindow(win);
    SDL_SetWindowInputFocus(win);
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        std::fprintf(stderr, "OpenGL 3.3 core context failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!loadGL()) return 1;
    if (SDL_GL_SetSwapInterval(cfg.vsync ? 1 : 0) != 0) std::fprintf(stderr, "vsync: %s\n", SDL_GetError());
    std::printf("GL: %s | %s\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));

    // programs
    GLuint progWorld = buildProgram(WORLD_VS, WORLD_FS, "world");
    GLuint progShadow = buildProgram(SHADOW_VS, SHADOW_FS, "shadow-depth");
    GLuint progSky = buildProgram(SKY_VS, SKY_FS, "sky");
    GLuint progVideo = buildProgram(SKY_VS, VIDEO_FS, "analog-video");
    GLuint progOsd = buildProgram(OSD_VS, OSD_FS, "osd");
    GLuint progDroneShadow = buildProgram(DRONE_SHADOW_VS, DRONE_SHADOW_FS, "drone-shadow");
    GLuint progPropOverlay = buildProgram(PROP_OVERLAY_VS, PROP_OVERLAY_FS, "prop-overlay");
    GLint uVP = gl.glGetUniformLocation(progWorld, "uVP");
    GLint uCam = gl.glGetUniformLocation(progWorld, "uCam");
    GLint uFog = gl.glGetUniformLocation(progWorld, "uFog");
GLint uTextureDetail = gl.glGetUniformLocation(progWorld, "uDetail");
    GLint uLightVP = gl.glGetUniformLocation(progWorld, "uLightVP");
    GLint uShadowMap = gl.glGetUniformLocation(progWorld, "uShadowMap");
    GLint uShadowFocus = gl.glGetUniformLocation(progWorld, "uShadowFocus");
    GLint uShadowTexel = gl.glGetUniformLocation(progWorld, "uShadowTexel");
    GLint uShadowDistance = gl.glGetUniformLocation(progWorld, "uShadowDistance");
    GLint uShadowFadeDistance = gl.glGetUniformLocation(progWorld, "uShadowFadeDistance");
    GLint uShadowAltitudeFade = gl.glGetUniformLocation(progWorld, "uShadowAltitudeFade");
    GLint uShadowStrength = gl.glGetUniformLocation(progWorld, "uShadowStrength");
    GLint uShadowBias = gl.glGetUniformLocation(progWorld, "uShadowBias");
    GLint uShadowNormalOffset = gl.glGetUniformLocation(progWorld, "uShadowNormalOffset");
    GLint uShadowFilter = gl.glGetUniformLocation(progWorld, "uShadowFilter");
    GLint uShadowsEnabled = gl.glGetUniformLocation(progWorld, "uShadowsEnabled");
    // Drone shadow uniforms
    GLint uDroneShadowVP = gl.glGetUniformLocation(progDroneShadow, "uVP");
    GLint uDroneShadowModel = gl.glGetUniformLocation(progDroneShadow, "uModel");
    GLint uDroneShadowSunDir = gl.glGetUniformLocation(progDroneShadow, "uSunDir");
    GLint uDroneShadowHeight = gl.glGetUniformLocation(progDroneShadow, "uShadowHeight");
    GLint uDroneShadowPlaneNormal = gl.glGetUniformLocation(progDroneShadow, "uPlaneNormal");
    // Prop overlay uniforms
    GLint uPropOverlayVP = gl.glGetUniformLocation(progPropOverlay, "uVP");
    GLint uPropOverlayView = gl.glGetUniformLocation(progPropOverlay, "uView");
    GLint uPropOverlayTime = gl.glGetUniformLocation(progPropOverlay, "uTime");
    GLint uPropOverlayRPM[4];
    for (int i = 0; i < 4; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "uRPM[%d]", i);
        uPropOverlayRPM[i] = gl.glGetUniformLocation(progPropOverlay, buf);
    }
    GLint uPropOverlayOpacity = gl.glGetUniformLocation(progPropOverlay, "uOpacity");
    GLint uPropOverlayBlades = gl.glGetUniformLocation(progPropOverlay, "uBlades");
    GLint uPropOverlaySunDir = gl.glGetUniformLocation(progPropOverlay, "uSunDir");
    // Debug uniforms for flickering investigation
    GLint uDbgDisableHash21 = gl.glGetUniformLocation(progWorld, "uDbgDisableHash21");
    GLint uDbgDisableGrain = gl.glGetUniformLocation(progWorld, "uDbgDisableGrain");
    GLint uDbgDisableScratch = gl.glGetUniformLocation(progWorld, "uDbgDisableScratch");
    GLint uDbgDisableDetail = gl.glGetUniformLocation(progWorld, "uDbgDisableDetail");
    GLint uShadowVP = gl.glGetUniformLocation(progShadow, "uShadowVP");
    GLint uRight = gl.glGetUniformLocation(progSky, "uRight");
    GLint uUp = gl.glGetUniformLocation(progSky, "uUp");
    GLint uBack = gl.glGetUniformLocation(progSky, "uBack");
    GLint uTan = gl.glGetUniformLocation(progSky, "uTan");
    GLint uVideoResolution      = gl.glGetUniformLocation(progVideo, "uResolution");
    GLint uVideoTime            = gl.glGetUniformLocation(progVideo, "uTime");
    GLint uVideoLoss            = gl.glGetUniformLocation(progVideo, "uSignalLoss");
    GLint uVideoBurst           = gl.glGetUniformLocation(progVideo, "uBurstStrength");
    GLint uVideoNoise           = gl.glGetUniformLocation(progVideo, "uNoiseOn");
    GLint uVideoNoiseIntensity  = gl.glGetUniformLocation(progVideo, "uNoiseIntensity");
    GLint uVideoStaticIntensity = gl.glGetUniformLocation(progVideo, "uStaticIntensity");
    GLint uVideoSnowStrength    = gl.glGetUniformLocation(progVideo, "uSnowStrength");
    GLint uVideoScan            = gl.glGetUniformLocation(progVideo, "uScanlinesOn");
    GLint uVideoScanIntensity   = gl.glGetUniformLocation(progVideo, "uScanlineIntensity");
    GLint uVideoChroma          = gl.glGetUniformLocation(progVideo, "uChromaOn");
    GLint uVideoColorGlitch     = gl.glGetUniformLocation(progVideo, "uColorGlitch");
    GLint uVideoBreakup         = gl.glGetUniformLocation(progVideo, "uBreakupOn");
    GLint uVideoTearIntensity   = gl.glGetUniformLocation(progVideo, "uTearIntensity");
    GLint uVideoRollingBar      = gl.glGetUniformLocation(progVideo, "uRollingBarOn");
    GLint uVideoRollingPhase    = gl.glGetUniformLocation(progVideo, "uRollingBarPhase");
    GLint uVideoChromaGlitch    = gl.glGetUniformLocation(progVideo, "uChromaGlitchPhase");
    GLint uVideoTearRow         = gl.glGetUniformLocation(progVideo, "uTearRow");
    GLint uVideoTearOffset      = gl.glGetUniformLocation(progVideo, "uTearOffset");
    GLint uVideoDropoutFlash    = gl.glGetUniformLocation(progVideo, "uDropoutFlash");
    GLint uVideoDropoutActive   = gl.glGetUniformLocation(progVideo, "uDropoutFlashActive");
    GLint uVideoPerfMode        = gl.glGetUniformLocation(progVideo, "uPerfMode");
    GLint uVideoDotCrawl        = gl.glGetUniformLocation(progVideo, "uDotCrawlOn");
    GLint uVideoSyncLossOn      = gl.glGetUniformLocation(progVideo, "uSyncLossOn");
    GLint uVideoSyncLossTimer   = gl.glGetUniformLocation(progVideo, "uSyncLossTimer");
    GLint uVideoSyncLossSev     = gl.glGetUniformLocation(progVideo, "uSyncLossSeverity");
    GLint uVideoSyncLossPhase   = gl.glGetUniformLocation(progVideo, "uSyncLossPhase");
    GLint uVideoColorBurstOn    = gl.glGetUniformLocation(progVideo, "uColorBurstOn");
    GLint uVideoColorBurstTimer = gl.glGetUniformLocation(progVideo, "uColorBurstTimer");
    GLint uVideoColorBurstSev   = gl.glGetUniformLocation(progVideo, "uColorBurstSeverity");
    GLint uVideoGhostOn         = gl.glGetUniformLocation(progVideo, "uMultipathGhostOn");
    GLint uVideoGhostOffset     = gl.glGetUniformLocation(progVideo, "uMultipathGhostOffset");

    // Persistent video signal state
    VideoSignalState vsig;
    GLint uRes = gl.glGetUniformLocation(progOsd, "uRes");

    // Render the camera view to a texture, then apply analog RF artifacts before the OSD.
    GLuint sceneFbo = 0, resolveFbo = 0, sceneColor = 0, sceneColorMsaa = 0, sceneDepth = 0;
    int sceneWidth = 0, sceneHeight = 0;
    gl.glGenFramebuffers(1, &sceneFbo);
    gl.glGenFramebuffers(1, &resolveFbo);
    glGenTextures(1, &sceneColor);
    gl.glGenRenderbuffers(1, &sceneDepth);
    gl.glGenRenderbuffers(1, &sceneColorMsaa);
    GLuint shadowFbo = 0, shadowDepthTexture = 0;
    int shadowMapSize = 0;
    gl.glGenFramebuffers(1, &shadowFbo);
    glGenTextures(1, &shadowDepthTexture);
    
    // Debug framebuffer for prop overlay verification (created early to avoid GL issues)
    GLuint debugFbo = 0, debugTex = 0, debugDepth = 0;
    gl.glGenFramebuffers(1, &debugFbo);
    glGenTextures(1, &debugTex);
    gl.glGenRenderbuffers(1, &debugDepth);
    
    auto resizeShadowTarget = [&](int resolution) {
        resolution = resolution <= 512 ? 512 : (resolution <= 1024 ? 1024 : 2048);
        if (resolution == shadowMapSize) return;
        shadowMapSize = resolution;
        glBindTexture(GL_TEXTURE_2D, shadowDepthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, resolution, resolution, 0,
                     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthTexture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::fprintf(stderr, "real-time shadow framebuffer is incomplete\n");
            std::exit(1);
        }
        gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };
    resizeShadowTarget(cfg.shadowResolution);
    gl.glUseProgram(progWorld);
    gl.glUniform1i(uShadowMap, 1);
    auto resizeSceneTarget = [&](int width, int height) {
        if (width <= 0 || height <= 0 || (width == sceneWidth && height == sceneHeight)) return;
        sceneWidth = width; sceneHeight = height;
        glBindTexture(GL_TEXTURE_2D, sceneColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
        gl.glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
        gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepth);
        if (cfg.msaa > 1) {
            gl.glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
            gl.glRenderbufferStorageMultisample(GL_RENDERBUFFER, cfg.msaa, GL_DEPTH24_STENCIL8, width, height);
            gl.glBindRenderbuffer(GL_RENDERBUFFER, sceneColorMsaa);
            gl.glRenderbufferStorageMultisample(GL_RENDERBUFFER, cfg.msaa, GL_RGBA8, width, height);
            gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sceneColorMsaa);
        } else {
            gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);
        }
        if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::fprintf(stderr, "analog video framebuffer is incomplete\n");
            std::exit(1);
        }
        if (cfg.msaa > 1) {
            gl.glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);
            if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::fprintf(stderr, "analog video resolve framebuffer is incomplete\n");
                std::exit(1);
            }
        }
        gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };

    // simulation + world mesh
    Sim sim;
    sim.P = cfg.drone;
    applyWindConfig(sim, cfg);
    int mapIndex = (startMapIndex >= 0 && startMapIndex < 5) ? startMapIndex : 0;
    const char* mapNames[] = {"FREESTYLE PARK", "GATE TRAINING", "SLALOM TRAINING", "3.5 TECH PARK", "ABANDONED BASE"};
    if (mapIndex == 0) { sim.world = buildFreestylePark(); sim.thinCylinders.clear(); sim.startPos = {0.f, 0.12f, 14.f}; }
    else if (mapIndex == 1) { sim.world = buildGateTraining(); sim.thinCylinders.clear(); sim.startPos = {0.f, 0.12f, 14.f}; }
    else if (mapIndex == 2) { sim.world = buildSlalomTraining(); sim.thinCylinders.clear(); sim.startPos = {0.f, 0.12f, 14.f}; }
    else if (mapIndex == 3) { sim.world = build35TechPark(); sim.thinCylinders.clear(); sim.startPos = {-42.f, 0.12f, 42.f}; }
    else if (mapIndex == 4) { sim.world = buildAbandonedBase(cfg.abandonedBaseSeed); sim.thinCylinders.clear(); sim.startPos = {0.f, 1.5f, 10.f}; }
    
    // Ground audit - analyze layers after world is built
    if (groundAudit || colorCensusTest) {
        std::vector<GroundLayer> layers;
        analyzeGroundLayers(sim.world, layers);
        printGroundLayerInfo(layers);
        // Print projection info
        int dw, dh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        float aspect = (float)dw / (float)dh;
        float tanH = std::tan(rad(cfg.fovH) * 0.5f);
        static const float qualityFar[] = {210.f, 1000.f, 3000.f};
        const int quality = std::max(0, std::min(2, cfg.quality));
        const float zn = 0.05f, zf = qualityFar[quality];
        float P[16] = {0};
        P[0] = 1.f / tanH;
        P[5] = 1.f / (tanH / aspect);
        P[10] = (zf + zn) / (zn - zf);
        P[11] = -1.f;
        P[14] = 2.f * zf * zn / (zn - zf);
        printProjectionInfo(zn, zf, P, dw, dh, cfg.fovH);
    }
    
    sim.reset();

    std::vector<float> mesh = buildMesh(sim.world);
    GLsizei meshVerts = (GLsizei)(mesh.size() / 10);
    GLuint vaoWorld, vboWorld;
    gl.glGenVertexArrays(1, &vaoWorld);
    gl.glBindVertexArray(vaoWorld);
    gl.glGenBuffers(1, &vboWorld);
    gl.glBindBuffer(GL_ARRAY_BUFFER, vboWorld);
    gl.glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(float), mesh.data(), GL_STATIC_DRAW);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    gl.glEnableVertexAttribArray(1);
    gl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    gl.glEnableVertexAttribArray(2);
    gl.glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    gl.glEnableVertexAttribArray(3);
    gl.glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(9 * sizeof(float)));

    // Drone mesh for shadow
    std::vector<float> droneMesh = buildDroneMesh(sim.P);
    GLsizei droneMeshVerts = (GLsizei)(droneMesh.size() / 10);
    GLuint vaoDrone, vboDrone;
    gl.glGenVertexArrays(1, &vaoDrone);
    gl.glBindVertexArray(vaoDrone);
    gl.glGenBuffers(1, &vboDrone);
    gl.glBindBuffer(GL_ARRAY_BUFFER, vboDrone);
    gl.glBufferData(GL_ARRAY_BUFFER, droneMesh.size() * sizeof(float), droneMesh.data(), GL_STATIC_DRAW);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    gl.glEnableVertexAttribArray(1);
    gl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    gl.glEnableVertexAttribArray(2);
    gl.glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    gl.glEnableVertexAttribArray(3);
    gl.glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(9 * sizeof(float)));
    
    // Print triangle counts for both presets for verification
    DroneParams p35 = sim.P; // 3.5" preset has armLength=0.075
    p35.armLength = 0.075f;
    std::vector<float> droneMesh35 = buildDroneMesh(p35);
    std::printf("Drone mesh triangles: %d (3.5\" preset, armLen=%.3fm)\n", (int)(droneMesh35.size() / 30), p35.armLength);
    
    DroneParams p5 = sim.P; // 5" preset has armLength=0.105
    p5.armLength = 0.105f;
    std::vector<float> droneMesh5 = buildDroneMesh(p5);
    std::printf("Drone mesh triangles: %d (5\" preset, armLen=%.3fm)\n", (int)(droneMesh5.size() / 30), p5.armLength);
    
    // Propeller overlay mesh (for FPV camera view)
    std::vector<float> propOverlayMesh = buildPropOverlayMesh(sim.P);
    GLsizei propOverlayMeshVerts = (GLsizei)(propOverlayMesh.size() / 11);
    GLuint vaoPropOverlay, vboPropOverlay;
    gl.glGenVertexArrays(1, &vaoPropOverlay);
    gl.glBindVertexArray(vaoPropOverlay);
    gl.glGenBuffers(1, &vboPropOverlay);
    gl.glBindBuffer(GL_ARRAY_BUFFER, vboPropOverlay);
    gl.glBufferData(GL_ARRAY_BUFFER, propOverlayMesh.size() * sizeof(float), propOverlayMesh.data(), GL_STATIC_DRAW);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)0);
    gl.glEnableVertexAttribArray(1);
    gl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(3 * sizeof(float)));
    gl.glEnableVertexAttribArray(2);
    gl.glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(6 * sizeof(float)));
    gl.glEnableVertexAttribArray(3);
    gl.glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(9 * sizeof(float)));
    gl.glEnableVertexAttribArray(4);
    gl.glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(10 * sizeof(float)));
    std::printf("Prop overlay mesh triangles: %d\n", propOverlayMeshVerts / 3);
    
    // Projection test: print prop center screen positions for various FOV/tilt combinations
    // Prop positions in drone-local frame (same as in buildPropOverlayMesh)
    float armLen = sim.P.armLength;
    float armCoord = armLen * 0.70710678f;
    // float bodyHeight = 0.025f;  // unused in this function
    // float bodyDepth = armLen * 0.25f;  // unused in this function
    float motorHeight = 0.02f;
    // float cameraSize = armLen * 0.15f;  // unused in this function
    // Camera mount offset - MATCH MAIN CAMERA: {0, 0.03f, -0.07f}
    V3 camLocalPos = {0, 0.03f, -0.07f};
    V3 propPositions[4] = {
        { armCoord, motorHeight + 0.002f, -armCoord }, // front-right
        {-armCoord, motorHeight + 0.002f, -armCoord }, // front-left
        {-armCoord, motorHeight + 0.002f,  armCoord }, // back-left
        { armCoord, motorHeight + 0.002f,  armCoord }, // back-right
    };
    
    // Test FOV and tilt combinations
    float testFovs[] = {90.f, 120.f, 160.f};
    float testTilts[] = {0.f, 20.f, 33.f, 45.f};
    if (debugMode) {
        std::printf("\n=== PROP OVERLAY PROJECTION TEST ===\n");
        std::printf("Camera local pos: (%.3f, %.3f, %.3f)\n", camLocalPos.x, camLocalPos.y, camLocalPos.z);
        std::printf("Prop positions (drone-local):\n");
        for (int i = 0; i < 4; ++i) {
            std::printf("  Prop %d: (%.3f, %.3f, %.3f)\n", i, propPositions[i].x, propPositions[i].y, propPositions[i].z);
        }
        std::printf("\n");
        
        for (float fov : testFovs) {
            float tanH = std::tan(rad(fov) * 0.5f);
            for (float tilt : testTilts) {
                float tiltRad = rad(tilt);
                float cosT = cosf(tiltRad);
                float sinT = sinf(tiltRad);
                // View matrix (camera tilt only, no translation)
                V3 overlayRight = {1, 0, 0};
                V3 overlayUp = {0, cosT, sinT};
                V3 overlayBack = {0, -sinT, cosT};
                
                std::printf("FOV %.0f  Tilt %.0f:\n", fov, tilt);
                int visibleCount = 0;
                for (int i = 0; i < 4; ++i) {
                    // Transform prop position to camera-local coordinates
                    V3 relPos = {propPositions[i].x - camLocalPos.x, propPositions[i].y - camLocalPos.y, propPositions[i].z - camLocalPos.z};
                    // Apply camera tilt (view matrix R^T)
                    float cx = dot(overlayRight, relPos);
                    float cy = dot(overlayUp, relPos);
                    float cz = dot(overlayBack, relPos);
                    
                    // In camera space, points in front have NEGATIVE Z (camera looks down -Z)
                    // Visible if cz < 0 (in front) and within NDC bounds
                    float aspect = 16.f / 9.f;
                    float tanV = tanH / aspect;
                    float ndcX = cx / (-cz * tanH);
                    float ndcY = cy / (-cz * tanV);
                    
                    bool visible = (cz < 0) && (ndcX >= -1.0f && ndcX <= 1.0f) && (ndcY >= -1.0f && ndcY <= 1.0f);
                    if (visible) visibleCount++;
                    
                    std::printf("  Prop %d: ndc=(%.3f, %.3f) cz=%.3f %s\n", i, ndcX, ndcY, cz, visible ? "VISIBLE" : "culled");
                }
                std::printf("  -> %d/4 props visible\n", visibleCount);
            }
        }
        std::printf("=== END PROJECTION TEST ===\n\n");
    }
    
    // Spin test: print prop angle over 5 frames at 3 throttle levels
    if (debugMode) {
        std::printf("=== PROP OVERLAY SPIN TEST ===\n");
        float testRPMs[3][4] = {
            {1000.f, 1000.f, 1000.f, 1000.f},   // Idle ~1000 RPM
            {15000.f, 15000.f, 15000.f, 15000.f}, // 50% throttle ~15000 RPM
            {28000.f, 28000.f, 28000.f, 28000.f}  // 100% throttle ~28000 RPM
        };
        const char* throttleNames[3] = {"Idle (1000 RPM)", "50% (15000 RPM)", "100% (28000 RPM)"};
        for (int t = 0; t < 3; ++t) {
            std::printf("\n%s:\n", throttleNames[t]);
            for (int frame = 0; frame < 5; ++frame) {
                float time = frame * (1.f / 60.f); // 60 FPS
                std::printf("  Frame %d (t=%.3fs): ", frame, time);
                for (int i = 0; i < 4; ++i) {
                    float rpm = testRPMs[t][i];
                    float dir = (i == 0 || i == 3) ? 1.0f : -1.0f;
                    float angle = rpm * (2.f * PI / 60.f) * time * dir;
                    float angleDeg = angle * 180.f / PI;
                    // Normalize to 0-360
                    while (angleDeg < 0) angleDeg += 360.f;
                    while (angleDeg >= 360.f) angleDeg -= 360.f;
                    std::printf("Prop%d=%.1f° ", i, angleDeg);
                }
                std::printf("\n");
            }
        }
        std::printf("=== END SPIN TEST ===\n\n");
    }
    
    // Blur test: print disc alpha at idle/50%/100% throttle
    if (debugMode) {
        std::printf("=== PROP OVERLAY BLUR TEST ===\n");
        float testRPMsBlur[3] = {1000.f, 15000.f, 28000.f};
        const char* throttleNamesBlur[3] = {"Idle (1000 RPM)", "50% (15000 RPM)", "100% (28000 RPM)"};
        float maxRPM = 30000.f;
        for (int t = 0; t < 3; ++t) {
            float rpm = testRPMsBlur[t];
            float rpmNorm = std::min(1.f, rpm / maxRPM);
            // smoothstep(0.3, 0.8, rpmNorm)
            float bladeAlpha, discAlpha;
            if (rpmNorm <= 0.3f) {
                bladeAlpha = 1.0f;
                discAlpha = 0.0f;
            } else if (rpmNorm >= 0.8f) {
                bladeAlpha = 0.0f;
                discAlpha = 1.0f;
            } else {
                float t_smooth = (rpmNorm - 0.3f) / (0.8f - 0.3f);
                float smooth = t_smooth * t_smooth * (3.f - 2.f * t_smooth); // smoothstep
                bladeAlpha = 1.0f - smooth;
                discAlpha = smooth;
            }
            // Propeller alpha = bladeAlpha * wobble + discAlpha * 0.3 * wobble (wobble ~1.0 at these RPMs)
            float propAlpha = bladeAlpha + discAlpha * 0.3f;
            float finalAlpha = propAlpha * 0.85f; // 85% opacity default
            std::printf("%s: rpmNorm=%.3f, bladeAlpha=%.3f, discAlpha=%.3f, propAlpha=%.3f, finalAlpha=%.3f\n",
                        throttleNamesBlur[t], rpmNorm, bladeAlpha, discAlpha, propAlpha, finalAlpha);
        }
        std::printf("=== END BLUR TEST ===\n\n");
    }
    
    // Depth test: verify props render on top of wall 5cm in front of camera
    if (debugMode) {
        std::printf("=== PROP OVERLAY DEPTH TEST ===\n");
        // Reuse camera position from projection test (already computed)
        // Camera position in drone-local frame
        float armLenDepth = sim.P.armLength;
        float bodyHeightDepth = 0.025f;
        float bodyDepthDepth = armLenDepth * 0.25f;
        float cameraSizeDepth = armLenDepth * 0.15f;
        V3 camLocalPosDepth = {0, bodyHeightDepth * 0.5f + cameraSizeDepth * 0.5f, -bodyDepthDepth - cameraSizeDepth * 0.5f};
        std::printf("Camera local pos: (%.3f, %.3f, %.3f)\n", camLocalPosDepth.x, camLocalPosDepth.y, camLocalPosDepth.z);
        
        // Prop positions (front props)
        V3 prop0Depth = { armLenDepth * 0.70710678f, 0.022f, -armLenDepth * 0.70710678f };
        V3 prop1Depth = {-armLenDepth * 0.70710678f, 0.022f, -armLenDepth * 0.70710678f };
        std::printf("Prop 0 (drone-local): (%.3f, %.3f, %.3f)\n", prop0Depth.x, prop0Depth.y, prop0Depth.z);
        std::printf("Prop 1 (drone-local): (%.3f, %.3f, %.3f)\n", prop1Depth.x, prop1Depth.y, prop1Depth.z);
        
        // Wall 5cm in front of camera (in camera space, this is at Z = -0.05)
        float tiltRadDepth = rad(cfg.camTilt); // default 33°
        float cosTDepth = cosf(tiltRadDepth);
        float sinTDepth = sinf(tiltRadDepth);
        V3 overlayBackDepth = {0, -sinTDepth, cosTDepth}; // Camera -Z axis in drone-local
        
        // Wall is 5cm along camera -Z from camera position
        V3 wallPosDepth = camLocalPosDepth + overlayBackDepth * (-0.05f); // 5cm in front
        std::printf("Wall pos (drone-local, 5cm in front of cam): (%.3f, %.3f, %.3f)\n", wallPosDepth.x, wallPosDepth.y, wallPosDepth.z);
        
        // Compute depth (distance along camera -Z) for props and wall
        // In camera space, depth = -dot(overlayBack, point - camLocalPos)
        auto computeDepth = [&](const V3& p, const V3& camPos, const V3& camBack) -> float {
        V3 rel = {p.x - camPos.x, p.y - camPos.y, p.z - camPos.z};
        return -dot(camBack, rel); // positive = in front of camera
    };
    
    float prop0DepthVal = computeDepth(prop0Depth, camLocalPosDepth, overlayBackDepth);
    float prop1DepthVal = computeDepth(prop1Depth, camLocalPosDepth, overlayBackDepth);
    float wallDepthVal = computeDepth(wallPosDepth, camLocalPosDepth, overlayBackDepth);
    std::printf("Prop 0 depth (camera -Z): %.4f m\n", prop0DepthVal);
    std::printf("Prop 1 depth (camera -Z): %.4f m\n", prop1DepthVal);
    std::printf("Wall depth (camera -Z):   %.4f m\n", wallDepthVal);
    
    // Props should be closer (smaller depth) than wall
    bool prop0InFront = prop0DepthVal < wallDepthVal;
    bool prop1InFront = prop1DepthVal < wallDepthVal;
    std::printf("Prop 0 in front of wall: %s (%.4f < %.4f)\n", prop0InFront ? "YES" : "NO", prop0DepthVal, wallDepthVal);
    std::printf("Prop 1 in front of wall: %s (%.4f < %.4f)\n", prop1InFront ? "YES" : "NO", prop1DepthVal, wallDepthVal);
    std::printf("=== END DEPTH TEST ===\n\n");
    }
    
    GLuint vaoTraffic, vboTraffic;
    gl.glGenVertexArrays(1, &vaoTraffic);
    gl.glBindVertexArray(vaoTraffic);
    gl.glGenBuffers(1, &vboTraffic);
    gl.glBindBuffer(GL_ARRAY_BUFFER, vboTraffic);
    gl.glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    gl.glEnableVertexAttribArray(1);
    gl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    gl.glEnableVertexAttribArray(2);
    gl.glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    gl.glEnableVertexAttribArray(3);
    gl.glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(9 * sizeof(float)));

    // Level file for abandoned base (loaded on demand)
    std::string levelFilePath = "abandoned_base.level";

    // Variables needed by lambdas (must be declared before lambdas)
    float overlayTimer = 0.f;
    float videoNoticeTimer = 0.f;
    int frameCounter = 0;

    auto loadAbandonedBaseFromFile = [&](const char* path) {
        LevelData data = loadLevelFile(path);
        if (!data.valid) return false;
        sim.world = levelDataToBoxes(data);
        sim.thinCylinders.clear(); // thin objects rendered as boxes
        sim.startPos = data.spawn.pos;
        // Note: spawn yaw not currently used by sim
        sim.reset();
        
        std::vector<float> meshData = buildMesh(sim.world);
        gl.glBindBuffer(GL_ARRAY_BUFFER, vboWorld);
        gl.glBufferData(GL_ARRAY_BUFFER, meshData.size() * sizeof(float), meshData.data(), GL_STATIC_DRAW);
        meshVerts = (GLsizei)(meshData.size() / 10);
        return true;
    };

    auto loadAbandonedBase = [&](uint32_t seed) {
        sim.world = buildAbandonedBase(seed);
        sim.thinCylinders.clear();
        sim.startPos = {0.f, 1.5f, 10.f};  // Speed zone entry
        sim.reset();
        
        std::vector<float> data = buildMesh(sim.world);
        gl.glBindBuffer(GL_ARRAY_BUFFER, vboWorld);
        gl.glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
        meshVerts = (GLsizei)(data.size() / 10);
    };

    auto loadMap = [&]() {
        if (mapIndex == 0) { sim.world = buildFreestylePark(); sim.thinCylinders.clear(); sim.startPos = {0.f, 0.12f, 14.f}; }
        else if (mapIndex == 1) { sim.world = buildGateTraining(); sim.thinCylinders.clear(); sim.startPos = {0.f, 0.12f, 14.f}; }
        else if (mapIndex == 2) { sim.world = buildSlalomTraining(); sim.thinCylinders.clear(); sim.startPos = {0.f, 0.12f, 14.f}; }
        else if (mapIndex == 3) { sim.world = build35TechPark(); sim.thinCylinders.clear(); sim.startPos = {-42.f, 0.12f, 42.f}; }
        else if (mapIndex == 4) { 
            // Try to load level file, fall back to procedural generation
            if (!loadAbandonedBaseFromFile(levelFilePath.c_str())) {
                loadAbandonedBase(cfg.abandonedBaseSeed);
            }
        }
        
        // Ground audit - analyze layers after world is built
        if (groundAudit || colorCensusTest) {
            std::vector<GroundLayer> layers;
            analyzeGroundLayers(sim.world, layers);
            printGroundLayerInfo(layers);
            // Print projection info
            int dw, dh;
            SDL_GL_GetDrawableSize(win, &dw, &dh);
            float aspect = (float)dw / (float)dh;
            float tanH = std::tan(rad(cfg.fovH) * 0.5f);
            static const float qualityFar[] = {210.f, 1000.f, 3000.f};
            const int quality = std::max(0, std::min(2, cfg.quality));
            const float zn = 0.05f, zf = qualityFar[quality];
            float P[16] = {0};
            P[0] = 1.f / tanH;
            P[5] = 1.f / (tanH / aspect);
            P[10] = (zf + zn) / (zn - zf);
            P[11] = -1.f;
            P[14] = 2.f * zf * zn / (zn - zf);
            printProjectionInfo(zn, zf, P, dw, dh, cfg.fovH);
        }
        
        sim.reset();
        if (mapIndex != 4) {
            std::vector<float> data = buildMesh(sim.world);
            gl.glBindBuffer(GL_ARRAY_BUFFER, vboWorld);
            gl.glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
            meshVerts = (GLsizei)(data.size() / 10);
        }
    };
    
    GLuint vaoSky;
    gl.glGenVertexArrays(1, &vaoSky);

    GLuint vaoOsd, vboOsd;
    gl.glGenVertexArrays(1, &vaoOsd);
    gl.glBindVertexArray(vaoOsd);
    gl.glGenBuffers(1, &vboOsd);
    gl.glBindBuffer(GL_ARRAY_BUFFER, vboOsd);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    gl.glEnableVertexAttribArray(1);
    gl.glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));

    // radio
    Radio radio;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) tryOpen(radio, i, cfg);
    if (!radio.joy) std::printf("no radio found - keyboard mode (plug in the Pocket in USB Joystick mode, it hot-plugs)\n");
    
    // Print key bindings
    std::printf("\n=== WenaFPV KEY BINDINGS ===\n");
    std::printf("F1: Toggle monitor mode\n");
    std::printf("F2: Toggle keyboard/radio mode\n");
    std::printf("F3/F4: Previous/Next map\n");
    std::printf("F5: Settings (abandoned base: reload level file)\n");
    std::printf("F11: Toggle fullscreen\n");
    std::printf("R: Reset drone\n");
    std::printf("SPACE: Arm/disarm\n");
    std::printf("[/]: Camera tilt down/up\n");
    std::printf("-/=: FOV down/up\n");
    std::printf("W/S: Keyboard throttle up/down\n");
    std::printf("ARROWS: Keyboard pitch/roll\n");
    std::printf("A/D: Keyboard yaw\n");
    std::printf("C: Toggle analog video\n");
    std::printf("=============================\n\n");

    // state
    bool running = true, monitor = false, forceKeyboard = false;
    bool settingsOpen = false, editingText = false, restartNotice = false;
    int settingsTab = 0, settingsSelection = 0, settingsScroll = 0;
    std::string settingsMessage, originalText;
    float settingsMessageTimer = 0.f;
    float kbThrottle = 0.f;
    bool kbArm = false, resetPrev = false;
    double fpsAvg = 60.0;
    const float PHYS_DT = 0.002f;  // 500 Hz
    double acc = 0.0;
    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = (double)SDL_GetPerformanceFrequency();
    Batch osd;
    V3 smoothedShadowFocus{};
    bool shadowFocusInitialized = false;

    // Debug feature toggles for flickering investigation
    bool dbgWireframe = false;
    int dbgFreezeFrame = -1;
    V3 dbgFreezeCamPos{};
    Quat dbgFreezeCamQuat{};
    std::vector<uint8_t> dbgPrevFramePixels;

    // Dynamic FPS (like Minecraft Dynamic FPS mod)
    // Lowers FPS to 5 when window unfocused or after 60s inactivity
    Uint64 lastInputTime = SDL_GetPerformanceCounter();
    bool windowFocused = true;
    const double DYNAMIC_FPS_THRESHOLD = 60.0;  // seconds of inactivity
    const int IDLE_FPS = 5;  // FPS when idle
    const double IDLE_FRAME_TIME = 1.0 / IDLE_FPS;  // 0.2s = 200ms per frame
    bool isThrottled = false;
    double currentFPS = 60.0;

    // Debug overlay analysis (runs once before main loop)
    if (debugOverlay) {
        // Get window size for framebuffer
        int dw, dh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        
        debugRenderPropOverlay(
            sim, cfg,
            vaoPropOverlay, propOverlayMeshVerts,
            dw, dh,
            debugFbo, debugTex, debugDepth
        );
    }

    // Debug props lowering verification (runs once before main loop)
    if (debugPropsLowering) {
        int dw, dh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        
        verifyPropsLowering(
            sim,
            vaoPropOverlay, propOverlayMeshVerts,
            dw, dh,
            debugFbo, debugTex, debugDepth
        );
        // Exit after verification
        running = false;
    }

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_JOYDEVICEADDED: tryOpen(radio, e.jdevice.which, cfg); break;
                case SDL_JOYDEVICEREMOVED:
                    if (radio.joy && e.jdevice.which == radio.id) {
                        SDL_JoystickClose(radio.joy);
                        radio = Radio{};
                        std::printf("radio disconnected\n");
                    }
                    break;
                case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                        windowFocused = true;
                        lastInputTime = SDL_GetPerformanceCounter();  // Reset timer on focus
                    } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                        windowFocused = false;
                    }
                    break;
                case SDL_MOUSEMOTION:
                case SDL_MOUSEBUTTONUP:
                case SDL_MOUSEWHEEL:
                    lastInputTime = SDL_GetPerformanceCounter();
                    break;
                case SDL_KEYDOWN: {
                    SDL_Scancode sc = e.key.keysym.scancode;
                    if (settingsOpen) {
                        if (sc == SDL_SCANCODE_F6 && !e.key.repeat) {
                            restartNotice = cfg.width != startupCfg.width || cfg.height != startupCfg.height ||
                                            cfg.fullscreen != startupCfg.fullscreen || cfg.msaa != startupCfg.msaa ||
                                            cfg.joystickName != startupCfg.joystickName;
                            if (saveConfig(cfg, cfgPath)) {
                                sim.P = cfg.drone;
                                applyWindConfig(sim, cfg);
                                sim.reset();
                                kbArm = false; kbThrottle = 0.f;
                                SDL_GL_SetSwapInterval(cfg.vsync ? 1 : 0);
                                settingsMessage = restartNotice ? "SOME SETTINGS NEED A RESTART" : "SAVED TO CONFIG INI";
                            } else settingsMessage = "SAVE FAILED - CHECK CONFIG PATH";
                            settingsMessageTimer = 3.f;
                        } else if (editingText && (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)) {
                            editingText = false; SDL_StopTextInput();
                        } else if (editingText && sc == SDL_SCANCODE_BACKSPACE && !settingEntries.empty()) {
                            std::vector<int> visible;
                            for (size_t i = 0; i < settingEntries.size(); ++i)
                                if (std::string(settingEntries[i].key) != "config_version" && settingTab(settingEntries[i]) == settingsTab)
                                    visible.push_back((int)i);
                            if (!visible.empty()) {
                                Entry& ent = settingEntries[visible[std::min(settingsSelection, (int)visible.size() - 1)]];
                                if (ent.type == Entry::STR && !((std::string*)ent.ptr)->empty()) ((std::string*)ent.ptr)->pop_back();
                            }
                        } else if (sc == SDL_SCANCODE_ESCAPE) {
                            if (editingText) {
                                std::vector<int> visible;
                                for (size_t i = 0; i < settingEntries.size(); ++i)
                                    if (std::string(settingEntries[i].key) != "config_version" && settingTab(settingEntries[i]) == settingsTab)
                                        visible.push_back((int)i);
                                if (!visible.empty()) {
                                    Entry& ent = settingEntries[visible[std::min(settingsSelection, (int)visible.size() - 1)]];
                                    if (ent.type == Entry::STR) *(std::string*)ent.ptr = originalText;
                                }
                                editingText = false; SDL_StopTextInput();
                            }
                            else settingsOpen = false;
                        } else if (sc == SDL_SCANCODE_TAB) {
                            settingsTab = (settingsTab + (e.key.keysym.mod & KMOD_SHIFT ? SETTING_TAB_COUNT - 1 : 1)) % SETTING_TAB_COUNT;
                            settingsSelection = settingsScroll = 0;
                        } else if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN) {
                            std::vector<int> visible;
                            for (size_t i = 0; i < settingEntries.size(); ++i)
                                if (std::string(settingEntries[i].key) != "config_version" && settingTab(settingEntries[i]) == settingsTab)
                                    visible.push_back((int)i);
                            if (!visible.empty()) {
                                settingsSelection = (settingsSelection + (sc == SDL_SCANCODE_DOWN ? 1 : (int)visible.size() - 1)) % (int)visible.size();
                                if (settingsSelection < settingsScroll) settingsScroll = settingsSelection;
                                if (settingsSelection >= settingsScroll + 9) settingsScroll = settingsSelection - 8;
                            }
                        } else if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_RETURN ||
                                   sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_SPACE) {
                            std::vector<int> visible;
                            for (size_t i = 0; i < settingEntries.size(); ++i)
                                if (std::string(settingEntries[i].key) != "config_version" && settingTab(settingEntries[i]) == settingsTab)
                                    visible.push_back((int)i);
                            if (!visible.empty()) {
                                Entry& ent = settingEntries[visible[std::min(settingsSelection, (int)visible.size() - 1)]];
                                if (ent.type == Entry::STR && (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_SPACE)) {
                                    originalText = *(std::string*)ent.ptr;
                                    editingText = true; SDL_StartTextInput();
                                } else if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT) {
                                    changeSetting(ent, sc == SDL_SCANCODE_RIGHT ? 1 : -1);
                                    if (std::string(ent.key) == "quality") applyQuality(cfg);
                                    if (std::string(ent.key) == "drone_size") applyDroneProfile(cfg);
                                    markCustomDrone(cfg, ent);
                                } else if (ent.type == Entry::BOOL) {
                                    changeSetting(ent, 1);
                                    markCustomDrone(cfg, ent);
                                }
                                else {
                                    changeSetting(ent, 1);
                                    if (std::string(ent.key) == "quality") applyQuality(cfg);
                                    if (std::string(ent.key) == "drone_size") applyDroneProfile(cfg);
                                    markCustomDrone(cfg, ent);
                                }
                            }
                        }
                        break;
                    }
                    if (sc == SDL_SCANCODE_ESCAPE) running = false;
                    else if (sc == SDL_SCANCODE_F5 && !e.key.repeat) {
                        settingsOpen = true; settingsTab = 0; settingsSelection = settingsScroll = 0;
                        sim.d.armed = false; sim.d.thrust = 0.f;
                    }
                    else if (sc == SDL_SCANCODE_F1 && !e.key.repeat) monitor = !monitor;
                    else if (sc == SDL_SCANCODE_C && !e.key.repeat) {
                        cfg.videoEnabled = !cfg.videoEnabled;
                        videoNoticeTimer = 2.f;
                    }
                    else if (sc == SDL_SCANCODE_F2 && !e.key.repeat) forceKeyboard = !forceKeyboard;
                    else if (sc == SDL_SCANCODE_R && !e.key.repeat) { sim.reset(); kbArm = false; kbThrottle = 0; }
                    else if ((sc == SDL_SCANCODE_F3 || sc == SDL_SCANCODE_F4) && !e.key.repeat) {
                        mapIndex = sc == SDL_SCANCODE_F3 ? (mapIndex + 4) % 5 : (mapIndex + 1) % 5;
                        loadMap(); kbArm = false; kbThrottle = 0; overlayTimer = 3.f;
                    }
                    else if (sc == SDL_SCANCODE_0 && !e.key.repeat) {
                        if (mapIndex == 4) {
                            // Reload level file for abandoned base
                            if (loadAbandonedBaseFromFile(levelFilePath.c_str())) {
                                overlayTimer = 2.f;
                            }
                        }
                    }
                    else if (sc == SDL_SCANCODE_F5 && !e.key.repeat) {
                        settingsOpen = true; settingsTab = 0; settingsSelection = settingsScroll = 0;
                        sim.d.armed = false; sim.d.thrust = 0.f;
                    }
                    else if (sc == SDL_SCANCODE_F11 && !e.key.repeat) {
                        cfg.fullscreen = !cfg.fullscreen;
                        SDL_SetWindowFullscreen(win, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        startupCfg.fullscreen = cfg.fullscreen;
                    }
                    // Debug feature toggles for flickering investigation
                    else if (sc == SDL_SCANCODE_F6 && !e.key.repeat) { dbgDisableHash21 = !dbgDisableHash21; std::printf("DEBUG: hash21 %s\n", dbgDisableHash21 ? "DISABLED" : "ENABLED"); }
                    else if (sc == SDL_SCANCODE_F7 && !e.key.repeat) { dbgDisableGrain = !dbgDisableGrain; std::printf("DEBUG: grain %s\n", dbgDisableGrain ? "DISABLED" : "ENABLED"); }
                    else if (sc == SDL_SCANCODE_F8 && !e.key.repeat) { dbgDisableScratch = !dbgDisableScratch; std::printf("DEBUG: scratch %s\n", dbgDisableScratch ? "DISABLED" : "ENABLED"); }
                    else if (sc == SDL_SCANCODE_F9 && !e.key.repeat) { dbgDisableDetail = !dbgDisableDetail; std::printf("DEBUG: detail %s\n", dbgDisableDetail ? "DISABLED" : "ENABLED"); }
                    else if (sc == SDL_SCANCODE_F10 && !e.key.repeat) { dbgDisableShadows = !dbgDisableShadows; std::printf("DEBUG: shadows %s\n", dbgDisableShadows ? "DISABLED" : "ENABLED"); }
                    else if (sc == SDL_SCANCODE_F12 && !e.key.repeat) { dbgWireframe = !dbgWireframe; std::printf("DEBUG: wireframe %s\n", dbgWireframe ? "ENABLED" : "DISABLED"); glPolygonMode(GL_FRONT_AND_BACK, dbgWireframe ? GL_LINE : GL_FILL); }
                    // Ground flickering diagnostics
                    else if (sc == SDL_SCANCODE_G && !e.key.repeat) { 
                        std::vector<GroundLayer> layers;
                        analyzeGroundLayers(sim.world, layers);
                        printGroundLayerInfo(layers);
                    }
                    else if (sc == SDL_SCANCODE_H && !e.key.repeat) {
                        // Trigger color census on next frame
                        colorCensusTest = true;
                        std::printf("Color census will run on next frame\n");
                    }
                    else if (sc == SDL_SCANCODE_J && !e.key.repeat) { disableGroundLayer0 = !disableGroundLayer0; std::printf("DEBUG: ground layer 0 %s\n", disableGroundLayer0 ? "DISABLED" : "ENABLED"); 
                        bool disableLayers[8] = {disableGroundLayer0, disableGroundLayer1, disableGroundLayer2, disableGroundLayer3, disableGroundLayer4, disableGroundLayer5, disableGroundLayer6, disableGroundLayer7};
                        std::vector<float> newMesh = buildMeshWithGroundToggles(sim.world, disableLayers);
                        gl.glBindBuffer(GL_ARRAY_BUFFER, vboWorld);
                        gl.glBufferData(GL_ARRAY_BUFFER, newMesh.size() * sizeof(float), newMesh.data(), GL_STATIC_DRAW);
                        meshVerts = (GLsizei)(newMesh.size() / 10);
                    }
                    else if (sc == SDL_SCANCODE_K && !e.key.repeat) { disableGroundLayer1 = !disableGroundLayer1; std::printf("DEBUG: ground layer 1 %s\n", disableGroundLayer1 ? "DISABLED" : "ENABLED"); 
                        bool disableLayers[8] = {disableGroundLayer0, disableGroundLayer1, disableGroundLayer2, disableGroundLayer3, disableGroundLayer4, disableGroundLayer5, disableGroundLayer6, disableGroundLayer7};
                        std::vector<float> newMesh = buildMeshWithGroundToggles(sim.world, disableLayers);
                        gl.glBindBuffer(GL_ARRAY_BUFFER, vboWorld);
                        gl.glBufferData(GL_ARRAY_BUFFER, newMesh.size() * sizeof(float), newMesh.data(), GL_STATIC_DRAW);
                        meshVerts = (GLsizei)(newMesh.size() / 10);
                    }
                    else if (sc == SDL_SCANCODE_L && !e.key.repeat) { disableGroundLayer2 = !disableGroundLayer2; std::printf("DEBUG: ground layer 2 %s\n", disableGroundLayer2 ? "DISABLED" : "ENABLED"); 
                        bool disableLayers[8] = {disableGroundLayer0, disableGroundLayer1, disableGroundLayer2, disableGroundLayer3, disableGroundLayer4, disableGroundLayer5, disableGroundLayer6, disableGroundLayer7};
                        std::vector<float> newMesh = buildMeshWithGroundToggles(sim.world, disableLayers);
                        gl.glBindBuffer(GL_ARRAY_BUFFER, vboWorld);
                        gl.glBufferData(GL_ARRAY_BUFFER, newMesh.size() * sizeof(float), newMesh.data(), GL_STATIC_DRAW);
                        meshVerts = (GLsizei)(newMesh.size() / 10);
                    }
                    else if (sc == SDL_SCANCODE_SEMICOLON && !e.key.repeat) { disableGroundLayer3 = !disableGroundLayer3; std::printf("DEBUG: ground layer 3 %s\n", disableGroundLayer3 ? "DISABLED" : "ENABLED"); 
                        bool disableLayers[8] = {disableGroundLayer0, disableGroundLayer1, disableGroundLayer2, disableGroundLayer3, disableGroundLayer4, disableGroundLayer5, disableGroundLayer6, disableGroundLayer7};
                        std::vector<float> newMesh = buildMeshWithGroundToggles(sim.world, disableLayers);
                        gl.glBindBuffer(GL_ARRAY_BUFFER, vboWorld);
                        gl.glBufferData(GL_ARRAY_BUFFER, newMesh.size() * sizeof(float), newMesh.data(), GL_STATIC_DRAW);
                        meshVerts = (GLsizei)(newMesh.size() / 10);
                    }
                    else if (sc == SDL_SCANCODE_SPACE && !e.key.repeat) kbArm = !kbArm;
                    else if (sc == SDL_SCANCODE_RIGHTBRACKET || sc == SDL_SCANCODE_PAGEUP) { cfg.camTilt = clampf(cfg.camTilt + 1.f, -10.f, 80.f); overlayTimer = 2.f; }
                    else if (sc == SDL_SCANCODE_LEFTBRACKET || sc == SDL_SCANCODE_PAGEDOWN) { cfg.camTilt = clampf(cfg.camTilt - 1.f, -10.f, 80.f); overlayTimer = 2.f; }
                    else if (sc == SDL_SCANCODE_EQUALS || sc == SDL_SCANCODE_KP_PLUS) { cfg.fovH = clampf(cfg.fovH + 5.f, 60.f, 150.f); overlayTimer = 2.f; }
                    else if (sc == SDL_SCANCODE_MINUS || sc == SDL_SCANCODE_KP_MINUS) { cfg.fovH = clampf(cfg.fovH - 5.f, 60.f, 150.f); overlayTimer = 2.f; }
                    break;
                }
                case SDL_TEXTINPUT:
                    if (settingsOpen && editingText && !settingEntries.empty()) {
                        std::vector<int> visible;
                        for (size_t i = 0; i < settingEntries.size(); ++i)
                            if (std::string(settingEntries[i].key) != "config_version" && settingTab(settingEntries[i]) == settingsTab)
                                visible.push_back((int)i);
                        if (!visible.empty()) {
                            Entry& ent = settingEntries[visible[std::min(settingsSelection, (int)visible.size() - 1)]];
                            if (ent.type == Entry::STR) *(std::string*)ent.ptr += e.text.text;
                        }
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    // Update lastInputTime for Dynamic FPS
                    lastInputTime = SDL_GetPerformanceCounter();
                    
                    if (settingsOpen && e.button.button == SDL_BUTTON_LEFT) {
                        int ww, wh, dw, dh;
                        SDL_GetWindowSize(win, &ww, &wh); SDL_GL_GetDrawableSize(win, &dw, &dh);
                        float mx = e.button.x * (float)dw / std::max(1, ww);
                        float my = e.button.y * (float)dh / std::max(1, wh);
                        float panelX = dw * 0.08f, panelY = dh * 0.08f, panelW = dw * 0.84f;
                        if (my >= panelY + 42 && my < panelY + 78) {
                            int tab = (int)((mx - panelX) / (panelW / SETTING_TAB_COUNT));
                            if (tab >= 0 && tab < SETTING_TAB_COUNT) { settingsTab = tab; settingsSelection = settingsScroll = 0; }
                        } else if (my > dh - panelY - 56 && mx > dw * 0.58f) {
                            float saveX = panelX + panelW - 220.f, closeX = panelX + panelW - 112.f;
                            if (mx >= saveX && mx <= saveX + 94.f) {
                                restartNotice = cfg.width != startupCfg.width || cfg.height != startupCfg.height ||
                                                cfg.fullscreen != startupCfg.fullscreen || cfg.msaa != startupCfg.msaa ||
                                                cfg.joystickName != startupCfg.joystickName;
                                if (saveConfig(cfg, cfgPath)) {
                                    sim.P = cfg.drone;
                                    applyWindConfig(sim, cfg);
                                    sim.reset();
                                    kbArm = false; kbThrottle = 0.f;
                                    SDL_GL_SetSwapInterval(cfg.vsync ? 1 : 0);
                                    settingsMessage = restartNotice ? "SOME SETTINGS NEED A RESTART" : "SAVED TO CONFIG INI";
                                } else settingsMessage = "SAVE FAILED - CHECK CONFIG PATH";
                                settingsMessageTimer = 3.f;
                            } else if (mx >= closeX && mx <= closeX + 94.f) {
                                settingsOpen = false;
                                if (editingText) { editingText = false; SDL_StopTextInput(); }
                            }
                        } else if (my >= panelY + 96 && my < panelY + 91.f + 9.f * 32.f) {
                            int row = settingsScroll + (int)((my - panelY - 96) / 32.f);
                            std::vector<int> visible;
                            for (size_t i = 0; i < settingEntries.size(); ++i)
                                if (std::string(settingEntries[i].key) != "config_version" && settingTab(settingEntries[i]) == settingsTab)
                                    visible.push_back((int)i);
                            if (row >= 0 && row < (int)visible.size()) {
                                settingsSelection = row;
                                Entry& ent = settingEntries[visible[row]];
                                if (mx > dw * 0.75f) {
                                    changeSetting(ent, 1);
                                    if (std::string(ent.key) == "quality") applyQuality(cfg);
                                    if (std::string(ent.key) == "drone_size") applyDroneProfile(cfg);
                                    markCustomDrone(cfg, ent);
                                } else if (mx > dw * 0.68f) {
                                    changeSetting(ent, -1);
                                    if (std::string(ent.key) == "quality") applyQuality(cfg);
                                    if (std::string(ent.key) == "drone_size") applyDroneProfile(cfg);
                                    markCustomDrone(cfg, ent);
                                }
                                else if (ent.type == Entry::BOOL) changeSetting(ent, 1);
                                else if (ent.type == Entry::STR) { originalText = *(std::string*)ent.ptr; editingText = true; SDL_StartTextInput(); }
                                else if (settingsTab == 0 && std::string(ent.key) == "quality") { changeSetting(ent, 1); applyQuality(cfg); }
                            }
                        }
                    }
                    break;
                default: break;
            }
        }

        // ---- timing
        ++frameCounter;
        Uint64 now = SDL_GetPerformanceCounter();
        double frameDt = (double)(now - last) / freq;
        last = now;
        frameDt = std::min(frameDt, 0.05);
        if (frameDt > 1e-5) fpsAvg += (1.0 / frameDt - fpsAvg) * 0.05;
        overlayTimer = std::max(0.f, overlayTimer - (float)frameDt);
        videoNoticeTimer = std::max(0.f, videoNoticeTimer - (float)frameDt);
        settingsMessageTimer = std::max(0.f, settingsMessageTimer - (float)frameDt);

        // ---- input
        Input in;
        const bool useRadio = radio.joy && !forceKeyboard;
        float mapped[6] = {0, 0, 0, 0, 0, 0};  // roll, pitch, throttle(-1..1), yaw, arm, reset
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        
        // Check for any input activity to update lastInputTime (for Dynamic FPS)
        bool hasInput = false;
        if (useRadio) {
            SDL_JoystickUpdate();
            mapped[0] = readAxis(radio, cfg.axRoll, cfg.invRoll);
            mapped[1] = readAxis(radio, cfg.axPitch, cfg.invPitch);
            mapped[2] = readAxis(radio, cfg.axThrottle, cfg.invThrottle);
            mapped[3] = readAxis(radio, cfg.axYaw, cfg.invYaw);
            mapped[4] = readAxis(radio, cfg.axArm, cfg.invArm);
            mapped[5] = readAxis(radio, cfg.axReset, cfg.invReset);
            // Check if any joystick axis is significantly moved
            if (fabsf(mapped[0]) > 0.1f || fabsf(mapped[1]) > 0.1f || 
                fabsf(mapped[2]) > 0.1f || fabsf(mapped[3]) > 0.1f ||
                fabsf(mapped[4]) > 0.1f || fabsf(mapped[5]) > 0.1f) {
                hasInput = true;
            }
            in.roll = applyDeadband(mapped[0], cfg.deadband);
            in.pitch = applyDeadband(mapped[1], cfg.deadband);
            in.yaw = applyDeadband(mapped[3], cfg.deadband);
            in.throttle = clampf((mapped[2] + 1.f) * 0.5f, 0.f, 1.f);
            if (in.throttle < 0.02f) in.throttle = 0.f;
            in.arm = cfg.axArm >= 0 ? mapped[4] > cfg.armThreshold : kbArm;
            bool resetNow = cfg.axReset >= 0 && mapped[5] > cfg.armThreshold;
            if (resetNow && !resetPrev) sim.reset();
            resetPrev = resetNow;
        } else {
            if (keys[SDL_SCANCODE_W]) kbThrottle += 0.6f * (float)frameDt;
            if (keys[SDL_SCANCODE_S]) kbThrottle -= 0.9f * (float)frameDt;
            kbThrottle = clampf(kbThrottle, 0.f, 1.f);
            in.throttle = kbThrottle;
            in.pitch = (keys[SDL_SCANCODE_UP] ? 1.f : 0.f) - (keys[SDL_SCANCODE_DOWN] ? 1.f : 0.f);
            in.roll = (keys[SDL_SCANCODE_RIGHT] ? 1.f : 0.f) - (keys[SDL_SCANCODE_LEFT] ? 1.f : 0.f);
            in.yaw = (keys[SDL_SCANCODE_D] ? 1.f : 0.f) - (keys[SDL_SCANCODE_A] ? 1.f : 0.f);
            in.arm = kbArm;
            mapped[0] = in.roll; mapped[1] = in.pitch; mapped[2] = in.throttle * 2.f - 1.f; mapped[3] = in.yaw;
            mapped[4] = kbArm ? 1.f : -1.f;
            // Check for keyboard input activity
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_UP] || 
                keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT] ||
                keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_D]) {
                hasInput = true;
            }
        }

        // Update lastInputTime if any input detected
        if (hasInput) {
            lastInputTime = SDL_GetPerformanceCounter();
        }

        // ---- physics (fixed step)
        acc += frameDt;
        int steps = 0;
        while (!settingsOpen && acc >= PHYS_DT && steps < 200) {
            sim.step(PHYS_DT, in);
            acc -= PHYS_DT;
            ++steps;
        }
        if (settingsOpen) acc = 0.0;
        
        audio.muted.store(cfg.muteAudio, std::memory_order_relaxed);
        audio.droneSize.store(cfg.droneSize, std::memory_order_relaxed);
        for (int m = 0; m < 4; ++m) {
            audio.motorRPM[m].store(sim.d.motorRPM[m], std::memory_order_relaxed);
        }

        // ---- camera
        int dw, dh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        resizeSceneTarget(dw, dh);
        if (dw < 1 || dh < 1) { SDL_GL_SwapWindow(win); continue; }
        const DroneState& d = sim.d;
        Quat cq = normalize(d.q * fromAxisAngle({1, 0, 0}, rad(cfg.camTilt)));
        V3 camPos = d.pos + rotate(d.q, {0, 0.03f, -0.07f});
        // Freeze test: use frozen camera after first frame
        if (freezeTest && dbgFreezeFrame >= 0) {
            cq = dbgFreezeCamQuat;
            camPos = dbgFreezeCamPos;
        }
        V3 right = rotate(cq, {1, 0, 0}), up = rotate(cq, {0, 1, 0}), back = rotate(cq, {0, 0, 1});
        float aspect = (float)dw / (float)dh;
        float tanH = std::tan(rad(cfg.fovH) * 0.5f);
        float tanV = tanH / aspect;
        static const float qualityFar[] = {210.f, 1000.f, 3000.f};
        static const float textureDetail[] = {0.22f, 0.62f, 1.0f};
        const int quality = std::max(0, std::min(2, cfg.quality));
        const float zn = 0.05f, zf = qualityFar[quality];
        float P[16] = {0};
        P[0] = 1.f / tanH;
        P[5] = 1.f / tanV;
        P[10] = (zf + zn) / (zn - zf);
        P[11] = -1.f;
        P[14] = 2.f * zf * zn / (zn - zf);
        float V[16] = {right.x, up.x, back.x, 0, right.y, up.y, back.y, 0, right.z, up.z, back.z, 0,
                       -dot(right, camPos), -dot(up, camPos), -dot(back, camPos), 1};
        float VP[16];
        mat4mul(P, V, VP);

        const float shadowDistance = clampf((float)cfg.shadowDistance, 40.f, 250.f);
        const float shadowFadeDistance = clampf((float)cfg.shadowFadeDistance, 5.f, shadowDistance);
        const float maxShadowAltitude = clampf((float)cfg.shadowMaxAltitude, 20.f, 250.f);
        resizeShadowTarget(cfg.shadowResolution);
        const V3 lightDir = normalize3({0.45f, 0.85f, 0.30f});
        const V3 shadowForward = lightDir * -1.f;
        const V3 shadowRight = normalize3(cross(shadowForward, {0.f, 1.f, 0.f}));
        const V3 shadowUp = cross(shadowRight, shadowForward);
        const V3 cameraForward = rotate(cq, {0.f, 0.f, -1.f});
        const V3 focusTarget = camPos + cameraForward * std::min(shadowDistance * 0.25f, 30.f);
        if (!shadowFocusInitialized) {
            smoothedShadowFocus = focusTarget;
            shadowFocusInitialized = true;
        } else {
            const float follow = 1.f - std::exp(-8.f * (float)frameDt);
            smoothedShadowFocus += (focusTarget - smoothedShadowFocus) * follow;
        }
        V3 shadowFocus = smoothedShadowFocus;
        const float shadowExtent = shadowDistance;
        const float shadowTexelWorld = (2.f * shadowExtent) / std::max(1, shadowMapSize);
        // Disable stable snapping on Abandoned Base (mapIndex 4) - its long speed corridor
        // causes high-speed focus movement that makes texel snapping flicker between frames
        bool useStableSnapping = cfg.shadowStableSnapping && mapIndex != 4;
        if (useStableSnapping && shadowTexelWorld > 0.f) {
            const float x = dot(shadowFocus, shadowRight);
            const float y = dot(shadowFocus, shadowUp);
            shadowFocus += shadowRight * (std::round(x / shadowTexelWorld) * shadowTexelWorld - x);
            shadowFocus += shadowUp * (std::round(y / shadowTexelWorld) * shadowTexelWorld - y);
        }
        const float lightCameraDistance = shadowDistance + maxShadowAltitude + 50.f;
        const V3 lightEye = shadowFocus + lightDir * lightCameraDistance;
        float shadowView[16], shadowProjection[16], shadowVP[16];
        lookAtMatrix(lightEye, shadowFocus, {0.f, 1.f, 0.f}, shadowView);
        
        // Compute tight near/far planes for the orthographic projection to maximize depth precision
        // In orthographic projection, depth only varies with height (Y), not X/Y position.
        // Light is at lightEye looking toward shadowFocus along -lightDir.
        // Geometry height range: ground bottom (y≈-5) to maxShadowAltitude above ground.
        // Depth = dot(P - lightEye, -lightDir) = lightCameraDistance - (P.y - shadowFocus.y) * lightDirY
        const float lightDirY = lightDir.y; // ~0.85 after normalization
        const float groundThickness = 5.0f; // ground box spans y=-5..0
        const float nearPlane = lightCameraDistance - maxShadowAltitude * lightDirY - 2.0f; // highest geometry + small margin
        const float farPlane  = lightCameraDistance + groundThickness * lightDirY + 2.0f;  // ground bottom + small margin
        orthoMatrix(-shadowExtent, shadowExtent, -shadowExtent, shadowExtent,
                    std::max(1.0f, nearPlane), farPlane, shadowProjection);
        mat4mul(shadowProjection, shadowView, shadowVP);
        const float altitudeT = clampf((maxShadowAltitude - d.pos.y) / (maxShadowAltitude * 0.25f), 0.f, 1.f);
        const float shadowAltitudeFade = altitudeT * altitudeT * (3.f - 2.f * altitudeT);
        const bool renderShadows = cfg.shadowsEnabled && shadowAltitudeFade > 0.001f && !dbgDisableShadows;

        // Build the moving car geometry once, then use it in both camera and shadow passes.
        std::vector<float> trafficMesh;
        if (mapIndex == 0) {
            std::vector<Box> cars = buildTrafficCars((float)SDL_GetTicks() / 1000.f, trafficCarCount(cfg.quality));
            trafficMesh = buildMesh(cars);
            gl.glBindBuffer(GL_ARRAY_BUFFER, vboTraffic);
            gl.glBufferData(GL_ARRAY_BUFFER, trafficMesh.size() * sizeof(float), trafficMesh.data(), GL_DYNAMIC_DRAW);
        }

        // ---- drone-following real-time shadow depth pass
        if (renderShadows) {
            gl.glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
            glViewport(0, 0, shadowMapSize, shadowMapSize);
            glEnable(GL_DEPTH_TEST);
            glClear(GL_DEPTH_BUFFER_BIT);
            gl.glUseProgram(progShadow);
            gl.glUniformMatrix4fv(uShadowVP, 1, GL_FALSE, shadowVP);
            gl.glBindVertexArray(vaoWorld);
            glDrawArrays(GL_TRIANGLES, 0, meshVerts);
            if (!trafficMesh.empty()) {
                gl.glBindVertexArray(vaoTraffic);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(trafficMesh.size() / 10));
            }
        }

        // if (frameCounter == 1) print_draw_order();
        if (debugGlState && frameCounter == 1) dump_gl_state("START_FRAME");

        // ---- draw camera view
        gl.glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
        glViewport(0, 0, dw, dh);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        gl.glUseProgram(progSky);
        gl.glUniform3f(uRight, right.x, right.y, right.z);
        gl.glUniform3f(uUp, up.x, up.y, up.z);
        gl.glUniform3f(uBack, back.x, back.y, back.z);
        gl.glUniform2f(uTan, tanH, tanV);
        gl.glBindVertexArray(vaoSky);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glEnable(GL_DEPTH_TEST);
        gl.glUseProgram(progWorld);
        gl.glUniformMatrix4fv(uVP, 1, GL_FALSE, VP);
        gl.glUniformMatrix4fv(uLightVP, 1, GL_FALSE, shadowVP);
        gl.glUniform3f(uCam, camPos.x, camPos.y, camPos.z);
        gl.glUniform1f(uFog, cfg.fogDensity);
        gl.glUniform1f(uTextureDetail, textureDetail[quality]);
        gl.glUniform3f(uShadowFocus, shadowFocus.x, shadowFocus.y, shadowFocus.z);
        gl.glUniform2f(uShadowTexel, 1.f / shadowMapSize, 1.f / shadowMapSize);
        gl.glUniform1f(uShadowDistance, shadowDistance);
        gl.glUniform1f(uShadowFadeDistance, shadowFadeDistance);
        gl.glUniform1f(uShadowAltitudeFade, shadowAltitudeFade);
        gl.glUniform1f(uShadowStrength, clampf(cfg.shadowStrength * 0.01f, 0.f, 1.f));
        gl.glUniform1f(uShadowBias, clampf(cfg.shadowBias, 0.f, 0.01f));
        gl.glUniform1f(uShadowNormalOffset, clampf(cfg.shadowNormalOffset, 0.f, 0.2f));
        gl.glUniform1i(uShadowFilter, std::max(0, std::min(2, cfg.shadowFilter)));
        gl.glUniform1i(uShadowsEnabled, renderShadows ? 1 : 0);
        // Debug uniforms for flickering investigation
        gl.glUniform1i(uDbgDisableHash21, dbgDisableHash21 ? 1 : 0);
        gl.glUniform1i(uDbgDisableGrain, dbgDisableGrain ? 1 : 0);
        gl.glUniform1i(uDbgDisableScratch, dbgDisableScratch ? 1 : 0);
        gl.glUniform1i(uDbgDisableDetail, dbgDisableDetail ? 1 : 0);
        gl.glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadowDepthTexture);
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindVertexArray(vaoWorld);
        glDrawArrays(GL_TRIANGLES, 0, meshVerts);
        if (!trafficMesh.empty()) {
            gl.glBindBuffer(GL_ARRAY_BUFFER, vboTraffic);
            gl.glBufferData(GL_ARRAY_BUFFER, trafficMesh.size() * sizeof(float), trafficMesh.data(), GL_DYNAMIC_DRAW);
            gl.glBindVertexArray(vaoTraffic);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(trafficMesh.size() / 10));
        }

        // ---- Drone planar projected shadow
        if (cfg.droneShadow) {
            // Raycast down from drone to find surface
            float hitY;
            V3 hitNormal;
            const float maxShadowDist = 60.0f;
            if (raycastDown(d.pos, sim.world, sim.thinCylinders, maxShadowDist, hitY, hitNormal)) {
                // Build drone model matrix
                float model[16];
                // Model matrix: translate to drone pos, rotate by drone orientation
                V3 xAxis = rotate(d.q, {1, 0, 0});
                V3 yAxis = rotate(d.q, {0, 1, 0});
                V3 zAxis = rotate(d.q, {0, 0, 1});
                model[0] = xAxis.x; model[4] = xAxis.y; model[8] = xAxis.z; model[12] = d.pos.x;
                model[1] = yAxis.x; model[5] = yAxis.y; model[9] = yAxis.z; model[13] = d.pos.y;
                model[2] = zAxis.x; model[6] = zAxis.y; model[10] = zAxis.z; model[14] = d.pos.z;
                model[3] = 0; model[7] = 0; model[11] = 0; model[15] = 1;
                
                // Sun direction (matches sky/light)
                V3 sunDir = normalize3({0.45f, 0.85f, 0.30f});
                
                // Render shadow with stencil buffer to avoid double-darkening
                glEnable(GL_STENCIL_TEST);
                glStencilFunc(GL_ALWAYS, 1, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                glStencilMask(0xFF);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDepthMask(GL_FALSE);
                
                // First pass: write stencil
                gl.glUseProgram(progDroneShadow);
                gl.glUniformMatrix4fv(uDroneShadowVP, 1, GL_FALSE, VP);
                gl.glUniformMatrix4fv(uDroneShadowModel, 1, GL_FALSE, model);
                gl.glUniform3f(uDroneShadowSunDir, sunDir.x, sunDir.y, sunDir.z);
                // Add 1.5cm offset to avoid z-fighting
                gl.glUniform1f(uDroneShadowHeight, hitY + 0.015f);
                gl.glUniform3f(uDroneShadowPlaneNormal, hitNormal.x, hitNormal.y, hitNormal.z);
                gl.glBindVertexArray(vaoDrone);
                glDrawArrays(GL_TRIANGLES, 0, droneMeshVerts);
                
                // Second pass: render shadow where stencil == 1
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glStencilFunc(GL_EQUAL, 1, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_FALSE);
                
                glDrawArrays(GL_TRIANGLES, 0, droneMeshVerts);
                
                // Restore state
                glDepthFunc(GL_LESS);
                glDepthMask(GL_TRUE);
                glDisable(GL_STENCIL_TEST);
                glDisable(GL_BLEND);
            }
        }
        
        if (debugGlState && frameCounter == 1) dump_gl_state("AFTER_WORLD_PASS");

        // ---- Propeller overlay (FPV camera view)
        if (cfg.propsInView) {
            if (debugGlState && frameCounter == 1) dump_gl_state("BEFORE_PROP_OVERLAY");
            // Clear depth only, keep color (world already rendered)
            glClear(GL_DEPTH_BUFFER_BIT);

            // Build view matrix for propeller overlay: camera tilt only, no translation
            // Camera local position in drone frame - MATCH MAIN CAMERA: {0, 0.03f, -0.07f}
            V3 camLocalPos = {0, 0.03f, -0.07f};

            // Camera tilt quaternion (around X axis) - positive tilt means camera looks UP
            float tiltRad = rad(cfg.camTilt);
            
            // Props lowering: additional rotation around lateral (X) axis to move props down in view
            // angle = (percent / 100) * (vertical_half_FOV + 20 degrees)
            float aspect = (float)dw / (float)dh;
            float tanH = std::tan(rad(cfg.fovH) * 0.5f);
            float tanV = tanH / aspect;
            float verticalHalfFOV = std::atan(tanV);  // radians
            float loweringAngleRad = (cfg.propsLowering / 100.0f) * (verticalHalfFOV + rad(20.0f));
            
            // Total tilt = camera tilt + props lowering (both rotate around X axis)
            float totalTiltRad = tiltRad + loweringAngleRad;
            float cosT = cosf(totalTiltRad);
            float sinT = sinf(totalTiltRad);
            V3 overlayRight = {1, 0, 0};
            V3 overlayUp = {0, cosT, sinT};
            V3 overlayBack = {0, -sinT, cosT};

            // View matrix: R^T * T(-camLocalPos)
            float view[16] = {
                overlayRight.x, overlayUp.x, overlayBack.x, 0,
                overlayRight.y, overlayUp.y, overlayBack.y, 0,
                overlayRight.z, overlayUp.z, overlayBack.z, 0,
                -dot(overlayRight, camLocalPos), -dot(overlayUp, camLocalPos), -dot(overlayBack, camLocalPos), 1
            };

            // Overlay projection: same FOV/aspect as main, but near=0.002, far=1.0
            // aspect, tanH, tanV already computed above for lowering angle
            float overlayNear = 0.002f;
            float overlayFar = 1.0f;
            float P_overlay[16] = {0};
            P_overlay[0] = 1.f / tanH;
            P_overlay[5] = 1.f / tanV;
            P_overlay[10] = (overlayFar + overlayNear) / (overlayNear - overlayFar);
            P_overlay[11] = -1.f;
            P_overlay[14] = 2.f * overlayFar * overlayNear / (overlayNear - overlayFar);

            // Sun direction
            V3 sunDir = normalize3({0.45f, 0.85f, 0.30f});

            // Current time for propeller animation
            float currentTime = (float)SDL_GetTicks() * 0.001f;

            // Render propeller overlay
            gl.glUseProgram(progPropOverlay);
            gl.glUniformMatrix4fv(uPropOverlayVP, 1, GL_FALSE, P_overlay);  // Use overlay projection
            gl.glUniformMatrix4fv(uPropOverlayView, 1, GL_FALSE, view);
            gl.glUniform1f(uPropOverlayTime, currentTime);

            // Motor RPM from physics (sim.d.motorRPM)
            for (int i = 0; i < 4; ++i) {
                gl.glUniform1f(uPropOverlayRPM[i], sim.d.motorRPM[i]);
            }

            gl.glUniform1f(uPropOverlayOpacity, cfg.propsOpacity);
            gl.glUniform1i(uPropOverlayBlades, 3);
            gl.glUniform3f(uPropOverlaySunDir, sunDir.x, sunDir.y, sunDir.z);

            // Enable blending for propeller transparency
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            // Disable culling so blades are visible from both sides
            glDisable(GL_CULL_FACE);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);

            // Performance timing for propeller overlay
            static uint64_t perfOverlayStart = 0;
            static double perfOverlayAccum = 0.0;
            static int perfOverlayFrames = 0;
            perfOverlayStart = SDL_GetPerformanceCounter();

            gl.glBindVertexArray(vaoPropOverlay);
            glDrawArrays(GL_TRIANGLES, 0, propOverlayMeshVerts);

            // End timing
            uint64_t perfOverlayEnd = SDL_GetPerformanceCounter();
            double perfOverlayMs = (double)(perfOverlayEnd - perfOverlayStart) * 1000.0 / SDL_GetPerformanceFrequency();
            perfOverlayAccum += perfOverlayMs;
            perfOverlayFrames++;
            
            // Print performance every 60 frames (~1 second at 60 FPS) - only in debug mode
            if (debugMode && perfOverlayFrames % 60 == 0) {
                double avgMs = perfOverlayAccum / perfOverlayFrames;
                std::printf("[PERF] Prop overlay avg: %.3f ms over %d frames (target < 0.3ms)\n", avgMs, perfOverlayFrames);
            }

            // Restore state
            glEnable(GL_CULL_FACE);
            glDepthFunc(GL_LESS);
            glDisable(GL_BLEND);
            
            if (debugGlState && frameCounter == 1) dump_gl_state("AFTER_PROP_OVERLAY");
        }

        // ---- Analog video signal update
        const float videoTime = (float)SDL_GetTicks() * 0.001f;
        {
            V3 pilotPos = sim.startPos + V3{0.f, 1.6f, 0.f};
            float rawQ = computeRawSignalQuality(camPos, pilotPos, sim.world,
                                                 sim.P, sim.d, cfg, vsig);
            updateVideoSignal(vsig, rawQ, (float)frameDt, frameCounter, cfg);
        }

        // ---- Resolve MSAA if needed, then post-process
        if (cfg.msaa > 1) {
            gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo);
            gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo);
            gl.glBlitFramebuffer(0, 0, dw, dh, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }

        // Video post-processing (or direct blit if disabled)
        gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, dw, dh);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        if (disableVideo) {
            // Skip video post-processing, just blit scene to screen
            GLuint srcFbo = (cfg.msaa > 1) ? resolveFbo : sceneFbo;
            gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
            gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            gl.glBlitFramebuffer(0, 0, dw, dh, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        } else {
            const bool videoOn = cfg.videoEnabled;
            const float loss01 = 1.f - clampf(vsig.signalQuality * 0.01f, 0.f, 1.f);

            gl.glUseProgram(progVideo);
            gl.glUniform2f(uVideoResolution, (float)dw, (float)dh);
            gl.glUniform1f(uVideoTime, videoTime);
            gl.glUniform1f(uVideoLoss, videoOn ? loss01 : 0.f);
            gl.glUniform1f(uVideoBurst, videoOn ? vsig.burstStrength : 0.f);
            gl.glUniform1f(uVideoNoise, videoOn && cfg.videoNoise ? 1.f : 0.f);
            gl.glUniform1f(uVideoNoiseIntensity,  clampf(cfg.videoNoiseIntensity,  0.f, 2.f));
            gl.glUniform1f(uVideoStaticIntensity, clampf(cfg.videoStaticIntensity, 0.f, 2.f));
            gl.glUniform1f(uVideoSnowStrength,    1.0f);
            gl.glUniform1f(uVideoScan,            videoOn && cfg.videoScanlines ? 1.f : 0.f);
            gl.glUniform1f(uVideoScanIntensity,   clampf(cfg.videoScanlineIntensity, 0.f, 2.f));
            gl.glUniform1f(uVideoChroma,          videoOn && cfg.videoChromatic ? 1.f : 0.f);
            gl.glUniform1f(uVideoColorGlitch,     clampf(cfg.videoColorGlitch,    0.f, 2.f));
            gl.glUniform1f(uVideoBreakup,         videoOn && cfg.videoBreakup ? 1.f : 0.f);
            gl.glUniform1f(uVideoTearIntensity,   clampf(cfg.videoTearIntensity,  0.f, 2.f));
            gl.glUniform1f(uVideoRollingBar,      videoOn && cfg.videoRollingBar ? 1.f : 0.f);
            gl.glUniform1f(uVideoRollingPhase,    vsig.rollingBarPhase);
            gl.glUniform1f(uVideoChromaGlitch,    vsig.chromaGlitchPhase);
            gl.glUniform1f(uVideoTearRow,         vsig.tearRow);
            gl.glUniform1f(uVideoTearOffset,      vsig.tearOffset);
            gl.glUniform1f(uVideoDropoutFlash,    vsig.dropoutFlashValue);
            gl.glUniform1f(uVideoDropoutActive,   videoOn && vsig.dropoutFlashTimer > 0.f ? 1.f : 0.f);
            gl.glUniform1i(uVideoPerfMode,        cfg.videoPerformanceMode);
            // New analog artifact uniforms
            gl.glUniform1f(uVideoDotCrawl,        videoOn && cfg.videoDotCrawl ? 1.f : 0.f);
            gl.glUniform1f(uVideoSyncLossOn,      videoOn && cfg.videoSyncLossEffect ? 1.f : 0.f);
            gl.glUniform1f(uVideoSyncLossTimer,   vsig.syncLossTimer);
            gl.glUniform1f(uVideoSyncLossSev,     vsig.syncLossSeverity);
            gl.glUniform1f(uVideoSyncLossPhase,   vsig.syncLossPhase);
            gl.glUniform1f(uVideoColorBurstOn,    videoOn && cfg.videoColorBurstEffect ? 1.f : 0.f);
            gl.glUniform1f(uVideoColorBurstTimer, vsig.colorBurstLossTimer);
            gl.glUniform1f(uVideoColorBurstSev,   vsig.colorBurstLossSeverity);
            gl.glUniform1f(uVideoGhostOn,         videoOn && cfg.videoMultipathGhost ? 1.f : 0.f);
            gl.glUniform1f(uVideoGhostOffset,     vsig.multipathGhostOffset);
            glBindTexture(GL_TEXTURE_2D, sceneColor);
            gl.glBindVertexArray(vaoSky);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        if (debugGlState && frameCounter == 1) dump_gl_state("BEFORE_OSD");

        // ---- OSD
        osd.v.clear();
        const float S = std::max(2.f, std::floor(dh / 240.f));
        const float M = 4.f * S;
        const float big = S * 2.f;
        char buf[96];
        const float cells = (float)sim.P.cells;
        float vcell = d.volts / cells;
        bool low = d.batteryEmpty || (d.volts / cells) < 3.45f;
        float blink = std::fmod((float)SDL_GetTicks() / 1000.f, 0.8f) < 0.5f ? 1.f : 0.f;

        std::snprintf(buf, sizeof buf, "%.1fV", d.volts);
        osd.text(M, M, big, buf, low ? 1.f : 1.f, low ? 0.25f : 1.f, low ? 0.25f : 1.f, low ? 0.4f + 0.6f * blink : 1.f);
        std::snprintf(buf, sizeof buf, "%.2fV/C", vcell);
        osd.text(M, M + 9.f * big, S, buf, 1, 1, 1);
        std::snprintf(buf, sizeof buf, "%d MAH", (int)d.usedMah);
        osd.text(M, M + 9.f * big + 9.f * S, S, buf, 1, 1, 1);

        if (cfg.drone.showHitbox) {
            const float panel = clampf(dh * 0.12f, 76.f, 104.f);
            const float hx = M, hy = M + 39.f * S;
            const float cx = hx + panel * 0.5f, cy = hy + panel * 0.58f;
            const float armViz = panel * 0.28f * cfg.drone.colliderScale;
            osd.rect(hx, hy, panel, panel, 0.025f, 0.045f, 0.06f, 0.72f);
            osd.text(hx + 4.f, hy + 4.f, std::max(1.f, panel / 88.f), "HITBOX / PADS", 0.7f, 0.94f, 0.96f);
            for (int i = 0; i < 32; ++i) {
                float a0 = 2.f * PI * i / 32.f, a1 = 2.f * PI * (i + 1) / 32.f;
                osd.line(cx + std::cos(a0) * panel * 0.34f, cy + std::sin(a0) * panel * 0.34f,
                         cx + std::cos(a1) * panel * 0.34f, cy + std::sin(a1) * panel * 0.34f,
                         1.f, 0.25f, 0.88f, 0.84f, 0.65f);
            }
            for (float sx : {-1.f, 1.f}) for (float sz : {-1.f, 1.f}) {
                float px = cx + sx * armViz * 0.7071f;
                float py = cy + sz * armViz * 0.7071f;
                osd.line(cx, cy, px, py, 1.5f, 0.95f, 0.68f, 0.24f, 0.85f);
                osd.circle(px, py, 3.f, 1.f, 0.68f, 0.22f, 0.98f);
            }
            const float bodyR = std::max(3.f, cfg.drone.radius * 80.f * cfg.drone.colliderScale);
            for (int i = 0; i < 24; ++i) {
                float a0 = 2.f * PI * i / 24.f, a1 = 2.f * PI * (i + 1) / 24.f;
                osd.line(cx + std::cos(a0) * bodyR, cy + std::sin(a0) * bodyR,
                         cx + std::cos(a1) * bodyR, cy + std::sin(a1) * bodyR,
                         1.3f, 0.32f, 0.92f, 0.87f, 0.95f);
            }
            osd.circle(cx, cy, 2.5f, 0.32f, 0.92f, 0.87f, 0.95f);
        }

        float kmh = len(d.vel) * 3.6f;
        std::snprintf(buf, sizeof buf, "%d KMH", (int)(kmh + 0.5f));
        osd.textRight(dw - M, M, big, buf, 1, 1, 1);
        std::snprintf(buf, sizeof buf, "ALT %.1f M", std::max(0.f, d.pos.y - 0.12f));
        osd.textRight(dw - M, M + 9.f * big, S, buf, 1, 1, 1);

        std::snprintf(buf, sizeof buf, "THR %d%%", (int)(in.throttle * 100.f + 0.5f));
        if (!monitor) osd.text(M, dh - M - 7.f * S, S, buf, 1, 1, 1);

        int secs = (int)d.flightTime;
        std::snprintf(buf, sizeof buf, "%02d:%02d", secs / 60, secs % 60);
        osd.textCenter(dw * 0.5f, dh - M - 7.f * big - 9.f * S, big, buf, 1, 1, 1);
        if (d.armed) osd.textCenter(dw * 0.5f, dh - M - 7.f * S, S, "ARMED", 0.3f, 1.f, 0.4f);
        else if (d.armBlocked) osd.textCenter(dw * 0.5f, dh - M - 7.f * S, S, "ARM BLOCKED - LOWER THROTTLE", 1.f, 0.3f, 0.3f);
        else osd.textCenter(dw * 0.5f, dh - M - 7.f * S, S, "DISARMED", 1, 1, 1);

        std::snprintf(buf, sizeof buf, "%d FPS", (int)(isThrottled ? currentFPS : fpsAvg + 0.5));
        if (monitor) {
            const float inputBox = clampf(dh * 0.23f, 112.f, 184.f);
            osd.textCenter(dw - M - inputBox * 0.5f, dh - M - inputBox - 9.f * S, S,
                           buf, 0.8f, 0.8f, 0.8f, 0.9f);
        } else {
            osd.textRight(dw - M, dh - M - 16.f * S, S, buf, 0.8f, 0.8f, 0.8f, 0.8f);
        }
        std::snprintf(buf, sizeof buf, "TILT %d  FOV %d", (int)std::lround(cfg.camTilt), (int)std::lround(cfg.fovH));
        if (!monitor) osd.textRight(dw - M, dh - M - 7.f * S, S, buf, 1, 1, 1, overlayTimer > 0 ? 1.f : 0.55f);
        osd.textCenter(dw * 0.5f, M + 9.f * S, S * 1.5f, mapNames[mapIndex],
                       1.f, 1.f, 1.f, overlayTimer > 0 ? 1.f : 0.72f);
        
        if (cfg.windHud && sim.windEnabled) {
            const float meanWindMps = clampf((float)cfg.windStrength, 0.f, 100.f) * 0.18f;
            std::snprintf(buf, sizeof buf, "WIND %.1f M/S  %03d DEG", meanWindMps,
                          ((cfg.windDirectionDeg % 360) + 360) % 360);
            osd.textRight(dw - M, M + 27.f * S, S, buf, 0.70f, 0.90f, 1.f, 0.88f);
        }
        if (cfg.shadowDebug) {
            std::snprintf(buf, sizeof buf, "SHADOW %dPX  %dM", shadowMapSize, (int)shadowDistance);
            osd.textRight(dw - M, M + 36.f * S, S, buf, 0.95f, 0.80f, 0.35f, 0.95f);
            std::snprintf(buf, sizeof buf, "BOUNDS +/-%.0fM  ALT %.0f/%.0fM",
                          shadowDistance, d.pos.y, maxShadowAltitude);
            osd.textRight(dw - M, M + 45.f * S, S, buf, 0.95f, 0.80f, 0.35f, 0.95f);
            std::snprintf(buf, sizeof buf, "BIAS %.4f  PCF %s",
                          cfg.shadowBias, cfg.shadowFilter == 0 ? "OFF" : (cfg.shadowFilter == 1 ? "2X2" : "3X3"));
            osd.textRight(dw - M, M + 54.f * S, S, buf, 0.95f, 0.80f, 0.35f, 0.95f);
        }
        if (videoNoticeTimer > 0.f)
            osd.textCenter(dw * 0.5f, M + 21.f * S, S, cfg.videoEnabled ? "ANALOG VIDEO ON" : "ANALOG VIDEO OFF",
                           0.45f, 0.95f, 0.88f, 1.f);

        // RSSI / video link quality HUD (under battery MAH on left side)
        if (cfg.videoEnabled && cfg.videoShowRSSI) {
            int rssi = (int)std::round(vsig.signalQuality);
            std::snprintf(buf, sizeof buf, "VID %d%%", rssi);
            float rR = 1.f, rG = 1.f, rB = 1.f;
            if (rssi < 20)      { rR = 1.f; rG = 0.1f; rB = 0.1f; }  // critical: red
            else if (rssi < 50) { rR = 1.f; rG = 0.6f; rB = 0.0f; }  // low: orange
            else if (rssi < 80) { rR = 1.f; rG = 1.f;  rB = 0.0f; }  // medium: yellow
            // Position under battery MAH: MAH is at M + 9*big + 9*S, so VID goes one line below
            osd.text(M, M + 9.f * big + 18.f * S, S, buf, rR, rG, rB);
            // Warning icon when critical
            if (rssi < 20 && blink > 0.5f) {
                osd.text(M, M + 9.f * big + 27.f * S, S, "!SIGNAL!", 1.f, 0.1f, 0.1f);
            }
        }

        if (d.batteryEmpty) osd.textCenter(dw * 0.5f, dh * 0.3f, big, "BATTERY EMPTY - R TO RESET", 1.f, 0.2f, 0.2f);
        else if (low && blink > 0.5f) osd.textCenter(dw * 0.5f, dh * 0.3f, big, "LOW BATTERY", 1.f, 0.2f, 0.2f);
        if (d.crashTimer > 0) osd.textCenter(dw * 0.5f, dh * 0.4f, big * 1.5f, "CRASH", 1.f, 0.3f, 0.1f, std::min(1.f, d.crashTimer * 2.f));
        if (!useRadio) osd.textCenter(dw * 0.5f, M, S, radio.joy ? "KEYBOARD MODE (F2 = RADIO)" : "NO RADIO - KEYBOARD: W/S THR  A/D YAW  ARROWS  SPACE ARM", 1.f, 0.85f, 0.3f);

        if (monitor) {
            const float box = clampf(dh * 0.23f, 112.f, 184.f);
            const float pad = std::max(8.f, box * 0.065f);
            const float side = box - 2.f * pad;
            const float y = dh - M - box;
            const float leftX = M, rightX = dw - M - box;
            auto drawStickBox = [&](float x, const char* title, float stickX, float stickY, bool throttleAxis) {
                osd.rect(x, y, box, box, 0.10f, 0.12f, 0.14f, 0.40f);
                const float edge = 1.f;
                osd.rect(x, y, box, edge, 0.82f, 0.88f, 0.92f, 0.72f);
                osd.rect(x, y + box - edge, box, edge, 0.82f, 0.88f, 0.92f, 0.72f);
                osd.rect(x, y, edge, box, 0.82f, 0.88f, 0.92f, 0.72f);
                osd.rect(x + box - edge, y, edge, box, 0.82f, 0.88f, 0.92f, 0.72f);
                const float cx = x + box * 0.5f, cy = y + pad + side * 0.5f;
                osd.line(cx, y + pad, cx, y + box - pad, 1.f, 0.88f, 0.92f, 0.96f, 0.20f);
                osd.line(x + pad, cy, x + box - pad, cy, 1.f, 0.88f, 0.92f, 0.96f, 0.20f);
                osd.textCenter(cx, y + 4.f, std::max(1.f, S * 0.8f), title, 0.92f, 0.96f, 1.f, 0.9f);
                stickX = clampf(stickX, -1.f, 1.f);
                stickY = clampf(stickY, throttleAxis ? 0.f : -1.f, 1.f);
                const float dx = cx + stickX * side * 0.5f;
                const float dy = throttleAxis ? y + box - pad - stickY * side : cy - stickY * side * 0.5f;
                osd.line(cx, throttleAxis ? y + box - pad : cy, dx, dy, 1.5f, 1.f, 1.f, 1.f, 0.68f);
                osd.circle(dx, dy, 5.f, 1.f, 1.f, 1.f, 0.98f);
            };
            // Mode 2: left stick is yaw/throttle; right stick is roll/pitch.
            drawStickBox(leftX, "YAW / THR", in.yaw, in.throttle, true);
            drawStickBox(rightX, "ROLL / PITCH", in.roll, in.pitch, false);
        }

        if (settingsOpen) {
            float px = dw * 0.08f, py = dh * 0.08f, pw = dw * 0.84f, ph = dh * 0.84f;
            float titleScale = std::max(1.f, std::floor(dh / 700.f));
            osd.rect(0, 0, (float)dw, (float)dh, 0.015f, 0.025f, 0.045f, 0.84f);
            osd.rect(px, py, pw, ph, 0.055f, 0.075f, 0.10f, 0.99f);
            osd.rect(px, py, pw, 3.f * titleScale, 0.15f, 0.85f, 0.78f, 1.f);
            osd.text(px + 20.f, py + 15.f, titleScale * 1.5f, "FPVSIM SETTINGS", 0.9f, 1.f, 1.f);
            osd.textRight(px + pw - 18.f, py + 19.f, titleScale, "ESC CLOSE", 0.6f, 0.75f, 0.8f);

            float tabY = py + 43.f, tabW = pw / SETTING_TAB_COUNT;
            for (int i = 0; i < SETTING_TAB_COUNT; ++i) {
                float tx = px + i * tabW;
                bool active = i == settingsTab;
                osd.rect(tx + 2, tabY, tabW - 4, 29.f, active ? 0.10f : 0.07f, active ? 0.32f : 0.10f,
                         active ? 0.30f : 0.14f, 1.f);
                osd.textCenter(tx + tabW * 0.5f, tabY + 10.f, titleScale, SETTING_TABS[i],
                               active ? 0.4f : 0.68f, active ? 1.f : 0.78f, active ? 0.9f : 0.82f);
            }

            std::vector<int> visible;
            for (size_t i = 0; i < settingEntries.size(); ++i)
                if (std::string(settingEntries[i].key) != "config_version" && settingTab(settingEntries[i]) == settingsTab)
                    visible.push_back((int)i);
            const int rowsVisible = 9;
            float rowTop = py + 91.f, rowH = 32.f;
            for (int r = 0; r < rowsVisible; ++r) {
                int idx = settingsScroll + r;
                if (idx >= (int)visible.size()) break;
                const Entry& ent = settingEntries[visible[idx]];
                float y = rowTop + r * rowH;
                bool selected = idx == settingsSelection;
                osd.rect(px + 12.f, y, pw - 24.f, rowH - 3.f, selected ? 0.10f : 0.075f,
                         selected ? 0.18f : 0.095f, selected ? 0.20f : 0.12f, 0.96f);
                osd.rect(px + 12.f, y, selected ? 3.f : 1.f, rowH - 3.f,
                         selected ? 0.18f : 0.16f, selected ? 0.85f : 0.25f, selected ? 0.78f : 0.3f, 1.f);
                std::string label = settingLabel(ent.key);
                if (label.size() > 32) label.resize(32);
                osd.text(px + 26.f, y + 10.f, titleScale, label, selected ? 0.94f : 0.72f,
                         selected ? 0.98f : 0.78f, selected ? 0.96f : 0.82f);
                osd.rect(px + pw - 158.f, y + 4.f, 27.f, 23.f, 0.12f, 0.17f, 0.20f, 1.f);
                osd.textCenter(px + pw - 144.5f, y + 12.f, titleScale, "-", 0.75f, 0.88f, 0.9f);
                osd.textRight(px + pw - 51.f, y + 10.f, titleScale,
                              settingValue(ent).substr(0, 18), 0.40f, 0.94f, 0.86f);
                osd.rect(px + pw - 37.f, y + 4.f, 27.f, 23.f, 0.12f, 0.17f, 0.20f, 1.f);
                osd.textCenter(px + pw - 23.5f, y + 12.f, titleScale, "+", 0.75f, 0.88f, 0.9f);
            }
            osd.text(px + 18.f, py + ph - 48.f, titleScale,
                     editingText ? "TYPE VALUE - ENTER DONE / ESC CANCEL" : "UP/DOWN SELECT   LEFT/RIGHT CHANGE   ENTER/SPACE EDIT   TAB SECTION   F6 SAVE",
                     0.58f, 0.72f, 0.76f);
            osd.rect(px + pw - 220.f, py + ph - 56.f, 94.f, 34.f, 0.08f, 0.44f, 0.37f, 1.f);
            osd.textCenter(px + pw - 173.f, py + ph - 44.f, titleScale, "SAVE", 0.92f, 1.f, 0.96f);
            osd.rect(px + pw - 112.f, py + ph - 56.f, 94.f, 34.f, 0.16f, 0.21f, 0.25f, 1.f);
            osd.textCenter(px + pw - 65.f, py + ph - 44.f, titleScale, "CLOSE", 0.85f, 0.9f, 0.92f);
            if (settingsMessageTimer > 0.f) {
                float mw = std::min(pw - 40.f, 480.f), mh = 58.f;
                osd.rect((dw - mw) * 0.5f, (dh - mh) * 0.5f, mw, mh, 0.03f, 0.12f, 0.13f, 0.98f);
                osd.textCenter(dw * 0.5f, (dh - 7.f * titleScale) * 0.5f, titleScale,
                               settingsMessage, 0.62f, 1.f, 0.88f);
            }
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl.glUseProgram(progOsd);
        gl.glUniform2f(uRes, (float)dw, (float)dh);
        gl.glBindVertexArray(vaoOsd);
        gl.glBindBuffer(GL_ARRAY_BUFFER, vboOsd);
        gl.glBufferData(GL_ARRAY_BUFFER, osd.v.size() * sizeof(float), osd.v.data(), GL_STREAM_DRAW);
        
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(osd.v.size() / 6));

        // UI Visibility Test: run on first frame when debug GL state is enabled
        if (debugGlState && (frameCounter == 1 || frameCounter == 2)) {
            testUIVisibility(dw, dh, "FRAME", cfg.propsInView, osd.v.size() / 6, frameCounter);
        }

        SDL_GL_SwapWindow(win);
        
        // Dynamic FPS: throttle to 5 FPS when window unfocused or after 60s inactivity
        Uint64 frameEnd = SDL_GetPerformanceCounter();
        double elapsedFrameTime = (double)(frameEnd - now) / freq;
        isThrottled = false;
        if (!windowFocused) {
            // Window unfocused: throttle to IDLE_FPS
            double targetFrameTime = IDLE_FRAME_TIME;
            if (elapsedFrameTime < targetFrameTime) {
                double delayMs = (targetFrameTime - elapsedFrameTime) * 1000.0;
                SDL_Delay((Uint32)delayMs);
                isThrottled = true;
            }
            currentFPS = IDLE_FPS;
        } else {
            // Window focused: check for inactivity
            double inactiveTime = (double)(frameEnd - lastInputTime) / freq;
            if (inactiveTime > DYNAMIC_FPS_THRESHOLD) {
                // Inactive for 60+ seconds: throttle to IDLE_FPS
                double targetFrameTime = IDLE_FRAME_TIME;
                if (elapsedFrameTime < targetFrameTime) {
                    double delayMs = (targetFrameTime - elapsedFrameTime) * 1000.0;
                    SDL_Delay((Uint32)delayMs);
                    isThrottled = true;
                }
                currentFPS = IDLE_FPS;
            } else {
                currentFPS = 1.0 / elapsedFrameTime;
            }
        }
        
        // ---- Debug tests for flickering investigation
        if (freezeTest && dbgFreezeFrame == -1) {
            dbgFreezeFrame = frameCounter;
            dbgFreezeCamPos = camPos;
            dbgFreezeCamQuat = cq;
            std::printf("FREEZE TEST: Camera frozen at frame %d, pos=(%.2f,%.2f,%.2f)\n", 
                        dbgFreezeFrame, camPos.x, camPos.y, camPos.z);
        }
        if (freezeTest && dbgFreezeFrame >= 0) {
            if (frameCounter - dbgFreezeFrame >= freezeTestFrames) {
                std::printf("FREEZE TEST COMPLETE: %d frames rendered with frozen camera. Check for flicker.\n", freezeTestFrames);
                running = false;
            }
        }
        if (frameDiffTest) {
            // Read back frame pixels
            std::vector<uint8_t> pixels(dw * dh * 4);
            glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            if (!dbgPrevFramePixels.empty()) {
                int diffCount = 0;
                int maxDiff = 0;
                for (size_t i = 0; i < pixels.size(); ++i) {
                    int diff = abs((int)pixels[i] - (int)dbgPrevFramePixels[i]);
                    if (diff > 1) diffCount++;
                    if (diff > maxDiff) maxDiff = diff;
                }
                float diffPercent = 100.0f * diffCount / (dw * dh * 4);
                if (frameCounter % 10 == 0) {
                    std::printf("FRAME DIFF: %d/%d pixels differ (%.3f%%), max diff=%d\n", 
                                diffCount, dw * dh * 4, diffPercent, maxDiff);
                }
                if (diffPercent > 0.01f) {
                    std::printf("FRAME DIFF TEST: NON-DETERMINISTIC RENDERING DETECTED (%.3f%% pixels differ)\n", diffPercent);
                }
            }
            
            // Static frame test with PPM output - on frame 2 of freeze test
            if (freezeTest && frameCounter == dbgFreezeFrame + 1) {
                writePPM("frame1.ppm", dbgPrevFramePixels, dw, dh);
                writePPM("frame2.ppm", pixels, dw, dh);
                // Create diff image
                std::vector<uint8_t> diffImg(dw * dh * 3);
                for (int y = 0; y < dh; ++y) {
                    for (int x = 0; x < dw; ++x) {
                        int idx = (y * dw + x) * 4;
                        int idx3 = (y * dw + x) * 3;
                        int dr = abs((int)pixels[idx] - (int)dbgPrevFramePixels[idx]);
                        int dg = abs((int)pixels[idx+1] - (int)dbgPrevFramePixels[idx+1]);
                        int db = abs((int)pixels[idx+2] - (int)dbgPrevFramePixels[idx+2]);
                        int maxD = std::max({dr, dg, db});
                        diffImg[idx3] = std::min(255, maxD * 8);
                        diffImg[idx3+1] = std::min(255, maxD * 8);
                        diffImg[idx3+2] = std::min(255, maxD * 8);
                    }
                }
                // Write diff as PPM (need to convert to RGB format for PPM)
                FILE* f = std::fopen("diff.ppm", "wb");
                if (f) {
                    std::fprintf(f, "P6\n%d %d\n255\n", dw, dh);
                    std::fwrite(diffImg.data(), 1, diffImg.size(), f);
                    std::fclose(f);
                    std::printf("  Wrote diff.ppm (%dx%d)\n", dw, dh);
                }
            }
            
            // Color census test
            if (colorCensusTest) {
                int greenCount, grayCount, brownCount, totalCount;
                colorCensus(pixels, dw, dh, greenCount, grayCount, brownCount, totalCount);
                std::printf("COLOR CENSUS (lower half): green=%d gray=%d brown=%d total=%d\n",
                            greenCount, grayCount, brownCount, totalCount);
                if (frameCounter > 1) {
                    static int prevGreen=0, prevGray=0, prevBrown=0;
                    if (prevGreen > 0 || prevGray > 0 || prevBrown > 0) {
                        std::printf("  DELTA: green=%+d gray=%+d brown=%+d\n",
                                    greenCount - prevGreen, grayCount - prevGray, brownCount - prevBrown);
                    }
                    prevGreen = greenCount; prevGray = grayCount; prevBrown = brownCount;
                }
                colorCensusTest = false;  // run once
            }
            
            dbgPrevFramePixels = pixels;
            if (frameCounter >= frameDiffFrames) {
                std::printf("FRAME DIFF TEST COMPLETE: %d frames analyzed\n", frameDiffFrames);
                running = false;
            }
        }
        if (drawCallAudit) {
            static bool auditDone = false;
            if (!auditDone) {
                std::printf("=== DRAW CALL AUDIT ===\n");
                std::printf("1. Sky pass: 1 draw call (fullscreen triangle)\n");
                std::printf("2. Shadow depth pass: 1 draw call (world mesh) + %s\n", trafficMesh.empty() ? "0" : "1 (traffic)");
                std::printf("3. World pass: 1 draw call (world mesh) + %s\n", trafficMesh.empty() ? "0" : "1 (traffic)");
                std::printf("4. Video post-process: 1 draw call (fullscreen triangle)\n");
                std::printf("5. OSD: 1 draw call\n");
                std::printf("Total: ~5-7 draw calls per frame\n");
                std::printf("Vertex count: world=%d, traffic=%d\n", meshVerts, trafficMesh.empty() ? 0 : (int)(trafficMesh.size() / 10));
                std::printf("Shader: WORLD_FS with hash21 texture randomization\n");
                std::printf("Debug toggles: hash21=%s grain=%s scratch=%s detail=%s shadows=%s wireframe=%s\n",
                            dbgDisableHash21 ? "OFF" : "ON", dbgDisableGrain ? "OFF" : "ON",
                            dbgDisableScratch ? "OFF" : "ON", dbgDisableDetail ? "OFF" : "ON",
                            dbgDisableShadows ? "OFF" : "ON", dbgWireframe ? "ON" : "OFF");
                auditDone = true;
            }
        }
        glFinish();  // don't let the driver queue frames ahead: that is extra stick-to-screen latency
    }

    saveConfig(cfg, cfgPath);
    if (audioDevice) SDL_CloseAudioDevice(audioDevice);
    if (radio.joy) SDL_JoystickClose(radio.joy);
    // Cleanup debug framebuffer
    if (debugFbo) gl.glDeleteFramebuffers(1, &debugFbo);
    if (debugTex) gl.glDeleteTextures(1, &debugTex);
    if (debugDepth) gl.glDeleteRenderbuffers(1, &debugDepth);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
