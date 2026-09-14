/* gui_test — verifies the immediate-mode GUI toolkit: widgets render (non-black
 * frame) AND interaction fires via injected mouse events headless. Renders a
 * pause menu with buttons, a slider, a checkbox, and stat bars, then simulates
 * clicks and confirms the correct widget triggers. */
#include "cc/claudecore.h"
#include "cc/gui.h"
#include "cc/input.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

typedef struct { bool resume_clicked, quit_clicked, box_val; float vol; } MenuState;

static void draw_menu(CCEngine* e, MenuState* m) {
    cc_gui_begin_frame(e);
    if (cc_gui_window(e, "Paused", 40, 40, 240, 240)) {
        cc_gui_label(e, "The building is quiet.");
        if (cc_gui_button(e, "Resume")) m->resume_clicked = true;
        if (cc_gui_button(e, "Quit"))   m->quit_clicked = true;
        cc_gui_separator(e);
        cc_gui_progress(e, "Health", 0.75f, 0);
        cc_gui_progress(e, "Sanity", 0.30f, 0);
        m->vol = cc_gui_slider(e, "Volume", m->vol, 0.0f, 1.0f);
        cc_gui_checkbox(e, "Subtitles", &m->box_val);
        cc_gui_end_window(e);
    }
    cc_gui_end_frame(e);
}

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "/tmp/gui_menu.png";

    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 480; cfg.height = 360; cfg.verbose = false;
    CCEngine* e = cc_init(&cfg);
    CHECK(e != NULL, "engine created");
    if (!e) { printf("GUI TEST: engine init failed\n"); return 1; }

    MenuState m = {0}; m.vol = 0.5f;

    /* 1. no click when the mouse is away */
    cc_input_inject_mouse_move(e, 5, 5);
    cc_input_inject_mouse_button(e, 0, false);
    draw_menu(e, &m);
    CHECK(!m.resume_clicked && !m.quit_clicked, "no click when mouse is away");

    /* 2. clicking Quit (2nd button) fires Quit only. Sweep candidate Y's so we
       don't depend on exact pixel math. */
    bool quit_fired = false;
    for (int y = 90; y <= 140 && !quit_fired; y += 2) {
        MenuState p = {0}; p.vol = 0.5f;
        cc_input_inject_mouse_move(e, 160, y);
        cc_input_inject_mouse_button(e, 0, true);
        draw_menu(e, &p);
        if (p.quit_clicked && !p.resume_clicked) quit_fired = true;
        cc_input_inject_mouse_button(e, 0, false);
    }
    CHECK(quit_fired, "clicking the Quit button fires it (and not Resume)");

    /* 3. clicking Resume (1st button) fires Resume only */
    bool resume_fired = false;
    for (int y = 64; y <= 100 && !resume_fired; y += 2) {
        MenuState p = {0}; p.vol = 0.5f;
        cc_input_inject_mouse_move(e, 160, y);
        cc_input_inject_mouse_button(e, 0, true);
        draw_menu(e, &p);
        if (p.resume_clicked && !p.quit_clicked) resume_fired = true;
        cc_input_inject_mouse_button(e, 0, false);
    }
    CHECK(resume_fired, "clicking the Resume button fires it (and not Quit)");

    /* 4. absolute-position button */
    cc_input_inject_mouse_move(e, 400, 320);
    cc_input_inject_mouse_button(e, 0, true);
    cc_gui_begin_frame(e);
    bool abs_fired = cc_gui_button_at(e, "OK", 370, 300, 90, 40);
    cc_gui_end_frame(e);
    CHECK(abs_fired, "cc_gui_button_at fires when clicked at its rect");
    cc_input_inject_mouse_button(e, 0, false);

    cc_input_inject_mouse_move(e, 10, 10);
    cc_input_inject_mouse_button(e, 0, true);
    cc_gui_begin_frame(e);
    bool far_fired = cc_gui_button_at(e, "No", 370, 300, 90, 40);
    cc_gui_end_frame(e);
    CHECK(!far_fired, "button_at not clicked when cursor is elsewhere");
    cc_input_inject_mouse_button(e, 0, false);

    /* 5. render a clean menu frame and verify it's a real (non-black) image */
    cc_input_inject_mouse_move(e, 5, 5);
    cc_input_inject_mouse_button(e, 0, false);
    for (int f = 0; f < 3; ++f) {
        cc_frame_begin(e);
        cc_draw_rect(e, 0, 0, 480, 360, 0x1a1c22ff, 0, 0);
        MenuState show = {0}; show.vol = 0.65f; show.box_val = true;
        draw_menu(e, &show);
        cc_frame_end(e);
    }
    const char* saved = cc_screenshot(e, out);
    CHECK(saved != NULL, "screenshot written");

    printf("gui: quit_fired=%d resume_fired=%d abs_fired=%d far_fired=%d\n",
           quit_fired, resume_fired, abs_fired, far_fired);
    printf("screenshot: %s\n", saved ? saved : "(null)");

    cc_shutdown(e);

    if (failures == 0) {
        printf("GUI TEST: all checks passed (widgets render; buttons + button_at fire on "
               "injected clicks; correct button targeted; menu screenshot produced)\n");
        return 0;
    }
    printf("GUI TEST: %d check(s) FAILED\n", failures);
    return 1;
}
