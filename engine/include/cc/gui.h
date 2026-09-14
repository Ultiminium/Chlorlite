#pragma once
/*
 * CC immediate-mode GUI toolkit — menus, HUD, and debug panels. Immediate mode
 * means there's no retained widget tree: you call the widget functions every
 * frame and they both draw AND report interaction, with all state living in your
 * own variables. This is what surfaces save/load menus, dialogue choices,
 * inventories, and health/sanity/stamina HUDs to the player.
 *
 * Widgets draw through the 2D overlay (cc_draw_rect / cc_draw_text) so they
 * appear in screenshots, and interaction reads cc_mouse_pos / cc_mouse_down —
 * which in headless mode can be driven deterministically via cc_input_inject_
 * mouse_move / cc_input_inject_mouse_button (so menus are unit-testable).
 *
 * Frame shape:
 *   cc_gui_begin_frame(eng);                 // samples the mouse once
 *   if (cc_gui_window(eng, "Pause", 40,40, 240,200)) {
 *       cc_gui_label(eng, "Paused");
 *       if (cc_gui_button(eng, "Resume")) resume();
 *       if (cc_gui_button(eng, "Save"))   open_save_menu();
 *       cc_gui_progress(eng, "Sanity", sanity, 0);
 *       cc_gui_slider(eng, "Volume", vol, 0, 1);
 *       cc_gui_end_window(eng);
 *   }
 *   cc_gui_end_frame(eng);
 *
 * Single global context (fine for one HUD/menu stack at a time). Flow widgets
 * lay out top-to-bottom inside the current window; cc_gui_button_at places a
 * button at an explicit rect for free-form menu layouts.
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCEngine CCEngine;

/* ─── frame ───────────────────────────────────────────────────────────────── */
/* Begin a GUI frame: samples mouse position + button state once for the frame. */
void cc_gui_begin_frame(CCEngine* eng);
void cc_gui_end_frame(CCEngine* eng);

/* ─── window / panel ──────────────────────────────────────────────────────── */
/* Open a titled panel at (x,y) size (w,h). Returns true (call end_window after
 * its widgets). */
bool cc_gui_window(CCEngine* eng, const char* title, float x, float y, float w, float h);
void cc_gui_end_window(CCEngine* eng);

/* ─── flow widgets (laid out top-to-bottom in the current window) ─────────── */
void  cc_gui_label(CCEngine* eng, const char* text);
/* Button; returns true the frame it's clicked (mouse down while hovering). */
bool  cc_gui_button(CCEngine* eng, const char* label);
/* Horizontal slider; returns the (possibly dragged) value. */
float cc_gui_slider(CCEngine* eng, const char* label, float val, float mn, float mx);
/* Checkbox bound to *val; returns true the frame it toggles. */
bool  cc_gui_checkbox(CCEngine* eng, const char* label, bool* val);
/* Filled progress/stat bar (frac 0..1); rgba=0 uses the default color. For
 * health/sanity/stamina HUDs and load bars. */
void  cc_gui_progress(CCEngine* eng, const char* label, float frac, uint32_t rgba);
/* A thin divider line. */
void  cc_gui_separator(CCEngine* eng);
/* Vertical space between widgets. */
void  cc_gui_spacer(CCEngine* eng, float pixels);

/* ─── absolute-position widgets (free-form menu layouts) ──────────────────── */
/* Button at an explicit screen rect; returns true the frame it's clicked. */
bool  cc_gui_button_at(CCEngine* eng, const char* label, float x, float y, float w, float h);
/* Read the current flow cursor (x = left content edge, y = next widget top). */
void  cc_gui_get_cursor(CCEngine* eng, float* x, float* y);

#ifdef __cplusplus
}
#endif
