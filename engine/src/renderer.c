#include "cc/ccmath.h"
#include <qwerty/qwerty.h>   /* for the GLFW key callback → qwerty event queue */
/*
 * Chlorlite Renderer — renderer.c
 *
 * Backends (tried in order unless overridden):
 *   1. GLFW + OpenGL 3.3 core (windowed, GPU)
 *   2. OSMesa + OpenGL 3.3 core (headless, software, no GPU required)
 *   3. Null (no rendering, ECS/game logic still runs)
 *
 * Pipeline:
 *   Geometry pass (G-buffer: albedo/roughness/metallic/normal/depth)
 *   → Lighting pass (PBR + shadow maps)
 *   → Post-FX (bloom, FXAA, tone mapping, vignette, CA)
 *   → 2D layer (sprites, shapes)
 *   → GUI layer
 *   → Blit to window or offscreen buffer
 *
 * Screenshot: reads from GL framebuffer, writes PNG via stb_image_write.
 * No GPU required in OSMesa mode — fully self-contained in Claude sandbox.
 */

#define CC_RENDERER_IMPL
#include "cc/render.h"
#include "cc/claudecore.h"
#include "renderer_internal.h"

/* ── stb ──────────────────────────────────────────────────────────────── */
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "stb/stb_truetype.h"

/* ── OpenGL / GLFW / OSMesa ───────────────────────────────────────────── */
#ifdef CC_USE_GLFW
#  include <GLFW/glfw3.h>
#endif
#ifdef CC_USE_OSMESA
#  include <GL/osmesa.h>
#endif
#include <GL/gl.h>
#include "cc_gl_loader.h"
#include "cc_default_font.h"   /* embedded default TTF */   /* Windows GL3 loader (no-op on Linux) */

#if !defined(CC_UNIFORM_HELPERS_INSERTED) && !defined(_WIN32)
#define CC_UNIFORM_HELPERS_INSERTED
/* CC_UNIFORM_HELPERS — OSMesa's variadic glUniform{1,2,3,4}f are unreliable;
   use the array-pointer forms which work correctly. */
static inline void cc_u1f(GLint l,float a){float v[1]={a};glUniform1fv(l,1,v);}
static inline void cc_u2f(GLint l,float a,float b){float v[2]={a,b};glUniform2fv(l,1,v);}
static inline void cc_u3f(GLint l,float a,float b,float cc){float v[3]={a,b,cc};glUniform3fv(l,1,v);}
static inline void cc_u4f(GLint l,float a,float b,float cc,float d){float v[4]={a,b,cc,d};glUniform4fv(l,1,v);}
#define glUniform1f(l,a) cc_u1f((l),(a))
#define glUniform2f(l,a,b) cc_u2f((l),(a),(b))
#define glUniform3f(l,a,b,c) cc_u3f((l),(a),(b),(c))
#define glUniform4f(l,a,b,c,d) cc_u4f((l),(a),(b),(c),(d))
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp/strncasecmp for case-insensitive PBR stems */
#include <ctype.h>     /* tolower for PBR stem normalization */
#include <stdarg.h>
#include <math.h>
#include <dirent.h>    /* directory scan for cc_material_load_pbr */

/* ── Forward declarations ─────────────────────────────────────────────── */
static bool renderer_init_glfw(CCRenderer* r, uint32_t w, uint32_t h, const char* title);
static bool renderer_init_osmesa(CCRenderer* r, uint32_t w, uint32_t h);
static void renderer_shutdown(CCRenderer* r);
static GLuint compile_shader_src(const char* vert_src, const char* frag_src, const char* label);
static void build_gbuffer(CCRenderer* r, uint32_t w, uint32_t h);
static void build_postfx(CCRenderer* r, uint32_t w, uint32_t h);
static void render_lighting_pass(CCRenderer* r);
static void render_decal_pass(CCRenderer* r);
static void render_shadow_pass(CCRenderer* r);
static void render_ssao_pass(CCRenderer* r);
static void render_ssr_pass(CCRenderer* r);
static void render_ssgi_pass(CCRenderer* r);
static void render_postfx_pass(CCRenderer* r);
static void render_dof_pass(CCRenderer* r);
static void render_2d_flush(CCRenderer* r);
static void render_gui_flush(CCRenderer* r);

/* ══════════════════════════════════════════════════════════════════════
   GLSL SOURCES — embedded so no external shader files needed at runtime
   ══════════════════════════════════════════════════════════════════════ */

/* ── G-buffer geometry pass ───────────────────────────────────────────── */
static const char* GBUF_VERT = "#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
"layout(location=3) in vec4 aTangent;\n"
"layout(location=4) in vec4 aColor;\n"
"uniform mat4 uMVP;\n"
"uniform mat4 uModel;\n"
"uniform mat3 uNormalMat;\n"
"out vec3 vFragPos;\n"
"out vec3 vNormal;\n"
"out vec2 vUV;\n"
"out mat3 vTBN;\n"
"out vec4 vColor;\n"
"void main() {\n"
"    vec4 world = uModel * vec4(aPos, 1.0);\n"
"    vFragPos = world.xyz;\n"
"    vNormal  = normalize(uNormalMat * aNormal);\n"
"    vUV = aUV;\n"
"    vColor = aColor;\n"
"    vec3 N = vNormal;\n"
"    vec3 T;\n"
"    if (length(aTangent.xyz) < 1e-4) {\n"
"        /* no authored tangent (e.g. sphere primitive): derive an arbitrary\n"
"           tangent perpendicular to N so the TBN stays well-formed. */\n"
"        vec3 up = abs(N.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0);\n"
"        T = normalize(cross(up, N));\n"
"    } else {\n"
"        T = normalize(uNormalMat * aTangent.xyz);\n"
"        T = normalize(T - dot(T,N)*N);\n"
"    }\n"
"    float hs = (aTangent.w != 0.0) ? aTangent.w : 1.0;\n"
"    vec3 B = cross(N, T) * hs;\n"
"    vTBN = mat3(T, B, N);\n"
"    gl_Position = uMVP * vec4(aPos, 1.0);\n"
"}\n";

/* Instanced gbuffer vertex shader. Per-instance model matrix arrives as four
 * vec4 attributes (locations 5..8); the view-projection is a single uniform.
 * The normal matrix is derived per-instance from the instance model matrix
 * (upper 3x3), which is correct for uniform and non-uniform scale alike up to
 * the usual inverse-transpose approximation — for the common case of rigid +
 * uniform scale it is exact. */
static const char* GBUF_INST_VERT = "#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
"layout(location=3) in vec4 aTangent;\n"
"layout(location=4) in vec4 aColor;\n"
"layout(location=5) in mat4 aInstModel;\n"   /* consumes locations 5,6,7,8 */
"uniform mat4 uVP;\n"
"out vec3 vFragPos;\n"
"out vec3 vNormal;\n"
"out vec2 vUV;\n"
"out mat3 vTBN;\n"
"out vec4 vColor;\n"
"void main() {\n"
"    mat4 model = aInstModel;\n"
"    vec4 world = model * vec4(aPos, 1.0);\n"
"    vFragPos = world.xyz;\n"
"    mat3 nrm = mat3(model);\n"   /* upper-3x3; fine for rigid+uniform scale */
"    vNormal  = normalize(nrm * aNormal);\n"
"    vUV = aUV;\n"
"    vColor = aColor;\n"
"    vec3 T = normalize(nrm * aTangent.xyz);\n"
"    vec3 N = vNormal;\n"
"    T = normalize(T - dot(T,N)*N);\n"
"    vec3 B = cross(N, T) * aTangent.w;\n"
"    vTBN = mat3(T, B, N);\n"
"    gl_Position = uVP * world;\n"
"}\n";

/* Billboard shaders. A single unit quad (locations 0=corner offset in [-0.5,0.5],
 * 1=uv) is expanded per-instance around a world center using camera-space right/up
 * (spherical) or a yaw-only basis (cylindrical). Per-instance: center, size, color
 * via an instanced buffer at locations 2,3,4. Output writes the billboard color as
 * emissive so particles glow and bloom; the normal faces the camera so any lit
 * contribution is sane. */
static const char* BILLBOARD_VERT = "#version 330 core\n"
"layout(location=0) in vec2 aCorner;\n"   /* [-0.5,0.5] quad */
"layout(location=1) in vec2 aUV;\n"
"layout(location=2) in vec3 iCenter;\n"   /* per-instance */
"layout(location=3) in vec2 iSize;\n"
"layout(location=4) in vec4 iColor;\n"
"uniform mat4 uVP;\n"
"uniform vec3 uCamRight;\n"
"uniform vec3 uCamUp;\n"
"uniform vec3 uCamFwd;\n"
"uniform int  uCylindrical;\n"
"out vec2 vUV;\n"
"out vec4 vColor;\n"
"out vec3 vNormal;\n"
"void main() {\n"
"    vec3 right, up;\n"
"    if (uCylindrical > 0) {\n"
"        up = vec3(0.0, 1.0, 0.0);\n"
"        right = normalize(cross(up, uCamFwd));\n"
"    } else {\n"
"        right = uCamRight;\n"
"        up = uCamUp;\n"
"    }\n"
"    vec3 world = iCenter + right * (aCorner.x * iSize.x) + up * (aCorner.y * iSize.y);\n"
"    vUV = aUV;\n"
"    vColor = iColor;\n"
"    vNormal = -uCamFwd;\n"   /* face the camera */
"    gl_Position = uVP * vec4(world, 1.0);\n"
"}\n";

static const char* BILLBOARD_FRAG = "#version 330 core\n"
"layout(location=0) out vec4 gAlbedoRough;\n"
"layout(location=1) out vec4 gNormalMetal;\n"
"layout(location=2) out vec4 gEmissiveAO;\n"
"in vec2 vUV; in vec4 vColor; in vec3 vNormal;\n"
"uniform sampler2D uTex;\n"
"void main() {\n"
"    vec4 tex = texture(uTex, vUV);\n"
"    vec4 c = tex * vColor;\n"
"    if (c.a < 0.02) discard;\n"          /* alpha-cutout so quads don't box */
"    gAlbedoRough = vec4(0.0, 0.0, 0.0, 1.0);\n"
"    gNormalMetal = vec4(normalize(vNormal) * 0.5 + 0.5, 0.0);\n"
"    gEmissiveAO  = vec4(c.rgb, 1.0);\n"   /* emit color → visible + blooms */
"}\n";

/* Decal shaders. A unit cube (from cc_mesh_cube-style verts, [-0.5,0.5]) is
 * transformed by the decal model matrix and rasterized. For each covered pixel
 * we read scene depth, reconstruct world position, map it into decal-local space
 * via the inverse model, and reject anything outside the unit box. Inside, the
 * box's local (x,z) become UVs and the decal color/texture is written into the
 * albedo and (optionally) emissive G-buffer targets with alpha blending. */
static const char* DECAL_VERT = "#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"uniform mat4 uMVP;\n"
"void main(){ gl_Position = uMVP * vec4(aPos,1.0); }\n";

static const char* DECAL_FRAG = "#version 330 core\n"
"layout(location=0) out vec4 gAlbedoRough;\n"
"layout(location=2) out vec4 gEmissiveAO;\n"
"uniform sampler2D uDepth;\n"
"uniform sampler2D uDecalTex;\n"
"uniform mat4  uInvVP;\n"
"uniform mat4  uInvModel;\n"
"uniform vec2  uScreen;\n"
"uniform vec4  uColor;\n"
"uniform float uEmissive;\n"
"uniform float uAngleFade;\n"
"uniform vec3  uDecalDir;\n"
"uniform sampler2D gNormalTex;\n"   /* to read surface normal for angle culling */
"void main(){\n"
"    vec2 uv = gl_FragCoord.xy / uScreen;\n"
"    float d = texture(uDepth, uv).r;\n"
"    if (d >= 1.0) discard;\n"                 /* background: nothing to stamp */
"    vec4 clip = vec4(uv*2.0-1.0, d*2.0-1.0, 1.0);\n"
"    vec4 wp = uInvVP * clip; wp /= wp.w;\n"
"    vec3 local = (uInvModel * vec4(wp.xyz,1.0)).xyz;\n"   /* into box space */
"    if (any(greaterThan(abs(local), vec3(0.5)))) discard;\n" /* outside box */
"    // Angle culling: skip surfaces whose normal deviates too far from the\n"
"    // decal's projection axis (its world-space -Y). uDecalDir is that axis.\n"
"    if (uAngleFade > 0.0) {\n"
"        vec3 nrm = normalize(texture(gNormalTex, uv).rgb * 2.0 - 1.0);\n"
"        float a = dot(nrm, -normalize(uDecalDir));\n"
"        if (a < cos(uAngleFade)) discard;\n"
"    }\n"
"    vec2 dUV = local.xz + 0.5;\n"             /* [-0.5,0.5] → [0,1] */
"    vec4 t = texture(uDecalTex, dUV) * uColor;\n"
"    if (t.a < 0.01) discard;\n"
"    gAlbedoRough = vec4(t.rgb, t.a);\n"       /* blended by fixed-function */
"    gEmissiveAO  = vec4(t.rgb * uEmissive, t.a);\n"
"}\n";

/* Shadow depth pass: render geometry from the light's viewpoint, writing only
 * depth. The single uLightVP * model transform produces the light-space depth
 * the lighting pass compares against. Instanced draws replay with their baked
 * model matrix (we replay the drawlog, so per-instance batches collapse to their
 * representative matrix — acceptable for the shadow caster set). */
static const char* SHADOW_VERT = "#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"uniform mat4 uLightVP;\n"
"uniform mat4 uModel;\n"
"void main(){ gl_Position = uLightVP * uModel * vec4(aPos,1.0); }\n";
static const char* SHADOW_FRAG = "#version 330 core\n"
"void main(){}\n";   /* depth only */

/* SSAO pass. For each pixel: reconstruct view-space position + normal, sample a
 * cosine-weighted hemisphere of points around it (rotated per-pixel by a noise
 * vector to trade banding for high-frequency noise the blur removes), project
 * each sample to screen space, and compare its view-depth against the stored
 * depth. Occluders within the radius accumulate occlusion. Output is a single
 * visibility channel in [0,1] (1 = unoccluded). */
static const char* SSAO_FRAG = "#version 330 core\n"
"out float FragColor; in vec2 vUV;\n"
"uniform sampler2D gDepth;\n"
"uniform sampler2D gNormal;\n"
"uniform mat4 uProj;\n"
"uniform mat4 uInvProj;\n"
"uniform mat4 uView;\n"
"uniform vec3 uSamples[32];\n"
"uniform vec2 uNoiseScale;\n"
"uniform sampler2D uNoise;\n"
"uniform float uRadius;\n"
"uniform float uBias;\n"
"vec3 view_pos(vec2 uv){\n"
"    float d = texture(gDepth, uv).r;\n"
"    vec4 clip = vec4(uv*2.0-1.0, d*2.0-1.0, 1.0);\n"
"    vec4 v = uInvProj * clip; return v.xyz / v.w;\n"
"}\n"
"void main(){\n"
"    float d = texture(gDepth, vUV).r;\n"
"    if (d >= 1.0) { FragColor = 1.0; return; }\n"       /* background: no AO */
"    vec3 P = view_pos(vUV);\n"
"    vec3 nWorld = texture(gNormal, vUV).rgb * 2.0 - 1.0;\n"
"    if (dot(nWorld,nWorld) < 0.01) { FragColor = 1.0; return; }\n"
"    vec3 N = normalize(mat3(uView) * nWorld);\n"        /* world → view normal */
"    vec3 rvec = normalize(texture(uNoise, vUV * uNoiseScale).xyz * 2.0 - 1.0);\n"
"    vec3 T = normalize(rvec - N * dot(rvec, N));\n"
"    vec3 B = cross(N, T);\n"
"    mat3 TBN = mat3(T, B, N);\n"
"    float occ = 0.0;\n"
"    for (int i=0;i<32;i++){\n"
"        vec3 sp = P + (TBN * uSamples[i]) * uRadius;\n"
"        vec4 off = uProj * vec4(sp, 1.0);\n"
"        off.xyz /= off.w; off.xyz = off.xyz*0.5+0.5;\n"
"        if (off.x<0.0||off.x>1.0||off.y<0.0||off.y>1.0) continue;\n"
"        float sd = view_pos(off.xy).z;\n"
"        float rangeChk = smoothstep(0.0, 1.0, uRadius / abs(P.z - sd));\n"
"        occ += (sd >= sp.z + uBias ? 1.0 : 0.0) * rangeChk;\n"
"    }\n"
"    FragColor = 1.0 - (occ / 32.0);\n"
"}\n";

/* Separable-ish 4x4 box blur to denoise the SSAO (matches the 4x4 noise tile). */
static const char* SSAO_BLUR_FRAG = "#version 330 core\n"
"out float FragColor; in vec2 vUV;\n"
"uniform sampler2D uSSAO; uniform vec2 uTexel;\n"
"void main(){\n"
"    float sum = 0.0;\n"
"    for (int x=-2;x<2;x++) for (int y=-2;y<2;y++)\n"
"        sum += texture(uSSAO, vUV + vec2(x,y)*uTexel).r;\n"
"    FragColor = sum / 16.0;\n"
"}\n";

/* SSR pass. View-space ray march: reflect the view vector about the surface
 * normal, step the ray in view space, project each step to screen space, and
 * detect where the ray passes behind stored scene depth (a hit). A short binary
 * search refines the crossing. Reflected color is sampled from the lit HDR
 * buffer, weighted by metallic + fresnel, and faded near screen edges and at
 * grazing/backward rays. Output is premultiplied reflection (rgb) + weight (a),
 * composited over the HDR scene in a following step. */
static const char* SSR_FRAG = "#version 330 core\n"
"out vec4 FragColor; in vec2 vUV;\n"
"uniform sampler2D uColor;\n"    /* lit HDR scene */
"uniform sampler2D gDepth;\n"
"uniform sampler2D gNormal;\n"   /* rgb=normal, a=metallic */
"uniform sampler2D gAlbedoRough;\n" /* a=roughness */
"uniform mat4 uProj;\n"
"uniform mat4 uInvProj;\n"
"uniform mat4 uView;\n"
"uniform float uMaxDist;\n"
"uniform float uIntensity;\n"
"vec3 view_pos(vec2 uv, float d){\n"
"    vec4 clip = vec4(uv*2.0-1.0, d*2.0-1.0, 1.0);\n"
"    vec4 v = uInvProj * clip; return v.xyz / v.w;\n"
"}\n"
"void main(){\n"
"    float d = texture(gDepth, vUV).r;\n"
"    if (d >= 1.0) { FragColor = vec4(0.0); return; }\n"
"    vec4 nm = texture(gNormal, vUV);\n"
"    float metal = nm.a;\n"
"    float rough = texture(gAlbedoRough, vUV).a;\n"
"    if (metal < 0.02 || rough > 0.75) { FragColor = vec4(0.0); return; }\n" /* only shiny/metallic reflect */
"    vec3 P = view_pos(vUV, d);\n"
"    vec3 N = normalize(mat3(uView) * (nm.rgb*2.0-1.0));\n"
"    vec3 V = normalize(P);\n"                     /* view-space: eye at origin */
"    vec3 R = normalize(reflect(V, N));\n"
"    if (R.z > 0.0) { FragColor = vec4(0.0); return; }\n" /* reflecting toward camera plane: skip */
"    const int STEPS = 48;\n"
"    float stepLen = uMaxDist / float(STEPS);\n"
"    float thickness = stepLen * 2.0;\n"   /* accept hits within ~2 steps behind surface */
"    vec3 rayPos = P;\n"
"    vec2 hitUV = vec2(-1.0);\n"
"    for (int i=0;i<STEPS;i++){\n"
"        rayPos += R * stepLen;\n"
"        vec4 clip = uProj * vec4(rayPos, 1.0);\n"
"        if (clip.w <= 0.0) break;\n"
"        vec2 suv = (clip.xy/clip.w)*0.5+0.5;\n"
"        if (suv.x<0.0||suv.x>1.0||suv.y<0.0||suv.y>1.0) break;\n"
"        float sd = texture(gDepth, suv).r;\n"
"        vec3 sceneP = view_pos(suv, sd);\n"
"        float delta = rayPos.z - sceneP.z;\n"     /* ray behind surface = hit (z negative fwd) */
"        if (delta < 0.0 && delta > -thickness) {\n"      /* crossed, within thickness */
"            vec3 a = rayPos - R*stepLen, b = rayPos;\n"
"            for (int j=0;j<5;j++){\n"
"                vec3 mid = (a+b)*0.5;\n"
"                vec4 c2 = uProj*vec4(mid,1.0); vec2 mu=(c2.xy/c2.w)*0.5+0.5;\n"
"                float md = texture(gDepth, mu).r; vec3 mp = view_pos(mu, md);\n"
"                if (mid.z - mp.z < 0.0) b=mid; else a=mid;\n"
"                hitUV = mu;\n"
"            }\n"
"            break;\n"
"        }\n"
"    }\n"
"    if (hitUV.x < 0.0) { FragColor = vec4(0.0); return; }\n"
"    // Fades that hide screen-space artifacts:\n"
"    //  - screen-edge fade (wider band) so reflections vanish smoothly at borders\n"
"    //  - distance fade as the hit approaches the ray budget\n"
"    //  - grazing fresnel weighting + roughness/metallic gating\n"
"    vec2 e = smoothstep(vec2(0.0), vec2(0.22), hitUV) * (1.0-smoothstep(vec2(0.78), vec2(1.0), hitUV));\n"
"    float edge = e.x * e.y;\n"
"    float hitDist = length(view_pos(hitUV, texture(gDepth,hitUV).r) - P);\n"
"    float distFade = 1.0 - clamp(hitDist / uMaxDist, 0.0, 1.0);\n"
"    float fres = pow(1.0 - max(dot(-V, N), 0.0), 3.0);\n"
"    float w = clamp(metal, 0.0, 1.0) * (1.0 - rough) * edge * distFade * (0.35 + 0.65*fres) * uIntensity;\n"
"    vec3 refl = texture(uColor, hitUV).rgb;\n"
"    FragColor = vec4(refl * w, w);\n"
"}\n";

/* Composite SSR reflections over the HDR scene. The reflection weight is baked
   into ssr.a; blend the (already weighted) reflection color over the scene. */
static const char* SSR_COMPOSITE_FRAG = "#version 330 core\n"
"out vec4 FragColor; in vec2 vUV;\n"
"uniform sampler2D uScene; uniform sampler2D uSSR;\n"
"void main(){\n"
"    vec3 scene = texture(uScene, vUV).rgb;\n"
"    vec4 ssr = texture(uSSR, vUV);\n"
"    FragColor = vec4(scene * (1.0 - ssr.a) + ssr.rgb, 1.0);\n"
"}\n";

/* ── SSGI: screen-space global illumination (one indirect bounce) ──────────
 * For each pixel we gather light from nearby ON-SCREEN surfaces: sample a set
 * of directions in the hemisphere around N, step a short ray, project to
 * screen, and if we land on a surface that faces back toward us, add its LIT
 * color weighted by the cosine term. This is the core "lit by the world" cue —
 * indirect fill + COLOR BLEED (a red wall tints a nearby white floor). It is an
 * approximation: screen-space only (misses off-screen bounces), single bounce.
 * Structure mirrors SSAO's hemisphere sampling + SSR's project-and-read. */
static const char* SSGI_FRAG = "#version 330 core\n"
"out vec4 FragColor; in vec2 vUV;\n"
"uniform sampler2D uColor;\n"   /* lit HDR scene (previous frame's composite) */
"uniform sampler2D gDepth;\n"
"uniform sampler2D gNormal;\n"  /* rgb=normal(world), a=metallic */
"uniform sampler2D uNoise;\n"
"uniform mat4 uProj;\n"
"uniform mat4 uInvProj;\n"
"uniform mat4 uView;\n"
"uniform vec2 uNoiseScale;\n"
"uniform float uRadius;\n"
"uniform float uIntensity;\n"
"uniform float uFrame;\n"   /* rotates the kernel each frame so accumulation adds new samples */
"vec3 view_pos(vec2 uv, float d){\n"
"    vec4 clip = vec4(uv*2.0-1.0, d*2.0-1.0, 1.0);\n"
"    vec4 v = uInvProj * clip; return v.xyz / v.w;\n"
"}\n"
"void main(){\n"
"    float d = texture(gDepth, vUV).r;\n"
"    if (d >= 1.0) { FragColor = vec4(0.0); return; }\n"
"    vec3 P = view_pos(vUV, d);\n"
"    vec4 nm = texture(gNormal, vUV);\n"
"    vec3 N = normalize(mat3(uView) * (nm.rgb*2.0-1.0));\n"  /* view-space normal */
"    vec3 rvec = normalize(texture(uNoise, vUV*uNoiseScale).xyz*2.0-1.0);\n"
"    vec3 T = normalize(rvec - N*dot(rvec,N));\n"
"    vec3 B = cross(N, T);\n"
"    mat3 TBN = mat3(T, B, N);\n"
"    /* per-frame angular offset (golden-angle) so each accumulated frame samples\n"
"       a rotated set of directions — over N frames this multiplies effective\n"
"       sample count and denoises the bounce. */\n"
"    float frameRot = uFrame * 2.399963;\n"
"    const int SAMPLES = 12;\n"
"    vec3 gathered = vec3(0.0); float wsum = 0.0;\n"
"    for (int i=0;i<SAMPLES;i++){\n"
"        float a = (float(i)+0.5)/float(SAMPLES);\n"
"        float ang = a*6.2831853*3.0 + frameRot;\n"
"        vec3 h = normalize(vec3(cos(ang)*sqrt(a), sin(ang)*sqrt(a), 0.4+0.6*sqrt(1.0-a)));\n"
"        vec3 dir = TBN * h;\n"                 /* hemisphere dir around N */
"        vec3 samplePos = P + dir * uRadius * (0.3 + 0.7*a);\n"
"        vec4 clip = uProj * vec4(samplePos, 1.0);\n"
"        if (clip.w <= 0.0) continue;\n"
"        vec2 suv = (clip.xy/clip.w)*0.5+0.5;\n"
"        if (suv.x<0.0||suv.x>1.0||suv.y<0.0||suv.y>1.0) continue;\n"
"        float sd = texture(gDepth, suv).r; if (sd>=1.0) continue;\n"
"        vec3 sP = view_pos(suv, sd);\n"
"        vec3 toS = sP - P; float dist = length(toS);\n"
"        if (dist < 1e-4 || dist > uRadius*1.5) continue;\n"
"        vec3 L = toS / dist;\n"
"        float ndl = max(dot(N, L), 0.0);\n"            /* receiver cosine */
"        vec3 sN = normalize(mat3(uView)*(texture(gNormal,suv).rgb*2.0-1.0));\n"
"        float facing = max(dot(sN, -L), 0.0);\n"       /* emitter must face us */
"        float atten = 1.0 / (1.0 + dist*dist*0.5);\n"
"        float w = ndl * facing * atten;\n"
"        gathered += texture(uColor, suv).rgb * w;\n"
"        wsum += w;\n"
"    }\n"
"    /* Normalize by ACTUAL contributing weight (not fixed SAMPLES): this is what\n"
"       lets bounced color read at full saturation instead of washing to grey.\n"
"       A soft confidence term keeps sparse-hit pixels from over-amplifying. */\n"
"    float conf = wsum / (wsum + 0.5);\n"
"    vec3 gi = (wsum>1e-4) ? (gathered / wsum) * conf : vec3(0.0);\n"
"    gi *= uIntensity;\n"
"    FragColor = vec4(gi, 1.0);\n"
"}\n";

/* SSGI temporal accumulation: exponential blend of the fresh gather with the
   previous frame's accumulation. For the static headless camera reprojection is
   identity, so over the multi-frame loop this converges to a high-sample,
   denoised bounce. History is clamped toward the current value's magnitude to
   avoid runaway when content changes. */
static const char* SSGI_ACCUM_FRAG = "#version 330 core\n"
"out vec4 FragColor; in vec2 vUV;\n"
"uniform sampler2D uCurrent;\n"
"uniform sampler2D uHistory;\n"
"uniform int uHistoryValid;\n"
"uniform float uBlend;\n"       /* history weight, ~0.8 */
"void main(){\n"
"    vec3 cur = texture(uCurrent, vUV).rgb;\n"
"    if (uHistoryValid == 0) { FragColor = vec4(cur,1.0); return; }\n"
"    vec3 hist = texture(uHistory, vUV).rgb;\n"
"    /* clamp history to a few x the current gather so a suddenly-changed pixel\n"
"       can't keep injecting stale bright GI (cheap anti-ghost for GI). */\n"
"    vec3 hi = max(cur*3.0 + 0.02, vec3(0.02));\n"
"    hist = min(hist, hi);\n"
"    FragColor = vec4(mix(cur, hist, uBlend), 1.0);\n"
"}\n";

/* Composite SSGI indirect light additively onto the scene (modulated by albedo
   so bounced light picks up the receiver's own color, like real diffuse GI). */
static const char* SSGI_COMPOSITE_FRAG = "#version 330 core\n"
"out vec4 FragColor; in vec2 vUV;\n"
"uniform sampler2D uScene; uniform sampler2D uSSGI; uniform sampler2D gAlbedoRough;\n"
"uniform vec2 uTexel;\n"
"void main(){\n"
"    vec3 scene = texture(uScene, vUV).rgb;\n"
"    /* 5x5 box blur of the indirect gather to hide the low-sample noise. */\n"
"    vec3 gi = vec3(0.0);\n"
"    for (int y=-2;y<=2;y++) for (int x=-2;x<=2;x++)\n"
"        gi += texture(uSSGI, vUV + vec2(x,y)*uTexel).rgb;\n"
"    gi /= 25.0;\n"
"    vec3 albedo = texture(gAlbedoRough, vUV).rgb;\n"
"    FragColor = vec4(scene + gi*albedo, 1.0);\n"  /* indirect tinted by receiver albedo */
"}\n";

/* ── TAA resolve: blend current frame with reprojected history ─────────────
 * Camera is jittered a sub-pixel amount per frame (Halton), so each frame is a
 * slightly different sample of the same image; blending them yields true
 * supersampling — clean edges, no shimmer, and free denoise of SSGI/shadows.
 * History is neighborhood-CLAMPED to the current 3x3 color box so moving/
 * changing content can't ghost. For a static camera (the headless test case)
 * reprojection is identity and this converges to N-sample SSAA over N frames. */
static const char* AA_SMAA_FRAG = "#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 vUV;\n"
"uniform sampler2D uTex;\n"
"uniform vec2 uTexel;\n"
"uniform float uThreshold;\n"   /* edge luma threshold (High vs Ultra) */
"uniform float uSearch;\n"      /* max search steps along the edge (High vs Ultra) */
"float L(vec3 c){ return dot(c, vec3(0.299,0.587,0.114)); }\n"
"vec3 T(vec2 uv){ return texture(uTex, uv).rgb; }\n"
"void main(){\n"
"  vec2 px = uTexel;\n"
"  vec3 m = T(vUV);\n"
"  float lm=L(m);\n"
"  float ln=L(T(vUV+vec2(0.0,-px.y)));\n"
"  float ls=L(T(vUV+vec2(0.0, px.y)));\n"
"  float lw=L(T(vUV+vec2(-px.x,0.0)));\n"
"  float le=L(T(vUV+vec2( px.x,0.0)));\n"
"  float gx = abs(lw-le), gy = abs(ln-ls);\n"
"  float edge = max(gx,gy);\n"
"  if(edge < uThreshold){ FragColor=vec4(m,1.0); return; }\n"
"  /* Directional edge smoothing: the edge runs ALONG the low-gradient axis. We\n"
"     blend the pixel with its two neighbors ACROSS the edge (perpendicular),\n"
"     weighted by how far along the edge the contrasting run continues — i.e.\n"
"     reconstructing coverage. We do NOT blur along both axes (that's the box\n"
"     blur that leaves stairsteps); we move strictly across the detected edge. */\n"
"  bool horiz = gy > gx;         /* horizontal edge → blend vertically (across) */\n"
"  vec2 along = horiz ? vec2(px.x,0.0) : vec2(0.0,px.y);   /* edge direction */\n"
"  vec2 acrs  = horiz ? vec2(0.0,px.y) : vec2(px.x,0.0);   /* across (blend) dir */\n"
"  /* which side is the bright side? blend toward the average across the edge,\n"
"     but only proportional to coverage estimated by walking along the edge. */\n"
"  float lpos = L(T(vUV+acrs)), lneg = L(T(vUV-acrs));\n"
"  /* search along the edge in both directions for where the edge ends (luma on\n"
"     the 'across+' side stops differing) → distance gives sub-pixel coverage. */\n"
"  float dpos=0.0, dneg=0.0; float maxs = uSearch;\n"
"  for(float i=1.0;i<=32.0;i+=1.0){ if(i>maxs) break;\n"
"    float d = abs(L(T(vUV+along*i+acrs)) - L(T(vUV+along*i))) ;\n"
"    if(d < uThreshold*0.5){ dpos=i; break; } dpos=i; }\n"
"  for(float i=1.0;i<=32.0;i+=1.0){ if(i>maxs) break;\n"
"    float d = abs(L(T(vUV-along*i+acrs)) - L(T(vUV-along*i))) ;\n"
"    if(d < uThreshold*0.5){ dneg=i; break; } dneg=i; }\n"
"  /* coverage: position of this pixel within the edge span → triangular weight,\n"
"     the classic morphological AA area estimate. */\n"
"  float span = dpos + dneg + 1.0;\n"
"  float pos  = dneg + 0.5;\n"
"  float cov  = 1.0 - abs(2.0*pos/span - 1.0);   /* 0 at ends, 1 at middle */\n"
"  cov *= 0.5;                                    /* max half-pixel shift */\n"
"  /* blend across the edge toward the neighbor on the higher-contrast side */\n"
"  vec3 across_pos = T(vUV+acrs), across_neg = T(vUV-acrs);\n"
"  vec3 target = (abs(lpos-lm) > abs(lneg-lm)) ? across_pos : across_neg;\n"
"  FragColor = vec4(mix(m, target, cov), 1.0);\n"
"}\n";

/* Analytic coverage — the "pixel is a diagonal" resolve, driven by TRUE GEOMETRY
 * from the depth buffer (not screen-space luma, which was weak on low-contrast
 * silhouettes). It finds silhouette edges as depth discontinuities, derives the
 * edge line's normal from the depth gradient, estimates the sub-pixel coverage,
 * and composites object vs background by the EXACT half-plane area of the pixel
 * cell (same math as cc_pixshape_halfplane_area on CPU). Near-USD3 smoothness at
 * ~1x cost: no supersampling, coverage computed analytically. */
static const char* AA_ANALYTIC_FRAG = "#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 vUV;\n"
"uniform sampler2D uTex;\n"
"uniform sampler2D uDepth;\n"
"uniform vec2 uTexel;\n"
"uniform float uNear, uFar;\n"
"float lin(vec2 uv){ float d=texture(uDepth,uv).r; float z=d*2.0-1.0;\n"
"  return (2.0*uNear*uFar)/(uFar+uNear - z*(uFar-uNear)); }\n"
"/* exact area of unit cell on the object side of a line: unit normal n, signed\n"
"   distance sd of the cell center (pixels, + toward object). */\n"
"float cover(vec2 n, float sd){\n"
"  float a=abs(n.x), b=abs(n.y); float s=a+b; if(s<1e-5) return sd>=0.0?1.0:0.0;\n"
"  a/=s; b/=s; float t=clamp(sd+0.5,0.0,1.0); float amin=min(a,b);\n"
"  if(amin<1e-5) return t;\n"
"  float f=amin*0.5;\n"
"  if(t<f) return (t*t)/(2.0*amin);\n"
"  if(t>1.0-f){ float u=1.0-t; return 1.0-(u*u)/(2.0*amin); }\n"
"  return t;\n"
"}\n"
"void main(){\n"
"  vec3 cC=texture(uTex,vUV).rgb;\n"
"  float dC=lin(vUV);\n"
"  float dL=lin(vUV+vec2(-uTexel.x,0)), dR=lin(vUV+vec2(uTexel.x,0));\n"
"  float dD=lin(vUV+vec2(0,-uTexel.y)), dU=lin(vUV+vec2(0,uTexel.y));\n"
"  /* depth gradient → edge normal; magnitude relative to local depth = a real\n"
"     silhouette (scale-invariant so far objects still resolve). */\n"
"  float gx=(dR-dL)*0.5, gy=(dU-dD)*0.5;\n"
"  float gmag=length(vec2(gx,gy));\n"
"  /* A true silhouette is a SHARP one-pixel depth step, not the gentle depth\n"
"     slope across a tilted face. Require the second difference (curvature) to be\n"
"     large too — i.e. the center sits on a discontinuity, not a ramp. */\n"
"  float lap = abs(dL+dR+dD+dU - 4.0*dC);\n"
"  float ref=max(dC,1e-3);\n"
"  float step_sharp = lap/ref;\n"
"  if(step_sharp < 0.02){ FragColor=vec4(cC,1.0); return; }  /* smooth slope/flat → keep */\n"
"  vec2 n=normalize(vec2(gx,gy));\n"
"  n = -n;                          /* → toward object (nearer) */\n"
"  float dn=min(min(dL,dR),min(dD,dU)), df=max(max(dL,dR),max(dD,dU));\n"
"  float mid=(dn+df)*0.5, rng=max(df-dn,1e-4);\n"
"  float sd=(mid-dC)/rng;\n"
"  float cov=cover(n, sd);\n"
"  if(cov > 0.98 || cov < 0.02){ FragColor=vec4(cC,1.0); return; }\n"
"  vec3 cNear=texture(uTex, vUV + n*uTexel*0.5).rgb;\n"
"  FragColor=vec4(mix(cC, cNear, cov*0.5), 1.0);\n"
"}\n";

static const char* AA_DOWNSAMPLE_FRAG = "#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 vUV;\n"
"uniform sampler2D uTex;\n"
"uniform vec2 uSrcTexel;\n"   /* 1/src_size */
"uniform int uN;\n"          /* box size, samples NxN source texels */
"void main(){\n"
"  /* proper box downsample: average an NxN block of source texels for this\n"
"     output pixel. A LINEAR blit only taps 2x2 and discards the rest of the\n"
"     supersampled detail — THIS is what makes SSAA actually resolve to smooth\n"
"     edges instead of a soft near-aliased image. */\n"
"  vec3 acc = vec3(0.0); float cnt=0.0;\n"
"  float h = float(uN)*0.5;\n"
"  for(int y=0;y<uN;y++){ for(int x=0;x<uN;x++){\n"
"    vec2 o = (vec2(float(x),float(y)) - h + 0.5) * uSrcTexel;\n"
"    acc += texture(uTex, vUV + o).rgb; cnt+=1.0;\n"
"  }}\n"
"  FragColor = vec4(acc/cnt, 1.0);\n"
"}\n";

static const char* AA_USD_FRAG = "#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 vUV;\n"
"uniform sampler2D uTex;\n"
"uniform vec2 uTexel;\n"
"uniform float uAmount;\n"   /* smoothing strength (user-defined) */
"float L(vec3 c){ return dot(c, vec3(0.299,0.587,0.114)); }\n"
"vec3 T(vec2 uv){ return texture(uTex, uv).rgb; }\n"
"void main(){\n"
"  /* USD 'smooth': DIRECTIONAL edge smoothing (not a box blur). Detect the edge\n"
"     orientation from the local gradient and blend ALONG the edge tangent only,\n"
"     which straightens the staircase into a clean ramp while leaving flat areas\n"
"     and across-edge detail untouched. Runs on the supersampled image between\n"
"     upscale rounds; uAmount scales how strongly edges are reflowed. */\n"
"  vec2 px = uTexel;\n"
"  vec3 m = T(vUV); float lm=L(m);\n"
"  float ln=L(T(vUV+vec2(0.0,-px.y))), ls=L(T(vUV+vec2(0.0,px.y)));\n"
"  float lw=L(T(vUV+vec2(-px.x,0.0))), le=L(T(vUV+vec2(px.x,0.0)));\n"
"  float gx=abs(lw-le), gy=abs(ln-ls);\n"
"  float edge=max(gx,gy);\n"
"  if(edge < 0.015){ FragColor=vec4(m,1.0); return; }\n"  /* flat → keep crisp */
"  /* tangent = along the edge (low-gradient axis). Sample several taps ALONG the\n"
"     tangent and average — this reflows the edge into a straight line. */\n"
"  bool horiz = gy > gx;\n"
"  vec2 tan = horiz ? vec2(px.x,0.0) : vec2(0.0,px.y);\n"
"  /* tight along-edge reflow: center-weighted, only 2 taps each side, so the\n"
"     edge straightens without the transition widening into a blur. */\n"
"  vec3 acc = m*3.0; float wsum=3.0;\n"
"  acc += (T(vUV+tan)+T(vUV-tan))*1.5; wsum += 3.0;\n"
"  acc += (T(vUV+tan*2.0)+T(vUV-tan*2.0))*0.5; wsum += 1.0;\n"
"  vec3 along = acc/wsum;\n"
"  float k = clamp(uAmount, 0.0, 4.0)*0.5;\n"   /* amount 1.0 => 50% reflow */
"  k = clamp(k, 0.0, 1.0);\n"
"  FragColor = vec4(mix(m, along, k), 1.0);\n"
"}\n";

static const char* TAA_FRAG = "#version 330 core\n"
"out vec4 FragColor; in vec2 vUV;\n"
"uniform sampler2D uCurrent;\n"
"uniform sampler2D uHistory;\n"
"uniform vec2 uTexel;\n"
"uniform float uBlend;\n"        /* history weight (0=no AA, ~0.9 = strong accumulate) */
"uniform int uHistoryValid;\n"
"void main(){\n"
"    vec3 cur = texture(uCurrent, vUV).rgb;\n"
"    if (uHistoryValid == 0) { FragColor = vec4(cur,1.0); return; }\n"
"    /* current-neighborhood color box for history clamping (anti-ghost) */\n"
"    vec3 lo = cur, hi = cur;\n"
"    for (int y=-1;y<=1;y++) for (int x=-1;x<=1;x++){\n"
"        vec3 c = texture(uCurrent, vUV + vec2(x,y)*uTexel).rgb;\n"
"        lo = min(lo,c); hi = max(hi,c);\n"
"    }\n"
"    vec3 hist = texture(uHistory, vUV).rgb;\n"
"    hist = clamp(hist, lo, hi);\n"           /* reject ghosts outside local range */
"    vec3 outc = mix(cur, hist, uBlend);\n"
"    FragColor = vec4(outc, 1.0);\n"
"}\n";

/* ── Depth of field ────────────────────────────────────────────────────────
 * CoC (circle of confusion) from linearized view-space depth: pixels within
 * uFocusRange of uFocusDist stay sharp; beyond that, CoC ramps to 1.0 and
 * scales the gather radius to uMaxBlur pixels. The gather is a 2-ring disk
 * (16 taps) whose contributions are weighted DOWN when a neighbor is much
 * sharper than the center — this stops an in-focus foreground from smearing
 * its color into a blurred background (the classic DOF bleeding artifact).
 * Operates on the already-tonemapped LDR image. */
static const char* DOF_FRAG = "#version 330 core\n"
"out vec4 FragColor; in vec2 vUV;\n"
"uniform sampler2D uColor;\n"
"uniform sampler2D uDepth;\n"
"uniform vec2  uTexel;\n"
"uniform float uNear, uFar;\n"
"uniform float uFocusDist, uFocusRange, uMaxBlur;\n"
"float lin_depth(vec2 uv){\n"
"    float d = texture(uDepth, uv).r;\n"
"    float z = d*2.0-1.0;\n"
"    return (2.0*uNear*uFar) / (uFar+uNear - z*(uFar-uNear));\n"
"}\n"
"float coc(float dist){\n"
"    float dz = abs(dist - uFocusDist);\n"
"    return clamp((dz - uFocusRange) / max(uFocusRange, 1e-3), 0.0, 1.0);\n"
"}\n"
"void main(){\n"
"    float cd = lin_depth(vUV);\n"
"    float centerCoC = coc(cd);\n"
"    if (centerCoC < 0.003) { FragColor = vec4(texture(uColor, vUV).rgb, 1.0); return; }\n"
"    float radius = centerCoC * uMaxBlur;\n"
"    vec3 sum = texture(uColor, vUV).rgb; float wsum = 1.0;\n"
"    /* 16-tap two-ring disk (golden-angle spiral) */\n"
"    const int N = 16;\n"
"    for (int i=0;i<N;i++){\n"
"        float t = (float(i)+0.5)/float(N);\n"
"        float ang = float(i)*2.399963;\n"          /* golden angle */
"        float rr = sqrt(t) * radius;\n"
"        vec2 off = vec2(cos(ang),sin(ang)) * rr * uTexel;\n"
"        vec2 suv = vUV + off;\n"
"        vec3 c = texture(uColor, suv).rgb;\n"
"        float sCoC = coc(lin_depth(suv));\n"
"        /* weight: a sample contributes only as much as its own blurriness — a\n"
"           sharp (in-focus) neighbor won't bleed into this blurred pixel. */\n"
"        float w = max(sCoC, 0.05);\n"
"        sum += c * w; wsum += w;\n"
"    }\n"
"    FragColor = vec4(sum/wsum, 1.0);\n"
"}\n";

/* ── Image-based lighting (split-sum, after Karis/Epic) ────────────────────
 * Real cubemap IBL. The environment is captured into a GL cubemap, then
 * preprocessed into (1) a diffuse irradiance cubemap (cosine-convolved) and
 * (2) a prefiltered specular cubemap (GGX-importance-sampled across roughness
 * mips), combined at runtime with a (3) BRDF integration LUT. All IBL passes
 * share a cube vertex shader that rasterizes a unit cube per face. */
static const char* IBL_CUBE_VERT = "#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"uniform mat4 uVP;\n"
"out vec3 vDir;\n"
"void main(){ vDir = aPos; gl_Position = uVP * vec4(aPos,1.0); }\n";

/* Capture the procedural sky into the cubemap faces (same gradient as the
   lighting shader's sky_color, so the environment matches the visible sky). */
static const char* IBL_CAPTURE_FRAG = "#version 330 core\n"
"out vec4 FragColor; in vec3 vDir;\n"
"uniform vec3 uSkyZenith; uniform vec3 uSkyHorizon; uniform vec3 uSkyGround;\n"
"uniform float uSkyIntensity;\n"
"vec3 sky_color(vec3 d){\n"
"    float y = clamp(normalize(d).y, -1.0, 1.0);\n"
"    float up = smoothstep(0.0, 0.55, y);\n"
"    vec3 above = mix(uSkyHorizon, uSkyZenith, up);\n"
"    float dn = smoothstep(0.0, 0.6, -y);\n"
"    vec3 below = mix(uSkyHorizon, uSkyGround, dn);\n"
"    vec3 base = (y >= 0.0) ? above : below;\n"
"    base += uSkyHorizon * exp(-abs(y)*6.0) * 0.15;\n"
"    return base * uSkyIntensity;\n"
"}\n"
"void main(){ FragColor = vec4(sky_color(vDir), 1.0); }\n";

/* Diffuse irradiance: hemispherical cosine convolution of the environment. */
static const char* IBL_IRRADIANCE_FRAG = "#version 330 core\n"
"out vec4 FragColor; in vec3 vDir;\n"
"uniform samplerCube uEnv;\n"
"const float PI = 3.14159265;\n"
"void main(){\n"
"    vec3 N = normalize(vDir);\n"
"    vec3 up = abs(N.y) < 0.999 ? vec3(0,1,0) : vec3(1,0,0);\n"
"    vec3 right = normalize(cross(up, N));\n"
"    up = cross(N, right);\n"
"    vec3 irr = vec3(0.0); float samples = 0.0;\n"
"    for (float phi=0.0; phi<2.0*PI; phi+=0.15){\n"
"        for (float theta=0.0; theta<0.5*PI; theta+=0.05){\n"
"            vec3 tang = cos(phi)*right + sin(phi)*up;\n"
"            vec3 sampleV = cos(theta)*N + sin(theta)*tang;\n"
"            irr += texture(uEnv, sampleV).rgb * cos(theta)*sin(theta);\n"
"            samples += 1.0;\n"
"        }\n"
"    }\n"
"    irr = PI * irr / samples;\n"
"    FragColor = vec4(irr, 1.0);\n"
"}\n";

/* Prefiltered specular: GGX importance sampling at a given roughness (uRough). */
static const char* IBL_PREFILTER_FRAG = "#version 330 core\n"
"out vec4 FragColor; in vec3 vDir;\n"
"uniform samplerCube uEnv;\n"
"uniform float uRough;\n"
"const float PI = 3.14159265;\n"
"float radicalInverse(uint bits){\n"
"    bits = (bits << 16u) | (bits >> 16u);\n"
"    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n"
"    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n"
"    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n"
"    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n"
"    return float(bits) * 2.3283064365386963e-10;\n"
"}\n"
"vec2 hammersley(uint i, uint n){ return vec2(float(i)/float(n), radicalInverse(i)); }\n"
"vec3 importanceGGX(vec2 xi, vec3 N, float rough){\n"
"    float a = rough*rough;\n"
"    float phi = 2.0*PI*xi.x;\n"
"    float ct = sqrt((1.0-xi.y)/(1.0+(a*a-1.0)*xi.y));\n"
"    float st = sqrt(1.0-ct*ct);\n"
"    vec3 H = vec3(cos(phi)*st, sin(phi)*st, ct);\n"
"    vec3 up = abs(N.z)<0.999 ? vec3(0,0,1) : vec3(1,0,0);\n"
"    vec3 tx = normalize(cross(up,N)); vec3 ty = cross(N,tx);\n"
"    return normalize(tx*H.x + ty*H.y + N*H.z);\n"
"}\n"
"void main(){\n"
"    vec3 N = normalize(vDir); vec3 V = N;\n"
"    const uint SAMPLES = 128u;\n"
"    vec3 color = vec3(0.0); float total = 0.0;\n"
"    for (uint i=0u; i<SAMPLES; i++){\n"
"        vec2 xi = hammersley(i, SAMPLES);\n"
"        vec3 H = importanceGGX(xi, N, uRough);\n"
"        vec3 L = normalize(2.0*dot(V,H)*H - V);\n"
"        float ndl = max(dot(N,L), 0.0);\n"
"        if (ndl > 0.0){ color += texture(uEnv, L).rgb * ndl; total += ndl; }\n"
"    }\n"
"    FragColor = vec4(color/max(total,0.001), 1.0);\n"
"}\n";

/* BRDF integration LUT (fullscreen; x=NdotV, y=roughness → scale,bias). */
static const char* IBL_BRDF_FRAG = "#version 330 core\n"
"out vec2 FragColor; in vec2 vUV;\n"
"const float PI = 3.14159265;\n"
"float radicalInverse(uint bits){\n"
"    bits = (bits << 16u) | (bits >> 16u);\n"
"    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n"
"    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n"
"    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n"
"    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n"
"    return float(bits) * 2.3283064365386963e-10;\n"
"}\n"
"vec2 hammersley(uint i, uint n){ return vec2(float(i)/float(n), radicalInverse(i)); }\n"
"vec3 importanceGGX(vec2 xi, vec3 N, float rough){\n"
"    float a = rough*rough; float phi = 2.0*PI*xi.x;\n"
"    float ct = sqrt((1.0-xi.y)/(1.0+(a*a-1.0)*xi.y)); float st = sqrt(1.0-ct*ct);\n"
"    vec3 H = vec3(cos(phi)*st, sin(phi)*st, ct);\n"
"    vec3 up = abs(N.z)<0.999 ? vec3(0,0,1) : vec3(1,0,0);\n"
"    vec3 tx = normalize(cross(up,N)); vec3 ty = cross(N,tx);\n"
"    return normalize(tx*H.x + ty*H.y + N*H.z);\n"
"}\n"
"float geomSchlick(float ndv, float k){ return ndv/(ndv*(1.0-k)+k); }\n"
"float geomSmith(vec3 N, vec3 V, vec3 L, float rough){\n"
"    float k = rough*rough/2.0;\n"
"    return geomSchlick(max(dot(N,V),0.0),k) * geomSchlick(max(dot(N,L),0.0),k);\n"
"}\n"
"void main(){\n"
"    float NdotV = max(vUV.x, 0.01); float rough = vUV.y;\n"
"    vec3 V = vec3(sqrt(1.0-NdotV*NdotV), 0.0, NdotV);\n"
"    vec3 N = vec3(0,0,1);\n"
"    float A=0.0, B=0.0; const uint SAMPLES=256u;\n"
"    for (uint i=0u;i<SAMPLES;i++){\n"
"        vec2 xi = hammersley(i, SAMPLES);\n"
"        vec3 H = importanceGGX(xi, N, rough);\n"
"        vec3 L = normalize(2.0*dot(V,H)*H - V);\n"
"        float ndl=max(L.z,0.0), ndh=max(H.z,0.0), vdh=max(dot(V,H),0.0);\n"
"        if (ndl>0.0){\n"
"            float G = geomSmith(N,V,L,rough);\n"
"            float Gv = G*vdh/(ndh*NdotV);\n"
"            float Fc = pow(1.0-vdh,5.0);\n"
"            A += (1.0-Fc)*Gv; B += Fc*Gv;\n"
"        }\n"
"    }\n"
"    FragColor = vec2(A,B)/float(SAMPLES);\n"
"}\n";

static const char* GBUF_SKIN_VERT = "#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
"layout(location=3) in vec4 aTangent;\n"
"layout(location=4) in vec4 aColor;\n"
"layout(location=5) in vec4 aJoints;\n"   /* bone indices (as floats) */
"layout(location=6) in vec4 aWeights;\n"  /* bone weights */
"uniform mat4 uMVP;\n"
"uniform mat4 uModel;\n"
"uniform mat3 uNormalMat;\n"
"layout(std140) uniform BoneBlock { mat4 uBones[256]; };\n"
"out vec3 vFragPos;\n"
"out vec3 vNormal;\n"
"out vec2 vUV;\n"
"out mat3 vTBN;\n"
"out vec4 vColor;\n"
"void main() {\n"
"    mat4 skin = uBones[int(aJoints.x)] * aWeights.x\n"
"              + uBones[int(aJoints.y)] * aWeights.y\n"
"              + uBones[int(aJoints.z)] * aWeights.z\n"
"              + uBones[int(aJoints.w)] * aWeights.w;\n"
"    float wsum = aWeights.x+aWeights.y+aWeights.z+aWeights.w;\n"
"    if (wsum < 0.001) skin = mat4(1.0);\n"   /* unweighted → identity */
"    vec4 skinnedPos = skin * vec4(aPos, 1.0);\n"
"    mat3 skin3 = mat3(skin);\n"
"    vec4 world = uModel * skinnedPos;\n"
"    vFragPos = world.xyz;\n"
"    vNormal  = normalize(uNormalMat * (skin3 * aNormal));\n"
"    vUV = aUV;\n"
"    vColor = aColor;\n"
"    vec3 T = normalize(uNormalMat * (skin3 * aTangent.xyz));\n"
"    vec3 N = vNormal;\n"
"    T = normalize(T - dot(T,N)*N);\n"
"    vec3 B = cross(N, T) * aTangent.w;\n"
"    vTBN = mat3(T, B, N);\n"
"    gl_Position = uMVP * skinnedPos;\n"
"}\n";

static const char* GBUF_FRAG = "#version 330 core\n"
"layout(location=0) out vec4 gAlbedoRough;\n"
"layout(location=1) out vec4 gNormalMetal;\n"
"layout(location=2) out vec4 gEmissiveAO;\n"
"in vec3 vFragPos; in vec3 vNormal; in vec2 vUV; in mat3 vTBN; in vec4 vColor;\n"
"uniform sampler2D uAlbedo;\n"
"uniform sampler2D uNormalMap;\n"
"uniform sampler2D uRoughMetal;\n"
"uniform sampler2D uEmissive;\n"
"uniform sampler2D uAO;\n"
"uniform vec4  uBaseColor;\n"
"uniform float uRoughness;\n"
"uniform float uMetallic;\n"
"uniform vec3  uEmissiveFactor;\n"
"uniform int   uHasNormalMap;\n"
"uniform sampler2D uDetailNormal;\n"
"uniform int   uHasDetailNormal;\n"
"uniform float uDetailScale;\n"
"uniform float uDetailStrength;\n"
"uniform int   uHasAOMap;\n"
"uniform vec4  uTint;\n"
"uniform int   uUnlit;\n"
"uniform int   uShadingModel;\n"
"uniform int   uToonBands;\n"
"void main() {\n"
"    vec4 albedo = texture(uAlbedo, vUV) * uBaseColor * vColor * uTint;\n"
"    if (albedo.a < 0.01) discard;\n"
"    vec2 rm = texture(uRoughMetal, vUV).rg;\n"
"    float rough = rm.r * uRoughness;\n"
"    float metal = rm.g * uMetallic;\n"
"    vec3 N = vNormal;\n"
"    vec3 tsN = vec3(0.0,0.0,1.0);\n"
"    if (uHasNormalMap > 0) tsN = texture(uNormalMap, vUV).rgb * 2.0 - 1.0;\n"
"    if (uHasDetailNormal > 0) {\n"
"        vec3 dN = texture(uDetailNormal, vUV * uDetailScale).rgb * 2.0 - 1.0;\n"
"        /* perturb base normal by the detail's tangent-space slope, scaled down\n"
"           so micro-detail adds relief without biasing the overall N·L (which\n"
"           would darken curved surfaces). */\n"
"        tsN = normalize(vec3(tsN.xy + dN.xy * uDetailStrength * 0.3, tsN.z));\n"
"    }\n"
"    if (uHasNormalMap > 0 || uHasDetailNormal > 0) N = normalize(vTBN * tsN);\n"
"    vec3 emissive = texture(uEmissive, vUV).rgb * uEmissiveFactor;\n"
"    float ao = (uHasAOMap > 0) ? texture(uAO, vUV).r : 1.0;\n"
"    if (uUnlit > 0) { gAlbedoRough=vec4(0.0,0.0,0.0,1.0); gNormalMetal=vec4(N*0.5+0.5,0.0); gEmissiveAO=vec4(albedo.rgb+emissive,1.0); return; }\n""    gAlbedoRough  = vec4(albedo.rgb, rough);\n"
"    gNormalMetal  = vec4(N * 0.5 + 0.5, metal);\n"
/* pack shading model + toon bands + AO into emissive alpha: integer part =
   model*10 + bands(0..9), fractional part = ao. PBR(0)+bands(0) → 0, so legacy
   materials decode to model 0 with their original AO unchanged. */
"    float styleCode = float(uShadingModel*10 + clamp(uToonBands,0,9));\n"
"    gEmissiveAO   = vec4(emissive, styleCode + clamp(ao,0.0,0.999));\n"
"}\n";

/* ── Deferred lighting (PBR) ──────────────────────────────────────────── */
static const char* LIGHT_VERT = "#version 330 core\n"
"layout(location=0) in vec2 aPos;\n"
"out vec2 vUV;\n"
"void main() { vUV = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";

static const char* LIGHT_FRAG = "#version 330 core\n"
"out vec4 FragColor;\n"
"uniform vec4 uAmbient;\n"
"in vec2 vUV;\n"
"uniform sampler2D gAlbedoRough;\n"
"uniform sampler2D gNormalMetal;\n"
"uniform sampler2D gEmissiveAO;\n"
"uniform sampler2D gDepth;\n"
"uniform mat4 uInvVP;\n"
"uniform vec3 uCamPos;\n"
"uniform sampler2D uShadowMap;\n"
"uniform mat4 uShadowVP;\n"
"uniform int  uShadowOn;\n"
"uniform int  uShadowLight;\n"
"uniform float uShadowBias;\n"
"uniform float uShadowTexel;\n"   /* 1/shadow_size for PCF */
"uniform sampler2D uSSAO;\n"
"uniform int uSSAOOn;\n"
"uniform float uSSAOIntensity;\n"
"uniform int  uSkyOn;\n"
"uniform vec3 uSkyZenith;\n"
"uniform vec3 uSkyHorizon;\n"
"uniform vec3 uSkyGround;\n"
"uniform float uSkyIntensity;\n"
"uniform samplerCube uIrradiance;\n"
"uniform samplerCube uPrefilter;\n"
"uniform sampler2D  uBrdfLUT;\n"
"uniform int uIblReady;\n"
"uniform int uDebugView;\n"
"uniform int uFog;\n"
"uniform float uFogDensity;\n"
"uniform vec3 uFogColor;\n"
"uniform float uFogHeight;\n"
"struct Light { vec4 pos; vec4 dir; vec4 color; vec4 params; };\n"
"layout(std140) uniform LightBlock { Light lights[64]; int nLights; };\n"
"const float PI = 3.14159265;\n"
"vec3 reconstruct_pos(vec2 uv, float depth) {\n"
"    vec4 clip = vec4(uv*2.0-1.0, depth*2.0-1.0, 1.0);\n"
"    vec4 world = uInvVP * clip;\n"
"    if (abs(world.w) < 1e-6) return vec3(0.0);\n"
"    return world.xyz / world.w;\n"
"}\n"
"/* Procedural sky: three-stop gradient (ground .. horizon .. zenith) by view-ray\n"
"   elevation, with a soft atmospheric horizon glow. Smoothstep blends give a\n"
"   continuous derivative through y=0 so there is no hard seam at the horizon,\n"
"   and an exponential glow band brightens the transition like real haze. */\n"
"vec3 sky_color(vec3 dir) {\n"
"    float y = clamp(dir.y, -1.0, 1.0);\n"
"    // Upper hemisphere: horizon -> zenith over a wide, eased band.\n"
"    float up = smoothstep(0.0, 0.55, y);\n"
"    vec3 above = mix(uSkyHorizon, uSkyZenith, up);\n"
"    // Lower hemisphere: horizon -> ground, eased so it fades rather than steps.\n"
"    float dn = smoothstep(0.0, 0.6, -y);\n"
"    vec3 below = mix(uSkyHorizon, uSkyGround, dn);\n"
"    vec3 base = (y >= 0.0) ? above : below;\n"
"    // Horizon glow: a soft additive band peaking at y=0, decaying both ways.\n"
"    float glow = exp(-abs(y) * 6.0);\n"
"    base += uSkyHorizon * glow * 0.15;\n"
"    return base * uSkyIntensity;\n"
"}\n"
"float DistGGX(float NdotH, float r) {\n"
"    /* clamp roughness away from 0: at tiny r the peak a2/(PI*a2*a2)=1/(PI*a2)\n"
"       explodes on the single pixel where NdotH->1, producing specular\n"
"       'firefly' speckles. A 0.05 floor caps the peak at a sane value. */\n"
"    r = max(r, 0.045);\n"
"    float a = r*r, a2 = a*a;\n"
"    float d = (NdotH*NdotH*(a2-1.0)+1.0);\n"
"    return a2 / (PI*d*d);\n"
"}\n"
"float GeomSmith(float NdotV, float NdotL, float r) {\n"
"    float k = (r+1.0)*(r+1.0)/8.0;\n"
"    float g1 = NdotV/(NdotV*(1.0-k)+k);\n"
"    float g2 = NdotL/(NdotL*(1.0-k)+k);\n"
"    return g1*g2;\n"
"}\n"
"vec3 FresnelShlick(float cosT, vec3 F0) {\n"
"    return F0 + (1.0-F0)*pow(1.0-cosT,5.0);\n"
"}\n"
"vec3 pbr(vec3 albedo, float rough, float metal, vec3 N, vec3 V,\n"
"         vec3 L, vec3 lc, float att) {\n"
"    vec3 H = normalize(V+L);\n"
"    float NdotL = max(dot(N,L),0.0);\n"
"    float NdotV = max(dot(N,V),0.0);\n"
"    float NdotH = max(dot(N,H),0.0);\n"
"    vec3 F0 = mix(vec3(0.04), albedo, metal);\n"
"    float D = DistGGX(NdotH, rough);\n"
"    float G = GeomSmith(NdotV, NdotL, rough);\n"
"    vec3 F  = FresnelShlick(max(dot(H,V),0.0), F0);\n"
"    vec3 spec = D*G*F / max(4.0*NdotV*NdotL, 0.001);\n"
"    vec3 kd = (1.0-F)*(1.0-metal);\n"
"    return (kd*albedo/PI + spec) * lc * att * NdotL;\n"
"}\n"
/* ── Non-realistic shading: toon/flat/rim, sharing PBR's G-buffer inputs ──
   model 1 TOON: diffuse quantized into `bands` steps + optional stepped spec.
   model 2 FLAT: single hard lambert step, no specular (matte stylized).
   model 3 RIM : soft lambert + strong fresnel rim accent.
   All are driven by the same lights so realistic + stylized objects coexist. */
"uniform float uToonSpecular;\n"
"uniform float uRimStrength;\n"
"uniform float uRimPower;\n"
"uniform vec3  uRimColor;\n"
"vec3 stylized(int model, int bands, vec3 albedo, float rough, float metal,\n"
"              vec3 N, vec3 V, vec3 L, vec3 lc, float att) {\n"
"    float NdotL = max(dot(N,L), 0.0);\n"
"    vec3 base = albedo * lc * att;\n"
"    if (model == 2) {\n"                       /* FLAT: one hard step */
"        float s = step(0.5, NdotL);\n"
"        return base * s;\n"
"    }\n"
"    if (model == 3) {\n"                       /* RIM: smooth lambert + fresnel */
"        float rim = pow(1.0 - max(dot(N,V),0.0), (uRimPower>0.0?uRimPower:3.0));\n"
"        vec3 rc = (uRimColor.r+uRimColor.g+uRimColor.b > 0.0) ? uRimColor : lc;\n"
"        vec3 diff = base * NdotL;\n"
"        return diff + rc * rim * uRimStrength * att;\n"
"    }\n"
"    /* TOON (model 1): quantize the diffuse term into bands. */\n"
"    float q = floor(NdotL * float(bands)) / float(bands);\n"
"    q = clamp(q + 0.05, 0.0, 1.0);\n"
"    vec3 col = base * q;\n"
"    if (uToonSpecular > 0.0) {\n"              /* stepped Blinn highlight */
"        vec3 H = normalize(V + L);\n"
"        float spec = pow(max(dot(N,H),0.0), mix(64.0, 8.0, rough));\n"
"        float ss = step(0.5, spec);\n"
"        col += lc * ss * uToonSpecular * att;\n"
"    }\n"
"    return col;\n"
"}\n"
"/* Percentage-Closer Soft Shadows (PCSS): contact-hardening soft shadows. Three\n"
"   stages: (1) blocker search averages occluder depth near the sample, (2) the\n"
"   penumbra width follows the blocker/receiver ratio (sharp at contact, soft far\n"
"   away), (3) a variable-radius Poisson PCF filters at that width. Biggest single\n"
"   shadow-realism cue vs fixed-width PCF. */\n"
"uniform float uShadowSoftness;\n"
"const vec2 PZN[16] = vec2[](\n"
"  vec2(-0.94,-0.34),vec2(0.95,-0.06),vec2(-0.09,-0.93),vec2(0.34,0.29),\n"
"  vec2(-0.72,0.53),vec2(0.51,0.79),vec2(-0.30,0.30),vec2(0.78,-0.55),\n"
"  vec2(0.20,-0.40),vec2(-0.44,-0.72),vec2(0.15,0.66),vec2(-0.66,-0.03),\n"
"  vec2(0.44,0.05),vec2(-0.16,0.87),vec2(0.66,0.40),vec2(-0.55,0.79));\n"
"float shadow_factor(vec3 P, float NdotL) {\n"
"    if (uShadowOn == 0) return 1.0;\n"
"    vec4 lp = uShadowVP * vec4(P, 1.0);\n"
"    vec3 proj = lp.xyz / lp.w;\n"
"    proj = proj * 0.5 + 0.5;\n"
"    if (proj.z > 1.0) return 1.0;\n"
"    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;\n"
"    float bias = max(uShadowBias * (1.0 - NdotL), uShadowBias * 0.3);\n"
"    float zRecv = proj.z - bias;\n"
"    float lightSize = (uShadowSoftness > 0.0) ? uShadowSoftness : 3.0;\n"
"    float srchR = lightSize * uShadowTexel * 4.0;\n"
"    float blockerSum = 0.0; int blockerCount = 0;\n"
"    for (int i=0;i<16;i++) {\n"
"        float d = texture(uShadowMap, proj.xy + PZN[i]*srchR).r;\n"
"        if (d < zRecv) { blockerSum += d; blockerCount++; }\n"
"    }\n"
"    if (blockerCount == 0) return 1.0;\n"
"    float avgBlocker = blockerSum / float(blockerCount);\n"
"    float penumbra = (zRecv - avgBlocker) / max(avgBlocker, 1e-4);\n"
"    float filterR = clamp(penumbra * lightSize, 0.5, 12.0) * uShadowTexel;\n"
"    float sum = 0.0;\n"
"    for (int i=0;i<16;i++) {\n"
"        float d = texture(uShadowMap, proj.xy + PZN[i]*filterR).r;\n"
"        sum += (zRecv > d) ? 0.0 : 1.0;\n"
"    }\n"
"    return sum / 16.0;\n"
"}\n"
"void main() {\n"
"    float depth = texture(gDepth, vUV).r;\n"
"    vec4 nm_check = texture(gNormalMetal, vUV);\n"
"    if (nm_check.rgb == vec3(0.0)) {\n"
"        if (uSkyOn > 0) {\n"
"            vec3 far = reconstruct_pos(vUV, 1.0);\n"
"            vec3 rd  = normalize(far - uCamPos);\n"
"            FragColor = vec4(sky_color(rd), 1.0);\n"
"        } else {\n"
"            FragColor = vec4(0.0,0.0,0.0,1.0);\n"
"        }\n"
"        return;\n"
"    }\n"
"    vec4 ar = texture(gAlbedoRough, vUV);\n"
"    vec4 nm = texture(gNormalMetal, vUV);\n"
"    vec4 ea = texture(gEmissiveAO, vUV);\n"
"    vec3 albedo = ar.rgb; float rough = max(ar.a, 0.04);\n"
"    vec3 N = normalize(nm.rgb * 2.0 - 1.0); float metal = nm.a;\n"
"    vec3 emissive = ea.rgb;\n"
"    float styleCode = floor(ea.a);\n"                         /* model*10 + bands */
"    float ao = ea.a - styleCode;\n"                           /* fractional part */
"    int shadeModel = int(floor(styleCode/10.0 + 0.5));\n"
"    int toonBands = int(styleCode - float(shadeModel*10) + 0.5);\n"
"    if (toonBands < 2) toonBands = 4;\n"
"    if (uSSAOOn > 0) {\n"
"        float vis = texture(uSSAO, vUV).r;\n"
"        vis = pow(clamp(vis, 0.0, 1.0), uSSAOIntensity);\n"
"        ao *= vis;\n"
"    }\n"
"    vec3 P = reconstruct_pos(vUV, depth);\n"
"    vec3 V = normalize(uCamPos - P);\n"
"    vec3 Lo = uAmbient.rgb * uAmbient.a * albedo * ao + emissive;\n"
"    if (uSkyOn > 0 && shadeModel == 0) {\n"
"        vec3 F0 = mix(vec3(0.04), albedo, metal);\n"
"        vec3 Fr = F0 + (max(vec3(1.0-rough), F0) - F0) * pow(1.0 - max(dot(N,V),0.0), 5.0);\n"
"        if (uIblReady > 0) {\n"
"            // Real split-sum IBL from convolved cubemaps.\n"
"            vec3 irradiance = texture(uIrradiance, N).rgb;\n"
"            vec3 kd = (1.0 - Fr) * (1.0 - metal);\n"
"            vec3 diffuse = irradiance * albedo * kd;\n"
"            vec3 R = reflect(-V, N);\n"
"            float MAX_LOD = 4.0;\n"
"            vec3 prefiltered = textureLod(uPrefilter, R, rough * MAX_LOD).rgb;\n"
"            vec2 brdf = texture(uBrdfLUT, vec2(max(dot(N,V),0.0), rough)).rg;\n"
"            vec3 specular = prefiltered * (Fr * brdf.x + brdf.y);\n"
"            Lo += (diffuse + specular) * ao;\n"
"        } else {\n"
"            // Fallback: procedural sky IBL (used before the cubemaps bake).\n"
"            vec3 irr = sky_color(N) * ao;\n"
"            Lo += irr * albedo * (1.0 - metal) * 0.6;\n"
"            vec3 R = reflect(-V, N);\n"
"            vec3 envspec = mix(sky_color(R), sky_color(N), rough);\n"
"            Lo += envspec * Fr * ao;\n"
"        }\n"
"    }\n"
"    for (int i = 0; i < nLights; i++) {\n"
"        Light li = lights[i];\n"
"        int type = int(li.params.x);\n"
"        vec3 lc = li.color.rgb * li.color.a;\n"
"        float att = 1.0;\n"
"        vec3 L;\n"
"        float shadow = 1.0;\n"
"        if (type == 0) { L = normalize(-li.dir.xyz); if (uShadowOn>0 && uShadowLight==i) shadow = shadow_factor(P, max(dot(N,L),0.0)); }\n"
"        else {\n"
"            L = li.pos.xyz - P;\n"
"            float dist = length(L); L = (dist > 1e-5) ? L/dist : vec3(0.0,1.0,0.0);\n"
"            float range = max(li.params.y, 1e-3);\n"   /* guard: range 0 → div-by-zero → NaN → black square (BUG-002) */
"            att = clamp(1.0 - dist/range, 0.0, 1.0);\n"
"            att *= att;\n"
"            if (type == 2) {\n"
"                float theta = dot(L, normalize(-li.dir.xyz));\n"
"                float inner = li.params.z, outer = li.params.w;\n"
"                att *= clamp((theta-outer)/(inner-outer), 0.0, 1.0);\n"
"                if (uShadowOn>0 && uShadowLight==i) shadow = shadow_factor(P, max(dot(N,L),0.0));\n"
"            }\n"
"        }\n"
"        if (shadeModel == 0) Lo += pbr(albedo, rough, metal, N, V, L, lc, att) * shadow;\n"
"        else Lo += stylized(shadeModel, toonBands, albedo, rough, metal, N, V, L, lc, att) * shadow;\n"
"    }\n"
"    if (uFog > 0) {\n""        float dist = length(P - uCamPos);\n""        float f = 1.0 - exp(-uFogDensity * dist);\n""        if (uFogHeight > 0.0) { float hf = clamp((uFogHeight - P.y)/uFogHeight, 0.0, 1.0); f *= hf; }\n""        Lo = mix(Lo, uFogColor, clamp(f,0.0,1.0));\n""    }\n""    if (uDebugView==2) { FragColor=vec4(N*0.5+0.5,1.0); return; }\n"
"    if (uDebugView==3) { float dz=pow(depth,0.35); FragColor=vec4(dz,dz,dz,1.0); return; }\n"
"    if (uDebugView==4) { FragColor=vec4(albedo,1.0); return; }\n"
"    if (uDebugView==6) { vec3 lit=Lo-(uAmbient.rgb*uAmbient.a*albedo*ao+emissive); FragColor=vec4(lit,1.0); return; }\n"
"    FragColor = vec4(Lo, 1.0);\n"
"}\n";

/* ── Fullscreen blit / post-FX ────────────────────────────────────────── */
static const char* BLIT_VERT = "#version 330 core\n"
"layout(location=0) in vec2 aPos;\n"
"out vec2 vUV;\n"
"void main() { vUV = aPos*0.5+0.5; gl_Position = vec4(aPos,0,1); }\n";

static const char* POSTFX_FRAG = "#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 vUV;\n"
"uniform sampler2D uHDR;\n"
"uniform sampler2D uBloom;\n"
"uniform float uBloomIntensity;\n"
"uniform float uExposure;\n"
"uniform float uGamma;\n"
"uniform float uSaturation;\n"
"uniform float uContrast;\n"
"uniform float uVignette;\n"
"uniform float uCAStrength;\n"
"uniform int uFXAA;\n"
"uniform vec2 uTexelSize;\n"
"uniform float uGrain;\n"
"uniform float uScanline;\n"
"uniform float uTime;\n"
"uniform sampler2D uOutlineDepth;\n"
"uniform sampler2D uOutlineNormal;\n"
"uniform int   uOutlineOn;\n"
"uniform vec3  uOutlineColor;\n"
"uniform float uOutlineThickness;\n"
"uniform float uOutlineDepthSens;\n"
"uniform float uOutlineNormalSens;\n"
"float ol_linearize(float d){\n"
"    float z = d*2.0-1.0;\n"
"    return 1.0 / max(1.0 - z, 1e-4);\n"   /* monotonic remap; relative edges only */
"}\n"
"float edge_factor(vec2 uv, vec2 ts){\n"
"    vec2 o = ts * uOutlineThickness;\n"
"    float dc = ol_linearize(texture(uOutlineDepth, uv).r);\n"
"    if (texture(uOutlineDepth, uv).r >= 1.0) return 0.0;\n"  /* skip background */
"    float dl = ol_linearize(texture(uOutlineDepth, uv - vec2(o.x,0)).r);\n"
"    float dr = ol_linearize(texture(uOutlineDepth, uv + vec2(o.x,0)).r);\n"
"    float du = ol_linearize(texture(uOutlineDepth, uv - vec2(0,o.y)).r);\n"
"    float dd = ol_linearize(texture(uOutlineDepth, uv + vec2(0,o.y)).r);\n"
"    float dEdge = (abs(dc-dl)+abs(dc-dr)+abs(dc-du)+abs(dc-dd)) / max(dc,1e-3) * uOutlineDepthSens * 4.0;\n"
"    vec3 nc = texture(uOutlineNormal, uv).rgb*2.0-1.0;\n"
"    vec3 nl = texture(uOutlineNormal, uv - vec2(o.x,0)).rgb*2.0-1.0;\n"
"    vec3 nr = texture(uOutlineNormal, uv + vec2(o.x,0)).rgb*2.0-1.0;\n"
"    vec3 nu = texture(uOutlineNormal, uv - vec2(0,o.y)).rgb*2.0-1.0;\n"
"    vec3 nd = texture(uOutlineNormal, uv + vec2(0,o.y)).rgb*2.0-1.0;\n"
"    float nEdge = ((1.0-dot(nc,nl))+(1.0-dot(nc,nr))+(1.0-dot(nc,nu))+(1.0-dot(nc,nd))) * uOutlineNormalSens;\n"
"    return clamp(max(dEdge, nEdge), 0.0, 1.0);\n"
"}\n"
"vec3 aces_tonemap(vec3 x) {\n"
"    float a=2.51,b=.03,c=2.43,d=.59,e=.14;\n"
"    return clamp((x*(a*x+b))/(x*(c*x+d)+e),0.0,1.0);\n"
"}\n"
"vec3 fxaa(sampler2D tex, vec2 uv, vec2 ts) {\n"
"    /* Edge DETECTION in a perceptual proxy space (Reinhard luma x/(1+x)) so\n"
"       bright HDR silhouettes (metal vs sky) don't confuse the detector; the\n"
"       BLEND still returns linear HDR so the main pass tonemap/bloom/gamma\n"
"       pipeline is unchanged. */\n"
"    #define PL(O) ( dot(texture(tex,(O)).rgb,vec3(0.299,0.587,0.114)) )\n"
"    #define TL(O) ( PL(O)/(1.0+PL(O)) )\n"
"    float lNW=TL(uv+vec2(-ts.x,-ts.y)), lNE=TL(uv+vec2(ts.x,-ts.y));\n"
"    float lSW=TL(uv+vec2(-ts.x,ts.y)),  lSE=TL(uv+vec2(ts.x,ts.y));\n"
"    float lM =TL(uv);\n"
"    float lMin=min(lM,min(min(lNW,lNE),min(lSW,lSE)));\n"
"    float lMax=max(lM,max(max(lNW,lNE),max(lSW,lSE)));\n"
"    if (lMax-lMin < max(0.0312, lMax*0.125)) return texture(tex,uv).rgb;\n"
"    vec2 dir; dir.x=-((lNW+lNE)-(lSW+lSE)); dir.y=((lNW+lSW)-(lNE+lSE));\n"
"    float dr=max((lNW+lNE+lSW+lSE)*0.25*0.125, 1.0/128.0);\n"
"    float rcp=1.0/(min(abs(dir.x),abs(dir.y))+dr);\n"
"    dir=clamp(dir*rcp,-8.0,8.0)*ts;\n"
"    vec3 rA=0.5*(texture(tex,uv+dir*(1.0/3.0-0.5)).rgb + texture(tex,uv+dir*(2.0/3.0-0.5)).rgb);\n"
"    vec3 rB=rA*0.5 + 0.25*(texture(tex,uv+dir*(-0.5)).rgb + texture(tex,uv+dir*(0.5)).rgb);\n"
"    float lrB=dot(rB,vec3(0.299,0.587,0.114)); float tlB=lrB/(1.0+lrB);\n"
"    return (tlB<lMin||tlB>lMax) ? rA : rB;\n"
"    #undef TL\n"
"    #undef PL\n"
"}\n"
"void main() {\n"
"    vec3 col;\n"
"    if (uFXAA > 0) col = fxaa(uHDR, vUV, uTexelSize);\n"
"    else           col = texture(uHDR, vUV).rgb;\n"
"    col += texture(uBloom, vUV).rgb * uBloomIntensity;\n"
"    col *= uExposure;\n"
"    col = aces_tonemap(col);\n"
"    float lum = dot(col, vec3(.299,.587,.114));\n"
"    col = mix(vec3(lum), col, uSaturation);\n"
"    col = (col - 0.5) * uContrast + 0.5;\n"
"    float vg = length(vUV - 0.5) * uVignette;\n"
"    col *= 1.0 - vg*vg;\n"
"    if (uCAStrength > 0.0) {\n"
"        float ca = uCAStrength;\n"
"        vec2 d = (vUV - 0.5) * ca;\n"
"        col.r = texture(uHDR, vUV + d).r;\n"
"        col.b = texture(uHDR, vUV - d).b;\n"
"    }\n"
"    if (uGrain > 0.0) {\n""        float n = fract(sin(dot(vUV*vec2(uTime+1.0,uTime+1.3), vec2(12.9898,78.233))) * 43758.5453);\n""        col += (n - 0.5) * uGrain;\n""    }\n""    if (uScanline > 0.0) {\n""        float s = sin(vUV.y * uTexelSize.y * 0.0 + gl_FragCoord.y * 3.14159);\n""        col *= 1.0 - uScanline * (0.5 - 0.5*s);\n""    }\n""    col = pow(max(col,0.0), vec3(1.0/uGamma));\n"
"    if (uOutlineOn > 0) {\n"
"        float e = edge_factor(vUV, uTexelSize);\n"
"        e = smoothstep(0.25, 0.6, e);\n"       /* threshold to crisp lines */
"        col = mix(col, uOutlineColor, e);\n"
"    }\n"
"    FragColor = vec4(col, 1.0);\n"
"}\n";

/* ── Bloom downsample/upsample ────────────────────────────────────────────
 * Physically-based bloom after Jimenez (SIGGRAPH 2014 "Next Generation Post
 * Processing in Call of Duty: Advanced Warfare"). The mip chain is built with
 * a 13-tap weighted downsample that suppresses fireflies, and combined back
 * with a 3x3 tent-filter progressive upsample for a smooth, wide, aliasing-
 * free glow. The energy threshold uses a soft knee applied ONCE at the first
 * downsample (uPrefilter=1) rather than a hard cutoff re-applied per level. */
static const char* BLOOM_DOWN_FRAG = "#version 330 core\n"
"out vec3 FragColor; in vec2 vUV;\n"
"uniform sampler2D uSrc; uniform vec2 uTexel;\n"
"uniform int   uPrefilter;      // 1 only on the first (level-0) downsample\n"
"uniform float uThreshold;      // knee center (luma)\n"
"uniform float uSoftKnee;       // knee width fraction (0..1)\n"
"vec3 prefilter(vec3 c) {\n"
"    float br = max(c.r, max(c.g, c.b));\n"
"    float knee = uThreshold * uSoftKnee + 1e-5;\n"
"    float soft = clamp(br - uThreshold + knee, 0.0, 2.0*knee);\n"
"    soft = soft*soft / (4.0*knee + 1e-5);\n"
"    float contrib = max(soft, br - uThreshold) / max(br, 1e-5);\n"
"    return c * contrib;\n"
"}\n"
"void main() {\n"
"    vec2 t = uTexel;\n"
"    // 13-tap 'partial Karis average' box downsample.\n"
"    vec3 a = texture(uSrc, vUV + t*vec2(-2,-2)).rgb;\n"
"    vec3 b = texture(uSrc, vUV + t*vec2( 0,-2)).rgb;\n"
"    vec3 c = texture(uSrc, vUV + t*vec2( 2,-2)).rgb;\n"
"    vec3 d = texture(uSrc, vUV + t*vec2(-2, 0)).rgb;\n"
"    vec3 e = texture(uSrc, vUV + t*vec2( 0, 0)).rgb;\n"
"    vec3 f = texture(uSrc, vUV + t*vec2( 2, 0)).rgb;\n"
"    vec3 g = texture(uSrc, vUV + t*vec2(-2, 2)).rgb;\n"
"    vec3 h = texture(uSrc, vUV + t*vec2( 0, 2)).rgb;\n"
"    vec3 i = texture(uSrc, vUV + t*vec2( 2, 2)).rgb;\n"
"    vec3 j = texture(uSrc, vUV + t*vec2(-1,-1)).rgb;\n"
"    vec3 k = texture(uSrc, vUV + t*vec2( 1,-1)).rgb;\n"
"    vec3 l = texture(uSrc, vUV + t*vec2(-1, 1)).rgb;\n"
"    vec3 m = texture(uSrc, vUV + t*vec2( 1, 1)).rgb;\n"
"    // weighted sum: center block + 4 corner blocks, weights sum to 1.\n"
"    vec3 col = e*0.125;\n"
"    col += (a+c+g+i)*0.03125;\n"
"    col += (b+d+f+h)*0.0625;\n"
"    col += (j+k+l+m)*0.125;\n"
"    if (uPrefilter > 0) col = prefilter(col);\n"
"    FragColor = col;\n"
"}\n";

static const char* BLOOM_UP_FRAG = "#version 330 core\n"
"out vec3 FragColor; in vec2 vUV;\n"
"uniform sampler2D uSrc; uniform vec2 uTexel; uniform float uIntensity;\n"
"void main() {\n"
"    // 3x3 tent filter (Jimenez). uTexel is the radius in the SOURCE mip.\n"
"    vec2 t = uTexel;\n"
"    vec3 s = texture(uSrc, vUV + t*vec2(-1,-1)).rgb * 1.0;\n"
"    s += texture(uSrc, vUV + t*vec2( 0,-1)).rgb * 2.0;\n"
"    s += texture(uSrc, vUV + t*vec2( 1,-1)).rgb * 1.0;\n"
"    s += texture(uSrc, vUV + t*vec2(-1, 0)).rgb * 2.0;\n"
"    s += texture(uSrc, vUV + t*vec2( 0, 0)).rgb * 4.0;\n"
"    s += texture(uSrc, vUV + t*vec2( 1, 0)).rgb * 2.0;\n"
"    s += texture(uSrc, vUV + t*vec2(-1, 1)).rgb * 1.0;\n"
"    s += texture(uSrc, vUV + t*vec2( 0, 1)).rgb * 2.0;\n"
"    s += texture(uSrc, vUV + t*vec2( 1, 1)).rgb * 1.0;\n"
"    FragColor = (s / 16.0) * uIntensity;\n"
"}\n";

/* ── 2D sprite / shape ────────────────────────────────────────────────── */
static const char* SPRITE_VERT = "#version 330 core\n"
"layout(location=0) in vec2 aPos;\n"
"layout(location=1) in vec2 aUV;\n"
"layout(location=2) in vec4 aColor;\n"
"uniform vec2 uResolution;\n"
"out vec2 vUV; out vec4 vColor;\n"
"void main() {\n"
"    vUV = aUV; vColor = aColor;\n"
"    vec2 clip = (aPos / uResolution) * 2.0 - 1.0;\n"
"    gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0);\n"
"}\n";

static const char* SPRITE_FRAG = "#version 330 core\n"
"out vec4 FragColor; in vec2 vUV; in vec4 vColor;\n"
"uniform sampler2D uTex; uniform int uUseTex; uniform int uFontMode;\n"
"void main() {\n"
"    if (uFontMode > 0) {\n"
"        float a = texture(uTex, vUV).r;\n"     /* R8 atlas holds coverage */
"        FragColor = vec4(vColor.rgb, vColor.a * a);\n"
"    } else if (uUseTex > 0) FragColor = texture(uTex, vUV) * vColor;\n"
"    else             FragColor = vColor;\n"
"    if (FragColor.a < 0.004) discard;\n"
"}\n";

/* ══════════════════════════════════════════════════════════════════════
   RENDERER STATE
   ══════════════════════════════════════════════════════════════════════ */

#define CC_MAX_TEXTURES   512
#define CC_MAX_MESHES     512
#define CC_MAX_MATERIALS  512
#define CC_MAX_SHADERS    128
#define CC_MAX_RTS        32
#define CC_MAX_LIGHTS     64
#define CC_MAX_2D_VERTS   (1 << 16)
#define CC_BLOOM_LEVELS   5

typedef struct {
    GLuint id;
    uint32_t w, h;
    CCPixelFmt fmt;
    bool valid;
} GLTexEntry;

typedef struct {
    GLuint vao, vbo, ebo;
    GLuint skin_vbo;      /* parallel joint+weight buffer for skinned meshes (0 if none) */
    uint32_t index_count;
    bool valid;
} GLMeshEntry;

typedef struct {
    GLuint program;
    bool valid;
} GLShaderEntry;

typedef struct {
    CCMaterialDesc desc;
    bool valid;
} MaterialEntry;

typedef struct {
    GLuint fbo, color_tex, depth_tex;
    uint32_t w, h;
    bool valid;
} RTEntry;

/* 2D sprite vertex */
typedef struct { float x,y,u,v; uint8_t r,g,b,a; } SpriteVert;

/* Queued decal: baked model matrix (box→world) + its inverse (world→box, for
   projecting sampled world positions into the unit box) plus shading params. */
typedef struct CCDecalEntry {
    float    model[16];
    float    inv_model[16];
    float    color[4];
    float    emissive;
    float    angle_fade;
    CCTexture tex;
} CCDecalEntry;

struct CCRenderer {
    CCRendererBackend backend;
    uint32_t w, h;

    /* Context */
#ifdef CC_USE_GLFW
    GLFWwindow* window;
#endif
#ifdef CC_USE_OSMESA
    OSMesaContext osmesa_ctx;
    uint8_t*      osmesa_buf;
#endif

    /* Instancing: a single growable VBO of per-instance mat4 model matrices,
       reused across cc_draw_mesh_instanced calls. */
    GLuint   inst_vbo;
    uint32_t inst_cap;         /* capacity in matrices (16 floats each) */

    /* Billboards: a static unit-quad VBO + a growable per-instance VBO
       (center/size/color = 9 floats each) + dedicated VAO and shader. */
    GLuint   bb_shader;
    GLuint   bb_quad_vbo;
    GLuint   bb_vao;
    GLuint   bb_inst_vbo;
    uint32_t bb_inst_cap;      /* capacity in billboards */

    /* Decals: queued during the frame, flushed before lighting. A depth copy
       is needed because we sample scene depth while writing the G-buffer color
       targets (can't sample an FBO's own attached depth). */
    GLuint   decal_shader;
    GLuint   decal_box_vao, decal_box_vbo, decal_box_ebo;
    GLuint   decal_depth_copy;     /* depth texture snapshot for sampling */
    GLuint   decal_depth_fbo;      /* FBO to blit depth into the copy */
    struct CCDecalEntry* decal_queue;
    uint32_t decal_count, decal_cap;

    /* Directional shadow map: a depth-only render of the caster geometry from
       the sun's viewpoint. Sampled with PCF in the lighting pass. */
    GLuint   shadow_shader;
    GLuint   shadow_fbo;
    GLuint   shadow_tex;
    uint32_t shadow_size;      /* square resolution (0 = shadows off) */
    float    shadow_vp[16];    /* light-space view-projection */
    bool     shadow_active;    /* a directional caster was found this frame */
    int      shadow_light;     /* index of the casting light (-1 = none) */
    float    shadow_bias;
    float    shadow_softness;   /* PCSS light size in shadow-UV units (0=default 3.0) */
    float    max_anisotropy;    /* detected GL_MAX_TEXTURE_MAX_ANISOTROPY (1.0 = unsupported) */

    /* SSAO: occlusion + blur targets (single channel), a 4x4 rotation-noise
       texture, and a hemisphere sample kernel uploaded to the shader. */
    GLuint   ssao_shader, ssao_blur_shader;
    GLuint   ssao_fbo, ssao_tex;
    GLuint   ssao_blur_fbo, ssao_blur_tex;
    GLuint   ssao_noise_tex;
    float    ssao_kernel[32*3];

    /* SSR: reflection target (RGBA16F: premultiplied refl + weight) + a temp
       HDR target to composite into (can't read hdr_color_tex while writing it). */
    GLuint   ssr_shader, ssr_composite_shader;
    GLuint   ssr_fbo, ssr_tex;
    GLuint   ssr_composite_fbo, ssr_composite_tex;
    GLuint   ssgi_shader, ssgi_composite_shader;
    GLuint   ssgi_fbo, ssgi_tex;
    GLuint   ssgi_composite_fbo, ssgi_composite_tex;
    /* Temporal accumulation for SSGI: blend each frame's gather with history so
       the indirect bounce denoises and builds up over the multi-frame headless
       loop. accum is ping-ponged against history each frame. */
    GLuint   ssgi_accum_fbo, ssgi_accum_tex;      /* this frame's accumulated GI */
    GLuint   ssgi_ghist_fbo, ssgi_ghist_tex;      /* previous frame's accumulated GI */
    bool     ssgi_hist_valid;
    GLuint   ssgi_accum_shader;

    /* Image-based lighting: real cubemaps preprocessed from the environment.
       env = captured sky; irradiance = diffuse convolution; prefilter = specular
       roughness mips; brdf_lut = split-sum integration. Regenerated when the sky
       changes (ibl_dirty). */
    GLuint   ibl_capture_shader, ibl_irradiance_shader, ibl_prefilter_shader, ibl_brdf_shader;
    GLuint   ibl_cube_vao, ibl_cube_vbo;
    GLuint   ibl_env_cube;       /* raw environment */
    GLuint   ibl_irradiance_cube;
    GLuint   ibl_prefilter_cube; /* mip chain over roughness */
    GLuint   ibl_brdf_lut;
    GLuint   ibl_capture_fbo, ibl_capture_rbo;
    bool     ibl_ready;
    bool     ibl_dirty;

    /* G-buffer */
    /* Realism-axis stylized-shading knobs (last stylized material wins; applied
       globally in the deferred lighting pass to any non-PBR material). */
    float style_toon_specular, style_rim_strength, style_rim_power, style_rim_color[3];
    float ae_adapted_lum;   /* auto-exposure: current adapted scene luminance */
    GLuint gbuf_fbo;
    GLuint gbuf_albedo_rough;  /* RGBA: albedo.rgb + roughness */
    GLuint gbuf_normal_metal;  /* RGBA: normal.rgb + metallic */
    GLuint gbuf_emissive_ao;   /* RGBA: emissive.rgb + ao */
    GLuint gbuf_depth;

    /* HDR framebuffer */
    GLuint hdr_fbo;
    GLuint hdr_color_tex;

    /* Bloom */
    GLuint bloom_fbo[CC_BLOOM_LEVELS];
    GLuint bloom_tex[CC_BLOOM_LEVELS];

    /* Post output */
    GLuint post_fbo;
    GLuint post_tex;

    /* White 1x1 fallback texture */
    GLuint white_tex;
    GLuint black_tex;
    GLuint default_normal_tex;  /* flat blue */

    /* Shaders */
    GLuint sh_gbuf;
    GLuint sh_gbuf_skin;
    GLuint sh_gbuf_inst;      /* instanced gbuffer (per-instance model matrix) */
    GLuint bone_ubo;
    GLuint sh_light;
    GLuint sh_postfx;
    GLuint sh_bloom_down;
    GLuint sh_bloom_up;
    GLuint sh_sprite;

    /* Fullscreen quad */
    GLuint fsq_vao, fsq_vbo;

    /* Light UBO */
    GLuint light_ubo;

    /* 2D sprite batch */
    GLuint sprite_vao, sprite_vbo, sprite_ebo;
    SpriteVert* sprite_verts;
    uint32_t    sprite_nv;
    GLuint      sprite_current_tex;
    bool        sprite_use_tex;
    bool        sprite_font_mode;
    /* 2D command buffer — recorded during frame, executed after postfx */
    struct CC2DBatch { GLuint tex; bool use_tex; bool font_mode; uint32_t start; uint32_t count; } batches2d[4096];
    uint32_t    batch2d_count;

    /* Resource tables */
    GLTexEntry   textures[CC_MAX_TEXTURES];
    GLMeshEntry  meshes[CC_MAX_MESHES];
    GLShaderEntry shaders[CC_MAX_SHADERS];
    MaterialEntry materials[CC_MAX_MATERIALS];
    RTEntry       rts[CC_MAX_RTS];

    /* Lights */
    CCLight lights[CC_MAX_LIGHTS];
    bool    light_valid[CC_MAX_LIGHTS];
    int     num_lights;
    float   ambient_color[3];
    float   ambient_intensity;

    /* Sky / image-based lighting. When sky_enabled, background pixels get a
       procedural gradient (zenith→horizon→ground) and lit surfaces receive a
       hemispheric ambient term from the same colors, so the environment both
       shows and lights. sky_tex reserved for a future HDR-cubemap IBL path. */
    bool      sky_enabled;
    float     sky_zenith[3];
    float     sky_horizon[3];
    float     sky_ground[3];
    float     sky_intensity;
    CCTexture sky_tex;

    /* Camera */
    CCCameraDesc camera;
    float view_mat[16];
    float proj_mat[16];
    /* TAA (temporal anti-aliasing): sub-pixel camera jitter accumulated across
       frames into a history buffer for true supersampling. Also denoises SSGI/
       shadows/specular for free via temporal averaging. */
    uint32_t taa_frame;         /* frame counter driving the jitter sequence */
    GLuint   taa_history_fbo, taa_history_tex;  /* previous resolved frame */
    GLuint   taa_resolve_fbo, taa_resolve_tex;  /* this frame's resolved output */
    GLuint   taa_shader;
    bool     taa_history_valid; /* false on first frame / after resize */
    /* Depth of field: CoC-driven gather blur on the resolved LDR image. */
    GLuint   dof_fbo, dof_tex;
    GLuint   dof_shader;
    /* Anti-aliasing system (cc/aa.h). Unified resolve running after postfx/TAA. */
    int      aa_mode;            /* CCAAMode as int (avoid header cycle)          */
    float    aa_usd_smoothing;   /* USD smoothing strength                        */
    int      aa_usd_rounds;      /* USD upscale+smooth rounds                     */
    GLuint   aa_smaa_shader;     /* SMAA-style edge-aware blend                   */
    GLuint   aa_usd_shader;      /* USD smooth pass                               */
    GLuint   aa_fbo, aa_tex;     /* scratch target for AA passes                  */
    /* Supersampling (SSAA/USD): the 3D chain renders at (disp_w*ss, disp_h*ss)
       into oversized targets, then downsamples to display size before the 2D
       overlay. disp_w/disp_h are the true output size; r->w/r->h are the current
       (possibly supersampled) render size the whole 3D pipeline already uses. */
    uint32_t disp_w, disp_h;     /* display (output) resolution                   */
    float    ss_scale;           /* active supersample factor (1 = none)          */
    GLuint   ss_down_fbo, ss_down_tex; /* display-res downsample target           */
    GLuint   ss_down_shader;     /* box-downsample shader (proper SSAA resolve)   */
    GLuint   aa_analytic_shader; /* analytic coverage (pixel = diagonal) resolve  */
    float vp_mat[16];
    float inv_vp_mat[16];

    /* Post FX settings */
    CCPostFX postfx;

    /* Screenshot */
    uint8_t* pixel_buf;
    uint32_t pixel_buf_size;
    char     screenshot_dir[256];
    uint64_t screenshot_count;

    /* Fonts */
    struct CCFontEntry {
        GLuint          atlas_tex;      /* R8 coverage atlas */
        uint32_t        atlas_w, atlas_h;
        float           px_size;        /* baked pixel height */
        float           ascent, descent, line_gap;
        stbtt_packedchar cdata[96];     /* ASCII 32..127 (oversampled pack) */
        bool            valid;
    } fonts[16];
    uint32_t font_count;

    /* ── Debug / verification state ── */
    struct { uint32_t draw_calls, triangles, meshes_drawn, verts; } stats, stats_prev;
    int      debug_view;          /* 0=normal,1=wire,2=normal,3=depth,4=albedo,5=overdraw,6=lightonly */
    struct { char name[32]; uint32_t mesh, material, tris; float m[16]; } drawlog[512];
    uint32_t drawlog_count;
    struct { int type; float a[3], b[3], col[3]; float size; char label[32]; } gizmos[1024];
    uint32_t gizmo_count;
    uint32_t pick_ids[512]; uint32_t pick_count;  /* object-id per drawlog entry */
    char     build_hash[16];
};

/* ══════════════════════════════════════════════════════════════════════
   UTILITIES
   ══════════════════════════════════════════════════════════════════════ */

static void check_gl(const char* label) {
    GLenum e = glGetError();
    if (e) fprintf(stderr, "[cc:renderer] GL error at %s: 0x%x\n", label, e);
}

static GLuint compile_shader_src(const char* vert, const char* frag, const char* label) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vert, NULL);
    glCompileShader(vs);
    GLint ok; glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[2048]; glGetShaderInfoLog(vs, sizeof(buf), NULL, buf);
        fprintf(stderr, "[cc:renderer] Vert shader '%s' error:\n%s\n", label, buf);
        glDeleteShader(vs); return 0;
    }
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &frag, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[2048]; glGetShaderInfoLog(fs, sizeof(buf), NULL, buf);
        fprintf(stderr, "[cc:renderer] Frag shader '%s' error:\n%s\n", label, buf);
        glDeleteShader(vs); glDeleteShader(fs); return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[2048]; glGetProgramInfoLog(prog, sizeof(buf), NULL, buf);
        fprintf(stderr, "[cc:renderer] Link error '%s':\n%s\n", label, buf);
        glDeleteProgram(prog); prog = 0;
    }
    glDeleteShader(vs); glDeleteShader(fs);
    return prog;
}

static GLuint make_fbo_color_depth(uint32_t w, uint32_t h, GLenum fmt,
                                    GLuint* out_col, GLuint* out_dep) {
    GLuint fbo; glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLuint col; glGenTextures(1, &col);
    glBindTexture(GL_TEXTURE_2D, col);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col, 0);
    GLuint dep; glGenTextures(1, &dep);
    glBindTexture(GL_TEXTURE_2D, dep);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0,
                 GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                           GL_TEXTURE_2D, dep, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "[cc:renderer] FBO incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (out_col) *out_col = col;
    if (out_dep) *out_dep = dep;
    return fbo;
}

/* ── Fullscreen quad ──────────────────────────────────────────────────── */
static void build_fsq(CCRenderer* r) {
    float verts[] = { -1,-1, 1,-1, 1,1, -1,1 };
    glGenVertexArrays(1, &r->fsq_vao);
    glGenBuffers(1, &r->fsq_vbo);
    glBindVertexArray(r->fsq_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->fsq_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0);
    glBindVertexArray(0);

    /* Billboard unit quad: corner offset in [-0.5,0.5] + uv, two triangles.
       Per-instance center/size/color are wired in cc_renderer_draw_billboards. */
    {
        float quad[] = {
            /* corner.xy       uv */
            -0.5f,-0.5f,   0.0f,0.0f,
             0.5f,-0.5f,   1.0f,0.0f,
             0.5f, 0.5f,   1.0f,1.0f,
            -0.5f,-0.5f,   0.0f,0.0f,
             0.5f, 0.5f,   1.0f,1.0f,
            -0.5f, 0.5f,   0.0f,1.0f,
        };
        glGenVertexArrays(1, &r->bb_vao);
        glGenBuffers(1, &r->bb_quad_vbo);
        glBindVertexArray(r->bb_vao);
        glBindBuffer(GL_ARRAY_BUFFER, r->bb_quad_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); /* corner */
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1); /* uv */
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
        glBindVertexArray(0);
    }

    /* Decal unit box: 8 corners in [-0.5,0.5], 36 indices (12 tris). Only aPos
       is needed; the fragment shader reconstructs everything from depth. */
    {
        float box[] = {
            -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,0.5f,-0.5f, -0.5f,0.5f,-0.5f,
            -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f,0.5f, 0.5f, -0.5f,0.5f, 0.5f,
        };
        unsigned int bidx[] = {
            0,1,2, 2,3,0,  4,6,5, 6,4,7,   /* -z, +z */
            0,4,7, 7,3,0,  1,5,6, 6,2,1,   /* -x, +x */
            3,7,6, 6,2,3,  0,4,5, 5,1,0,   /* +y, -y */
        };
        glGenVertexArrays(1,&r->decal_box_vao);
        glGenBuffers(1,&r->decal_box_vbo);
        glGenBuffers(1,&r->decal_box_ebo);
        glBindVertexArray(r->decal_box_vao);
        glBindBuffer(GL_ARRAY_BUFFER, r->decal_box_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(box), box, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->decal_box_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(bidx), bidx, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);
        glBindVertexArray(0);
    }
}

/* ── G-buffer framebuffer ─────────────────────────────────────────────── */
static void build_gbuffer(CCRenderer* r, uint32_t w, uint32_t h) {
    if (r->gbuf_fbo) { glDeleteFramebuffers(1, &r->gbuf_fbo); }
    glGenFramebuffers(1, &r->gbuf_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->gbuf_fbo);

    /* Create G-buffer textures */
    {
        GLuint t; glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        r->gbuf_albedo_rough = t;
    }
    {
        GLuint t; glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        r->gbuf_normal_metal = t;
    }
    {
        /* HDR emissive: RGBA16F, not RGBA8 — emissive intensities above 1.0 must
           survive into the lighting pass so bloom can extract them. An 8-bit unorm
           target here silently clamps every bright emitter to 1.0, killing bloom. */
        GLuint t; glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        r->gbuf_emissive_ao = t;
    }

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->gbuf_albedo_rough, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, r->gbuf_normal_metal, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, r->gbuf_emissive_ao, 0);

    GLuint dep; glGenTextures(1, &dep);
    glBindTexture(GL_TEXTURE_2D, dep);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0,
                 GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    /* When sampled as sampler2D, return the depth component (not stencil) */
    glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_DEPTH_COMPONENT);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, dep, 0);
    r->gbuf_depth = dep;

    /* Decal depth-copy: a separate DEPTH texture we blit scene depth into, so
       the decal pass can sample depth while writing the G-buffer color targets
       (sampling an FBO's own attached depth is undefined). Recreated on resize. */
    if (r->decal_depth_copy) glDeleteTextures(1,&r->decal_depth_copy);
    { GLuint dc; glGenTextures(1,&dc); glBindTexture(GL_TEXTURE_2D,dc);
      glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH24_STENCIL8,w,h,0,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,NULL);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_DEPTH_STENCIL_TEXTURE_MODE,GL_DEPTH_COMPONENT);
      r->decal_depth_copy = dc; }
    if (!r->decal_depth_fbo) glGenFramebuffers(1,&r->decal_depth_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->decal_depth_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,r->decal_depth_copy,0);
    glBindFramebuffer(GL_FRAMEBUFFER, r->gbuf_fbo);

    GLenum bufs[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3, bufs);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ── SSAO / SSR screen-sized targets (rebuilt on resize) ──────────────── */
static void build_ssao_targets(CCRenderer* r, uint32_t w, uint32_t h) {
    if (r->ssao_fbo)      glDeleteFramebuffers(1,&r->ssao_fbo);
    if (r->ssao_tex)      glDeleteTextures(1,&r->ssao_tex);
    if (r->ssao_blur_fbo) glDeleteFramebuffers(1,&r->ssao_blur_fbo);
    if (r->ssao_blur_tex) glDeleteTextures(1,&r->ssao_blur_tex);
    glGenFramebuffers(1,&r->ssao_fbo); glGenTextures(1,&r->ssao_tex);
    glBindTexture(GL_TEXTURE_2D,r->ssao_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8,w,h,0,GL_RED,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,r->ssao_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->ssao_tex,0);
    glGenFramebuffers(1,&r->ssao_blur_fbo); glGenTextures(1,&r->ssao_blur_tex);
    glBindTexture(GL_TEXTURE_2D,r->ssao_blur_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8,w,h,0,GL_RED,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER,r->ssao_blur_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->ssao_blur_tex,0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}
static void build_ssr_targets(CCRenderer* r, uint32_t w, uint32_t h) {
    if (r->ssr_fbo)           glDeleteFramebuffers(1,&r->ssr_fbo);
    if (r->ssr_tex)           glDeleteTextures(1,&r->ssr_tex);
    if (r->ssr_composite_fbo) glDeleteFramebuffers(1,&r->ssr_composite_fbo);
    if (r->ssr_composite_tex) glDeleteTextures(1,&r->ssr_composite_tex);
    glGenFramebuffers(1,&r->ssr_fbo); glGenTextures(1,&r->ssr_tex);
    glBindTexture(GL_TEXTURE_2D,r->ssr_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER,r->ssr_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->ssr_tex,0);
    glGenFramebuffers(1,&r->ssr_composite_fbo); glGenTextures(1,&r->ssr_composite_tex);
    glBindTexture(GL_TEXTURE_2D,r->ssr_composite_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER,r->ssr_composite_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->ssr_composite_tex,0);
    /* SSGI targets (mirror SSR): a gather buffer + a composite buffer. */
    if (r->ssgi_fbo)           glDeleteFramebuffers(1,&r->ssgi_fbo);
    if (r->ssgi_tex)           glDeleteTextures(1,&r->ssgi_tex);
    if (r->ssgi_composite_fbo) glDeleteFramebuffers(1,&r->ssgi_composite_fbo);
    if (r->ssgi_composite_tex) glDeleteTextures(1,&r->ssgi_composite_tex);
    glGenFramebuffers(1,&r->ssgi_fbo); glGenTextures(1,&r->ssgi_tex);
    glBindTexture(GL_TEXTURE_2D,r->ssgi_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER,r->ssgi_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->ssgi_tex,0);
    glGenFramebuffers(1,&r->ssgi_composite_fbo); glGenTextures(1,&r->ssgi_composite_tex);
    glBindTexture(GL_TEXTURE_2D,r->ssgi_composite_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER,r->ssgi_composite_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->ssgi_composite_tex,0);
    /* SSGI temporal accumulation + history (RGBA16F — accumulates raw GI). */
    if (r->ssgi_accum_fbo) glDeleteFramebuffers(1,&r->ssgi_accum_fbo);
    if (r->ssgi_accum_tex) glDeleteTextures(1,&r->ssgi_accum_tex);
    if (r->ssgi_ghist_fbo) glDeleteFramebuffers(1,&r->ssgi_ghist_fbo);
    if (r->ssgi_ghist_tex) glDeleteTextures(1,&r->ssgi_ghist_tex);
    glGenFramebuffers(1,&r->ssgi_accum_fbo); glGenTextures(1,&r->ssgi_accum_tex);
    glBindTexture(GL_TEXTURE_2D,r->ssgi_accum_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER,r->ssgi_accum_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->ssgi_accum_tex,0);
    glGenFramebuffers(1,&r->ssgi_ghist_fbo); glGenTextures(1,&r->ssgi_ghist_tex);
    glBindTexture(GL_TEXTURE_2D,r->ssgi_ghist_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER,r->ssgi_ghist_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->ssgi_ghist_tex,0);
    r->ssgi_hist_valid = false;
    /* TAA history + resolve targets (LDR — TAA runs on the final display image). */
    if (r->taa_history_fbo) glDeleteFramebuffers(1,&r->taa_history_fbo);
    if (r->taa_history_tex) glDeleteTextures(1,&r->taa_history_tex);
    if (r->taa_resolve_fbo) glDeleteFramebuffers(1,&r->taa_resolve_fbo);
    if (r->taa_resolve_tex) glDeleteTextures(1,&r->taa_resolve_tex);
    glGenFramebuffers(1,&r->taa_history_fbo); glGenTextures(1,&r->taa_history_tex);
    glBindTexture(GL_TEXTURE_2D,r->taa_history_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER,r->taa_history_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->taa_history_tex,0);
    glGenFramebuffers(1,&r->taa_resolve_fbo); glGenTextures(1,&r->taa_resolve_tex);
    glBindTexture(GL_TEXTURE_2D,r->taa_resolve_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER,r->taa_resolve_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->taa_resolve_tex,0);
    r->taa_history_valid = false;   /* history invalid until first frame written */
    /* DOF target (LDR, same size) — built here so it tracks resize with TAA. */
    if (r->dof_fbo) glDeleteFramebuffers(1,&r->dof_fbo);
    if (r->dof_tex) glDeleteTextures(1,&r->dof_tex);
    glGenFramebuffers(1,&r->dof_fbo); glGenTextures(1,&r->dof_tex);
    glBindTexture(GL_TEXTURE_2D,r->dof_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER,r->dof_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->dof_tex,0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}

/* ── Bloom FBOs ───────────────────────────────────────────────────────── */
static void build_bloom(CCRenderer* r, uint32_t w, uint32_t h) {
    for (int i = 0; i < CC_BLOOM_LEVELS; i++) {
        if (r->bloom_fbo[i]) glDeleteFramebuffers(1, &r->bloom_fbo[i]);
        if (r->bloom_tex[i]) glDeleteTextures(1, &r->bloom_tex[i]);
        uint32_t bw = w >> (i+1), bh = h >> (i+1);
        if (!bw) bw = 1; if (!bh) bh = 1;
        glGenFramebuffers(1, &r->bloom_fbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, r->bloom_fbo[i]);
        glGenTextures(1, &r->bloom_tex[i]);
        glBindTexture(GL_TEXTURE_2D, r->bloom_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, bw, bh, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->bloom_tex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ── Fallback textures ────────────────────────────────────────────────── */
static GLuint make_solid_tex(uint8_t r2, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t px[4] = {r2, g, b, a};
    GLuint t; glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

/* ── Sprite batch setup ───────────────────────────────────────────────── */
static void build_sprite_batch(CCRenderer* r) {
    r->sprite_verts = malloc(CC_MAX_2D_VERTS * sizeof(SpriteVert));
    glGenVertexArrays(1, &r->sprite_vao);
    glGenBuffers(1, &r->sprite_vbo);
    glGenBuffers(1, &r->sprite_ebo);

    uint32_t* idx = malloc((CC_MAX_2D_VERTS / 4 * 6) * sizeof(uint32_t));
    for (uint32_t i = 0, v = 0; i < CC_MAX_2D_VERTS/4; i++, v+=4) {
        idx[i*6+0]=v;   idx[i*6+1]=v+1; idx[i*6+2]=v+2;
        idx[i*6+3]=v;   idx[i*6+4]=v+2; idx[i*6+5]=v+3;
    }
    glBindVertexArray(r->sprite_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->sprite_vbo);
    glBufferData(GL_ARRAY_BUFFER, CC_MAX_2D_VERTS * sizeof(SpriteVert), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->sprite_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (CC_MAX_2D_VERTS/4*6)*sizeof(uint32_t), idx, GL_STATIC_DRAW);
    free(idx);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(SpriteVert),(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(SpriteVert),(void*)8);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(SpriteVert),(void*)16);
    glBindVertexArray(0);
}

/* ══════════════════════════════════════════════════════════════════════
   BACKEND INIT
   ══════════════════════════════════════════════════════════════════════ */

#ifdef CC_USE_GLFW
static bool renderer_init_glfw(CCRenderer* r, uint32_t w, uint32_t h, const char* title) {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    r->window = glfwCreateWindow(w, h, title, NULL, NULL);
    if (!r->window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(r->window);
    glfwSwapInterval(1);
    /* STICKY KEYS: without this, glfwGetKey only reports the CURRENT physical key
       state, so a key pressed AND released within a single frame (fast alternating
       inputs) is never seen → the input "doesn't land". Sticky keys makes GLFW
       latch a press until it's polled once, so no fast tap is lost. We clear the
       latch by polling each key every frame (cc_input_end_frame reads them). */
    glfwSetInputMode(r->window, GLFW_STICKY_KEYS, GLFW_TRUE);
    cc_gl_load((void*(*)(const char*))glfwGetProcAddress);  /* load GL3 fns on Windows */
    return true;
}
#endif

static bool renderer_init_osmesa(CCRenderer* r, uint32_t w, uint32_t h) {
#ifdef CC_USE_OSMESA
    r->osmesa_ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
    if (!r->osmesa_ctx) return false;
    r->osmesa_buf = malloc(w * h * 4);
    if (!OSMesaMakeCurrent(r->osmesa_ctx, r->osmesa_buf, GL_UNSIGNED_BYTE, w, h)) {
        OSMesaDestroyContext(r->osmesa_ctx); free(r->osmesa_buf); return false;
    }
    return true;
#else
    (void)r; (void)w; (void)h;
    return false;
#endif
}

/* ── Image-based lighting setup ───────────────────────────────────────────
   Allocates the cube geometry and the three cubemaps + BRDF LUT once. The heavy
   convolution work happens in regenerate_ibl(), called when the sky changes. */
#define IBL_ENV_SIZE      128
#define IBL_IRR_SIZE      32
#define IBL_PREFILTER_SIZE 128
#define IBL_PREFILTER_MIPS 5
#define IBL_BRDF_SIZE     256

static void build_ibl(CCRenderer* r) {
    /* Unit cube (positions only) for rendering the 6 faces. */
    float cube[] = {
        -1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,  /* -z */
        -1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1,  /* +z */
    };
    unsigned int idx[] = {
        0,1,2, 2,3,0,  4,6,5, 6,4,7,
        0,4,7, 7,3,0,  1,5,6, 6,2,1,
        3,7,6, 6,2,3,  0,4,5, 5,1,0,
    };
    GLuint ebo;
    glGenVertexArrays(1,&r->ibl_cube_vao);
    glGenBuffers(1,&r->ibl_cube_vbo);
    glGenBuffers(1,&ebo);
    glBindVertexArray(r->ibl_cube_vao);
    glBindBuffer(GL_ARRAY_BUFFER,r->ibl_cube_vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(cube),cube,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);
    glBindVertexArray(0);

    /* Helper to allocate a cubemap. */
    #define ALLOC_CUBE(handle, size, mips) do { \
        glGenTextures(1,&(handle)); glBindTexture(GL_TEXTURE_CUBE_MAP,(handle)); \
        for (int f=0; f<6; f++) \
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+f, 0, GL_RGB16F, (size),(size),0,GL_RGB,GL_FLOAT,NULL); \
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); \
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE); \
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE); \
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,(mips)?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR); \
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR); \
        if (mips) glGenerateMipmap(GL_TEXTURE_CUBE_MAP); \
    } while(0)

    ALLOC_CUBE(r->ibl_env_cube,        IBL_ENV_SIZE,        false);
    ALLOC_CUBE(r->ibl_irradiance_cube, IBL_IRR_SIZE,        false);
    ALLOC_CUBE(r->ibl_prefilter_cube,  IBL_PREFILTER_SIZE,  true);
    #undef ALLOC_CUBE

    /* BRDF LUT (2-channel RG16F). */
    glGenTextures(1,&r->ibl_brdf_lut);
    glBindTexture(GL_TEXTURE_2D,r->ibl_brdf_lut);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,IBL_BRDF_SIZE,IBL_BRDF_SIZE,0,GL_RG,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

    glGenFramebuffers(1,&r->ibl_capture_fbo);
    r->ibl_dirty = true;
}

/* The 6 cube-face view matrices (looking down each axis). */
static void ibl_face_views(CCMat4 out[6]) {
    CCVec3 o={0,0,0};
    CCVec3 tgt[6]={{ 1,0,0},{-1,0,0},{0, 1,0},{0,-1,0},{0,0, 1},{0,0,-1}};
    CCVec3 up[6] ={{0,-1,0},{0,-1,0},{0,0, 1},{0,0,-1},{0,-1,0},{0,-1,0}};
    for (int i=0;i<6;i++) out[i]=mat4_look_at(o, tgt[i], up[i]);
}

/* Run capture → irradiance → prefilter → BRDF. Called when sky params change. */
static void regenerate_ibl(CCRenderer* r) {
    if (!r->ibl_env_cube) return;
    CCMat4 proj = mat4_perspective(1.5707963f, 1.0f, 0.1f, 10.0f); /* 90° */
    CCMat4 views[6]; ibl_face_views(views);
    GLint prevVP[4]; glGetIntegerv(GL_VIEWPORT, prevVP);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glBindVertexArray(r->ibl_cube_vao);

    /* 1. Capture procedural sky into env cube. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->ibl_capture_fbo);
    glViewport(0,0,IBL_ENV_SIZE,IBL_ENV_SIZE);
    glUseProgram(r->ibl_capture_shader);
    glUniform3fv(glGetUniformLocation(r->ibl_capture_shader,"uSkyZenith"),1,r->sky_zenith);
    glUniform3fv(glGetUniformLocation(r->ibl_capture_shader,"uSkyHorizon"),1,r->sky_horizon);
    glUniform3fv(glGetUniformLocation(r->ibl_capture_shader,"uSkyGround"),1,r->sky_ground);
    glUniform1f (glGetUniformLocation(r->ibl_capture_shader,"uSkyIntensity"),r->sky_intensity);
    for (int f=0; f<6; f++){
        CCMat4 vp = mat4_mul(proj, views[f]);
        glUniformMatrix4fv(glGetUniformLocation(r->ibl_capture_shader,"uVP"),1,GL_FALSE,vp.m);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,r->ibl_env_cube,0);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES,36,GL_UNSIGNED_INT,0);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP,r->ibl_env_cube); glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    /* 2. Irradiance convolution. */
    glViewport(0,0,IBL_IRR_SIZE,IBL_IRR_SIZE);
    glUseProgram(r->ibl_irradiance_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_CUBE_MAP,r->ibl_env_cube);
    glUniform1i(glGetUniformLocation(r->ibl_irradiance_shader,"uEnv"),0);
    for (int f=0; f<6; f++){
        CCMat4 vp = mat4_mul(proj, views[f]);
        glUniformMatrix4fv(glGetUniformLocation(r->ibl_irradiance_shader,"uVP"),1,GL_FALSE,vp.m);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,r->ibl_irradiance_cube,0);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES,36,GL_UNSIGNED_INT,0);
    }

    /* 3. Prefiltered specular across roughness mips. */
    glUseProgram(r->ibl_prefilter_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_CUBE_MAP,r->ibl_env_cube);
    glUniform1i(glGetUniformLocation(r->ibl_prefilter_shader,"uEnv"),0);
    for (int mip=0; mip<IBL_PREFILTER_MIPS; mip++){
        uint32_t sz = IBL_PREFILTER_SIZE >> mip; if(!sz)sz=1;
        glViewport(0,0,sz,sz);
        float rough = (float)mip/(float)(IBL_PREFILTER_MIPS-1);
        glUniform1f(glGetUniformLocation(r->ibl_prefilter_shader,"uRough"),rough);
        for (int f=0; f<6; f++){
            CCMat4 vp = mat4_mul(proj, views[f]);
            glUniformMatrix4fv(glGetUniformLocation(r->ibl_prefilter_shader,"uVP"),1,GL_FALSE,vp.m);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,r->ibl_prefilter_cube,mip);
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawElements(GL_TRIANGLES,36,GL_UNSIGNED_INT,0);
        }
    }

    /* 4. BRDF integration LUT (fullscreen quad). */
    glViewport(0,0,IBL_BRDF_SIZE,IBL_BRDF_SIZE);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->ibl_brdf_lut,0);
    glUseProgram(r->ibl_brdf_shader);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindVertexArray(r->fsq_vao);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(prevVP[0],prevVP[1],prevVP[2],prevVP[3]);
    glEnable(GL_DEPTH_TEST);
    r->ibl_ready = true;
    r->ibl_dirty = false;
}

CCRenderer* cc_renderer_create(CCRendererBackend backend, uint32_t w, uint32_t h,
                                const char* title, const char* screenshot_dir) {
    CCRenderer* r = calloc(1, sizeof(CCRenderer));
    r->w = w; r->h = h;
    r->disp_w = w; r->disp_h = h; r->ss_scale = 1.0f;
    r->backend = backend;
    snprintf(r->screenshot_dir, sizeof(r->screenshot_dir), "%s", screenshot_dir ? screenshot_dir : "/tmp");

    bool ok = false;
    if (backend == CC_RENDERER_AUTO || backend == CC_RENDERER_VULKAN
        || backend == CC_RENDERER_OPENGL) {
#ifdef CC_USE_GLFW
        ok = renderer_init_glfw(r, w, h, title);
        if (ok) { r->backend = CC_RENDERER_OPENGL; goto gl_init; }
#endif
    }
    if (!ok && (backend == CC_RENDERER_AUTO || backend == CC_RENDERER_OSMESA
                || backend == CC_RENDERER_HEADLESS)) {
        ok = renderer_init_osmesa(r, w, h);
        if (ok) { r->backend = CC_RENDERER_OSMESA; goto gl_init; }
    }
    if (!ok) {
        r->backend = CC_RENDERER_NULL;
        return r;
    }

gl_init:
    /* Compile all shaders */
    r->sh_gbuf      = compile_shader_src(GBUF_VERT,     GBUF_FRAG,      "gbuf");
    r->sh_gbuf_skin = compile_shader_src(GBUF_SKIN_VERT,GBUF_FRAG,      "gbuf_skin");
    r->sh_gbuf_inst = compile_shader_src(GBUF_INST_VERT,GBUF_FRAG,      "gbuf_inst");
    r->sh_light     = compile_shader_src(LIGHT_VERT,    LIGHT_FRAG,     "light");
    r->sh_postfx    = compile_shader_src(BLIT_VERT,     POSTFX_FRAG,    "postfx");
    r->sh_bloom_down= compile_shader_src(BLIT_VERT,     BLOOM_DOWN_FRAG,"bloom_dn");
    r->sh_bloom_up  = compile_shader_src(BLIT_VERT,     BLOOM_UP_FRAG,  "bloom_up");
    r->sh_sprite    = compile_shader_src(SPRITE_VERT,   SPRITE_FRAG,    "sprite");
    r->bb_shader    = compile_shader_src(BILLBOARD_VERT,BILLBOARD_FRAG, "billboard");
    r->decal_shader = compile_shader_src(DECAL_VERT,    DECAL_FRAG,     "decal");
    r->shadow_shader= compile_shader_src(SHADOW_VERT,   SHADOW_FRAG,    "shadow");
    r->ssao_shader      = compile_shader_src(BLIT_VERT, SSAO_FRAG,      "ssao");
    r->ssao_blur_shader = compile_shader_src(BLIT_VERT, SSAO_BLUR_FRAG, "ssao_blur");
    r->ssr_shader           = compile_shader_src(BLIT_VERT, SSR_FRAG,           "ssr");
    r->ssr_composite_shader = compile_shader_src(BLIT_VERT, SSR_COMPOSITE_FRAG, "ssr_composite");
    r->ssgi_shader           = compile_shader_src(BLIT_VERT, SSGI_FRAG,           "ssgi");
    r->ssgi_composite_shader = compile_shader_src(BLIT_VERT, SSGI_COMPOSITE_FRAG, "ssgi_composite");
    r->ssgi_accum_shader     = compile_shader_src(BLIT_VERT, SSGI_ACCUM_FRAG,     "ssgi_accum");
    r->taa_shader            = compile_shader_src(BLIT_VERT, TAA_FRAG,            "taa");
    r->aa_smaa_shader        = compile_shader_src(BLIT_VERT, AA_SMAA_FRAG,        "aa_smaa");
    r->aa_usd_shader         = compile_shader_src(BLIT_VERT, AA_USD_FRAG,         "aa_usd");
    r->ss_down_shader        = compile_shader_src(BLIT_VERT, AA_DOWNSAMPLE_FRAG,  "aa_downsample");
    r->aa_analytic_shader    = compile_shader_src(BLIT_VERT, AA_ANALYTIC_FRAG,    "aa_analytic");
    r->dof_shader            = compile_shader_src(BLIT_VERT, DOF_FRAG,            "dof");
    r->ibl_capture_shader    = compile_shader_src(IBL_CUBE_VERT, IBL_CAPTURE_FRAG,    "ibl_capture");
    r->ibl_irradiance_shader = compile_shader_src(IBL_CUBE_VERT, IBL_IRRADIANCE_FRAG, "ibl_irradiance");
    r->ibl_prefilter_shader  = compile_shader_src(IBL_CUBE_VERT, IBL_PREFILTER_FRAG,  "ibl_prefilter");
    r->ibl_brdf_shader       = compile_shader_src(BLIT_VERT,     IBL_BRDF_FRAG,       "ibl_brdf");

    build_fsq(r);
    build_gbuffer(r, w, h);
    build_bloom(r, w, h);
    build_sprite_batch(r);

    /* Directional shadow map (depth-only). Fixed 2048² is a good default. */
    r->shadow_size = 4096;
    r->shadow_bias = 0.0015f;
    r->shadow_softness = 3.0f;
    {
        glGenFramebuffers(1, &r->shadow_fbo);
        glGenTextures(1, &r->shadow_tex);
        glBindTexture(GL_TEXTURE_2D, r->shadow_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, r->shadow_size, r->shadow_size,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[4] = {1,1,1,1};   /* outside the map = fully lit */
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, r->shadow_tex, 0);
        glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    /* SSAO targets (R8) + blur; screen-sized so also rebuilt on resize. */
    build_ssao_targets(r, w, h);
    {
        /* 4x4 rotation noise: random vectors in the tangent plane (z=0). */
        float noise[4*4*3];
        for (int i=0;i<16;i++){
            noise[i*3+0]=((float)rand()/RAND_MAX)*2.0f-1.0f;
            noise[i*3+1]=((float)rand()/RAND_MAX)*2.0f-1.0f;
            noise[i*3+2]=0.0f;
        }
        glGenTextures(1,&r->ssao_noise_tex);
        glBindTexture(GL_TEXTURE_2D,r->ssao_noise_tex);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16F,4,4,0,GL_RGB,GL_FLOAT,noise);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);

        /* Cosine-weighted hemisphere kernel, scaled to cluster near the origin. */
        for (int i=0;i<32;i++){
            float x=((float)rand()/RAND_MAX)*2.0f-1.0f;
            float y=((float)rand()/RAND_MAX)*2.0f-1.0f;
            float z=(float)rand()/RAND_MAX;                 /* hemisphere: z>=0 */
            float len=sqrtf(x*x+y*y+z*z); if(len<1e-5f)len=1;
            x/=len; y/=len; z/=len;
            float s=(float)i/32.0f;
            float scale=0.1f+0.9f*s*s;                      /* accelerate outward */
            r->ssao_kernel[i*3+0]=x*scale;
            r->ssao_kernel[i*3+1]=y*scale;
            r->ssao_kernel[i*3+2]=z*scale;
        }
    }

    /* SSR targets (RGBA16F reflection + composite); rebuilt on resize. */
    build_ssr_targets(r, w, h);

    /* Image-based lighting cubemaps + BRDF LUT (built once; regenerated lazily
       in frame_end when the sky changes). */
    build_ibl(r);

    /* HDR framebuffer */
    r->hdr_fbo = make_fbo_color_depth(w, h, GL_RGBA16F, &r->hdr_color_tex, NULL);
    /* enable a mip chain on the HDR color texture so auto-exposure can reduce
       the whole frame to a 1x1 average luminance via glGenerateMipmap. */
    glBindTexture(GL_TEXTURE_2D, r->hdr_color_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Post output */
    r->post_fbo = make_fbo_color_depth(w, h, GL_RGBA8, &r->post_tex, NULL);

    /* Fallback textures */
    r->white_tex          = make_solid_tex(255,255,255,255);
    r->black_tex          = make_solid_tex(0,0,0,255);
    r->default_normal_tex = make_solid_tex(128,128,255,255);

    /* Light UBO */
    glGenBuffers(1, &r->light_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, r->light_ubo);
    glBufferData(GL_UNIFORM_BUFFER, 64*sizeof(float)*4*4 + 16, NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->light_ubo);

    /* Bone UBO — 256 mat4 skinning matrices, binding point 1 */
    glGenBuffers(1, &r->bone_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, r->bone_ubo);
    glBufferData(GL_UNIFORM_BUFFER, 256*16*sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, r->bone_ubo);
    /* Link the BoneBlock in the skinned shader to binding point 1 */
    {
        GLuint bidx = glGetUniformBlockIndex(r->sh_gbuf_skin, "BoneBlock");
        if (bidx != GL_INVALID_INDEX) glUniformBlockBinding(r->sh_gbuf_skin, bidx, 1);
    }
    r->postfx = cc_postfx_default();

    /* Default camera */
    r->camera.fov_deg   = 60.0f;
    r->camera.near_plane= 0.1f;
    r->camera.far_plane = 1000.0f;
    r->camera.exposure  = 1.0f;
    r->camera.pos[0]=0; r->camera.pos[1]=1; r->camera.pos[2]=5;
    r->camera.target[2]=-1; r->camera.up[1]=1;

    /* Pixel readback buffer */
    r->pixel_buf_size = w * h * 4;
    r->pixel_buf = malloc(r->pixel_buf_size);

    /* Default ambient */
    r->ambient_color[0] = r->ambient_color[1] = r->ambient_color[2] = 0.05f;
    r->ambient_intensity = 1.0f;
    /* Sky defaults: a calm daytime gradient, off until cc_light_set_sky. */
    r->sky_enabled = false;
    r->sky_zenith[0]=0.10f; r->sky_zenith[1]=0.22f; r->sky_zenith[2]=0.45f;
    r->sky_horizon[0]=0.55f; r->sky_horizon[1]=0.65f; r->sky_horizon[2]=0.78f;
    r->sky_ground[0]=0.20f; r->sky_ground[1]=0.17f; r->sky_ground[2]=0.14f;
    r->sky_intensity = 1.0f;
    r->sky_tex = 0;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    check_gl("renderer_create");
    /* Detect anisotropic filtering support (core in GL 4.6, EXT elsewhere). Used
       to sharpen material textures at grazing angles — the single biggest texture
       clarity win for ground planes / walls receding to the horizon. */
    r->max_anisotropy = 1.0f;
    {
        const char* ext = (const char*)glGetString(GL_EXTENSIONS);
        GLfloat maxa = 1.0f;
        /* 0x84FF = GL_MAX_TEXTURE_MAX_ANISOTROPY(_EXT) */
        glGetFloatv(0x84FF, &maxa);
        if (maxa > 1.0f && maxa <= 64.0f) r->max_anisotropy = maxa;
        else r->max_anisotropy = 1.0f;
        (void)ext;
    }
    fprintf(stderr, "[cc:renderer] Online — backend=%s  %ux%u  aniso=%.0fx\n",
            r->backend == CC_RENDERER_OSMESA ? "OSMesa(headless)" : "OpenGL/GLFW", w, h, r->max_anisotropy);
    return r;
}

void cc_renderer_destroy(CCRenderer* r) {
    if (!r) return;
    free(r->sprite_verts);
    free(r->pixel_buf);
    if (r->inst_vbo) glDeleteBuffers(1, &r->inst_vbo);
    if (r->bb_quad_vbo) glDeleteBuffers(1, &r->bb_quad_vbo);
    if (r->bb_inst_vbo) glDeleteBuffers(1, &r->bb_inst_vbo);
    if (r->bb_vao) glDeleteVertexArrays(1, &r->bb_vao);
    if (r->decal_box_vbo) glDeleteBuffers(1, &r->decal_box_vbo);
    if (r->decal_box_ebo) glDeleteBuffers(1, &r->decal_box_ebo);
    if (r->decal_box_vao) glDeleteVertexArrays(1, &r->decal_box_vao);
    if (r->decal_depth_copy) glDeleteTextures(1, &r->decal_depth_copy);
    if (r->decal_depth_fbo) glDeleteFramebuffers(1, &r->decal_depth_fbo);
    free(r->decal_queue);
    if (r->shadow_tex) glDeleteTextures(1, &r->shadow_tex);
    if (r->shadow_fbo) glDeleteFramebuffers(1, &r->shadow_fbo);
    if (r->ssao_tex) glDeleteTextures(1, &r->ssao_tex);
    if (r->ssao_blur_tex) glDeleteTextures(1, &r->ssao_blur_tex);
    if (r->ssao_noise_tex) glDeleteTextures(1, &r->ssao_noise_tex);
    if (r->ssao_fbo) glDeleteFramebuffers(1, &r->ssao_fbo);
    if (r->ssao_blur_fbo) glDeleteFramebuffers(1, &r->ssao_blur_fbo);
    if (r->ssr_tex) glDeleteTextures(1, &r->ssr_tex);
    if (r->ssr_composite_tex) glDeleteTextures(1, &r->ssr_composite_tex);
    if (r->ssr_fbo) glDeleteFramebuffers(1, &r->ssr_fbo);
    if (r->ssr_composite_fbo) glDeleteFramebuffers(1, &r->ssr_composite_fbo);
    if (r->ssgi_tex) glDeleteTextures(1, &r->ssgi_tex);
    if (r->ssgi_composite_tex) glDeleteTextures(1, &r->ssgi_composite_tex);
    if (r->ssgi_fbo) glDeleteFramebuffers(1, &r->ssgi_fbo);
    if (r->ssgi_composite_fbo) glDeleteFramebuffers(1, &r->ssgi_composite_fbo);
    if (r->ssgi_accum_tex) glDeleteTextures(1, &r->ssgi_accum_tex);
    if (r->ssgi_ghist_tex) glDeleteTextures(1, &r->ssgi_ghist_tex);
    if (r->ssgi_accum_fbo) glDeleteFramebuffers(1, &r->ssgi_accum_fbo);
    if (r->ssgi_ghist_fbo) glDeleteFramebuffers(1, &r->ssgi_ghist_fbo);
    if (r->dof_tex) glDeleteTextures(1, &r->dof_tex);
    if (r->dof_fbo) glDeleteFramebuffers(1, &r->dof_fbo);
    if (r->ibl_env_cube) glDeleteTextures(1, &r->ibl_env_cube);
    if (r->ibl_irradiance_cube) glDeleteTextures(1, &r->ibl_irradiance_cube);
    if (r->ibl_prefilter_cube) glDeleteTextures(1, &r->ibl_prefilter_cube);
    if (r->ibl_brdf_lut) glDeleteTextures(1, &r->ibl_brdf_lut);
    if (r->ibl_cube_vao) glDeleteVertexArrays(1, &r->ibl_cube_vao);
    if (r->ibl_cube_vbo) glDeleteBuffers(1, &r->ibl_cube_vbo);
    if (r->ibl_capture_fbo) glDeleteFramebuffers(1, &r->ibl_capture_fbo);
#ifdef CC_USE_OSMESA
    if (r->osmesa_ctx) {
        OSMesaDestroyContext(r->osmesa_ctx);
        free(r->osmesa_buf);
    }
#endif
#ifdef CC_USE_GLFW
    if (r->window) { glfwDestroyWindow(r->window); glfwTerminate(); }
#endif
    free(r);
}

/* ══════════════════════════════════════════════════════════════════════
   FRAME
   ══════════════════════════════════════════════════════════════════════ */

void cc_renderer_frame_begin(CCRenderer* r) {
    r->stats = (typeof(r->stats)){0};
    r->drawlog_count = 0; r->gizmo_count = 0;
    r->batch2d_count = 0; r->sprite_nv = 0;
    if (r->backend == CC_RENDERER_NULL) return;
    glBindFramebuffer(GL_FRAMEBUFFER, r->gbuf_fbo);
    glViewport(0, 0, r->w, r->h);
    glClearColor(0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    r->sprite_nv = 0;
}

void cc_renderer_upload_lights(CCRenderer* r);
static void render_lighting_pass(CCRenderer* r) {
    glBindFramebuffer(GL_FRAMEBUFFER, r->hdr_fbo);
    glViewport(0,0,r->w,r->h);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(r->sh_light);
    /* Upload + bind lights */
    cc_renderer_upload_lights(r);
    GLuint block_idx = glGetUniformBlockIndex(r->sh_light, "LightBlock");
    if (block_idx != GL_INVALID_INDEX) glUniformBlockBinding(r->sh_light, block_idx, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->light_ubo);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->gbuf_albedo_rough);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->gbuf_normal_metal);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, r->gbuf_emissive_ao);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);

    glUniform1i(glGetUniformLocation(r->sh_light, "gAlbedoRough"), 0);
    glUniform1i(glGetUniformLocation(r->sh_light, "gNormalMetal"), 1);
    glUniform1i(glGetUniformLocation(r->sh_light, "gEmissiveAO"),  2);
    glUniform1i(glGetUniformLocation(r->sh_light, "uDebugView"), r->debug_view);
    glUniform1f(glGetUniformLocation(r->sh_light, "uToonSpecular"), r->style_toon_specular);
    glUniform1f(glGetUniformLocation(r->sh_light, "uRimStrength"),  r->style_rim_strength);
    glUniform1f(glGetUniformLocation(r->sh_light, "uRimPower"),     r->style_rim_power);
    glUniform3f(glGetUniformLocation(r->sh_light, "uRimColor"),     r->style_rim_color[0],r->style_rim_color[1],r->style_rim_color[2]);
    glUniform1i(glGetUniformLocation(r->sh_light, "uFog"), r->postfx.fog?1:0);
    glUniform1f(glGetUniformLocation(r->sh_light, "uFogDensity"), r->postfx.fog_density);
    glUniform3f(glGetUniformLocation(r->sh_light, "uFogColor"), r->postfx.fog_color[0],r->postfx.fog_color[1],r->postfx.fog_color[2]);
    glUniform1f(glGetUniformLocation(r->sh_light, "uFogHeight"), r->postfx.fog_height);
    glUniform1i(glGetUniformLocation(r->sh_light, "gDepth"),        3);
    glUniform3fv(glGetUniformLocation(r->sh_light,"uCamPos"),1,r->camera.pos);
    float ambient_vec[4] = { r->ambient_color[0], r->ambient_color[1],
                             r->ambient_color[2], r->ambient_intensity };
    glUniform4fv(glGetUniformLocation(r->sh_light,"uAmbient"), 1, ambient_vec);
    /* Sky / IBL uniforms */
    glUniform1i(glGetUniformLocation(r->sh_light,"uSkyOn"), r->sky_enabled ? 1 : 0);
    glUniform3fv(glGetUniformLocation(r->sh_light,"uSkyZenith"), 1, r->sky_zenith);
    glUniform3fv(glGetUniformLocation(r->sh_light,"uSkyHorizon"),1, r->sky_horizon);
    glUniform3fv(glGetUniformLocation(r->sh_light,"uSkyGround"), 1, r->sky_ground);
    glUniform1f (glGetUniformLocation(r->sh_light,"uSkyIntensity"), r->sky_intensity);
    /* Shadows */
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, r->shadow_tex);
    glUniform1i(glGetUniformLocation(r->sh_light,"uShadowMap"), 4);
    glUniform1i(glGetUniformLocation(r->sh_light,"uShadowOn"), r->shadow_active ? 1 : 0);
    glUniform1i(glGetUniformLocation(r->sh_light,"uShadowLight"), r->shadow_light);
    glUniformMatrix4fv(glGetUniformLocation(r->sh_light,"uShadowVP"),1,GL_FALSE,r->shadow_vp);
    glUniform1f(glGetUniformLocation(r->sh_light,"uShadowBias"), r->shadow_bias);
    glUniform1f(glGetUniformLocation(r->sh_light,"uShadowTexel"), r->shadow_size?1.0f/(float)r->shadow_size:0.0f);
    glUniform1f(glGetUniformLocation(r->sh_light,"uShadowSoftness"), r->shadow_softness);
    /* SSAO */
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, r->ssao_blur_tex);
    glUniform1i(glGetUniformLocation(r->sh_light,"uSSAO"), 5);
    glUniform1i(glGetUniformLocation(r->sh_light,"uSSAOOn"), r->postfx.ssao ? 1 : 0);
    glUniform1f(glGetUniformLocation(r->sh_light,"uSSAOIntensity"), r->postfx.ssao_intensity>0.0f?r->postfx.ssao_intensity:1.0f);
    /* IBL cubemaps (units 6,7,8) */
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_CUBE_MAP, r->ibl_irradiance_cube);
    glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_CUBE_MAP, r->ibl_prefilter_cube);
    glActiveTexture(GL_TEXTURE8); glBindTexture(GL_TEXTURE_2D, r->ibl_brdf_lut);
    glUniform1i(glGetUniformLocation(r->sh_light,"uIrradiance"), 6);
    glUniform1i(glGetUniformLocation(r->sh_light,"uPrefilter"), 7);
    glUniform1i(glGetUniformLocation(r->sh_light,"uBrdfLUT"), 8);
    glUniform1i(glGetUniformLocation(r->sh_light,"uIblReady"), (r->sky_enabled && r->ibl_ready) ? 1 : 0);
    /* Upload inv VP */
    glUniformMatrix4fv(glGetUniformLocation(r->sh_light,"uInvVP"),1,GL_FALSE,r->inv_vp_mat);

    glBindVertexArray(r->fsq_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glEnable(GL_DEPTH_TEST);
}

/* bloom_tex[i] is a mip at (w>>(i+1), h>>(i+1)). Level 0 is half-res. */
static inline void bloom_mip_size(CCRenderer* r, int i, uint32_t* bw, uint32_t* bh) {
    uint32_t w = r->w >> (i+1), h = r->h >> (i+1);
    *bw = w ? w : 1; *bh = h ? h : 1;
}

/* Screen-space reflections. Runs after lighting (needs the lit HDR color),
   before bloom. Marches reflection rays in view space against the depth buffer,
   samples the lit color at hits, and composites the result back into hdr_fbo. */
static void render_ssr_pass(CCRenderer* r) {
    if (!r->postfx.ssr) return;
    CCMat4 proj; memcpy(proj.m, r->proj_mat, 64);
    CCMat4 invp = mat4_inverse(proj);
    float maxd = r->postfx.ssr_max_distance>0.0f ? r->postfx.ssr_max_distance : 20.0f;
    float inten= r->postfx.ssr_intensity>0.0f ? r->postfx.ssr_intensity : 1.0f;

    /* 1. Trace reflections into ssr_tex. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->ssr_fbo);
    glViewport(0,0,r->w,r->h);
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glUseProgram(r->ssr_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->hdr_color_tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, r->gbuf_normal_metal);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, r->gbuf_albedo_rough);
    glUniform1i(glGetUniformLocation(r->ssr_shader,"uColor"),0);
    glUniform1i(glGetUniformLocation(r->ssr_shader,"gDepth"),1);
    glUniform1i(glGetUniformLocation(r->ssr_shader,"gNormal"),2);
    glUniform1i(glGetUniformLocation(r->ssr_shader,"gAlbedoRough"),3);
    glUniformMatrix4fv(glGetUniformLocation(r->ssr_shader,"uProj"),1,GL_FALSE,r->proj_mat);
    glUniformMatrix4fv(glGetUniformLocation(r->ssr_shader,"uInvProj"),1,GL_FALSE,invp.m);
    glUniformMatrix4fv(glGetUniformLocation(r->ssr_shader,"uView"),1,GL_FALSE,r->view_mat);
    glUniform1f(glGetUniformLocation(r->ssr_shader,"uMaxDist"),maxd);
    glUniform1f(glGetUniformLocation(r->ssr_shader,"uIntensity"),inten);
    glBindVertexArray(r->fsq_vao);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);

    /* 2. Composite scene + reflections into ssr_composite_tex. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->ssr_composite_fbo);
    glViewport(0,0,r->w,r->h);
    glUseProgram(r->ssr_composite_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->hdr_color_tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->ssr_tex);
    glUniform1i(glGetUniformLocation(r->ssr_composite_shader,"uScene"),0);
    glUniform1i(glGetUniformLocation(r->ssr_composite_shader,"uSSR"),1);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);

    /* 3. Copy composited result back into hdr_fbo for bloom + postfx. */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->ssr_composite_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->hdr_fbo);
    glBlitFramebuffer(0,0,r->w,r->h, 0,0,r->w,r->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Screen-space global illumination. Runs after lighting (needs lit HDR color),
   before bloom. Gathers one indirect bounce from nearby on-screen surfaces and
   composites (albedo-modulated) back into hdr_fbo, giving indirect fill + color
   bleed. Structure mirrors render_ssr_pass. */
static void render_ssgi_pass(CCRenderer* r) {
    if (!r->postfx.ssgi) return;
    CCMat4 proj; memcpy(proj.m, r->proj_mat, 64);
    CCMat4 invp = mat4_inverse(proj);
    float radius = r->postfx.ssgi_radius>0.0f ? r->postfx.ssgi_radius : 2.0f;
    float inten  = r->postfx.ssgi_intensity>0.0f ? r->postfx.ssgi_intensity : 1.0f;

    /* 1. Gather indirect light into ssgi_tex. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->ssgi_fbo);
    glViewport(0,0,r->w,r->h);
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glUseProgram(r->ssgi_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->hdr_color_tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, r->gbuf_normal_metal);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, r->ssao_noise_tex);
    glUniform1i(glGetUniformLocation(r->ssgi_shader,"uColor"),0);
    glUniform1i(glGetUniformLocation(r->ssgi_shader,"gDepth"),1);
    glUniform1i(glGetUniformLocation(r->ssgi_shader,"gNormal"),2);
    glUniform1i(glGetUniformLocation(r->ssgi_shader,"uNoise"),3);
    glUniformMatrix4fv(glGetUniformLocation(r->ssgi_shader,"uProj"),1,GL_FALSE,r->proj_mat);
    glUniformMatrix4fv(glGetUniformLocation(r->ssgi_shader,"uInvProj"),1,GL_FALSE,invp.m);
    glUniformMatrix4fv(glGetUniformLocation(r->ssgi_shader,"uView"),1,GL_FALSE,r->view_mat);
    glUniform2f(glGetUniformLocation(r->ssgi_shader,"uNoiseScale"),(float)r->w/4.0f,(float)r->h/4.0f);
    glUniform1f(glGetUniformLocation(r->ssgi_shader,"uRadius"),radius);
    glUniform1f(glGetUniformLocation(r->ssgi_shader,"uIntensity"),inten);
    glUniform1f(glGetUniformLocation(r->ssgi_shader,"uFrame"),(float)r->taa_frame);
    glBindVertexArray(r->fsq_vao);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);

    /* 1b. Temporal accumulate: blend fresh gather (ssgi_tex) with history
       (ssgi_ghist_tex) into ssgi_accum_tex, then copy accum -> history. Over the
       multi-frame headless loop this denoises and strengthens the bounce. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->ssgi_accum_fbo);
    glViewport(0,0,r->w,r->h);
    glUseProgram(r->ssgi_accum_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->ssgi_tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->ssgi_ghist_tex);
    glUniform1i(glGetUniformLocation(r->ssgi_accum_shader,"uCurrent"),0);
    glUniform1i(glGetUniformLocation(r->ssgi_accum_shader,"uHistory"),1);
    glUniform1i(glGetUniformLocation(r->ssgi_accum_shader,"uHistoryValid"), r->ssgi_hist_valid?1:0);
    glUniform1f(glGetUniformLocation(r->ssgi_accum_shader,"uBlend"), 0.8f);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);
    /* store accum as next frame's history */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->ssgi_accum_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->ssgi_ghist_fbo);
    glBlitFramebuffer(0,0,r->w,r->h, 0,0,r->w,r->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    r->ssgi_hist_valid = true;

    /* 2. Composite scene + accumulated indirect (albedo-modulated) into ssgi_composite_tex. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->ssgi_composite_fbo);
    glViewport(0,0,r->w,r->h);
    glUseProgram(r->ssgi_composite_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->hdr_color_tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->ssgi_accum_tex);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, r->gbuf_albedo_rough);
    glUniform1i(glGetUniformLocation(r->ssgi_composite_shader,"uScene"),0);
    glUniform1i(glGetUniformLocation(r->ssgi_composite_shader,"uSSGI"),1);
    glUniform1i(glGetUniformLocation(r->ssgi_composite_shader,"gAlbedoRough"),2);
    glUniform2f(glGetUniformLocation(r->ssgi_composite_shader,"uTexel"),1.0f/(float)r->w,1.0f/(float)r->h);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);

    /* 3. Copy composited result back into hdr_fbo for bloom + postfx. */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->ssgi_composite_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->hdr_fbo);
    glBlitFramebuffer(0,0,r->w,r->h, 0,0,r->w,r->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ── Anti-aliasing system (cc/aa.h) ─────────────────────────────────────────
 * A unified resolve stage. FXAA lives in the postfx shader (uFXAA); this stage
 * adds SMAA-style edge-aware blending and the USD (Upscale-Smooth-Downscale)
 * smoothing rounds, plus SSAA/USD downsampling. It runs on post_tex after TAA.
 *
 * SMAA (here): a compact edge-aware blend — detect luma edges, then blend across
 * them weighted by local gradient, at High/Ultra thresholds. Not the full 3-pass
 * SMAA with precomputed area/search textures, but the same idea (morphological,
 * subpixel edge blending) in a single dependent-texture pass, which keeps it
 * headless-safe and self-contained.
 *
 * USD: render is upscaled (aa render-scale), then this shader SMOOTHS neighboring
 * pixels; the engine runs it `rounds` times (USD1/2/3), then the final blit
 * downsamples to display. Smoothing strength is user-set (uAmount). */
/* TAA resolve: blend the jittered current frame (post_tex) with the clamped
   history, write into post_tex, and store the result as next frame's history.
   Runs after postfx, before the 2D overlay. Over the 6-8 frames the headless
   loop renders, this converges to multi-sample supersampling. */
void cc_renderer_set_aa(CCRenderer* r, int mode, float render_scale, int usd_rounds) {
    if (!r) return;
    r->aa_mode = mode;
    r->aa_usd_rounds = usd_rounds;
    r->postfx.fxaa = false;   /* FXAA removed from the AA system */

    /* The scale is resolved by the AA state (honors USD-scale / PDAA-division
       overrides), passed in directly. OFF/ANALYTIC/CUSTOM(default) → ~1x. */
    float want = render_scale >= 1.0f ? render_scale : 1.0f;
    if (mode == 0 || mode == 1) want = 1.0f;   /* OFF / ANALYTIC: display res */
    /* Practical safety clamp: the deferred pipeline allocates several targets at
       the internal resolution, so memory grows as scale^2. The API permits an
       absurd cap, but actually allocating e.g. 8192^2 would OOM. Clamp the
       internal dimension to a safe maximum (degrade gracefully, never crash). */
    const uint32_t MAX_INTERNAL_DIM = 4096;
    float max_scale_w = (float)MAX_INTERNAL_DIM / (float)(r->disp_w ? r->disp_w : 1);
    float max_scale_h = (float)MAX_INTERNAL_DIM / (float)(r->disp_h ? r->disp_h : 1);
    float max_scale = max_scale_w < max_scale_h ? max_scale_w : max_scale_h;
    if (max_scale < 1.0f) max_scale = 1.0f;
    if (want > max_scale) want = max_scale;
    if (want != r->ss_scale) {
        r->ss_scale = want;
        uint32_t iw = (uint32_t)(r->disp_w * want + 0.5f);
        uint32_t ih = (uint32_t)(r->disp_h * want + 0.5f);
        if (iw < 1) iw = 1; if (ih < 1) ih = 1;
        cc_renderer_resize_internal(r, iw, ih);   /* rebuild 3D chain at scaled res */
    }
}

/* ensure the AA scratch target exists at post_tex size */
static void aa_ensure_target(CCRenderer* r) {
    if (r->aa_fbo) return;
    glGenFramebuffers(1,&r->aa_fbo);
    glGenTextures(1,&r->aa_tex);
    glBindTexture(GL_TEXTURE_2D,r->aa_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,r->w,r->h,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER,r->aa_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->aa_tex,0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}

/* run one full-screen shader pass: src_tex -> dst_fbo, using `shader` */
static void aa_blit_shader(CCRenderer* r, GLuint shader, GLuint src_tex, GLuint dst_fbo,
                           float amount, float threshold, float strength) {
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(0,0,r->w,r->h);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glUseProgram(shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src_tex);
    glUniform1i(glGetUniformLocation(shader,"uTex"),0);
    glUniform2f(glGetUniformLocation(shader,"uTexel"),1.0f/(float)r->w,1.0f/(float)r->h);
    GLint la=glGetUniformLocation(shader,"uAmount");    if(la>=0) glUniform1f(la,amount);
    GLint lt=glGetUniformLocation(shader,"uThreshold"); if(lt>=0) glUniform1f(lt,threshold);
    GLint ls=glGetUniformLocation(shader,"uStrength");  if(ls>=0) glUniform1f(ls,strength);
    GLint lse=glGetUniformLocation(shader,"uSearch");   if(lse>=0) glUniform1f(lse,strength);
    glBindVertexArray(r->fsq_vao);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}

/* AA resolve stage: with the density-based design, ALL anti-aliasing is pure
 * supersampling — the scene is rendered at higher internal resolution and the
 * box-downsample in the screenshot path averages every sub-sample. There is no
 * per-frame post resolve here anymore (FXAA/SMAA removed, USD smoothing removed).
 * Kept as a no-op hook in case a future mode needs a display-res pass. */
static void render_aa_pass(CCRenderer* r) {
    /* ANALYTIC (mode 1): the "pixel is a diagonal" resolve — run the analytic
       coverage shader on post_tex at display resolution. No supersample. */
    if (r->aa_mode == 1) {
        aa_ensure_target(r);
        glBindFramebuffer(GL_FRAMEBUFFER, r->aa_fbo);
        glViewport(0,0,r->w,r->h);
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
        glUseProgram(r->aa_analytic_shader);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->post_tex);
        glUniform1i(glGetUniformLocation(r->aa_analytic_shader,"uTex"),0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
        glUniform1i(glGetUniformLocation(r->aa_analytic_shader,"uDepth"),1);
        glUniform2f(glGetUniformLocation(r->aa_analytic_shader,"uTexel"),
                    1.0f/(float)r->w, 1.0f/(float)r->h);
        glUniform1f(glGetUniformLocation(r->aa_analytic_shader,"uNear"), r->camera.near_plane);
        glUniform1f(glGetUniformLocation(r->aa_analytic_shader,"uFar"),  r->camera.far_plane);
        glBindVertexArray(r->fsq_vao);
        glDrawArrays(GL_TRIANGLE_FAN,0,4);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        /* copy resolved image back into post_tex */
        glBindFramebuffer(GL_READ_FRAMEBUFFER, r->aa_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->post_fbo);
        glBlitFramebuffer(0,0,r->w,r->h, 0,0,r->w,r->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
    }
    /* SSAA/USD/PDAA resolve happens in the screenshot downsample (box for
       SSAA/USD averaging; nearest for PDAA hard steps). Nothing to do here. */
}


static void render_taa_pass(CCRenderer* r) {
    if (!r->postfx.taa) return;
    float blend = r->postfx.taa_blend>0.0f ? r->postfx.taa_blend : 0.9f;
    if (blend > 0.97f) blend = 0.97f;
    /* 1. resolve: current (post_tex) + history -> taa_resolve_tex */
    glBindFramebuffer(GL_FRAMEBUFFER, r->taa_resolve_fbo);
    glViewport(0,0,r->w,r->h);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glUseProgram(r->taa_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->post_tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->taa_history_tex);
    glUniform1i(glGetUniformLocation(r->taa_shader,"uCurrent"),0);
    glUniform1i(glGetUniformLocation(r->taa_shader,"uHistory"),1);
    glUniform2f(glGetUniformLocation(r->taa_shader,"uTexel"),1.0f/(float)r->w,1.0f/(float)r->h);
    glUniform1f(glGetUniformLocation(r->taa_shader,"uBlend"),blend);
    glUniform1i(glGetUniformLocation(r->taa_shader,"uHistoryValid"), r->taa_history_valid?1:0);
    glBindVertexArray(r->fsq_vao);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);
    /* 2. copy resolve back into post_tex (the frame's canonical output) */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->taa_resolve_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->post_fbo);
    glBlitFramebuffer(0,0,r->w,r->h, 0,0,r->w,r->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    /* 3. store resolve as next frame's history */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->taa_resolve_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->taa_history_fbo);
    glBlitFramebuffer(0,0,r->w,r->h, 0,0,r->w,r->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    r->taa_history_valid = true;
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Depth of field: gather blur on post_tex driven by CoC from gbuf_depth.
   Runs after TAA (blurs the resolved image) and before the 2D overlay (UI
   stays sharp). Writes dof_tex, then blits back into post_tex. */
static void render_dof_pass(CCRenderer* r) {
    if (!r->postfx.dof) return;
    float focus = r->postfx.dof_focus_dist  > 0 ? r->postfx.dof_focus_dist  : 8.0f;
    float range = r->postfx.dof_focus_range > 0 ? r->postfx.dof_focus_range : 3.0f;
    float maxb  = r->postfx.dof_max_blur    > 0 ? r->postfx.dof_max_blur    : 6.0f;
    glBindFramebuffer(GL_FRAMEBUFFER, r->dof_fbo);
    glViewport(0,0,r->w,r->h);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glUseProgram(r->dof_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->post_tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
    glUniform1i(glGetUniformLocation(r->dof_shader,"uColor"),0);
    glUniform1i(glGetUniformLocation(r->dof_shader,"uDepth"),1);
    glUniform2f(glGetUniformLocation(r->dof_shader,"uTexel"),1.0f/(float)r->w,1.0f/(float)r->h);
    glUniform1f(glGetUniformLocation(r->dof_shader,"uNear"), r->camera.near_plane>0?r->camera.near_plane:0.1f);
    glUniform1f(glGetUniformLocation(r->dof_shader,"uFar"),  r->camera.far_plane>0?r->camera.far_plane:1000.0f);
    glUniform1f(glGetUniformLocation(r->dof_shader,"uFocusDist"),  focus);
    glUniform1f(glGetUniformLocation(r->dof_shader,"uFocusRange"), range);
    glUniform1f(glGetUniformLocation(r->dof_shader,"uMaxBlur"),    maxb);
    glBindVertexArray(r->fsq_vao);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);
    /* copy dof result back into post_tex (the frame's canonical output) */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->dof_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->post_fbo);
    glBlitFramebuffer(0,0,r->w,r->h, 0,0,r->w,r->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}

static void render_bloom_pass(CCRenderer* r) {
    if (!r->postfx.bloom) return;
    glBindVertexArray(r->fsq_vao);
    glDisable(GL_BLEND);

    /* ── Downsample chain: HDR → mip0 → mip1 → … ─────────────────────────
     * Prefilter (soft-knee threshold) is applied ONCE, on the first pass,
     * so brightness selection happens a single time and later mips are pure
     * blur. Texel size is that of the SOURCE being sampled. */
    glUseProgram(r->sh_bloom_down);
    GLint dSrc  = glGetUniformLocation(r->sh_bloom_down,"uSrc");
    GLint dTex  = glGetUniformLocation(r->sh_bloom_down,"uTexel");
    GLint dPre  = glGetUniformLocation(r->sh_bloom_down,"uPrefilter");
    GLint dThr  = glGetUniformLocation(r->sh_bloom_down,"uThreshold");
    GLint dKnee = glGetUniformLocation(r->sh_bloom_down,"uSoftKnee");
    glUniform1f(dThr,  r->postfx.bloom_threshold);
    glUniform1f(dKnee, 0.5f);   /* half-width soft knee: smooth, no flicker */
    for (int i = 0; i < CC_BLOOM_LEVELS; i++) {
        uint32_t bw, bh; bloom_mip_size(r, i, &bw, &bh);
        glBindFramebuffer(GL_FRAMEBUFFER, r->bloom_fbo[i]);
        glViewport(0,0,bw,bh);
        GLuint src; uint32_t sw, sh;
        if (i == 0) { src = r->hdr_color_tex; sw = r->w; sh = r->h; }
        else        { src = r->bloom_tex[i-1]; bloom_mip_size(r, i-1, &sw, &sh); }
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src);
        glUniform1i(dSrc, 0);
        glUniform2f(dTex, 1.0f/(float)sw, 1.0f/(float)sh);
        glUniform1i(dPre, i == 0 ? 1 : 0);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    /* ── Upsample chain: additively fold each mip into the next-larger one.
     * Additive ONE:ONE blend accumulates the multi-scale glow. Intensity is
     * NOT applied here — it is applied once at the final composite into the
     * HDR buffer, so radius and strength stay independent. */
    glUseProgram(r->sh_bloom_up);
    GLint uSrc = glGetUniformLocation(r->sh_bloom_up,"uSrc");
    GLint uTex = glGetUniformLocation(r->sh_bloom_up,"uTexel");
    GLint uInt = glGetUniformLocation(r->sh_bloom_up,"uIntensity");
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
    glUniform1f(uInt, 1.0f);
    for (int i = CC_BLOOM_LEVELS-1; i > 0; i--) {
        uint32_t sw, sh; bloom_mip_size(r, i, &sw, &sh);       /* source mip */
        uint32_t dw, dh; bloom_mip_size(r, i-1, &dw, &dh);     /* dest mip   */
        glBindFramebuffer(GL_FRAMEBUFFER, r->bloom_fbo[i-1]);
        glViewport(0,0,dw,dh);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->bloom_tex[i]);
        glUniform1i(uSrc, 0);
        glUniform2f(uTex, 1.0f/(float)sw, 1.0f/(float)sh);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
    glDisable(GL_BLEND);
    /* Final glow now lives in bloom_tex[0]; POSTFX composites it with
       uBloom * intensity applied there. Intensity passed via postfx pass. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void render_postfx_pass(CCRenderer* r) {
    glBindFramebuffer(GL_FRAMEBUFFER, r->post_fbo);
    glViewport(0,0,r->w,r->h);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    /* ── Auto-exposure: reduce the HDR frame to its average luminance via the
       mip chain, adapt toward it, and derive an exposure that lands the scene
       at the middle-grey key. Falls back to manual camera.exposure when off. */
    float exposure = r->camera.exposure;
    if (r->postfx.auto_exposure) {
        glBindTexture(GL_TEXTURE_2D, r->hdr_color_tex);
        glGenerateMipmap(GL_TEXTURE_2D);
        int maxlev = 0; { int ww=r->w>r->h?r->w:r->h; while(ww>1){ww>>=1;maxlev++;} }
        float texel[4]={0.18f,0.18f,0.18f,1.0f};
        glGetTexImage(GL_TEXTURE_2D, maxlev, GL_RGBA, GL_FLOAT, texel);
        float measured = 0.2126f*texel[0] + 0.7152f*texel[1] + 0.0722f*texel[2];
        float aemin = r->postfx.ae_min>0?r->postfx.ae_min:0.03f;
        float aemax = r->postfx.ae_max>0?r->postfx.ae_max:8.0f;
        measured = measured<aemin?aemin:(measured>aemax?aemax:measured);
        if (r->ae_adapted_lum <= 0.0f) r->ae_adapted_lum = measured; /* first frame: snap */
        else {
            float speed = r->postfx.ae_speed>0?r->postfx.ae_speed:3.0f;
            float dt = 1.0f/60.0f;                 /* headless: fixed step */
            float k = 1.0f - expf(-speed*dt);
            r->ae_adapted_lum += (measured - r->ae_adapted_lum) * k;
        }
        float key = r->postfx.ae_key>0?r->postfx.ae_key:0.18f;
        float autoExp = key / (r->ae_adapted_lum>1e-4f ? r->ae_adapted_lum : 1e-4f);
        exposure = autoExp * (r->camera.exposure>0?r->camera.exposure:1.0f); /* manual bias on top */
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glUseProgram(r->sh_postfx);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->hdr_color_tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,
        r->postfx.bloom ? r->bloom_tex[0] : r->black_tex);
    glUniform1i(glGetUniformLocation(r->sh_postfx,"uHDR"),  0);
    glUniform1i(glGetUniformLocation(r->sh_postfx,"uBloom"),1);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uBloomIntensity"), r->postfx.bloom ? r->postfx.bloom_intensity : 0.0f);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uExposure"),    exposure);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uGamma"),       r->postfx.gamma);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uSaturation"),  r->postfx.saturation);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uContrast"),    r->postfx.contrast);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uVignette"),    r->postfx.vignette ? r->postfx.vignette_strength : 0.0f);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uGrain"),       r->postfx.film_grain ? r->postfx.grain_strength : 0.0f);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uScanline"),    r->postfx.scanlines ? r->postfx.scanline_strength : 0.0f);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uTime"),        (float)r->screenshot_count + (float)(r->stats.draw_calls));
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uCAStrength"),  r->postfx.chromatic_aberration ? r->postfx.ca_strength : 0.0f);
    glUniform1i(glGetUniformLocation(r->sh_postfx,"uFXAA"),        r->postfx.fxaa ? 1 : 0);
    glUniform2f(glGetUniformLocation(r->sh_postfx,"uTexelSize"),   1.0f/r->w, 1.0f/r->h);
    /* Outline: sample gbuffer depth + normal for edge detection. */
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, r->gbuf_normal_metal);
    glUniform1i(glGetUniformLocation(r->sh_postfx,"uOutlineDepth"), 2);
    glUniform1i(glGetUniformLocation(r->sh_postfx,"uOutlineNormal"), 3);
    glUniform1i(glGetUniformLocation(r->sh_postfx,"uOutlineOn"), r->postfx.outline ? 1 : 0);
    glUniform3f(glGetUniformLocation(r->sh_postfx,"uOutlineColor"),
                r->postfx.outline_color[0], r->postfx.outline_color[1], r->postfx.outline_color[2]);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uOutlineThickness"), r->postfx.outline_thickness>0.0f?r->postfx.outline_thickness:1.0f);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uOutlineDepthSens"), r->postfx.outline_depth_sensitivity>0.0f?r->postfx.outline_depth_sensitivity:1.0f);
    glUniform1f(glGetUniformLocation(r->sh_postfx,"uOutlineNormalSens"), r->postfx.outline_normal_sensitivity>0.0f?r->postfx.outline_normal_sensitivity:1.0f);
    glBindVertexArray(r->fsq_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glEnable(GL_DEPTH_TEST);
}

/* Execute all recorded 2D batches, once, after postfx. Preserves submission
   order (painter's algorithm) so overlapping UI layers composite correctly. */
static void render_2d_flush(CCRenderer* r) {
    if (!r->batch2d_count || !r->sprite_nv) { r->batch2d_count = 0; r->sprite_nv = 0; return; }
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindFramebuffer(GL_FRAMEBUFFER, r->post_fbo);
    glViewport(0,0,r->w,r->h);
    glUseProgram(r->sh_sprite);
    /* 2D coordinates are DISPLAY-space pixels (0..disp_w, 0..disp_h). The post
       FBO / viewport are at the (possibly supersampled) INTERNAL size r->w×r->h.
       uResolution must therefore be the DISPLAY size, not r->w: dividing a display
       coordinate by the display size yields 0..1 → full NDC → fills the whole
       internal viewport, which then downsamples to the display image correctly.
       Using r->w here (the old bug) crammed all 2D into the top-left 1/ss_scale
       corner whenever supersampling AA (e.g. the default PDAA1 = 4×) was active. */
    float ui_res_w = (float)(r->disp_w ? r->disp_w : r->w);
    float ui_res_h = (float)(r->disp_h ? r->disp_h : r->h);
    glUniform2f(glGetUniformLocation(r->sh_sprite,"uResolution"), ui_res_w, ui_res_h);
    GLint locFont = glGetUniformLocation(r->sh_sprite,"uFontMode");
    GLint locUse  = glGetUniformLocation(r->sh_sprite,"uUseTex");
    GLint locTex  = glGetUniformLocation(r->sh_sprite,"uTex");
    /* Upload all frame verts once */
    glBindVertexArray(r->sprite_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->sprite_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, r->sprite_nv * sizeof(SpriteVert), r->sprite_verts);
    for (uint32_t i = 0; i < r->batch2d_count; i++) {
        struct CC2DBatch* b = &r->batches2d[i];
        if (!b->count) continue;
        glUniform1i(locFont, b->font_mode ? 1 : 0);
        if (b->use_tex || b->font_mode) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, b->tex);
            glUniform1i(locTex, 0);
            glUniform1i(locUse, 1);
        } else {
            glUniform1i(locUse, 0);
        }
        /* index range: each 4 verts → 6 indices, starting at (start/4)*6 */
        uint32_t first_index = (b->start / 4) * 6;
        uint32_t index_count = (b->count / 4) * 6;
        glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT,
                       (void*)(uintptr_t)(first_index * sizeof(uint32_t)));
    }
    r->batch2d_count = 0;
    r->sprite_nv = 0;
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void cc_renderer_frame_end(CCRenderer* r) {
    r->stats_prev = r->stats;   /* commit this frame's counters (readable after end) */
    if (r->backend == CC_RENDERER_NULL) return;

    /* If no 3D geometry was submitted this frame, this is a PURE-2D frame (e.g. a
       Tetris/menu). Running the whole deferred lighting + post chain (lighting,
       bloom, SSGI, SSR, TAA, DOF) over an EMPTY G-buffer is pure waste AND a source
       of per-frame variation (temporal passes reading uninitialized/last-frame
       state) that can flicker. So skip straight to a clean post_fbo + the 2D pass. */
    int has_3d = (r->stats.meshes_drawn > 0);
    if (!has_3d) {
        glBindFramebuffer(GL_FRAMEBUFFER, r->post_fbo);
        glViewport(0, 0, r->w, r->h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    } else {
        /* Lighting + post */
        if (r->sky_enabled && r->ibl_dirty) regenerate_ibl(r);  /* lazy IBL bake */
        render_shadow_pass(r);     /* directional shadow map from caster geometry */
        render_ssao_pass(r);       /* screen-space AO into ssao_blur_tex */
        render_decal_pass(r);      /* stamp queued decals into the G-buffer */
        render_lighting_pass(r);
        render_ssgi_pass(r);       /* screen-space GI (indirect + color bleed) before bloom */
        render_ssr_pass(r);        /* screen-space reflections into hdr before bloom */
        render_bloom_pass(r);
        render_postfx_pass(r);
        render_taa_pass(r);        /* temporal AA resolve on the final image */
        render_aa_pass(r);         /* spatial AA resolve (SMAA / USD) */
        render_dof_pass(r);        /* camera focus blur on the resolved image */
        r->taa_frame++;            /* advance jitter sequence for next frame */
    }

    /* 2D on top */
    render_2d_flush(r);

    /* Swap / finish */
#ifdef CC_USE_GLFW
    if (r->window) {
        /* The final image lives in post_fbo (offscreen) at the internal size
           r->w×r->h (display × the AA supersample factor). Blit it to the window's
           default framebuffer, scaled to fill the window.

           CRITICAL: use the REAL framebuffer size from GLFW, not r->disp_w/disp_h.
           disp_w/disp_h are set once at init (the requested size) and the engine
           has no resize callback, so they go stale the moment the window is resized
           or FULLSCREENED. Blitting to a stale (smaller) size drew the image into a
           tiny bottom-left patch of a large fullscreen window. Querying the live
           framebuffer size makes resize + fullscreen + HiDPI all correct, and lets
           the clear cover the whole window (no uncovered pixels → no flicker). */
        int win_w = 0, win_h = 0;
        glfwGetFramebufferSize(r->window, &win_w, &win_h);
        if (win_w <= 0 || win_h <= 0) {   /* minimized / not yet mapped */
            win_w = (int)(r->disp_w ? r->disp_w : r->w);
            win_h = (int)(r->disp_h ? r->disp_h : r->h);
        }
        /* Preserve the render aspect ratio (letterbox), or the image STRETCHES when
           the window aspect != render aspect (e.g. a 520×560 game on a 16:9 monitor
           in fullscreen looked stretched). Compute the largest centered rect inside
           the window that matches the source aspect; clear the rest to black. */
        float src_aspect = (float)r->w / (float)r->h;
        float win_aspect = (float)win_w / (float)win_h;
        int dst_w, dst_h, dst_x, dst_y;
        if (win_aspect > src_aspect) {       /* window wider → pillarbox (bars L/R) */
            dst_h = win_h;
            dst_w = (int)(win_h * src_aspect + 0.5f);
        } else {                             /* window taller → letterbox (bars T/B) */
            dst_w = win_w;
            dst_h = (int)(win_w / src_aspect + 0.5f);
        }
        dst_x = (win_w - dst_w) / 2;
        dst_y = (win_h - dst_h) / 2;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, win_w, win_h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);        /* black bars */
        glBindFramebuffer(GL_READ_FRAMEBUFFER, r->post_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, (GLint)r->w, (GLint)r->h,
                          dst_x, dst_y, dst_x + dst_w, dst_y + dst_h,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glfwSwapBuffers(r->window);
        glfwPollEvents();
        return;
    }
#endif
#ifdef CC_USE_OSMESA
    glFinish();
#endif
}

/* ══════════════════════════════════════════════════════════════════════
   SCREENSHOT
   ══════════════════════════════════════════════════════════════════════ */

/* ── GLFW windowed input ─────────────────────────────────────────────────
   qwerty's backends (evdev/X11) don't see events delivered to a GLFW window,
   so when windowed the engine reads input straight from GLFW here. */
/* Wire windowed key events → qwerty queue. Call once after the window + qwerty
   context exist. No-op if not windowed. */
#ifdef CC_USE_GLFW
static void cc_glfw_key_cb(GLFWwindow* win, int key, int scancode, int action, int mods);
#endif
#ifdef CC_USE_GLFW
/* When the window changes size (e.g. going FULLSCREEN on a high-res monitor), the
   render resolution must follow it — otherwise the small fixed-size render gets
   stretched up to fill the screen and looks blocky/pixelated. This callback
   re-renders at the true framebuffer size. Without it, an 800x500 game on a 1440p
   display rendered at 800x500 and upscaled ~3x → heavy pixelation. */
static CCRenderer* g_resize_renderer = NULL;
void cc_renderer_resize(CCRenderer* r, uint32_t w, uint32_t h);   /* defined later in file */
static void cc_glfw_fbsize_cb(GLFWwindow* win, int w, int h) {
    (void)win;
    if (g_resize_renderer && w > 0 && h > 0)
        cc_renderer_resize(g_resize_renderer, (uint32_t)w, (uint32_t)h);
}
#endif

void cc_renderer_wire_input(CCRenderer* r, void* qctx) {
#ifdef CC_USE_GLFW
    if (!r || !r->window || !qctx) return;
    glfwSetWindowUserPointer(r->window, qctx);
    glfwSetKeyCallback(r->window, cc_glfw_key_cb);
    /* render-resolution-follows-window (fixes fullscreen pixelation) */
    g_resize_renderer = r;
    glfwSetFramebufferSizeCallback(r->window, cc_glfw_fbsize_cb);
    /* apply the CURRENT framebuffer size once now, in case it already differs from
       the requested window size (e.g. HiDPI scaling). */
    { int fw=0, fh=0; glfwGetFramebufferSize(r->window, &fw, &fh);
      if (fw>0 && fh>0) cc_renderer_resize(r, (uint32_t)fw, (uint32_t)fh); }
#else
    (void)r;(void)qctx;
#endif
}

int cc_renderer_has_window(CCRenderer* r) {
#ifdef CC_USE_GLFW
    return (r && r->window) ? 1 : 0;
#else
    (void)r; return 0;
#endif
}

#ifdef CC_USE_GLFW
static int glfw_from_qkey(int qk) {
    if (qk >= QKEY_A && qk <= QKEY_Z) return GLFW_KEY_A + (qk - QKEY_A);
    switch (qk) {
        case QKEY_SPACE:  return GLFW_KEY_SPACE;
        case QKEY_ESCAPE: return GLFW_KEY_ESCAPE;
        case QKEY_ENTER:  return GLFW_KEY_ENTER;
        case QKEY_TAB:    return GLFW_KEY_TAB;
        case QKEY_LSHIFT: return GLFW_KEY_LEFT_SHIFT;
        case QKEY_RSHIFT: return GLFW_KEY_RIGHT_SHIFT;
        case QKEY_LCTRL:  return GLFW_KEY_LEFT_CONTROL;
        case QKEY_RCTRL:  return GLFW_KEY_RIGHT_CONTROL;
        case QKEY_UP:     return GLFW_KEY_UP;
        case QKEY_DOWN:   return GLFW_KEY_DOWN;
        case QKEY_LEFT:   return GLFW_KEY_LEFT;
        case QKEY_RIGHT:  return GLFW_KEY_RIGHT;
        default:          return -1;
    }
}

/* Inverse map: GLFW key → QKey, so window key events can feed qwerty's queue. */
static int qkey_from_glfw(int gk) {
    if (gk >= GLFW_KEY_A && gk <= GLFW_KEY_Z) return QKEY_A + (gk - GLFW_KEY_A);
    switch (gk) {
        case GLFW_KEY_SPACE:         return QKEY_SPACE;
        case GLFW_KEY_ESCAPE:        return QKEY_ESCAPE;
        case GLFW_KEY_ENTER:         return QKEY_ENTER;
        case GLFW_KEY_TAB:           return QKEY_TAB;
        case GLFW_KEY_LEFT_SHIFT:    return QKEY_LSHIFT;
        case GLFW_KEY_RIGHT_SHIFT:   return QKEY_RSHIFT;
        case GLFW_KEY_LEFT_CONTROL:  return QKEY_LCTRL;
        case GLFW_KEY_RIGHT_CONTROL: return QKEY_RCTRL;
        case GLFW_KEY_UP:            return QKEY_UP;
        case GLFW_KEY_DOWN:          return QKEY_DOWN;
        case GLFW_KEY_LEFT:          return QKEY_LEFT;
        case GLFW_KEY_RIGHT:         return QKEY_RIGHT;
        default:                     return -1;
    }
}

/* EVENT-DRIVEN INPUT: GLFW calls this the instant a key transitions. We translate
   to a QKey and dispatch into qwerty's ring/queue + mirrored state (qwerty_dispatch
   does both). This replaces the old per-frame glfwGetKey scan of the WHOLE keyboard
   every frame — now the per-frame cost is proportional to keys that actually
   changed (usually zero), which is the point of an event-driven path. GLFW key
   repeats are ignored (games derive their own repeat from held state). */
static void cc_glfw_key_cb(GLFWwindow* win, int key, int scancode, int action, int mods) {
    (void)scancode;(void)mods;
    if (action == GLFW_REPEAT) return;
    QContext* q = (QContext*)glfwGetWindowUserPointer(win);
    if (!q) return;
    int qk = qkey_from_glfw(key);
    if (qk < 0) return;
    QEvent ev = { .type = (action==GLFW_PRESS)?QEVENT_KEY_DOWN:QEVENT_KEY_UP };
    ev.key.key = (QKey)qk;
    qwerty_dispatch(q, &ev);
}
#endif



int cc_renderer_glfw_key(CCRenderer* r, int qkey) {
#ifdef CC_USE_GLFW
    if (!r || !r->window) return 0;
    int gk = glfw_from_qkey(qkey);
    if (gk < 0) return 0;
    return glfwGetKey(r->window, gk) == GLFW_PRESS ? 1 : 0;
#else
    (void)r; (void)qkey; return 0;
#endif
}

int cc_renderer_glfw_mouse_btn(CCRenderer* r, int qbtn) {
#ifdef CC_USE_GLFW
    if (!r || !r->window) return 0;
    int gb = (qbtn == 1) ? GLFW_MOUSE_BUTTON_RIGHT
           : (qbtn == 2) ? GLFW_MOUSE_BUTTON_MIDDLE
                         : GLFW_MOUSE_BUTTON_LEFT;
    return glfwGetMouseButton(r->window, gb) == GLFW_PRESS ? 1 : 0;
#else
    (void)r; (void)qbtn; return 0;
#endif
}

void cc_renderer_glfw_cursor(CCRenderer* r, double* x, double* y) {
#ifdef CC_USE_GLFW
    if (r && r->window) { glfwGetCursorPos(r->window, x, y); return; }
#endif
    if (x) *x = 0; if (y) *y = 0;
}

void cc_renderer_glfw_capture_cursor(CCRenderer* r, int capture) {
#ifdef CC_USE_GLFW
    if (r && r->window)
        glfwSetInputMode(r->window, GLFW_CURSOR,
                         capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
#else
    (void)r; (void)capture;
#endif
}

const char* cc_renderer_screenshot(CCRenderer* r, const char* path) {
    static char auto_path[512];
    if (!path) {
        snprintf(auto_path, sizeof(auto_path), "%s/frame_%06llu.png",
                 r->screenshot_dir, (unsigned long long)r->screenshot_count++);
        path = auto_path;
    }
    if (r->backend == CC_RENDERER_NULL) {
        fprintf(stderr, "[cc:renderer] WARNING: screenshot requested but renderer is NULL "
                        "(no GLFW window and no OSMesa). No image written. For headless "
                        "screenshots, build with OSMesa (cc dev) — a windowed-only build "
                        "run without a display has no framebuffer to capture.\n");
        return path;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    /* Read pixels from the post-process FBO (final composited image). When
       supersampling (SSAA/USD), post_fbo is at the supersampled render size;
       downsample it to display resolution with a LINEAR blit — this averaging IS
       the SSAA resolve — then read the display-res result. */
    glFinish();
    uint32_t out_w = r->disp_w ? r->disp_w : r->w;
    uint32_t out_h = r->disp_h ? r->disp_h : r->h;
    GLuint read_fbo = r->post_fbo;
    if (r->w != out_w || r->h != out_h) {
        /* ensure a display-res downsample target exists */
        if (!r->ss_down_fbo) {
            glGenFramebuffers(1,&r->ss_down_fbo);
            glGenTextures(1,&r->ss_down_tex);
            glBindTexture(GL_TEXTURE_2D,r->ss_down_tex);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,out_w,out_h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER,r->ss_down_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->ss_down_tex,0);
        } else {
            /* resize if display size changed */
            glBindTexture(GL_TEXTURE_2D,r->ss_down_tex);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,out_w,out_h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        }
        /* PDAA (modes 2/3/4 = PDAA1/2/3): hard nearest-point downsample — each
           output pixel takes ONE sample from the finer grid (black OR white, no
           gray). Finer/more accurate staircase, never blended. All other
           supersampled modes (SSAA/USD) box-average every sub-sample (smooth). */
        if (r->aa_mode >= 2 && r->aa_mode <= 4) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, r->post_fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->ss_down_fbo);
            glBlitFramebuffer(0,0,(GLint)r->w,(GLint)r->h, 0,0,(GLint)out_w,(GLint)out_h,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            read_fbo = r->ss_down_fbo;
        } else {
            int N = (int)(r->ss_scale + 0.999f); if (N < 2) N = 2;
            glBindFramebuffer(GL_FRAMEBUFFER, r->ss_down_fbo);
            glViewport(0,0,out_w,out_h);
            glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
            glUseProgram(r->ss_down_shader);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->post_tex);
            glUniform1i(glGetUniformLocation(r->ss_down_shader,"uTex"),0);
            glUniform2f(glGetUniformLocation(r->ss_down_shader,"uSrcTexel"),
                        1.0f/(float)r->w, 1.0f/(float)r->h);
            glUniform1i(glGetUniformLocation(r->ss_down_shader,"uN"), N);
            glBindVertexArray(r->fsq_vao);
            glDrawArrays(GL_TRIANGLE_FAN,0,4);
            glBindVertexArray(0);
            glBindFramebuffer(GL_FRAMEBUFFER,0);
            read_fbo = r->ss_down_fbo;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, read_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    uint8_t* readbuf = malloc((size_t)out_w * out_h * 4);
    glReadPixels(0, 0, (GLsizei)out_w, (GLsizei)out_h, GL_RGBA, GL_UNSIGNED_BYTE, readbuf);
    /* GL is bottom-up — flip for PNG (top-down) */
    uint8_t* flipbuf = malloc((size_t)out_w * out_h * 4);
    for (uint32_t row = 0; row < out_h; row++) {
        memcpy(flipbuf + row * out_w * 4,
               readbuf + (out_h - 1 - row) * out_w * 4,
               out_w * 4);
    }
    free(readbuf);
    stbi_write_png(path, (int)out_w, (int)out_h, 4, flipbuf, (int)(out_w * 4));
    free(flipbuf);
    fprintf(stderr, "[cc:renderer] Screenshot: %s\n", path);
    return path;
}

/* ══════════════════════════════════════════════════════════════════════
   2D DRAWING
   ══════════════════════════════════════════════════════════════════════ */

/* Open a new 2D batch or extend the current one if state matches. Returns
   the SpriteVert* to write `n` verts into, or NULL if the frame buffer is full. */
static SpriteVert* batch2d_alloc(CCRenderer* r, GLuint tex, bool use_tex, bool font_mode, uint32_t n) {
    if (r->sprite_nv + n > CC_MAX_2D_VERTS) return NULL;
    struct CC2DBatch* b = r->batch2d_count ? &r->batches2d[r->batch2d_count-1] : NULL;
    if (!b || b->tex != tex || b->use_tex != use_tex || b->font_mode != font_mode) {
        if (r->batch2d_count >= 4096) return NULL;
        b = &r->batches2d[r->batch2d_count++];
        b->tex = tex; b->use_tex = use_tex; b->font_mode = font_mode;
        b->start = r->sprite_nv; b->count = 0;
    }
    SpriteVert* v = r->sprite_verts + r->sprite_nv;
    r->sprite_nv += n;
    b->count += n;
    return v;
}

static void push_quad(CCRenderer* r, float x, float y, float w2, float h,
                       float u0, float v0, float u1, float v1,
                       uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                       GLuint tex, bool use_tex) {
    SpriteVert* v = batch2d_alloc(r, tex, use_tex, false, 4);
    if (!v) return;
    v[0] = (SpriteVert){x,   y,   u0,v0, cr,cg,cb,ca};
    v[1] = (SpriteVert){x+w2,y,   u1,v0, cr,cg,cb,ca};
    v[2] = (SpriteVert){x+w2,y+h, u1,v1, cr,cg,cb,ca};
    v[3] = (SpriteVert){x,   y+h, u0,v1, cr,cg,cb,ca};
}

static void push_quad_rot(CCRenderer* r, float x, float y, float w2, float h,
                          float u0, float v0, float u1, float v1,
                          uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                          GLuint tex, bool use_tex, float angle_deg) {
    SpriteVert* v = batch2d_alloc(r, tex, use_tex, false, 4);
    if (!v) return;
    float cxp=x+w2*0.5f, cyp=y+h*0.5f;         /* rotate about sprite center */
    float s=sinf(angle_deg*3.14159265f/180.0f), c=cosf(angle_deg*3.14159265f/180.0f);
    float cornx[4]={x,x+w2,x+w2,x}, corny[4]={y,y,y+h,y+h};
    float uu[4]={u0,u1,u1,u0}, vv[4]={v0,v0,v1,v1};
    for(int i=0;i<4;i++){
        float dx=cornx[i]-cxp, dy=corny[i]-cyp;
        v[i]=(SpriteVert){ cxp+dx*c-dy*s, cyp+dx*s+dy*c, uu[i],vv[i], cr,cg,cb,ca };
    }
}

void cc_renderer_draw_sprite(CCRenderer* r, uint32_t tex_id,
                              float x, float y, float w2, float h,
                              float u0, float v0, float u1, float v1,
                              float angle_deg, uint32_t tint) {
    if (r->backend == CC_RENDERER_NULL) return;
    GLuint gl_tex = (tex_id && tex_id < CC_MAX_TEXTURES && r->textures[tex_id].valid)
                    ? r->textures[tex_id].id : r->white_tex;
    uint8_t cr=(tint>>24)&0xff, cg=(tint>>16)&0xff, cb=(tint>>8)&0xff, ca=tint&0xff;
    if (angle_deg != 0.0f)
        push_quad_rot(r, x, y, w2, h, u0, v0, u1, v1, cr, cg, cb, ca, gl_tex, true, angle_deg);
    else
        push_quad(r, x, y, w2, h, u0, v0, u1, v1, cr, cg, cb, ca, gl_tex, true);
}

void cc_renderer_draw_rect(CCRenderer* r, float x, float y, float w2, float h, uint32_t col) {
    if (r->backend == CC_RENDERER_NULL) return;
    uint8_t cr=(col>>24)&0xff,cg=(col>>16)&0xff,cb=(col>>8)&0xff,ca=col&0xff;
    push_quad(r, x, y, w2, h, 0,0,1,1, cr,cg,cb,ca, r->white_tex, false);
}

/* ══════════════════════════════════════════════════════════════════════
   TEXTURE MANAGEMENT
   ══════════════════════════════════════════════════════════════════════ */

static CCTexture alloc_tex_slot(CCRenderer* r) {
    for (uint32_t i = 1; i < CC_MAX_TEXTURES; i++)
        if (!r->textures[i].valid) return i;
    return CC_NULL;
}

CCTexture cc_renderer_texture_load(CCRenderer* r, const char* path) {
    int w, h, c; uint8_t* px = stbi_load(path, &w, &h, &c, 4);
    if (!px) { fprintf(stderr,"[cc] Texture load fail: %s\n", path); return CC_NULL; }
    CCTextureDesc d = {.width=(uint32_t)w,.height=(uint32_t)h,.format=CC_FMT_RGBA8,.mipmaps=true,.linear_filter=true,.wrap_repeat=true};
    CCTexture t = cc_renderer_texture_create(r, &d, px);
    stbi_image_free(px);
    return t;
}

CCTexture cc_renderer_texture_load_srgb(CCRenderer* r, const char* path) {
    int w, h, c; uint8_t* px = stbi_load(path, &w, &h, &c, 4);
    if (!px) { fprintf(stderr,"[cc] Texture load fail: %s\n", path); return CC_NULL; }
    CCTextureDesc d = {.width=(uint32_t)w,.height=(uint32_t)h,.format=CC_FMT_SRGB8_ALPHA8,.mipmaps=true,.linear_filter=true,.wrap_repeat=true};
    CCTexture t = cc_renderer_texture_create(r, &d, px);
    stbi_image_free(px);
    return t;
}

/* Decode an image from an in-memory buffer (PNG/JPG/etc via stb_image). */
CCTexture cc_renderer_texture_from_memory(CCRenderer* r, const void* data, size_t size){
    int w,h,c; uint8_t* px = stbi_load_from_memory((const stbi_uc*)data,(int)size,&w,&h,&c,4);
    if(!px){ fprintf(stderr,"[cc] Texture decode-from-memory failed\n"); return CC_NULL; }
    CCTextureDesc d={.width=(uint32_t)w,.height=(uint32_t)h,.format=CC_FMT_RGBA8,.mipmaps=true,.linear_filter=true,.wrap_repeat=true};
    CCTexture t=cc_renderer_texture_create(r,&d,px);
    stbi_image_free(px);
    return t;
}

/* ── PBR texture-set loader ──────────────────────────────────────────────
   Scans a directory of scanned-PBR maps and wires them into a material with
   correct per-map color space. See cc/render.h for the filename conventions. */

typedef enum { PBR_NONE=0, PBR_ALBEDO, PBR_NORMAL, PBR_ROUGH, PBR_METAL,
               PBR_AO, PBR_EMISSIVE, PBR_ROUGHMETAL } PbrChannel;

/* Classify a filename stem (lowercased basename, extension stripped) into a
   PBR channel. Order matters: NORMAL is checked before the combined ORM/ARM
   map, because "normal" contains the substring "orm" and would otherwise be
   misrouted. Combined-map stems are matched with boundary characters so they
   only fire on genuine packed maps (e.g. "tile_orm", "arm_map", "roughmetal")
   and never inside another word. Returns PBR_NONE if unknown. */
static bool stem_has_token(const char* stem, const char* tok) {
    /* substring match that requires the char before/after to be a non-letter
       (or string edge), so "orm" matches "tile_orm" / "orm_1" but not "normal" */
    size_t tl = strlen(tok);
    for (const char* p = strstr(stem, tok); p; p = strstr(p+1, tok)) {
        char before = (p==stem) ? '_' : p[-1];
        char after  = p[tl] ? p[tl] : '_';
        int balpha = (before>='a'&&before<='z');
        int aalpha = (after>='a'&&after<='z');
        if (!balpha && !aalpha) return true;
    }
    return false;
}

static PbrChannel pbr_classify(const char* stem) {
    /* normal FIRST (guards against "n-orm-al" hitting the ORM combined match) */
    if (strstr(stem,"normal") || strstr(stem,"nrm") || strstr(stem,"_nor") ||
        strstr(stem,"normalgl") || strstr(stem,"normalmap"))
        return PBR_NORMAL;
    /* combined roughness+metallic packed maps (boundary-checked tokens) */
    if (stem_has_token(stem,"orm") || stem_has_token(stem,"arm") ||
        strstr(stem,"roughmetal") || strstr(stem,"rough_metal") || strstr(stem,"metalrough"))
        return PBR_ROUGHMETAL;
    if (strstr(stem,"albedo") || strstr(stem,"basecolor") || strstr(stem,"base_color") ||
        strstr(stem,"diffuse") || strstr(stem,"_col") || !strcmp(stem,"col") ||
        strstr(stem,"color"))
        return PBR_ALBEDO;
    if (strstr(stem,"rough") || strstr(stem,"rgh"))                 return PBR_ROUGH;
    if (strstr(stem,"metal") || strstr(stem,"mtl") || strstr(stem,"metalness")) return PBR_METAL;
    if (strstr(stem,"occlusion") || strstr(stem,"ambientocclusion") ||
        strcmp(stem,"ao")==0 || strstr(stem,"_ao"))                 return PBR_AO;
    if (strstr(stem,"emissive") || strstr(stem,"emission") || strstr(stem,"emit")) return PBR_EMISSIVE;
    return PBR_NONE;
}

static bool pbr_ext_ok(const char* name) {
    const char* dot = strrchr(name, '.');
    if (!dot) return false;
    return !strcasecmp(dot,".png") || !strcasecmp(dot,".jpg") || !strcasecmp(dot,".jpeg") ||
           !strcasecmp(dot,".tga") || !strcasecmp(dot,".bmp");
}

CCMaterial cc_renderer_material_load_pbr(CCRenderer* r, const char* dir, const CCMaterialDesc* base) {
    if (r->backend == CC_RENDERER_NULL) return CC_NULL;
    DIR* dp = opendir(dir);
    if (!dp) { fprintf(stderr,"[cc] PBR set: cannot open dir %s\n", dir); return CC_NULL; }

    char paths[PBR_ROUGHMETAL+1][1024];
    for (int i=0;i<=PBR_ROUGHMETAL;i++) paths[i][0]=0;

    struct dirent* e;
    while ((e = readdir(dp))) {
        if (e->d_name[0]=='.') continue;
        if (!pbr_ext_ok(e->d_name)) continue;
        /* lowercase stem (basename without extension) */
        char stem[256]; size_t n=0;
        for (const char* c=e->d_name; *c && *c!='.' && n<sizeof(stem)-1; c++)
            stem[n++] = (char)tolower((unsigned char)*c);
        stem[n]=0;
        PbrChannel ch = pbr_classify(stem);
        if (ch==PBR_NONE) continue;
        /* first match for a channel wins (stable, deterministic) */
        if (paths[ch][0]==0)
            snprintf(paths[ch], sizeof(paths[ch]), "%s/%s", dir, e->d_name);
    }
    closedir(dp);

    if (paths[PBR_ALBEDO][0]==0) {
        fprintf(stderr,"[cc] PBR set %s: no albedo/basecolor map found — aborting\n", dir);
        return CC_NULL;
    }

    CCMaterialDesc d = base ? *base : (CCMaterialDesc){0};
    /* neutral defaults if the caller passed nothing */
    if (!base) {
        d.base_color[0]=d.base_color[1]=d.base_color[2]=d.base_color[3]=1.0f;
        d.roughness = 1.0f; d.metallic = 1.0f;
    }
    /* base_color/roughness/metallic act as MULTIPLIERS on the maps; force them
       to 1 so a supplied map isn't unexpectedly scaled down (caller can still
       override afterward via cc_material_set_*). */
    if (d.base_color[0]==0 && d.base_color[1]==0 && d.base_color[2]==0 && d.base_color[3]==0) {
        d.base_color[0]=d.base_color[1]=d.base_color[2]=d.base_color[3]=1.0f;
    }
    if (d.roughness==0) d.roughness = 1.0f;
    if (d.metallic==0 && paths[PBR_METAL][0]) d.metallic = 1.0f;

    /* Albedo + emissive → sRGB; the rest → linear. */
    d.albedo_map = cc_renderer_texture_load_srgb(r, paths[PBR_ALBEDO]);
    if (paths[PBR_NORMAL][0])   d.normal_map   = cc_renderer_texture_load(r, paths[PBR_NORMAL]);
    if (paths[PBR_AO][0])       d.ao_map       = cc_renderer_texture_load(r, paths[PBR_AO]);
    if (paths[PBR_EMISSIVE][0]) d.emissive_map = cc_renderer_texture_load_srgb(r, paths[PBR_EMISSIVE]);

    /* Roughness/metallic packing. The gbuffer reads uRoughMetal.rg (R=rough,
       G=metal). Priority: a combined ORM/RM map if present, else pack the two
       single-channel files into one RG texture CPU-side, else leave unbound
       (scalar roughness/metallic apply). */
    if (paths[PBR_ROUGHMETAL][0]) {
        /* ORM convention: R=AO, G=roughness, B=metallic. We need R=rough,G=metal,
           so remap channels CPU-side. glTF metallic-roughness packs G=rough,
           B=metal too, so reading G→R and B→G covers both. */
        int w,h,c; uint8_t* px = stbi_load(paths[PBR_ROUGHMETAL], &w,&h,&c,4);
        if (px) {
            for (int i=0;i<w*h;i++) {
                uint8_t g=px[i*4+1], b=px[i*4+2];
                px[i*4+0]=g;  /* R := roughness (from G) */
                px[i*4+1]=b;  /* G := metallic  (from B) */
                px[i*4+2]=0; px[i*4+3]=255;
            }
            CCTextureDesc td={.width=(uint32_t)w,.height=(uint32_t)h,.format=CC_FMT_RGBA8,
                              .mipmaps=true,.linear_filter=true,.wrap_repeat=true};
            d.roughness_metallic_map = cc_renderer_texture_create(r,&td,px);
            stbi_image_free(px);
        }
    } else if (paths[PBR_ROUGH][0] || paths[PBR_METAL][0]) {
        int rw=0,rh=0,mw=0,mh=0; uint8_t *rpx=NULL,*mpx=NULL;
        if (paths[PBR_ROUGH][0]) { int c; rpx=stbi_load(paths[PBR_ROUGH],&rw,&rh,&c,1); }
        if (paths[PBR_METAL][0]) { int c; mpx=stbi_load(paths[PBR_METAL],&mw,&mh,&c,1); }
        int w = rw?rw:mw, h = rh?rh:mh;
        if (w>0 && h>0) {
            /* if both exist at different sizes, fall back to nearest-index sample */
            uint8_t* out = (uint8_t*)malloc((size_t)w*h*4);
            for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
                int o=(y*w+x)*4;
                uint8_t rough=255, metal=0;
                if (rpx) { int rx=x*rw/w, ry=y*rh/h; rough=rpx[ry*rw+rx]; }
                if (mpx) { int mx=x*mw/w, my=y*mh/h; metal=mpx[my*mw+mx]; }
                out[o+0]=rough; out[o+1]=metal; out[o+2]=0; out[o+3]=255;
            }
            CCTextureDesc td={.width=(uint32_t)w,.height=(uint32_t)h,.format=CC_FMT_RGBA8,
                              .mipmaps=true,.linear_filter=true,.wrap_repeat=true};
            d.roughness_metallic_map = cc_renderer_texture_create(r,&td,out);
            free(out);
            if (rpx) stbi_image_free(rpx);
            if (mpx) stbi_image_free(mpx);
        }
    }

    fprintf(stderr,"[cc] PBR set %s: albedo=%s normal=%s roughmetal=%s ao=%s emissive=%s\n",
            dir, paths[PBR_ALBEDO][0]?"y":"-", d.normal_map?"y":"-",
            d.roughness_metallic_map?"y":"-", d.ao_map?"y":"-", d.emissive_map?"y":"-");

    return cc_renderer_material_create(r, &d);
}

CCTexture cc_renderer_texture_create(CCRenderer* r, const CCTextureDesc* d, const void* px) {
    if (r->backend == CC_RENDERER_NULL) return CC_NULL;
    CCTexture slot = alloc_tex_slot(r);
    if (!slot) return CC_NULL;
    GLuint id; glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    GLenum ifmt = GL_RGBA8, fmt = GL_RGBA, type = GL_UNSIGNED_BYTE;
    switch(d->format) {
        case CC_FMT_R8:    ifmt=GL_R8;    fmt=GL_RED; break;
        case CC_FMT_RGB8:  ifmt=GL_RGB8;  fmt=GL_RGB; break;
        case CC_FMT_RGBA16F: ifmt=GL_RGBA16F; fmt=GL_RGBA; type=GL_FLOAT; break;
        /* sRGB color map: internal format carries the sRGB encoding so the GPU
         * linearizes on sample. Pixel data is still 8-bit RGBA on upload. */
        case CC_FMT_SRGB8_ALPHA8: ifmt=GL_SRGB8_ALPHA8; fmt=GL_RGBA; break;
        default: break;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, d->width, d->height, 0, fmt, type, px);
    /* Realism: material textures get a mip chain + max anisotropic filtering so
       they stay sharp at grazing angles and don't shimmer in the distance. We
       force mipmaps on any repeating (tiling) texture even if not requested,
       since those are almost always surface materials that benefit. */
    bool want_mips = d->mipmaps || d->wrap_repeat;
    if (want_mips) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
        want_mips ? GL_LINEAR_MIPMAP_LINEAR : (d->linear_filter ? GL_LINEAR : GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, d->linear_filter ? GL_LINEAR : GL_NEAREST);
    if (want_mips && r->max_anisotropy > 1.0f)
        glTexParameterf(GL_TEXTURE_2D, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY*/, r->max_anisotropy);
    GLenum wrap = d->wrap_repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    r->textures[slot] = (GLTexEntry){id, d->width, d->height, d->format, true};
    return slot;
}

CCTexture cc_renderer_texture_proc(CCRenderer* r, const CCTextureDesc* d,
    void (*gen)(uint8_t*,uint32_t,uint32_t,void*), void* ud) {
    uint8_t* px = malloc(d->width * d->height * 4);
    gen(px, d->width, d->height, ud);
    CCTexture t = cc_renderer_texture_create(r, d, px);
    free(px);
    return t;
}

void cc_renderer_texture_destroy(CCRenderer* r, CCTexture t) {
    if (!t || t >= CC_MAX_TEXTURES || !r->textures[t].valid) return;
    glDeleteTextures(1, &r->textures[t].id);
    r->textures[t].valid = false;
}

/* ══════════════════════════════════════════════════════════════════════
   MESH MANAGEMENT
   ══════════════════════════════════════════════════════════════════════ */

static CCMesh alloc_mesh_slot(CCRenderer* r) {
    for (uint32_t i=1;i<CC_MAX_MESHES;i++) if(!r->meshes[i].valid) return i;
    return CC_NULL;
}

CCMesh cc_renderer_mesh_create(CCRenderer* r, const CCVertex* verts, uint32_t nv,
                                const uint32_t* idx, uint32_t ni, CCMeshUsage usage) {
    if (r->backend == CC_RENDERER_NULL) return CC_NULL;
    CCMesh slot = alloc_mesh_slot(r);
    if (!slot) return CC_NULL;
    GLenum gl_usage = usage == CC_MESH_DYNAMIC ? GL_DYNAMIC_DRAW :
                      usage == CC_MESH_STREAM  ? GL_STREAM_DRAW  : GL_STATIC_DRAW;
    GLuint vao,vbo,ebo;
    glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo); glGenBuffers(1,&ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, nv*sizeof(CCVertex), verts, gl_usage);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ni*4, idx, gl_usage);
    /* layout: pos(0) normal(1) uv(2) tangent(3) color(4) — stride = sizeof(CCVertex) */
    GLsizei stride = (GLsizei)sizeof(CCVertex);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,stride,(void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,stride,(void*)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,stride,(void*)32);
    glEnableVertexAttribArray(4); glVertexAttribPointer(4,4,GL_UNSIGNED_BYTE,GL_TRUE,stride,(void*)48);
    glBindVertexArray(0);
    r->meshes[slot] = (GLMeshEntry){vao,vbo,ebo,0,ni,true};
    return slot;
}

void cc_renderer_mesh_destroy(CCRenderer* r, CCMesh m) {
    if (!m||m>=CC_MAX_MESHES||!r->meshes[m].valid) return;
    glDeleteVertexArrays(1,&r->meshes[m].vao);
    glDeleteBuffers(1,&r->meshes[m].vbo);
    glDeleteBuffers(1,&r->meshes[m].ebo);
    r->meshes[m].valid=false;
}

/* Re-upload vertex (and optionally index) data into an existing mesh's buffers.
 * If the index count changes, the EBO is re-sized. Vertex layout is unchanged. */
void cc_renderer_mesh_update(CCRenderer* r, CCMesh m,
                             const CCVertex* verts, uint32_t nv,
                             const uint32_t* idx, uint32_t ni) {
    if (r->backend==CC_RENDERER_NULL) return;
    if (!m||m>=CC_MAX_MESHES||!r->meshes[m].valid||!verts||!nv) return;
    GLMeshEntry* e=&r->meshes[m];
    glBindBuffer(GL_ARRAY_BUFFER, e->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nv*sizeof(CCVertex), verts, GL_DYNAMIC_DRAW);
    if (idx && ni){
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, e->ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ni*4, idx, GL_DYNAMIC_DRAW);
        e->index_count=ni;
    }
    glBindBuffer(GL_ARRAY_BUFFER,0);
}

/* ══════════════════════════════════════════════════════════════════════
   DRAW 3D MESH
   ══════════════════════════════════════════════════════════════════════ */

/* mat4 via ccmath.h */


/* Bind a material's PBR uniforms + textures onto the currently-bound gbuffer
   program (works for both sh_gbuf and sh_gbuf_inst — same fragment shader, so
   identical uniform/sampler names). Factored out so the single and instanced
   draw paths never drift apart. */
static void bind_gbuf_material(CCRenderer* r, GLuint prog, CCMaterial mat) {
    MaterialEntry* me = (mat && mat<CC_MAX_MATERIALS && r->materials[mat].valid)
                        ? &r->materials[mat] : NULL;
    CCMaterialDesc* md = me ? &me->desc : NULL;
    float bc[4]={1,1,1,1}; float rough=0.5f,metal=0.0f; float em[3]={0,0,0};
    if (md) { memcpy(bc,md->base_color,16); rough=md->roughness; metal=md->metallic; memcpy(em,md->emissive,12); }
    glUniform4fv(glGetUniformLocation(prog,"uBaseColor"),1,bc);
    glUniform1f (glGetUniformLocation(prog,"uRoughness"),rough);
    glUniform1f (glGetUniformLocation(prog,"uMetallic"),metal);
    glUniform3fv(glGetUniformLocation(prog,"uEmissiveFactor"),1,em);

    GLuint albedo_id = md && md->albedo_map ? r->textures[md->albedo_map].id : r->white_tex;
    GLuint norm_id   = md && md->normal_map ? r->textures[md->normal_map].id : r->default_normal_tex;
    GLuint rm_id     = md && md->roughness_metallic_map ? r->textures[md->roughness_metallic_map].id : r->white_tex;
    GLuint em_id     = md && md->emissive_map ? r->textures[md->emissive_map].id : r->white_tex; /* white: factor passes through */
    GLuint ao_id     = md && md->ao_map ? r->textures[md->ao_map].id : r->white_tex;
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, albedo_id);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, norm_id);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, rm_id);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, em_id);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, ao_id);
    glUniform1i(glGetUniformLocation(prog,"uAlbedo"),   0);
    glUniform1i(glGetUniformLocation(prog,"uNormalMap"),1);
    glUniform1i(glGetUniformLocation(prog,"uRoughMetal"),2);
    glUniform1i(glGetUniformLocation(prog,"uEmissive"), 3);
    glUniform1i(glGetUniformLocation(prog,"uAO"),       4);
    glUniform1i(glGetUniformLocation(prog,"uHasNormalMap"), md && md->normal_map ? 1 : 0);
    glUniform1i(glGetUniformLocation(prog,"uHasDetailNormal"), 0);
    glUniform1i(glGetUniformLocation(prog,"uHasAOMap"), md && md->ao_map ? 1 : 0);
    float tint[4]={1,1,1,1};
    if (md && (md->tint[0]||md->tint[1]||md->tint[2]||md->tint[3])) {
        tint[0]=md->tint[0]; tint[1]=md->tint[1]; tint[2]=md->tint[2]; tint[3]=md->tint[3];
    }
    glUniform4fv(glGetUniformLocation(prog,"uTint"), 1, tint);
    glUniform1i(glGetUniformLocation(prog,"uUnlit"), md && md->unlit ? 1 : 0);
    /* Realism-axis shading model (packed into the G-buffer for the deferred
       lighting pass). Style knobs are captured on the renderer for any stylized
       material so the lighting pass can apply them globally. */
    int sm = md ? md->shading_model : 0;
    int tb = (md && md->toon_bands) ? md->toon_bands : 4;
    glUniform1i(glGetUniformLocation(prog,"uShadingModel"), sm);
    glUniform1i(glGetUniformLocation(prog,"uToonBands"), tb);
    if (md && sm != 0) {
        r->style_toon_specular = md->toon_specular;
        r->style_rim_strength  = md->rim_strength;
        r->style_rim_power     = md->rim_power;
        r->style_rim_color[0]  = md->rim_color[0];
        r->style_rim_color[1]  = md->rim_color[1];
        r->style_rim_color[2]  = md->rim_color[2];
    }
}

void cc_renderer_draw_mesh(CCRenderer* r, CCMesh mesh, CCMaterial mat, const CCTransform3D* xf) {
    if (r->backend==CC_RENDERER_NULL||!mesh||mesh>=CC_MAX_MESHES||!r->meshes[mesh].valid) return;
    /* debug capture */
    { uint32_t tris = r->meshes[mesh].index_count/3;
      r->stats.draw_calls++; r->stats.meshes_drawn++; r->stats.triangles+=tris;
      if (r->drawlog_count < 512) {
        int i=r->drawlog_count++;
        r->drawlog[i].mesh=mesh; r->drawlog[i].material=mat; r->drawlog[i].tris=tris;
        CCVec3 p={xf->pos[0],xf->pos[1],xf->pos[2]};
        CCQuat q={xf->rot[0],xf->rot[1],xf->rot[2],xf->rot[3]};
        CCVec3 s={xf->scale[0],xf->scale[1],xf->scale[2]};
        if(s.x==0&&s.y==0&&s.z==0)s=(CCVec3){1,1,1};
        if(q.x==0&&q.y==0&&q.z==0&&q.w==0)q=(CCQuat){0,0,0,1};
        CCMat4 mm=mat4_trs(p,q,s); memcpy(r->drawlog[i].m,mm.m,64);
        r->drawlog[i].name[0]=0;
      }
    }
    /* Build model matrix from TRS using ccmath */
    CCVec3 pos  = {xf->pos[0],   xf->pos[1],   xf->pos[2]};
    CCQuat rot  = {xf->rot[0],   xf->rot[1],   xf->rot[2],   xf->rot[3]};
    CCVec3 scl  = {xf->scale[0], xf->scale[1], xf->scale[2]};
    /* Default scale to 1 if zeroed */
    if (scl.x==0&&scl.y==0&&scl.z==0) scl=(CCVec3){1,1,1};
    /* Default rotation to identity if zeroed */
    if (rot.x==0&&rot.y==0&&rot.z==0&&rot.w==0) rot=(CCQuat){0,0,0,1};
    CCMat4 model_mat = mat4_trs(pos, rot, scl);
    /* VP * M */
    CCMat4 vp_cc; memcpy(vp_cc.m, r->vp_mat, 64);
    CCMat4 mvp_mat = mat4_mul(vp_cc, model_mat);
    float* mvp = mvp_mat.m;
    float* model = model_mat.m;
    /* Normal matrix = inverse transpose of model upper-3x3 */
    CCMat4 normal_mat = mat4_upper3x3_inverse_transpose(model_mat);

    glBindFramebuffer(GL_FRAMEBUFFER, r->gbuf_fbo);
    glViewport(0, 0, r->w, r->h);
    GLenum gbuf_bufs[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3, gbuf_bufs);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glUseProgram(r->sh_gbuf);
    glUniformMatrix4fv(glGetUniformLocation(r->sh_gbuf,"uMVP"),      1,GL_FALSE,mvp);
    glUniformMatrix4fv(glGetUniformLocation(r->sh_gbuf,"uModel"),    1,GL_FALSE,model);
    /* uNormalMat is mat3 in the shader — extract upper 3x3 (column-major) */
    float nm3[9] = {
        normal_mat.m[0], normal_mat.m[1], normal_mat.m[2],
        normal_mat.m[4], normal_mat.m[5], normal_mat.m[6],
        normal_mat.m[8], normal_mat.m[9], normal_mat.m[10]
    };
    glUniformMatrix3fv(glGetUniformLocation(r->sh_gbuf,"uNormalMat"),1,GL_FALSE,nm3);

    /* Bind material */
    MaterialEntry* me = (mat && mat<CC_MAX_MATERIALS && r->materials[mat].valid)
                        ? &r->materials[mat] : NULL;
    CCMaterialDesc* md = me ? &me->desc : NULL;
    float bc[4]={1,1,1,1}; float rough=0.5f,metal=0.0f; float em[3]={0,0,0};
    if (md) {
        memcpy(bc,md->base_color,16);
        rough=md->roughness; metal=md->metallic;
        memcpy(em,md->emissive,12);
    }
    glUniform4fv(glGetUniformLocation(r->sh_gbuf,"uBaseColor"),1,bc);
    glUniform1f(glGetUniformLocation(r->sh_gbuf,"uRoughness"),rough);
    glUniform1f(glGetUniformLocation(r->sh_gbuf,"uMetallic"),metal);
    glUniform3fv(glGetUniformLocation(r->sh_gbuf,"uEmissiveFactor"),1,em);

    GLuint albedo_id = md && md->albedo_map ? r->textures[md->albedo_map].id : r->white_tex;
    GLuint norm_id   = md && md->normal_map ? r->textures[md->normal_map].id : r->default_normal_tex;
    GLuint rm_id     = md && md->roughness_metallic_map ? r->textures[md->roughness_metallic_map].id : r->white_tex;
    /* Default emissive map is WHITE, not black: the gbuffer computes
       emissive = texture(uEmissive) * uEmissiveFactor. A black default would
       zero out every material that sets an emissive factor without a map
       (the common case), so bloom never sees any emitter. White lets the
       factor pass through unchanged; a real map still modulates it. */
    GLuint em_id     = md && md->emissive_map ? r->textures[md->emissive_map].id : r->white_tex;
    GLuint ao_id     = md && md->ao_map ? r->textures[md->ao_map].id : r->white_tex;

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, albedo_id);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, norm_id);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, rm_id);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, em_id);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, ao_id);
    GLuint detail_id = md && md->detail_normal_map ? r->textures[md->detail_normal_map].id : r->default_normal_tex;
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, detail_id);
    glUniform1i(glGetUniformLocation(r->sh_gbuf,"uAlbedo"),   0);
    glUniform1i(glGetUniformLocation(r->sh_gbuf,"uNormalMap"),1);
    glUniform1i(glGetUniformLocation(r->sh_gbuf,"uRoughMetal"),2);
    glUniform1i(glGetUniformLocation(r->sh_gbuf,"uEmissive"), 3);
    glUniform1i(glGetUniformLocation(r->sh_gbuf,"uAO"),       4);
    glUniform1i(glGetUniformLocation(r->sh_gbuf,"uDetailNormal"), 5);
    glUniform1i(glGetUniformLocation(r->sh_gbuf,"uHasNormalMap"), md && md->normal_map ? 1 : 0);
    glUniform1i(glGetUniformLocation(r->sh_gbuf,"uHasDetailNormal"), md && md->detail_normal_map ? 1 : 0);
    glUniform1f(glGetUniformLocation(r->sh_gbuf,"uDetailScale"), md && md->detail_normal_scale>0 ? md->detail_normal_scale : 8.0f);
    glUniform1f(glGetUniformLocation(r->sh_gbuf,"uDetailStrength"), md && md->detail_normal_strength>0 ? md->detail_normal_strength : 1.0f);
    glUniform1i(glGetUniformLocation(r->sh_gbuf,"uHasAOMap"), md && md->ao_map ? 1 : 0);
    { float tint[4]={1,1,1,1};
      if (md && (md->tint[0]||md->tint[1]||md->tint[2]||md->tint[3])) {
          tint[0]=md->tint[0]; tint[1]=md->tint[1]; tint[2]=md->tint[2]; tint[3]=md->tint[3];
      }
      glUniform4fv(glGetUniformLocation(r->sh_gbuf,"uTint"), 1, tint);
      glUniform1i(glGetUniformLocation(r->sh_gbuf,"uUnlit"), md && md->unlit ? 1 : 0);
      /* realism-axis shading model (packed into G-buffer for the lighting pass) */
      int _sm = md ? md->shading_model : 0;
      int _tb = (md && md->toon_bands) ? md->toon_bands : 4;
      glUniform1i(glGetUniformLocation(r->sh_gbuf,"uShadingModel"), _sm);
      glUniform1i(glGetUniformLocation(r->sh_gbuf,"uToonBands"), _tb);
      if (md && _sm != 0) {
          r->style_toon_specular=md->toon_specular; r->style_rim_strength=md->rim_strength;
          r->style_rim_power=md->rim_power;
          r->style_rim_color[0]=md->rim_color[0]; r->style_rim_color[1]=md->rim_color[1]; r->style_rim_color[2]=md->rim_color[2];
      }
    }

    glBindVertexArray(r->meshes[mesh].vao);
    if(r->debug_view==1) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawElements(GL_TRIANGLES, r->meshes[mesh].index_count, GL_UNSIGNED_INT, 0);
    if(r->debug_view==1) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/* Hardware-instanced draw: one draw call for `count` copies of `mesh`, each
   with its own TRS. Model matrices are packed into a growable instance VBO and
   fed as a mat4 vertex attribute (locations 5..8). Same deferred gbuffer output
   as the single path, so instances light, bloom, and post-process identically. */
void cc_renderer_draw_mesh_instanced(CCRenderer* r, CCMesh mesh, CCMaterial mat,
                                     const CCTransform3D* xforms, uint32_t count) {
    if (r->backend==CC_RENDERER_NULL||!mesh||mesh>=CC_MAX_MESHES||!r->meshes[mesh].valid) return;
    if (!xforms || count==0) return;

    /* Build packed model matrices on the CPU. */
    float* data = (float*)malloc((size_t)count * 16 * sizeof(float));
    if (!data) return;
    for (uint32_t i=0;i<count;i++) {
        const CCTransform3D* xf=&xforms[i];
        CCVec3 pos={xf->pos[0],xf->pos[1],xf->pos[2]};
        CCQuat rot={xf->rot[0],xf->rot[1],xf->rot[2],xf->rot[3]};
        CCVec3 scl={xf->scale[0],xf->scale[1],xf->scale[2]};
        if (scl.x==0&&scl.y==0&&scl.z==0) scl=(CCVec3){1,1,1};
        if (rot.x==0&&rot.y==0&&rot.z==0&&rot.w==0) rot=(CCQuat){0,0,0,1};
        CCMat4 m=mat4_trs(pos,rot,scl);
        memcpy(&data[i*16], m.m, 64);
    }

    /* debug stats: instanced draw is ONE draw call but `count` meshes/tris. */
    { uint32_t tris=r->meshes[mesh].index_count/3;
      r->stats.draw_calls++; r->stats.meshes_drawn+=count; r->stats.triangles+=tris*count;
      if (r->drawlog_count<512) { int i=r->drawlog_count++;
        r->drawlog[i].mesh=mesh; r->drawlog[i].material=mat; r->drawlog[i].tris=tris*count;
        memcpy(r->drawlog[i].m,&data[0],64); r->drawlog[i].name[0]=0; }
    }

    /* Upload matrices, growing the instance VBO if needed. */
    if (!r->inst_vbo) glGenBuffers(1,&r->inst_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
    if (count > r->inst_cap) {
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count*16*sizeof(float), data, GL_DYNAMIC_DRAW);
        r->inst_cap = count;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)count*16*sizeof(float), data);
    }

    /* Wire the mat4 instance attribute (locations 5..8) onto the mesh VAO. */
    glBindVertexArray(r->meshes[mesh].vao);
    for (int c=0;c<4;c++) {
        GLuint loc=5+c;
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc,4,GL_FLOAT,GL_FALSE,16*sizeof(float),(void*)(size_t)(c*4*sizeof(float)));
        glVertexAttribDivisor(loc,1);   /* advance once per instance */
    }

    glBindFramebuffer(GL_FRAMEBUFFER, r->gbuf_fbo);
    glViewport(0,0,r->w,r->h);
    GLenum bufs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3,bufs);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_BLEND);
    glUseProgram(r->sh_gbuf_inst);
    glUniformMatrix4fv(glGetUniformLocation(r->sh_gbuf_inst,"uVP"),1,GL_FALSE,r->vp_mat);
    bind_gbuf_material(r, r->sh_gbuf_inst, mat);

    if(r->debug_view==1) glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
    glDrawElementsInstanced(GL_TRIANGLES, r->meshes[mesh].index_count,
                            GL_UNSIGNED_INT, 0, (GLsizei)count);
    if(r->debug_view==1) glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);

    /* Disable instance attribs so a later non-instanced draw sharing this VAO
       isn't accidentally instanced. */
    for (int c=0;c<4;c++) { GLuint loc=5+c; glVertexAttribDivisor(loc,0); glDisableVertexAttribArray(loc); }
    glBindVertexArray(0);
    free(data);
}

/* Instanced camera-facing billboards. Expands one unit quad per instance in the
   vertex shader using camera basis vectors, writing color as emissive into the
   gbuffer (alpha-cutout). One draw call for the whole batch. */
void cc_renderer_draw_billboards(CCRenderer* r, CCTexture tex,
                                 const CCBillboard* bbs, uint32_t count,
                                 CCBillboardMode mode) {
    if (r->backend==CC_RENDERER_NULL || !bbs || count==0) return;

    /* Pack per-instance data: center(3) size(2) color(4) = 9 floats. */
    const int STRIDE = 9;
    float* data = (float*)malloc((size_t)count * STRIDE * sizeof(float));
    if (!data) return;
    for (uint32_t i=0;i<count;i++) {
        float* d = &data[i*STRIDE];
        d[0]=bbs[i].pos[0]; d[1]=bbs[i].pos[1]; d[2]=bbs[i].pos[2];
        d[3]=bbs[i].size[0]; d[4]=bbs[i].size[1];
        d[5]=bbs[i].color[0]; d[6]=bbs[i].color[1]; d[7]=bbs[i].color[2]; d[8]=bbs[i].color[3];
    }

    /* stats: one draw call, `count` quads (2 tris each). */
    { r->stats.draw_calls++; r->stats.meshes_drawn+=count; r->stats.triangles+=count*2; }

    if (!r->bb_inst_vbo) glGenBuffers(1,&r->bb_inst_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->bb_inst_vbo);
    if (count > r->bb_inst_cap) {
        glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)count*STRIDE*sizeof(float),data,GL_DYNAMIC_DRAW);
        r->bb_inst_cap=count;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)count*STRIDE*sizeof(float),data);
    }

    /* Wire per-instance attribs (2=center,3=size,4=color) onto the quad VAO. */
    glBindVertexArray(r->bb_vao);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,STRIDE*sizeof(float),(void*)0);
    glVertexAttribDivisor(2,1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,STRIDE*sizeof(float),(void*)(3*sizeof(float)));
    glVertexAttribDivisor(3,1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4,4,GL_FLOAT,GL_FALSE,STRIDE*sizeof(float),(void*)(5*sizeof(float)));
    glVertexAttribDivisor(4,1);

    glBindFramebuffer(GL_FRAMEBUFFER, r->gbuf_fbo);
    glViewport(0,0,r->w,r->h);
    GLenum bufs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3,bufs);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_BLEND);
    glUseProgram(r->bb_shader);
    glUniformMatrix4fv(glGetUniformLocation(r->bb_shader,"uVP"),1,GL_FALSE,r->vp_mat);
    /* Camera basis from the view matrix rows (view is world→camera). */
    float* v = r->view_mat;
    float right[3] = { v[0], v[4], v[8] };
    float up[3]    = { v[1], v[5], v[9] };
    float fwd[3]   = { -v[2], -v[6], -v[10] };  /* camera looks down -Z */
    glUniform3fv(glGetUniformLocation(r->bb_shader,"uCamRight"),1,right);
    glUniform3fv(glGetUniformLocation(r->bb_shader,"uCamUp"),1,up);
    glUniform3fv(glGetUniformLocation(r->bb_shader,"uCamFwd"),1,fwd);
    glUniform1i(glGetUniformLocation(r->bb_shader,"uCylindrical"), mode==CC_BILLBOARD_CYLINDRICAL?1:0);
    GLuint tex_id = (tex && tex<CC_MAX_TEXTURES && r->textures[tex].valid) ? r->textures[tex].id : r->white_tex;
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex_id);
    glUniform1i(glGetUniformLocation(r->bb_shader,"uTex"),0);

    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)count);

    glVertexAttribDivisor(2,0); glVertexAttribDivisor(3,0); glVertexAttribDivisor(4,0);
    glDisableVertexAttribArray(2); glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
    glBindVertexArray(0);
    free(data);
}

/* Queue a decal for this frame. Bakes the box→world model matrix and its
   inverse now; the flush projects G-buffer pixels through inv_model. */
void cc_renderer_submit_decal(CCRenderer* r, CCTexture tex, const CCDecal* d) {
    if (r->backend==CC_RENDERER_NULL || !d) return;
    if (r->decal_count >= r->decal_cap) {
        uint32_t nc = r->decal_cap ? r->decal_cap*2 : 16;
        CCDecalEntry* nq = (CCDecalEntry*)realloc(r->decal_queue, nc*sizeof(CCDecalEntry));
        if (!nq) return;
        r->decal_queue = nq; r->decal_cap = nc;
    }
    CCVec3 pos={d->pos[0],d->pos[1],d->pos[2]};
    CCQuat rot={d->rot[0],d->rot[1],d->rot[2],d->rot[3]};
    if (rot.x==0&&rot.y==0&&rot.z==0&&rot.w==0) rot=(CCQuat){0,0,0,1};
    CCVec3 scl={d->size[0],d->size[1],d->size[2]};
    if (scl.x==0&&scl.y==0&&scl.z==0) scl=(CCVec3){1,1,1};
    CCMat4 model = mat4_trs(pos,rot,scl);
    CCMat4 inv   = mat4_inverse(model);
    CCDecalEntry* e = &r->decal_queue[r->decal_count++];
    memcpy(e->model, model.m, 64);
    memcpy(e->inv_model, inv.m, 64);
    memcpy(e->color, d->color, 16);
    e->emissive = d->emissive;
    e->angle_fade = d->angle_fade;
    e->tex = tex;
}

/* Compute SSAO into r->ssao_blur_tex (visibility, 1=unoccluded). Runs after the
   shadow pass, before lighting. No-op unless postfx.ssao is on. */
static void render_ssao_pass(CCRenderer* r) {
    if (!r->postfx.ssao) return;
    CCMat4 proj; memcpy(proj.m, r->proj_mat, 64);
    CCMat4 invp = mat4_inverse(proj);
    float radius = r->postfx.ssao_radius>0.0f ? r->postfx.ssao_radius : 0.5f;

    /* Occlusion pass. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->ssao_fbo);
    glViewport(0,0,r->w,r->h);
    glClearColor(1,1,1,1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glUseProgram(r->ssao_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->gbuf_normal_metal);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, r->ssao_noise_tex);
    glUniform1i(glGetUniformLocation(r->ssao_shader,"gDepth"),0);
    glUniform1i(glGetUniformLocation(r->ssao_shader,"gNormal"),1);
    glUniform1i(glGetUniformLocation(r->ssao_shader,"uNoise"),2);
    glUniformMatrix4fv(glGetUniformLocation(r->ssao_shader,"uProj"),1,GL_FALSE,r->proj_mat);
    glUniformMatrix4fv(glGetUniformLocation(r->ssao_shader,"uInvProj"),1,GL_FALSE,invp.m);
    glUniformMatrix4fv(glGetUniformLocation(r->ssao_shader,"uView"),1,GL_FALSE,r->view_mat);
    glUniform3fv(glGetUniformLocation(r->ssao_shader,"uSamples"),32,r->ssao_kernel);
    glUniform2f(glGetUniformLocation(r->ssao_shader,"uNoiseScale"),(float)r->w/4.0f,(float)r->h/4.0f);
    glUniform1f(glGetUniformLocation(r->ssao_shader,"uRadius"),radius);
    glUniform1f(glGetUniformLocation(r->ssao_shader,"uBias"),0.025f);
    glBindVertexArray(r->fsq_vao);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);

    /* Blur pass → ssao_blur_tex. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->ssao_blur_fbo);
    glViewport(0,0,r->w,r->h);
    glUseProgram(r->ssao_blur_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->ssao_tex);
    glUniform1i(glGetUniformLocation(r->ssao_blur_shader,"uSSAO"),0);
    glUniform2f(glGetUniformLocation(r->ssao_blur_shader,"uTexel"),1.0f/r->w,1.0f/r->h);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);
    glBindVertexArray(0);
}

/* Flush queued decals into the G-buffer. Runs after geometry, before lighting.
   Reads a depth COPY (blitted here) and writes albedo (+emissive) with alpha
   blending; depth test/write off so the box volume never occludes anything. */
static void render_decal_pass(CCRenderer* r) {
    if (r->decal_count == 0) return;
    /* Snapshot scene depth so we can sample it while the gbuffer is bound. */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->gbuf_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->decal_depth_fbo);
    glBlitFramebuffer(0,0,r->w,r->h, 0,0,r->w,r->h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

    /* Render into gbuffer albedo(0) + emissive(2); leave normal(1) untouched. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->gbuf_fbo);
    glViewport(0,0,r->w,r->h);
    GLenum bufs[]={GL_COLOR_ATTACHMENT0, GL_NONE, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3,bufs);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); /* back faces: robust if camera enters box */

    glUseProgram(r->decal_shader);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->decal_depth_copy);
    glUniform1i(glGetUniformLocation(r->decal_shader,"uDepth"),0);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, r->gbuf_normal_metal);
    glUniform1i(glGetUniformLocation(r->decal_shader,"gNormalTex"),2);
    glUniformMatrix4fv(glGetUniformLocation(r->decal_shader,"uInvVP"),1,GL_FALSE,r->inv_vp_mat);
    glUniform2f(glGetUniformLocation(r->decal_shader,"uScreen"),(float)r->w,(float)r->h);

    CCMat4 vp; memcpy(vp.m, r->vp_mat, 64);
    glBindVertexArray(r->decal_box_vao);
    for (uint32_t i=0;i<r->decal_count;i++) {
        CCDecalEntry* e=&r->decal_queue[i];
        CCMat4 model; memcpy(model.m, e->model, 64);
        CCMat4 mvp = mat4_mul(vp, model);
        glUniformMatrix4fv(glGetUniformLocation(r->decal_shader,"uMVP"),1,GL_FALSE,mvp.m);
        glUniformMatrix4fv(glGetUniformLocation(r->decal_shader,"uInvModel"),1,GL_FALSE,e->inv_model);
        /* World-space projection axis = -(model Y basis), normalized (col-major). */
        { float* m=e->model; float dy[3]={-m[4],-m[5],-m[6]};
          float l=sqrtf(dy[0]*dy[0]+dy[1]*dy[1]+dy[2]*dy[2]); if(l>1e-6f){dy[0]/=l;dy[1]/=l;dy[2]/=l;}
          glUniform3fv(glGetUniformLocation(r->decal_shader,"uDecalDir"),1,dy); }
        glUniform4fv(glGetUniformLocation(r->decal_shader,"uColor"),1,e->color);
        glUniform1f(glGetUniformLocation(r->decal_shader,"uEmissive"),e->emissive);
        glUniform1f(glGetUniformLocation(r->decal_shader,"uAngleFade"),e->angle_fade);
        GLuint tid = (e->tex && e->tex<CC_MAX_TEXTURES && r->textures[e->tex].valid)
                     ? r->textures[e->tex].id : r->white_tex;
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, tid);
        glUniform1i(glGetUniformLocation(r->decal_shader,"uDecalTex"),1);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        r->stats.draw_calls++;
    }
    glBindVertexArray(0);

    /* Restore gbuffer state for anything that follows. */
    glDisable(GL_CULL_FACE); glCullFace(GL_BACK);
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
    GLenum allbufs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3,allbufs);
    r->decal_count = 0;
}

/* Shadow pass. Finds the first shadow-casting light (directional preferred, then
   spot), builds the appropriate light-space matrix (ortho framing the scene for
   directional; perspective from the spot's cone for spot), and renders caster
   depth into the shadow map. Sets shadow_active, shadow_vp, and shadow_light
   (the casting light's index) for the lighting pass. */
static void render_shadow_pass(CCRenderer* r) {
    r->shadow_active = false;
    r->shadow_light = -1;
    if (!r->shadow_size || r->drawlog_count == 0) return;

    /* Find a caster: directional first (whole-scene), else a spot. */
    int li = -1; int lt = -1;
    for (int i=0;i<r->num_lights;i++) {
        if (!r->light_valid[i] || !r->lights[i].cast_shadows) continue;
        if (r->lights[i].type==CC_LIGHT_DIRECTIONAL) { li=i; lt=CC_LIGHT_DIRECTIONAL; break; }
    }
    if (li < 0) {
        for (int i=0;i<r->num_lights;i++) {
            if (!r->light_valid[i] || !r->lights[i].cast_shadows) continue;
            if (r->lights[i].type==CC_LIGHT_SPOT) { li=i; lt=CC_LIGHT_SPOT; break; }
        }
    }
    if (li < 0) return;

    CCMat4 lvp;
    if (lt == CC_LIGHT_DIRECTIONAL) {
        /* Scene bounds from the drawlog (mesh translation points). A loose sphere
           around them defines the ortho frustum; good enough for a single cascade. */
        CCVec3 mn={1e9f,1e9f,1e9f}, mx={-1e9f,-1e9f,-1e9f};
        for (uint32_t i=0;i<r->drawlog_count;i++) {
            float* m=r->drawlog[i].m;
            CCVec3 p={m[12],m[13],m[14]};
            if(p.x<mn.x)mn.x=p.x; if(p.y<mn.y)mn.y=p.y; if(p.z<mn.z)mn.z=p.z;
            if(p.x>mx.x)mx.x=p.x; if(p.y>mx.y)mx.y=p.y; if(p.z>mx.z)mx.z=p.z;
        }
        CCVec3 center={(mn.x+mx.x)*0.5f,(mn.y+mx.y)*0.5f,(mn.z+mx.z)*0.5f};
        float ext=0.0f;
        { float dx=mx.x-mn.x,dy=mx.y-mn.y,dz=mx.z-mn.z;
          ext=sqrtf(dx*dx+dy*dy+dz*dz)*0.5f+6.0f; }   /* pad for mesh radius */
        if (ext < 8.0f) ext = 8.0f;
        CCVec3 dir={r->lights[li].dir[0],r->lights[li].dir[1],r->lights[li].dir[2]};
        float dl=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z); if(dl<1e-5f){dir=(CCVec3){0,-1,0};dl=1;}
        dir.x/=dl;dir.y/=dl;dir.z/=dl;
        CCVec3 eye={center.x-dir.x*ext*2.0f, center.y-dir.y*ext*2.0f, center.z-dir.z*ext*2.0f};
        CCVec3 up = fabsf(dir.y)>0.98f ? (CCVec3){0,0,1} : (CCVec3){0,1,0};
        CCMat4 view = mat4_look_at(eye, center, up);
        CCMat4 proj = mat4_ortho(-ext,ext,-ext,ext, 0.1f, ext*4.0f+0.1f);
        lvp = mat4_mul(proj, view);
    } else {
        /* Spot: perspective from the light position along its cone axis. FOV is
           twice the outer half-angle (outer cutoff is stored as cos(angle)). */
        CCVec3 pos={r->lights[li].pos[0],r->lights[li].pos[1],r->lights[li].pos[2]};
        CCVec3 dir={r->lights[li].dir[0],r->lights[li].dir[1],r->lights[li].dir[2]};
        float dl=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z); if(dl<1e-5f){dir=(CCVec3){0,-1,0};dl=1;}
        dir.x/=dl;dir.y/=dl;dir.z/=dl;
        CCVec3 target={pos.x+dir.x, pos.y+dir.y, pos.z+dir.z};
        CCVec3 up = fabsf(dir.y)>0.98f ? (CCVec3){0,0,1} : (CCVec3){0,1,0};
        float range = r->lights[li].range>0.0f ? r->lights[li].range : 50.0f;
        float outer = r->lights[li].outer_angle>0.0f ? r->lights[li].outer_angle : 0.6f; /* radians, half-angle */
        float fov = fminf(3.0f, outer * 2.0f + 0.15f);  /* full cone + a little margin */
        CCMat4 view = mat4_look_at(pos, target, up);
        CCMat4 proj = mat4_perspective(fov, 1.0f, 0.15f, range);
        lvp = mat4_mul(proj, view);
    }
    memcpy(r->shadow_vp, lvp.m, 64);

    /* Render caster depth. Front-face cull to reduce peter-panning/acne. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);
    glViewport(0,0,r->shadow_size,r->shadow_size);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);
    glUseProgram(r->shadow_shader);
    glUniformMatrix4fv(glGetUniformLocation(r->shadow_shader,"uLightVP"),1,GL_FALSE,lvp.m);
    for (uint32_t i=0;i<r->drawlog_count;i++) {
        CCMesh mesh=r->drawlog[i].mesh;
        if(!mesh||mesh>=CC_MAX_MESHES||!r->meshes[mesh].valid) continue;
        glUniformMatrix4fv(glGetUniformLocation(r->shadow_shader,"uModel"),1,GL_FALSE,r->drawlog[i].m);
        glBindVertexArray(r->meshes[mesh].vao);
        glDrawElements(GL_TRIANGLES, r->meshes[mesh].index_count, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
    glCullFace(GL_BACK); glDisable(GL_CULL_FACE);
    r->shadow_active = true;
    r->shadow_light = li;

}

/* ══════════════════════════════════════════════════════════════════════
   POST-FX
   ══════════════════════════════════════════════════════════════════════ */

CCPostFX cc_postfx_default(void) {
    return (CCPostFX){
        .bloom=true, .bloom_threshold=1.0f, .bloom_intensity=0.15f,
        .tonemap_aces=true, .fxaa=false,
        .gamma=2.2f, .saturation=1.0f, .contrast=1.0f,
        .vignette=false, .vignette_strength=0.3f,
        .chromatic_aberration=false, .ca_strength=0.002f,
    };
}

/* cc_postfx_set is implemented in engine.c — renderer exposes internal setter */
void cc_renderer_postfx_set(CCRenderer* r, const CCPostFX* fx) {
    if (r) r->postfx = *fx;
}

/* ── Matrix upload — called by camera system ─────────────────────────── */
void cc_renderer_set_matrices(CCRenderer* r,
                               const float* view, const float* proj,
                               const float* view_proj, const float* inv_vp,
                               const float* cam_pos) {
    if (!r) return;
    if (view) memcpy(r->view_mat, view, 64);
    if (proj) memcpy(r->proj_mat, proj, 64);
    memcpy(r->vp_mat, view_proj, 64);
    memcpy(r->inv_vp_mat, inv_vp, 64);
    /* TAA sub-pixel jitter: shift clip-space x/y by a fraction of a pixel using a
       Halton(2,3) sequence, so each frame samples a slightly different position.
       Applied to vp_mat (what MVP is built from). The TAA resolve pass averages
       the frames back into a supersampled image. */
    if (r->postfx.taa && r->w && r->h) {
        static const float hx[8]={0.5f,0.25f,0.75f,0.125f,0.625f,0.375f,0.875f,0.0625f};
        static const float hy[8]={0.333333f,0.666667f,0.111111f,0.444444f,0.777778f,0.222222f,0.555556f,0.888889f};
        int idx = r->taa_frame & 7u;
        float jx = (hx[idx]-0.5f) * 2.0f / (float)r->w;   /* clip-space offset = 1px in NDC */
        float jy = (hy[idx]-0.5f) * 2.0f / (float)r->h;
        /* clip.xy += jitter * clip.w  →  add jitter into the w-column (col 3) rows 0,1.
           Column-major 4x4: element [col*4 + row]. col 3 → indices 12,13. */
        r->vp_mat[12] += jx * r->vp_mat[15];
        r->vp_mat[13] += jy * r->vp_mat[15];
    }
    if (cam_pos) {
        r->camera.pos[0]=cam_pos[0];
        r->camera.pos[1]=cam_pos[1];
        r->camera.pos[2]=cam_pos[2];
    }
}







/* ══════════════════════════════════════════════════════════════════════
   MATERIALS + LIGHTS (real implementation)
   ══════════════════════════════════════════════════════════════════════ */

CCMaterial cc_renderer_material_create(CCRenderer* r, const CCMaterialDesc* d) {
    for (uint32_t i=1;i<CC_MAX_MATERIALS;i++) {
        if (!r->materials[i].valid) {
            r->materials[i].desc = *d;
            r->materials[i].valid = true;
            return i;
        }
    }
    return CC_NULL;
}
void cc_renderer_material_destroy(CCRenderer* r, CCMaterial m) {
    if (m && m<CC_MAX_MATERIALS) r->materials[m].valid=false;
}
void cc_renderer_material_set_base_color(CCRenderer* r, CCMaterial m, float cr,float cg,float cb,float ca){
    if(m && m<CC_MAX_MATERIALS && r->materials[m].valid){
        r->materials[m].desc.base_color[0]=cr; r->materials[m].desc.base_color[1]=cg;
        r->materials[m].desc.base_color[2]=cb; r->materials[m].desc.base_color[3]=ca;
    }
}
void cc_renderer_material_set_roughness(CCRenderer* r, CCMaterial m, float v){
    if(m && m<CC_MAX_MATERIALS && r->materials[m].valid) r->materials[m].desc.roughness=v;
}
void cc_renderer_material_set_metallic(CCRenderer* r, CCMaterial m, float v){
    if(m && m<CC_MAX_MATERIALS && r->materials[m].valid) r->materials[m].desc.metallic=v;
}

/* expose the current view-projection matrix + framebuffer size (for projecting
   world points to screen space, e.g. world-space UI). */
void cc_renderer_get_vp(CCRenderer* r, float out_vp[16]){
    if(r&&out_vp) memcpy(out_vp, r->vp_mat, 64);
}
void cc_renderer_get_size(CCRenderer* r, uint32_t* w, uint32_t* h){
    if(!r) return; if(w)*w=r->w; if(h)*h=r->h;
}

/* ─── Render targets ─────────────────────────────────────────────────────
   An RT is an offscreen FBO with a color texture (+ optional depth). Its color
   and depth are registered as real CCTexture handles so they can be sampled in
   materials (security-camera monitors, mirrors, feedback effects). cc_rt_capture
   copies the current composited frame (post_tex) into the RT. */

/* register an existing GL texture id into the texture table, return CCTexture */
static CCTexture rt_register_tex(CCRenderer* r, GLuint gltex, uint32_t w, uint32_t h, CCPixelFmt fmt){
    for(uint32_t i=1;i<CC_MAX_TEXTURES;i++){
        if(!r->textures[i].valid){
            r->textures[i]=(GLTexEntry){gltex,w,h,fmt,true};
            return i;
        }
    }
    return CC_NULL;
}

CCRenderTarget cc_renderer_rt_create(CCRenderer* r, uint32_t w, uint32_t h,
                                     CCPixelFmt color_fmt, bool depth, uint32_t msaa){
    (void)msaa; (void)color_fmt;
    if(r->backend==CC_RENDERER_NULL) return CC_NULL;
    for(uint32_t i=1;i<CC_MAX_RTS;i++){
        if(!r->rts[i].valid){
            GLuint col=0, dep=0;
            GLuint fbo = make_fbo_color_depth(w,h,GL_RGBA8,&col, depth?&dep:NULL);
            r->rts[i].fbo=fbo; r->rts[i].color_tex=col; r->rts[i].depth_tex=dep;
            r->rts[i].w=w; r->rts[i].h=h; r->rts[i].valid=true;
            return i;
        }
    }
    return CC_NULL;
}
void cc_renderer_rt_destroy(CCRenderer* r, CCRenderTarget rt){
    if(rt && rt<CC_MAX_RTS && r->rts[rt].valid){
        glDeleteFramebuffers(1,&r->rts[rt].fbo);
        if(r->rts[rt].color_tex) glDeleteTextures(1,&r->rts[rt].color_tex);
        if(r->rts[rt].depth_tex) glDeleteTextures(1,&r->rts[rt].depth_tex);
        r->rts[rt].valid=false;
    }
}
CCTexture cc_renderer_rt_color_texture(CCRenderer* r, CCRenderTarget rt){
    if(!rt||rt>=CC_MAX_RTS||!r->rts[rt].valid) return CC_NULL;
    return rt_register_tex(r, r->rts[rt].color_tex, r->rts[rt].w, r->rts[rt].h, CC_FMT_RGBA8);
}
CCTexture cc_renderer_rt_depth_texture(CCRenderer* r, CCRenderTarget rt){
    if(!rt||rt>=CC_MAX_RTS||!r->rts[rt].valid||!r->rts[rt].depth_tex) return CC_NULL;
    return rt_register_tex(r, r->rts[rt].depth_tex, r->rts[rt].w, r->rts[rt].h, CC_FMT_DEPTH24_STENCIL8);
}
/* Copy the current composited frame into the RT's color buffer (scaled blit). */
void cc_renderer_rt_capture(CCRenderer* r, CCRenderTarget rt){
    if(!rt||rt>=CC_MAX_RTS||!r->rts[rt].valid) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->post_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->rts[rt].fbo);
    glBlitFramebuffer(0,0,r->w,r->h, 0,0,r->rts[rt].w,r->rts[rt].h,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
void cc_renderer_rt_read_pixels(CCRenderer* r, CCRenderTarget rt, void* out, uint32_t* w, uint32_t* h){
    if(!rt||rt>=CC_MAX_RTS||!r->rts[rt].valid){ if(w)*w=0; if(h)*h=0; return; }
    if(w)*w=r->rts[rt].w; if(h)*h=r->rts[rt].h;
    if(!out) return;
    glBindFramebuffer(GL_FRAMEBUFFER, r->rts[rt].fbo);
    glReadPixels(0,0,r->rts[rt].w,r->rts[rt].h, GL_RGBA, GL_UNSIGNED_BYTE, out);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Draw a mesh as a flat-colored wireframe (GL_LINE polygon mode) directly into
   the gbuffer albedo, so it composites with the scene. Used by cc_draw_mesh_
   wireframe for debug visualization of actual mesh edges. */
void cc_renderer_draw_mesh_wireframe(CCRenderer* r, CCMesh mesh, const CCTransform3D* xf,
                                     float cr, float cg, float cb){
    if (r->backend==CC_RENDERER_NULL||!mesh||mesh>=CC_MAX_MESHES||!r->meshes[mesh].valid) return;
    CCVec3 pos={xf->pos[0],xf->pos[1],xf->pos[2]};
    CCQuat rot={xf->rot[0],xf->rot[1],xf->rot[2],xf->rot[3]};
    CCVec3 scl={xf->scale[0],xf->scale[1],xf->scale[2]};
    if(scl.x==0&&scl.y==0&&scl.z==0)scl=(CCVec3){1,1,1};
    if(rot.x==0&&rot.y==0&&rot.z==0&&rot.w==0)rot=(CCQuat){0,0,0,1};
    CCMat4 mm=mat4_trs(pos,rot,scl);
    glBindFramebuffer(GL_FRAMEBUFFER, r->gbuf_fbo);
    glViewport(0,0,r->w,r->h);
    GLenum bufs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3,bufs);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_BLEND);
    glUseProgram(r->sh_gbuf);
    glUniformMatrix4fv(glGetUniformLocation(r->sh_gbuf,"uVP"),1,GL_FALSE,r->vp_mat);
    glUniformMatrix4fv(glGetUniformLocation(r->sh_gbuf,"uModel"),1,GL_FALSE,mm.m);
    /* flat emissive-ish color via base color; disable textures by binding white */
    glUniform4f(glGetUniformLocation(r->sh_gbuf,"uBaseColor"),cr,cg,cb,1.0f);
    glUniform4f(glGetUniformLocation(r->sh_gbuf,"uTint"),1,1,1,1);
    glBindVertexArray(r->meshes[mesh].vao);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawElements(GL_TRIANGLES, r->meshes[mesh].index_count, GL_UNSIGNED_INT, 0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBindVertexArray(0);
}

CCLightId cc_renderer_light_add(CCRenderer* r, const CCLight* l) {
    for (uint32_t i=0;i<CC_MAX_LIGHTS;i++) {
        if (!r->light_valid[i]) {
            r->lights[i]=*l; r->light_valid[i]=true;
            if ((int)i>=r->num_lights) r->num_lights=i+1;
            return i+1; /* 1-based id */
        }
    }
    return 0;
}
void cc_renderer_light_update(CCRenderer* r, CCLightId id, const CCLight* l) {
    if (id>0 && id<=CC_MAX_LIGHTS) r->lights[id-1]=*l;
}
void cc_renderer_light_remove(CCRenderer* r, CCLightId id) {
    if (id>0 && id<=CC_MAX_LIGHTS) r->light_valid[id-1]=false;
}
void cc_renderer_set_shadow_softness(CCRenderer* r, float s){ if(r) r->shadow_softness = s>=0?s:0; }
void cc_renderer_set_ambient(CCRenderer* r, float cr, float cg, float cb, float intensity) {
    r->ambient_color[0]=cr; r->ambient_color[1]=cg; r->ambient_color[2]=cb;
    r->ambient_intensity=intensity;
}

/* Enable/disable procedural sky + IBL. tex==0 uses the built-in gradient (the
   only mode currently, since nothing produces cubemaps yet); a non-zero tex is
   stored for a future HDR-cubemap path but the gradient still drives lighting. */
void cc_renderer_set_sky(CCRenderer* r, CCTexture tex, bool enable) {
    r->sky_enabled = enable;
    r->sky_tex = tex;
    r->ibl_dirty = true;
}
void cc_renderer_set_sky_colors(CCRenderer* r, const float zenith[3],
                                const float horizon[3], const float ground[3],
                                float intensity) {
    if (zenith)  memcpy(r->sky_zenith,  zenith,  12);
    if (horizon) memcpy(r->sky_horizon, horizon, 12);
    if (ground)  memcpy(r->sky_ground,  ground,  12);
    if (intensity > 0.0f) r->sky_intensity = intensity;
    r->sky_enabled = true;
    r->ibl_dirty = true;
}

/* Upload lights to UBO before lighting pass. std140 layout:
   struct Light { vec4 pos; vec4 dir; vec4 color; vec4 params; }; (64 bytes each)
   then int nLights at offset 64*64=4096 */
void cc_renderer_upload_lights(CCRenderer* r) {
    float ubo_data[64*16 + 4]; /* 64 lights * 16 floats + padding for nLights */
    memset(ubo_data,0,sizeof(ubo_data));
    int active=0;
    for (uint32_t i=0;i<CC_MAX_LIGHTS;i++) {
        if (!r->light_valid[i]) continue;
        CCLight* l=&r->lights[i];
        float* d=&ubo_data[active*16];
        d[0]=l->pos[0]; d[1]=l->pos[1]; d[2]=l->pos[2]; d[3]=1;
        d[4]=l->dir[0]; d[5]=l->dir[1]; d[6]=l->dir[2]; d[7]=0;
        d[8]=l->color[0]; d[9]=l->color[1]; d[10]=l->color[2]; d[11]=l->intensity;
        d[12]=(float)l->type; d[13]=(l->range>0.0f?l->range:50.0f); d[14]=cosf(l->inner_angle); d[15]=cosf(l->outer_angle);
        active++;
    }
    glBindBuffer(GL_UNIFORM_BUFFER, r->light_ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, active*16*sizeof(float), ubo_data);
    /* nLights int at offset 4096 */
    glBufferSubData(GL_UNIFORM_BUFFER, 64*16*sizeof(float), sizeof(int), &active);
}

/* ══════════════════════════════════════════════════════════════════════
   FONT / TEXT RENDERING (stb_truetype baked atlas)
   ══════════════════════════════════════════════════════════════════════ */

static CCFont bake_font_from_ttf(CCRenderer* r, const unsigned char* ttf,
                                  float px_size) {
    if (r->font_count >= 16) return CC_NULL;
    uint32_t slot = r->font_count + 1;  /* 1-based handle */
    struct CCFontEntry* fe = &r->fonts[r->font_count];
    memset(fe, 0, sizeof(*fe));

    /* Atlas size scales with requested px height. Oversampling (below) needs more
       room, so we bake into a generous atlas. */
    uint32_t aw = 1024, ah = 1024;
    if (px_size > 72) { aw = ah = 2048; }
    unsigned char* bitmap = malloc(aw * ah);
    memset(bitmap, 0, aw * ah);
    /* bake ASCII 32..127 (96 glyphs) using the PACKED baker with 2×2
       OVERSAMPLING. The old stbtt_BakeFontBitmap produces single-sample glyph
       edges that go thin/hollow when text is drawn smaller than the baked size
       (worse once the whole frame is supersample-downsampled). Oversampling bakes
       each glyph with sub-pixel coverage → crisp, solid edges at any size. */
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, bitmap, aw, ah, 0, 1, NULL)) { free(bitmap); return CC_NULL; }
    stbtt_PackSetOversampling(&pc, 2, 2);
    int res = stbtt_PackFontRange(&pc, ttf, 0, px_size, 32, 96, fe->cdata);
    stbtt_PackEnd(&pc);
    if (res == 0) { free(bitmap); return CC_NULL; }

    /* vertical metrics */
    stbtt_fontinfo info;
    stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0));
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    float scale = stbtt_ScaleForPixelHeight(&info, px_size);
    fe->ascent   = asc * scale;
    fe->descent  = desc * scale;
    fe->line_gap = gap * scale;
    fe->px_size  = px_size;
    fe->atlas_w = aw; fe->atlas_h = ah;

    /* Upload as R8 texture */
    glGenTextures(1, &fe->atlas_tex);
    glBindTexture(GL_TEXTURE_2D, fe->atlas_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, aw, ah, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* single-channel: swizzle so .r replicates (shader reads .r anyway) */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    free(bitmap);

    fe->valid = true;
    r->font_count++;
    return slot;
}

CCFont cc_renderer_font_load(CCRenderer* r, const char* path, float px_size) {
    if (r->backend == CC_RENDERER_NULL) return CC_NULL;
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[cc] font load fail: %s\n", path); return CC_NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* ttf = malloc(sz);
    if (fread(ttf, 1, sz, f) != (size_t)sz) { fclose(f); free(ttf); return CC_NULL; }
    fclose(f);
    CCFont handle = bake_font_from_ttf(r, ttf, px_size);
    free(ttf);
    return handle;
}

/* Builtin font: bake the skill-bundled default TTF. Path resolved relative to
   the engine's known asset dir; falls back to a system font if missing. */
CCFont cc_renderer_font_builtin(CCRenderer* r) {
    if (r->backend == CC_RENDERER_NULL) return CC_NULL;
    /* If already baked, reuse slot 1 */
    if (r->font_count > 0 && r->fonts[0].valid) return 1;
    /* Optional override: a cc_default.ttf next to the executable / cwd. */
    const char* candidates[] = {
        "assets/fonts/cc_default.ttf",
        "engine/assets/fonts/cc_default.ttf",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        FILE* t = fopen(candidates[i], "rb");
        if (t) { fclose(t); return cc_renderer_font_load(r, candidates[i], 48.0f); }
    }
    /* Default: bake the font embedded in the binary — works with zero external
       files, so a shipped standalone exe always has text. */
    return bake_font_from_ttf(r, cc_default_font_data, 48.0f);
}

static void push_glyph(CCRenderer* r, GLuint atlas, float x, float y,
                       float u0, float v0, float u1, float v1,
                       uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca, float w2, float h) {
    SpriteVert* v = batch2d_alloc(r, atlas, true, true, 4);
    if (!v) return;
    v[0] = (SpriteVert){x,    y,    u0,v0, cr,cg,cb,ca};
    v[1] = (SpriteVert){x+w2, y,    u1,v0, cr,cg,cb,ca};
    v[2] = (SpriteVert){x+w2, y+h,  u1,v1, cr,cg,cb,ca};
    v[3] = (SpriteVert){x,    y+h,  u0,v1, cr,cg,cb,ca};
}

void cc_renderer_draw_text(CCRenderer* r, CCFont font, const char* text,
                            float x, float y, float size, uint32_t color) {
    if (r->backend == CC_RENDERER_NULL || !text) return;
    if (!font || font > r->font_count || !r->fonts[font-1].valid) return;
    struct CCFontEntry* fe = &r->fonts[font-1];
    float scale = size > 0 ? size / fe->px_size : 1.0f;
    uint8_t cr=(color>>24)&0xff, cg=(color>>16)&0xff, cb=(color>>8)&0xff, ca=color&0xff;

    float cursor_x = x;
    float baseline = y + fe->ascent * scale;   /* y = top of text box */
    for (const char* p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n') { cursor_x = x; baseline += (fe->ascent - fe->descent + fe->line_gap) * scale; continue; }
        if (ch < 32 || ch > 127) ch = '?';
        /* stbtt_GetPackedQuad gives the glyph quad (in atlas UVs + a unit-scale
           pixel box at the baked px_size) and advances qx. It correctly accounts
           for the pack oversampling. We then scale the pixel box by `scale` about
           the pen position to render at the requested size. */
        stbtt_aligned_quad q;
        float qx = 0.0f, qy = 0.0f;   /* local pen; we place manually below */
        stbtt_GetPackedQuad(fe->cdata, (int)fe->atlas_w, (int)fe->atlas_h,
                            ch - 32, &qx, &qy, &q, 0);
        /* q.x0/y0/x1/y1 are pixel offsets from the pen at baked size; scale them */
        float x0 = cursor_x + q.x0 * scale;
        float y0 = baseline  + q.y0 * scale;
        float x1 = cursor_x + q.x1 * scale;
        float y1 = baseline  + q.y1 * scale;
        push_glyph(r, fe->atlas_tex, x0, y0, q.s0, q.t0, q.s1, q.t1,
                   cr, cg, cb, ca, x1-x0, y1-y0);
        cursor_x += qx * scale;        /* qx now holds this glyph's advance */
    }
}

float cc_renderer_text_width(CCRenderer* r, CCFont font, const char* text, float size) {
    if (!font || font > r->font_count || !r->fonts[font-1].valid || !text) return 0;
    struct CCFontEntry* fe = &r->fonts[font-1];
    float scale = size > 0 ? size / fe->px_size : 1.0f;
    float w2 = 0, maxw = 0;
    for (const char* p = text; *p; p++) {
        if (*p == '\n') { if (w2 > maxw) maxw = w2; w2 = 0; continue; }
        unsigned char ch = (unsigned char)*p;
        if (ch < 32 || ch > 127) ch = '?';
        w2 += fe->cdata[ch-32].xadvance * scale;
    }
    return w2 > maxw ? w2 : maxw;
}

float cc_renderer_font_line_height(CCRenderer* r, CCFont font, float size) {
    if (!font || font > r->font_count || !r->fonts[font-1].valid) return size;
    struct CCFontEntry* fe = &r->fonts[font-1];
    float scale = size > 0 ? size / fe->px_size : 1.0f;
    return (fe->ascent - fe->descent + fe->line_gap) * scale;
}

/* ══════════════════════════════════════════════════════════════════════
   2D PRIMITIVES (borders, circles, lines, triangles, rotated sprites)
   ══════════════════════════════════════════════════════════════════════ */

/* Push a single triangle of solid color (no texture). */
static void push_tri(CCRenderer* r, float ax,float ay,float bx,float by,float cx,float cy,
                     uint8_t cr,uint8_t cg,uint8_t cb,uint8_t ca) {
    /* Emit as a degenerate quad (4th vert == 3rd) to reuse the quad index buffer. */
    SpriteVert* v = batch2d_alloc(r, r->white_tex, false, false, 4);
    if (!v) return;
    v[0] = (SpriteVert){ax,ay, 0,0, cr,cg,cb,ca};
    v[1] = (SpriteVert){bx,by, 1,0, cr,cg,cb,ca};
    v[2] = (SpriteVert){cx,cy, 1,1, cr,cg,cb,ca};
    v[3] = (SpriteVert){cx,cy, 1,1, cr,cg,cb,ca};  /* degenerate 2nd tri */
}

static void unpack_rgba(uint32_t c, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    *r=(c>>24)&0xff; *g=(c>>16)&0xff; *b=(c>>8)&0xff; *a=c&0xff;
}

void cc_renderer_draw_rect_border(CCRenderer* r, float x, float y, float w, float h,
                                   uint32_t fill, float border_px, uint32_t border_col) {
    if (r->backend == CC_RENDERER_NULL) return;
    if ((fill & 0xff) != 0) {
        uint8_t cr,cg,cb,ca; unpack_rgba(fill,&cr,&cg,&cb,&ca);
        /* reuse push_quad via a solid-color quad */
        extern void cc_renderer_draw_rect(CCRenderer*,float,float,float,float,uint32_t);
        cc_renderer_draw_rect(r, x, y, w, h, fill);
    }
    if (border_px > 0.0f && (border_col & 0xff) != 0) {
        cc_renderer_draw_rect(r, x, y, w, border_px, border_col);                 /* top */
        cc_renderer_draw_rect(r, x, y+h-border_px, w, border_px, border_col);     /* bottom */
        cc_renderer_draw_rect(r, x, y, border_px, h, border_col);                 /* left */
        cc_renderer_draw_rect(r, x+w-border_px, y, border_px, h, border_col);     /* right */
    }
}

void cc_renderer_draw_circle(CCRenderer* r, float cx, float cy, float rad,
                              uint32_t fill, float border_px, uint32_t border_col) {
    if (r->backend == CC_RENDERER_NULL) return;
    const int seg = rad > 40 ? 48 : 24;
    uint8_t cr,cg,cb,ca; unpack_rgba(fill,&cr,&cg,&cb,&ca);
    if (ca != 0) {
        for (int i = 0; i < seg; i++) {
            float a0 = (float)i / seg * CC_TAU, a1 = (float)(i+1) / seg * CC_TAU;
            push_tri(r, cx, cy,
                     cx+cosf(a0)*rad, cy+sinf(a0)*rad,
                     cx+cosf(a1)*rad, cy+sinf(a1)*rad, cr,cg,cb,ca);
        }
    }
    if (border_px > 0 && (border_col & 0xff)) {
        uint8_t br,bg,bb,ba; unpack_rgba(border_col,&br,&bg,&bb,&ba);
        for (int i = 0; i < seg; i++) {
            float a0=(float)i/seg*CC_TAU, a1=(float)(i+1)/seg*CC_TAU;
            float ox0=cosf(a0),oy0=sinf(a0),ox1=cosf(a1),oy1=sinf(a1);
            float ri=rad-border_px;
            /* quad strip segment as two tris */
            push_tri(r, cx+ox0*ri,cy+oy0*ri, cx+ox0*rad,cy+oy0*rad, cx+ox1*rad,cy+oy1*rad, br,bg,bb,ba);
            push_tri(r, cx+ox0*ri,cy+oy0*ri, cx+ox1*rad,cy+oy1*rad, cx+ox1*ri,cy+oy1*ri, br,bg,bb,ba);
        }
    }
}

void cc_renderer_draw_line(CCRenderer* r, float x0,float y0,float x1,float y1,
                            float width, uint32_t col) {
    if (r->backend == CC_RENDERER_NULL) return;
    uint8_t cr,cg,cb,ca; unpack_rgba(col,&cr,&cg,&cb,&ca);
    float dx=x1-x0, dy=y1-y0, len=sqrtf(dx*dx+dy*dy);
    if (len < 1e-4f) return;
    float nx=-dy/len*width*0.5f, ny=dx/len*width*0.5f;
    push_tri(r, x0+nx,y0+ny, x1+nx,y1+ny, x1-nx,y1-ny, cr,cg,cb,ca);
    push_tri(r, x0+nx,y0+ny, x1-nx,y1-ny, x0-nx,y0-ny, cr,cg,cb,ca);
}

void cc_renderer_draw_triangle(CCRenderer* r, float ax,float ay,float bx,float by,
                                float cx,float cy, uint32_t col) {
    if (r->backend == CC_RENDERER_NULL) return;
    uint8_t cr,cg,cb,ca; unpack_rgba(col,&cr,&cg,&cb,&ca);
    push_tri(r, ax,ay, bx,by, cx,cy, cr,cg,cb,ca);
}

/* Rotated / sub-rect sprite. angle in degrees about the sprite center. */
void cc_renderer_draw_sprite_ex(CCRenderer* r, uint32_t tex_id,
                                 float x,float y,float w,float h,
                                 float u0,float v0,float u1,float v1,
                                 float angle_deg, uint32_t tint) {
    if (r->backend == CC_RENDERER_NULL) return;
    GLuint gl_tex = (tex_id && tex_id < CC_MAX_TEXTURES && r->textures[tex_id].valid)
                    ? r->textures[tex_id].id : r->white_tex;
    uint8_t cr,cg,cb,ca; unpack_rgba(tint,&cr,&cg,&cb,&ca);
    if (fabsf(angle_deg) < 1e-4f) {
        SpriteVert* v = batch2d_alloc(r, gl_tex, true, false, 4);
        if (!v) return;
        v[0]=(SpriteVert){x,   y,   u0,v0, cr,cg,cb,ca};
        v[1]=(SpriteVert){x+w, y,   u1,v0, cr,cg,cb,ca};
        v[2]=(SpriteVert){x+w, y+h, u1,v1, cr,cg,cb,ca};
        v[3]=(SpriteVert){x,   y+h, u0,v1, cr,cg,cb,ca};
        return;
    }
    /* rotated: rotate the 4 corners about center */
    float rad = angle_deg * CC_DEG2RAD, s = sinf(rad), c = cosf(rad);
    float halfw = w*0.5f, halfh = h*0.5f, ccx = x+halfw, ccy = y+halfh;
    float lx[4] = {-halfw, halfw, halfw, -halfw};
    float ly[4] = {-halfh, -halfh, halfh, halfh};
    float uu[4] = {u0,u1,u1,u0}, vv[4] = {v0,v0,v1,v1};
    SpriteVert* v = batch2d_alloc(r, gl_tex, true, false, 4);
    if (!v) return;
    for (int i=0;i<4;i++) {
        v[i].x = ccx + lx[i]*c - ly[i]*s;
        v[i].y = ccy + lx[i]*s + ly[i]*c;
        v[i].u = uu[i]; v[i].v = vv[i];
        v[i].r=cr; v[i].g=cg; v[i].b=cb; v[i].a=ca;
    }
}

/* ══════════════════════════════════════════════════════════════════════
   PIXEL READBACK + RESIZE
   ══════════════════════════════════════════════════════════════════════ */

/* Copy the final composited frame (post_fbo) into caller-managed RGBA buffer.
   Sets *out_w/*out_h; the returned pointer is owned by the renderer (valid
   until the next call). Top-down (row 0 = top). */
const uint8_t* cc_renderer_frame_pixels(CCRenderer* r, uint32_t* out_w, uint32_t* out_h) {
    if (r->backend == CC_RENDERER_NULL) { if(out_w)*out_w=0; if(out_h)*out_h=0; return NULL; }
    size_t need = (size_t)r->w * r->h * 4;
    if (r->pixel_buf_size < need) {
        r->pixel_buf = realloc(r->pixel_buf, need);
        r->pixel_buf_size = need;
    }
    glFinish();
    glBindFramebuffer(GL_FRAMEBUFFER, r->post_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    /* read bottom-up then flip into pixel_buf top-down */
    uint8_t* tmp = malloc(need);
    glReadPixels(0,0,(GLsizei)r->w,(GLsizei)r->h,GL_RGBA,GL_UNSIGNED_BYTE,tmp);
    for (uint32_t row=0; row<r->h; row++)
        memcpy(r->pixel_buf + row*r->w*4, tmp + (r->h-1-row)*r->w*4, r->w*4);
    free(tmp);
    if (out_w) *out_w = r->w;
    if (out_h) *out_h = r->h;
    return r->pixel_buf;
}

/* Rebuild the size-dependent 3D target chain at (w,h) WITHOUT resizing the
   OSMesa/window backing buffer — used for supersampling (SSAA/USD), where the 3D
   pipeline renders larger than the display and the screenshot downsamples. */
void cc_renderer_resize_internal(CCRenderer* r, uint32_t w, uint32_t h) {
    if (r->backend == CC_RENDERER_NULL || !w || !h) return;
    if (w == r->w && h == r->h) return;
    r->w = w; r->h = h;

#ifdef CC_USE_OSMESA
    /* OSMesa's backing buffer must be at least as large as the render size, since
       the default framebuffer (used by some passes) writes into it. Grow it to
       the supersampled size; the screenshot still emits display-res pixels. */
    if (r->backend == CC_RENDERER_OSMESA) {
        r->osmesa_buf = realloc(r->osmesa_buf, (size_t)w*h*4);
        OSMesaMakeCurrent(r->osmesa_ctx, r->osmesa_buf, GL_UNSIGNED_BYTE, w, h);
    }
#endif
    if (r->gbuf_fbo)  { glDeleteFramebuffers(1,&r->gbuf_fbo);  r->gbuf_fbo=0; }
    if (r->gbuf_albedo_rough) glDeleteTextures(1,&r->gbuf_albedo_rough);
    if (r->gbuf_normal_metal) glDeleteTextures(1,&r->gbuf_normal_metal);
    if (r->gbuf_emissive_ao)  glDeleteTextures(1,&r->gbuf_emissive_ao);
    if (r->gbuf_depth)        glDeleteTextures(1,&r->gbuf_depth);
    build_gbuffer(r, w, h);
    if (r->hdr_fbo)  glDeleteFramebuffers(1,&r->hdr_fbo);
    if (r->hdr_color_tex) glDeleteTextures(1,&r->hdr_color_tex);
    r->hdr_fbo = make_fbo_color_depth(w, h, GL_RGBA16F, &r->hdr_color_tex, NULL);
    if (r->post_fbo) glDeleteFramebuffers(1,&r->post_fbo);
    if (r->post_tex) glDeleteTextures(1,&r->post_tex);
    r->post_fbo = make_fbo_color_depth(w, h, GL_RGBA8, &r->post_tex, NULL);
    build_bloom(r, w, h);
    build_ssao_targets(r, w, h);
    build_ssr_targets(r, w, h);
    /* AA + TAA scratch targets track the render size too */
    if (r->aa_fbo) { glDeleteFramebuffers(1,&r->aa_fbo); r->aa_fbo=0; }
    if (r->aa_tex) { glDeleteTextures(1,&r->aa_tex); r->aa_tex=0; }
    r->taa_history_valid = false;   /* history size changed */
}

void cc_renderer_display_size(CCRenderer* r, uint32_t* w, uint32_t* h){
    if(!r){ if(w)*w=0; if(h)*h=0; return; }
    if(w)*w=r->disp_w; if(h)*h=r->disp_h;
}

void cc_renderer_resize(CCRenderer* r, uint32_t w, uint32_t h) {
    if (r->backend == CC_RENDERER_NULL || !w || !h) return;
    r->disp_w = w; r->disp_h = h;
    /* apply the current supersample factor to the new display size */
    uint32_t iw = (uint32_t)(w * r->ss_scale + 0.5f);
    uint32_t ih = (uint32_t)(h * r->ss_scale + 0.5f);
    if (iw==r->w && ih==r->h) return;
    cc_renderer_resize_internal(r, iw, ih);
}

/* ══════════════════════════════════════════════════════════════════════
   GPU SKINNING
   ══════════════════════════════════════════════════════════════════════ */

/* Attach per-vertex joint indices + weights to an existing mesh so it can be
   drawn skinned. joints/weights arrays are [vertex_count][4]. */
void cc_renderer_mesh_attach_skin(CCRenderer* r, CCMesh mesh,
                                   const uint16_t* joints4, const float* weights4,
                                   uint32_t vertex_count) {
    if (r->backend==CC_RENDERER_NULL || !mesh || mesh>=CC_MAX_MESHES || !r->meshes[mesh].valid) return;
    GLMeshEntry* me = &r->meshes[mesh];
    /* interleave as [jx jy jz jw wx wy wz ww] floats (joints as float for the shader) */
    float* data = malloc(vertex_count * 8 * sizeof(float));
    for (uint32_t i=0;i<vertex_count;i++) {
        data[i*8+0]=(float)joints4[i*4+0]; data[i*8+1]=(float)joints4[i*4+1];
        data[i*8+2]=(float)joints4[i*4+2]; data[i*8+3]=(float)joints4[i*4+3];
        data[i*8+4]=weights4[i*4+0]; data[i*8+5]=weights4[i*4+1];
        data[i*8+6]=weights4[i*4+2]; data[i*8+7]=weights4[i*4+3];
    }
    glBindVertexArray(me->vao);
    if (!me->skin_vbo) glGenBuffers(1, &me->skin_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, me->skin_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_count*8*sizeof(float), data, GL_STATIC_DRAW);
    glEnableVertexAttribArray(5); glVertexAttribPointer(5,4,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0);
    glEnableVertexAttribArray(6); glVertexAttribPointer(6,4,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(4*sizeof(float)));
    glBindVertexArray(0);
    free(data);
}

/* Upload skinning matrices (bone_count ≤ 256, column-major mat4 each). */
void cc_renderer_set_bones(CCRenderer* r, const float* mats16, uint32_t bone_count) {
    if (r->backend==CC_RENDERER_NULL) return;
    if (bone_count > 256) bone_count = 256;
    glBindBuffer(GL_UNIFORM_BUFFER, r->bone_ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, bone_count*16*sizeof(float), mats16);
}

/* Draw a skinned mesh. Identical material/matrix setup to draw_mesh but uses the
   skinning shader (reads the bone UBO + joint/weight attributes). Call
   cc_renderer_set_bones() first. */
void cc_renderer_draw_skinned(CCRenderer* r, CCMesh mesh, CCMaterial mat, const CCTransform3D* xf) {
    if (r->backend==CC_RENDERER_NULL||!mesh||mesh>=CC_MAX_MESHES||!r->meshes[mesh].valid) return;
    if (!r->meshes[mesh].skin_vbo) { cc_renderer_draw_mesh(r, mesh, mat, xf); return; } /* no skin → normal */

    CCVec3 pos={xf->pos[0],xf->pos[1],xf->pos[2]};
    CCQuat rot={xf->rot[0],xf->rot[1],xf->rot[2],xf->rot[3]};
    CCVec3 scl={xf->scale[0],xf->scale[1],xf->scale[2]};
    if (scl.x==0&&scl.y==0&&scl.z==0) scl=(CCVec3){1,1,1};
    if (rot.x==0&&rot.y==0&&rot.z==0&&rot.w==0) rot=(CCQuat){0,0,0,1};
    CCMat4 model_mat=mat4_trs(pos,rot,scl);
    CCMat4 vp_cc; memcpy(vp_cc.m,r->vp_mat,64);
    CCMat4 mvp_mat=mat4_mul(vp_cc,model_mat);
    CCMat4 normal_mat=mat4_upper3x3_inverse_transpose(model_mat);

    glBindFramebuffer(GL_FRAMEBUFFER,r->gbuf_fbo);
    glViewport(0,0,r->w,r->h);
    GLenum bufs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3,bufs);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_BLEND);
    glUseProgram(r->sh_gbuf_skin);
    glUniformMatrix4fv(glGetUniformLocation(r->sh_gbuf_skin,"uMVP"),1,GL_FALSE,mvp_mat.m);
    glUniformMatrix4fv(glGetUniformLocation(r->sh_gbuf_skin,"uModel"),1,GL_FALSE,model_mat.m);
    float nm3[9]={normal_mat.m[0],normal_mat.m[1],normal_mat.m[2],
                  normal_mat.m[4],normal_mat.m[5],normal_mat.m[6],
                  normal_mat.m[8],normal_mat.m[9],normal_mat.m[10]};
    glUniformMatrix3fv(glGetUniformLocation(r->sh_gbuf_skin,"uNormalMat"),1,GL_FALSE,nm3);

    MaterialEntry* me=(mat&&mat<CC_MAX_MATERIALS&&r->materials[mat].valid)?&r->materials[mat]:NULL;
    CCMaterialDesc* md=me?&me->desc:NULL;
    float bc[4]={1,1,1,1},rough=0.5f,metal=0.0f,em[3]={0,0,0};
    if(md){memcpy(bc,md->base_color,16);rough=md->roughness;metal=md->metallic;memcpy(em,md->emissive,12);}
    glUniform4fv(glGetUniformLocation(r->sh_gbuf_skin,"uBaseColor"),1,bc);
    glUniform1f(glGetUniformLocation(r->sh_gbuf_skin,"uRoughness"),rough);
    glUniform1f(glGetUniformLocation(r->sh_gbuf_skin,"uMetallic"),metal);
    glUniform3fv(glGetUniformLocation(r->sh_gbuf_skin,"uEmissiveFactor"),1,em);
    /* uTint/uUnlit MUST be set — the shared GBUF_FRAG multiplies albedo by uTint
       and discards on alpha<0.01; leaving uTint at its default 0 makes every
       skinned fragment discard (mesh renders invisibly). */
    float tint[4]={1,1,1,1};
    glUniform4fv(glGetUniformLocation(r->sh_gbuf_skin,"uTint"),1,tint);
    glUniform1i(glGetUniformLocation(r->sh_gbuf_skin,"uUnlit"), md && md->unlit ? 1 : 0);
    { int _sm=md?md->shading_model:0; int _tb=(md&&md->toon_bands)?md->toon_bands:4;
      glUniform1i(glGetUniformLocation(r->sh_gbuf_skin,"uShadingModel"), _sm);
      glUniform1i(glGetUniformLocation(r->sh_gbuf_skin,"uToonBands"), _tb);
      if(md&&_sm!=0){ r->style_toon_specular=md->toon_specular; r->style_rim_strength=md->rim_strength;
        r->style_rim_power=md->rim_power; r->style_rim_color[0]=md->rim_color[0];
        r->style_rim_color[1]=md->rim_color[1]; r->style_rim_color[2]=md->rim_color[2]; } }
    GLuint albedo_id=md&&md->albedo_map?r->textures[md->albedo_map].id:r->white_tex;
    GLuint norm_id  =md&&md->normal_map?r->textures[md->normal_map].id:r->default_normal_tex;
    GLuint rm_id    =md&&md->roughness_metallic_map?r->textures[md->roughness_metallic_map].id:r->white_tex;
    GLuint em_id    =md&&md->emissive_map?r->textures[md->emissive_map].id:r->white_tex; /* white so emissive factor passes through (see gbuf path) */
    GLuint ao_id    =md&&md->ao_map?r->textures[md->ao_map].id:r->white_tex;
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,albedo_id);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,norm_id);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,rm_id);
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,em_id);
    glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D,ao_id);
    glUniform1i(glGetUniformLocation(r->sh_gbuf_skin,"uAlbedo"),0);
    glUniform1i(glGetUniformLocation(r->sh_gbuf_skin,"uNormalMap"),1);
    glUniform1i(glGetUniformLocation(r->sh_gbuf_skin,"uRoughMetal"),2);
    glUniform1i(glGetUniformLocation(r->sh_gbuf_skin,"uEmissive"),3);
    glUniform1i(glGetUniformLocation(r->sh_gbuf_skin,"uAO"),4);
    glUniform1i(glGetUniformLocation(r->sh_gbuf_skin,"uHasNormalMap"),md&&md->normal_map?1:0);
    glUniform1i(glGetUniformLocation(r->sh_gbuf_skin,"uHasAOMap"),md&&md->ao_map?1:0);

    glBindVertexArray(r->meshes[mesh].vao);
    glDrawElements(GL_TRIANGLES,r->meshes[mesh].index_count,GL_UNSIGNED_INT,0);
    /* CRITICAL: mark that 3D geometry was drawn this frame. The deferred lighting
       pass is gated on stats.meshes_drawn > 0 (the pure-2D-frame skip optimization).
       draw_mesh increments this; draw_skinned historically did NOT, so a frame that
       drew ONLY skinned meshes left meshes_drawn==0 → the lighting/composite pass
       was skipped → the g-buffer (with the skinned geometry correctly rasterized)
       was never composited → the mesh rendered fully black. This was BUG-001. */
    r->stats.draw_calls++;
    r->stats.meshes_drawn++;
    r->stats.triangles += r->meshes[mesh].index_count/3;
    glBindVertexArray(0);
}

/* ══════════════════════════════════════════════════════════════════════
   CUSTOM SHADERS (user GLSL injection)
   ══════════════════════════════════════════════════════════════════════ */

CCShader cc_renderer_shader_create(CCRenderer* r, const char* vert_src, const char* frag_src) {
    if (r->backend == CC_RENDERER_NULL || !vert_src || !frag_src) return 0;
    GLuint prog = compile_shader_src(vert_src, frag_src, "user_shader");
    if (!prog) return 0;
    for (uint32_t i = 1; i < CC_MAX_SHADERS; i++) {
        if (!r->shaders[i].valid) {
            r->shaders[i].program = prog;
            r->shaders[i].valid = true;
            return i;
        }
    }
    glDeleteProgram(prog);
    return 0;
}

void cc_renderer_shader_destroy(CCRenderer* r, CCShader s) {
    if (s == 0 || s >= CC_MAX_SHADERS || !r->shaders[s].valid) return;
    glDeleteProgram(r->shaders[s].program);
    r->shaders[s].valid = false;
}

/* Uniform setters — operate on whichever shader is passed. */
static GLuint shader_prog(CCRenderer* r, CCShader s) {
    return (s > 0 && s < CC_MAX_SHADERS && r->shaders[s].valid) ? r->shaders[s].program : 0;
}
void cc_renderer_shader_set_int(CCRenderer* r, CCShader s, const char* n, int v) {
    GLuint p = shader_prog(r,s); if(!p) return; glUseProgram(p); glUniform1i(glGetUniformLocation(p,n),v);
}
void cc_renderer_shader_set_float(CCRenderer* r, CCShader s, const char* n, float v) {
    GLuint p = shader_prog(r,s); if(!p) return; glUseProgram(p); glUniform1f(glGetUniformLocation(p,n),v);
}
void cc_renderer_shader_set_vec3(CCRenderer* r, CCShader s, const char* n, float x,float y,float z) {
    GLuint p = shader_prog(r,s); if(!p) return; glUseProgram(p); glUniform3f(glGetUniformLocation(p,n),x,y,z);
}
void cc_renderer_shader_set_vec4(CCRenderer* r, CCShader s, const char* n, float x,float y,float z,float w) {
    GLuint p = shader_prog(r,s); if(!p) return; glUseProgram(p); glUniform4f(glGetUniformLocation(p,n),x,y,z,w);
}
void cc_renderer_shader_set_mat4(CCRenderer* r, CCShader s, const char* n, const float* m) {
    GLuint p = shader_prog(r,s); if(!p) return; glUseProgram(p); glUniformMatrix4fv(glGetUniformLocation(p,n),1,GL_FALSE,m);
}

/* Full-screen custom post pass: run a user fragment shader over the composited
   frame. Samples the current post_fbo color as uScene (unit 0). This is the
   "inject a custom render pass" hook. */
void cc_renderer_custom_fullscreen_pass(CCRenderer* r, CCShader s) {
    GLuint p = shader_prog(r, s);
    if (!p) return;
    /* ping-pong: read post_tex, write back via hdr as scratch — simplest correct
       approach is copy post→scratch, then run shader scratch→post. Reuse bloom[0]. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->post_fbo);
    glViewport(0,0,r->w,r->h);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(p);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->post_tex);
    glUniform1i(glGetUniformLocation(p,"uScene"), 0);
    glUniform2f(glGetUniformLocation(p,"uResolution"), (float)r->w, (float)r->h);
    glBindVertexArray(r->fsq_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glEnable(GL_DEPTH_TEST);
}

/* ══════════════════════════════════════════════════════════════════════
   WINDOW LIFECYCLE
   ══════════════════════════════════════════════════════════════════════ */
bool cc_renderer_should_close(CCRenderer* r) {
#ifdef CC_USE_GLFW
    if (r->window) return glfwWindowShouldClose(r->window);
#endif
    (void)r;
    return false;  /* headless never "closes" on its own */
}

void cc_renderer_poll_events(CCRenderer* r) {
#ifdef CC_USE_GLFW
    if (r->window) glfwPollEvents();
#endif
    (void)r;
}

/* ── Debug accessors (used by debug.c) ── */
void cc_renderer_get_stats(CCRenderer* r, uint32_t* dc, uint32_t* tri, uint32_t* meshes, uint32_t* lights, uint32_t* texs, size_t* texbytes){
    if(!r)return;
    if(dc)*dc=r->stats_prev.draw_calls; if(tri)*tri=r->stats_prev.triangles;
    if(meshes)*meshes=r->stats_prev.meshes_drawn; if(lights)*lights=(uint32_t)r->num_lights;
    uint32_t nt=0; size_t tb=0;
    for(uint32_t i=0;i<CC_MAX_TEXTURES;i++) if(r->textures[i].valid){ nt++; tb+=(size_t)r->textures[i].w*r->textures[i].h*4; }
    if(texs)*texs=nt; if(texbytes)*texbytes=tb;
}
uint32_t cc_renderer_drawlog_count(CCRenderer* r){ return r?r->drawlog_count:0; }
void cc_renderer_drawlog_get(CCRenderer* r, uint32_t i, uint32_t* mesh, uint32_t* mat, uint32_t* tris, float* m16){
    if(!r||i>=r->drawlog_count)return;
    if(mesh)*mesh=r->drawlog[i].mesh; if(mat)*mat=r->drawlog[i].material;
    if(tris)*tris=r->drawlog[i].tris; if(m16)memcpy(m16,r->drawlog[i].m,64);
}
void cc_renderer_light_get(CCRenderer* r, uint32_t i, int* type, float* dir3, float* col3, float* intensity){
    if(!r||i>=(uint32_t)r->num_lights)return;
    if(type)*type=(int)r->lights[i].type;
    if(dir3){dir3[0]=r->lights[i].dir[0];dir3[1]=r->lights[i].dir[1];dir3[2]=r->lights[i].dir[2];}
    if(col3){col3[0]=r->lights[i].color[0];col3[1]=r->lights[i].color[1];col3[2]=r->lights[i].color[2];}
    if(intensity)*intensity=r->lights[i].intensity;
}
void cc_renderer_light_pos_range(CCRenderer* r, uint32_t i, float* pos3, float* range){
    if(!r||i>=(uint32_t)r->num_lights)return;
    if(pos3){pos3[0]=r->lights[i].pos[0];pos3[1]=r->lights[i].pos[1];pos3[2]=r->lights[i].pos[2];}
    if(range)*range=r->lights[i].range;
}
void cc_renderer_set_debug_view(CCRenderer* r, int v){ if(r)r->debug_view=v; }
int  cc_renderer_get_debug_view(CCRenderer* r){ return r?r->debug_view:0; }
void cc_renderer_camera_vp(CCRenderer* r, float* vp16){ if(r&&vp16) memcpy(vp16,r->vp_mat,64); }
void cc_renderer_camera_matrices(CCRenderer* r, float* view16, float* proj16){
    if(!r)return; if(view16)memcpy(view16,r->view_mat,64); if(proj16)memcpy(proj16,r->proj_mat,64);
}

/* Update an existing texture's pixels (same dimensions). */
void cc_renderer_texture_update(CCRenderer* r, CCTexture t, const void* px){
    if(r->backend==CC_RENDERER_NULL||!t||t>=CC_MAX_TEXTURES||!r->textures[t].valid||!px)return;
    GLenum fmt=GL_RGBA, type=GL_UNSIGNED_BYTE;
    switch(r->textures[t].fmt){
        case CC_FMT_R8: fmt=GL_RED; break;
        case CC_FMT_RGB8: fmt=GL_RGB; break;
        case CC_FMT_RGBA16F: fmt=GL_RGBA; type=GL_FLOAT; break;
        default: break;
    }
    glBindTexture(GL_TEXTURE_2D, r->textures[t].id);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,r->textures[t].w,r->textures[t].h,fmt,type,px);
}

/* Unit quad in the XY plane (2 tris), facing +Z, UV 0..1. */
CCMesh cc_renderer_mesh_quad(CCRenderer* r){
    CCVertex v[4]={0}; 
    float p[4][3]={{-0.5f,-0.5f,0},{0.5f,-0.5f,0},{0.5f,0.5f,0},{-0.5f,0.5f,0}};
    float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for(int i=0;i<4;i++){ v[i].pos[0]=p[i][0];v[i].pos[1]=p[i][1];v[i].pos[2]=p[i][2];
        v[i].normal[2]=1.0f; v[i].uv[0]=uv[i][0]; v[i].uv[1]=uv[i][1];
        v[i].color[0]=v[i].color[1]=v[i].color[2]=v[i].color[3]=255; }
    uint32_t idx[6]={0,1,2,0,2,3};
    return cc_renderer_mesh_create(r,v,4,idx,6,CC_MESH_STATIC);
}
