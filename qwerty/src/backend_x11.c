#include "qwerty_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

/*
 * X11 backend — optional compile-time dependency.
 * If X11 headers are not present, this backend is a no-op stub.
 */

#ifdef QWERTY_HAS_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

/* ─── KeySym → QKey ───────────────────────────────────────────────────── */
QKey qwerty_x11_keysym_to_key(uint32_t ks) {
    /* Letters */
    if (ks >= XK_a && ks <= XK_z) return (QKey)(QKEY_A + (ks - XK_a));
    if (ks >= XK_A && ks <= XK_Z) return (QKey)(QKEY_A + (ks - XK_A));
    /* Digits */
    if (ks >= XK_0 && ks <= XK_9) return (QKey)(QKEY_0 + (ks - XK_0));
    /* Function */
    if (ks >= XK_F1 && ks <= XK_F12) return (QKey)(QKEY_F1 + (ks - XK_F1));

    switch (ks) {
        case XK_Escape:         return QKEY_ESCAPE;
        case XK_Return:         return QKEY_ENTER;
        case XK_BackSpace:      return QKEY_BACKSPACE;
        case XK_Tab:            return QKEY_TAB;
        case XK_space:          return QKEY_SPACE;
        case XK_Delete:         return QKEY_DELETE;
        case XK_Insert:         return QKEY_INSERT;
        case XK_Home:           return QKEY_HOME;
        case XK_End:            return QKEY_END;
        case XK_Prior:          return QKEY_PAGE_UP;
        case XK_Next:           return QKEY_PAGE_DOWN;
        case XK_Up:             return QKEY_UP;
        case XK_Down:           return QKEY_DOWN;
        case XK_Left:           return QKEY_LEFT;
        case XK_Right:          return QKEY_RIGHT;
        case XK_Shift_L:        return QKEY_LSHIFT;
        case XK_Shift_R:        return QKEY_RSHIFT;
        case XK_Control_L:      return QKEY_LCTRL;
        case XK_Control_R:      return QKEY_RCTRL;
        case XK_Alt_L:          return QKEY_LALT;
        case XK_Alt_R:          return QKEY_RALT;
        case XK_Super_L:        return QKEY_LSUPER;
        case XK_Super_R:        return QKEY_RSUPER;
        case XK_Caps_Lock:      return QKEY_CAPSLOCK;
        case XK_minus:          return QKEY_MINUS;
        case XK_equal:          return QKEY_EQUALS;
        case XK_bracketleft:    return QKEY_LBRACKET;
        case XK_bracketright:   return QKEY_RBRACKET;
        case XK_backslash:      return QKEY_BACKSLASH;
        case XK_semicolon:      return QKEY_SEMICOLON;
        case XK_apostrophe:     return QKEY_APOSTROPHE;
        case XK_grave:          return QKEY_GRAVE;
        case XK_comma:          return QKEY_COMMA;
        case XK_period:         return QKEY_PERIOD;
        case XK_slash:          return QKEY_SLASH;
        default:                return QKEY_NONE;
    }
}

static QMod x11_state_to_mods(unsigned int state) {
    QMod m = QMOD_NONE;
    if (state & ShiftMask)   m |= QMOD_SHIFT;
    if (state & ControlMask) m |= QMOD_CTRL;
    if (state & Mod1Mask)    m |= QMOD_ALT;
    if (state & Mod4Mask)    m |= QMOD_SUPER;
    if (state & LockMask)    m |= QMOD_CAPS;
    return m;
}

bool qwerty_x11_init(QContext* ctx) {
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) return false;

    ctx->x11_display = dpy;

    /* Create a hidden input-only window to receive global events */
    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    Window win = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);

    XSelectInput(dpy, win,
        KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | FocusChangeMask);

    XMapWindow(dpy, win);
    XFlush(dpy);

    ctx->x11_window = (int)win;
    return true;
}

void qwerty_x11_shutdown(QContext* ctx) {
    if (ctx->x11_display) {
        Display* dpy = (Display*)ctx->x11_display;
        XDestroyWindow(dpy, (Window)ctx->x11_window);
        XCloseDisplay(dpy);
        ctx->x11_display = NULL;
    }
}

void* qwerty_x11_thread(void* arg) {
    QContext* ctx = arg;
    Display* dpy  = (Display*)ctx->x11_display;
    int x11_fd    = ConnectionNumber(dpy);

    struct pollfd fds[2] = {
        { .fd = x11_fd,                .events = POLLIN },
        { .fd = ctx->notify_pipe[0],   .events = POLLIN },
    };

    while (atomic_load(&ctx->running)) {
        int r = poll(fds, 2, 100);
        if (r <= 0) continue;
        if (fds[1].revents & POLLIN) break; /* shutdown */
        if (!(fds[0].revents & POLLIN)) continue;

        while (XPending(dpy)) {
            XEvent xe;
            XNextEvent(dpy, &xe);

            switch (xe.type) {
                case KeyPress:
                case KeyRelease: {
                    KeySym ks = XLookupKeysym(&xe.xkey, 0);
                    QKey key = qwerty_x11_keysym_to_key((uint32_t)ks);
                    QMod mods = x11_state_to_mods(xe.xkey.state);

                    /* Simple repeat detection: KeyPress with no KeyRelease */
                    bool repeat = false;
                    if (xe.type == KeyPress && XEventsQueued(dpy, QueuedAfterReading)) {
                        XEvent next;
                        XPeekEvent(dpy, &next);
                        if (next.type == KeyPress && next.xkey.keycode == xe.xkey.keycode
                            && next.xkey.time == xe.xkey.time)
                            repeat = true;
                    }

                    QEvent ev = {
                        .type = (xe.type == KeyRelease) ? QEVENT_KEY_UP
                              : repeat                  ? QEVENT_KEY_REPEAT
                              :                           QEVENT_KEY_DOWN,
                        .key  = { .key = key, .mods = mods, .scancode = xe.xkey.keycode },
                    };
                    qwerty_dispatch(ctx, &ev);
                    break;
                }

                case ButtonPress:
                case ButtonRelease: {
                    unsigned btn = xe.xbutton.button;
                    if (btn == Button4 || btn == Button5) {
                        /* Scroll */
                        QEvent ev = {
                            .type = QEVENT_MOUSE_SCROLL,
                            .mouse_scroll = {
                                .dy = (btn == Button4) ? 1.0f : -1.0f,
                                .x  = xe.xbutton.x,
                                .y  = xe.xbutton.y,
                            },
                        };
                        qwerty_dispatch(ctx, &ev);
                    } else {
                        QMouseButton mb = QMOUSE_LEFT;
                        if      (btn == Button1) mb = QMOUSE_LEFT;
                        else if (btn == Button2) mb = QMOUSE_MIDDLE;
                        else if (btn == Button3) mb = QMOUSE_RIGHT;
                        QEvent ev = {
                            .type = (xe.type == ButtonPress) ? QEVENT_MOUSE_BUTTON_DOWN
                                                             : QEVENT_MOUSE_BUTTON_UP,
                            .mouse_button = {
                                .button = mb,
                                .x = xe.xbutton.x,
                                .y = xe.xbutton.y,
                                .mods = x11_state_to_mods(xe.xbutton.state),
                            },
                        };
                        qwerty_dispatch(ctx, &ev);
                    }
                    break;
                }

                case MotionNotify: {
                    int32_t ox, oy;
                    qwerty_mouse_pos(ctx, &ox, &oy);
                    QEvent ev = {
                        .type = QEVENT_MOUSE_MOVE,
                        .mouse_move = {
                            .x  = xe.xmotion.x,
                            .y  = xe.xmotion.y,
                            .dx = xe.xmotion.x - ox,
                            .dy = xe.xmotion.y - oy,
                        },
                    };
                    qwerty_dispatch(ctx, &ev);
                    break;
                }

                case FocusIn: {
                    QEvent ev = { .type = QEVENT_FOCUS_GAINED };
                    qwerty_dispatch(ctx, &ev);
                    break;
                }
                case FocusOut: {
                    QEvent ev = { .type = QEVENT_FOCUS_LOST };
                    qwerty_dispatch(ctx, &ev);
                    break;
                }
            }
        }
    }
    return NULL;
}

#else /* QWERTY_HAS_X11 not defined */

QKey qwerty_x11_keysym_to_key(uint32_t ks) { (void)ks; return QKEY_NONE; }
bool qwerty_x11_init(QContext* ctx) { (void)ctx; return false; }
void qwerty_x11_shutdown(QContext* ctx) { (void)ctx; }
void* qwerty_x11_thread(void* arg) { (void)arg; return NULL; }

#endif /* QWERTY_HAS_X11 */
