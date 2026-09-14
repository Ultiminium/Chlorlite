#pragma once
#include "cc/render.h"
#include <GL/gl.h>

/* Forward-declare the full renderer struct (defined in renderer.c) */
typedef struct CCRenderer CCRenderer;

/* Internal API used by engine.c to drive the renderer */
CCRenderer* cc_renderer_create(CCRendererBackend backend, uint32_t w, uint32_t h,
                                const char* title, const char* screenshot_dir);
void        cc_renderer_destroy(CCRenderer* r);
void        cc_renderer_frame_begin(CCRenderer* r);
void        cc_renderer_frame_end(CCRenderer* r);
const char* cc_renderer_screenshot(CCRenderer* r, const char* path);

/* GLFW windowed input: the engine reads these when a window exists, because
   qwerty's evdev/X11 backends never see events delivered to a GLFW window. */
int  cc_renderer_has_window(CCRenderer* r);
int  cc_renderer_glfw_key(CCRenderer* r, int qkey);
int  cc_renderer_glfw_mouse_btn(CCRenderer* r, int qbtn);
void cc_renderer_glfw_cursor(CCRenderer* r, double* x, double* y);
void cc_renderer_glfw_capture_cursor(CCRenderer* r, int capture);

/* Texture */
CCTexture cc_renderer_texture_load(CCRenderer* r, const char* path);
CCTexture cc_renderer_texture_load_srgb(CCRenderer* r, const char* path);
CCMaterial cc_renderer_material_create(CCRenderer* r, const CCMaterialDesc* d);
CCMaterial cc_renderer_material_load_pbr(CCRenderer* r, const char* dir, const CCMaterialDesc* base);
CCTexture cc_renderer_texture_create(CCRenderer* r, const CCTextureDesc* d, const void* px);
CCTexture cc_renderer_texture_proc(CCRenderer* r, const CCTextureDesc* d,
    void (*gen)(uint8_t*,uint32_t,uint32_t,void*), void* ud);
void      cc_renderer_texture_destroy(CCRenderer* r, CCTexture t);

/* Mesh */
CCMesh cc_renderer_mesh_create(CCRenderer* r, const CCVertex* v, uint32_t nv,
                                const uint32_t* idx, uint32_t ni, CCMeshUsage usage);
void   cc_renderer_mesh_destroy(CCRenderer* r, CCMesh m);

/* 3D draw */
void cc_renderer_draw_mesh(CCRenderer* r, CCMesh mesh, CCMaterial mat, const CCTransform3D* xf);
void cc_renderer_draw_mesh_instanced(CCRenderer* r, CCMesh mesh, CCMaterial mat, const CCTransform3D* xforms, uint32_t count);
void cc_renderer_draw_billboards(CCRenderer* r, CCTexture tex, const CCBillboard* bbs, uint32_t count, CCBillboardMode mode);
void cc_renderer_submit_decal(CCRenderer* r, CCTexture tex, const CCDecal* d);

/* 2D draw */
void cc_renderer_draw_sprite(CCRenderer* r, uint32_t tex,
                              float x, float y, float w, float h,
                              float u0, float v0, float u1, float v1,
                              float angle, uint32_t tint);
void cc_renderer_draw_rect(CCRenderer* r, float x, float y, float w, float h, uint32_t col);
void cc_renderer_postfx_set(CCRenderer* r, const CCPostFX* fx);
void cc_renderer_set_aa(CCRenderer* r, int mode, float render_scale, int usd_rounds);
void cc_renderer_resize_internal(CCRenderer* r, uint32_t w, uint32_t h);
void cc_renderer_set_matrices(CCRenderer* r, const float* view, const float* proj, const float* view_proj, const float* inv_vp, const float* cam_pos);

/* Fonts / text */
CCFont cc_renderer_font_load(CCRenderer* r, const char* path, float px_size);
CCFont cc_renderer_font_builtin(CCRenderer* r);
void   cc_renderer_draw_text(CCRenderer* r, CCFont font, const char* text, float x, float y, float size, uint32_t color);
float  cc_renderer_text_width(CCRenderer* r, CCFont font, const char* text, float size);
float  cc_renderer_font_line_height(CCRenderer* r, CCFont font, float size);

/* 2D primitives */
void cc_renderer_draw_rect_border(CCRenderer* r, float x, float y, float w, float h, uint32_t fill, float border_px, uint32_t border_col);
void cc_renderer_draw_circle(CCRenderer* r, float cx, float cy, float rad, uint32_t fill, float border_px, uint32_t border_col);
void cc_renderer_draw_line(CCRenderer* r, float x0, float y0, float x1, float y1, float width, uint32_t col);
void cc_renderer_draw_triangle(CCRenderer* r, float ax, float ay, float bx, float by, float cx, float cy, uint32_t col);
void cc_renderer_draw_sprite_ex(CCRenderer* r, uint32_t tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, float angle_deg, uint32_t tint);

/* Readback + resize */
const uint8_t* cc_renderer_frame_pixels(CCRenderer* r, uint32_t* out_w, uint32_t* out_h);
void cc_renderer_resize(CCRenderer* r, uint32_t w, uint32_t h);

/* GPU skinning */
void cc_renderer_mesh_attach_skin(CCRenderer* r, CCMesh mesh, const uint16_t* joints4, const float* weights4, uint32_t vertex_count);
void cc_renderer_set_bones(CCRenderer* r, const float* mats16, uint32_t bone_count);
void cc_renderer_draw_skinned(CCRenderer* r, CCMesh mesh, CCMaterial mat, const CCTransform3D* xf);

/* Custom shaders */
CCShader cc_renderer_shader_create(CCRenderer* r, const char* vert_src, const char* frag_src);
void cc_renderer_shader_destroy(CCRenderer* r, CCShader s);
void cc_renderer_shader_set_int(CCRenderer* r, CCShader s, const char* n, int v);
void cc_renderer_shader_set_float(CCRenderer* r, CCShader s, const char* n, float v);
void cc_renderer_shader_set_vec3(CCRenderer* r, CCShader s, const char* n, float x,float y,float z);
void cc_renderer_shader_set_vec4(CCRenderer* r, CCShader s, const char* n, float x,float y,float z,float w);
void cc_renderer_shader_set_mat4(CCRenderer* r, CCShader s, const char* n, const float* m);
void cc_renderer_custom_fullscreen_pass(CCRenderer* r, CCShader s);

/* Window lifecycle */
bool cc_renderer_should_close(CCRenderer* r);
void cc_renderer_poll_events(CCRenderer* r);

/* Debug accessors */
void cc_renderer_get_stats(CCRenderer* r, uint32_t* dc, uint32_t* tri, uint32_t* meshes, uint32_t* lights, uint32_t* texs, size_t* texbytes);
uint32_t cc_renderer_drawlog_count(CCRenderer* r);
void cc_renderer_drawlog_get(CCRenderer* r, uint32_t i, uint32_t* mesh, uint32_t* mat, uint32_t* tris, float* m16);
void cc_renderer_light_get(CCRenderer* r, uint32_t i, int* type, float* dir3, float* col3, float* intensity);
void cc_renderer_set_debug_view(CCRenderer* r, int v);
int  cc_renderer_get_debug_view(CCRenderer* r);
void cc_renderer_camera_matrices(CCRenderer* r, float* view16, float* proj16);
void cc_renderer_camera_vp(CCRenderer* r, float* vp16);
void cc_renderer_light_pos_range(CCRenderer* r, uint32_t i, float* pos3, float* range);
void cc_renderer_texture_update(CCRenderer* r, CCTexture t, const void* px);
CCMesh cc_renderer_mesh_quad(CCRenderer* r);
