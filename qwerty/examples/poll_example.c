#include <qwerty/qwerty.h>
#include <stdio.h>
#include <signal.h>

static volatile int running = 1;
static void on_signal(int s) { (void)s; running = 0; }

int main(void) {
    printf("%s — polling example\n", qwerty_version_string());

    QConfig cfg = qwerty_default_config();
    QContext* ctx = qwerty_init(&cfg);
    if (!ctx) {
        fprintf(stderr, "qwerty_init failed\n");
        return 1;
    }
    printf("Backend: %s\n\n", qwerty_backend_name(ctx));
    printf("Press keys / move mouse. Ctrl+C to exit.\n\n");

    signal(SIGINT, on_signal);

    QEvent events[64];
    while (running) {
        uint32_t n = qwerty_wait(ctx, events, 64, 500);
        for (uint32_t i = 0; i < n; i++) {
            QEvent* e = &events[i];
            switch (e->type) {
                case QEVENT_KEY_DOWN:
                    printf("KEY_DOWN   %-12s  mods=0x%02x  scan=%u\n",
                           qwerty_key_name(e->key.key), e->key.mods, e->key.scancode);
                    if (e->key.key == QKEY_ESCAPE) running = 0;
                    break;
                case QEVENT_KEY_UP:
                    printf("KEY_UP     %s\n", qwerty_key_name(e->key.key));
                    break;
                case QEVENT_KEY_REPEAT:
                    printf("KEY_REPEAT %s\n", qwerty_key_name(e->key.key));
                    break;
                case QEVENT_MOUSE_MOVE:
                    printf("MOUSE_MOVE (%d, %d)  delta=(%d, %d)\n",
                           e->mouse_move.x, e->mouse_move.y,
                           e->mouse_move.dx, e->mouse_move.dy);
                    break;
                case QEVENT_MOUSE_BUTTON_DOWN:
                    printf("MOUSE_BTN_DOWN  btn=%d  pos=(%d,%d)\n",
                           e->mouse_button.button, e->mouse_button.x, e->mouse_button.y);
                    break;
                case QEVENT_MOUSE_BUTTON_UP:
                    printf("MOUSE_BTN_UP    btn=%d\n", e->mouse_button.button);
                    break;
                case QEVENT_MOUSE_SCROLL:
                    printf("MOUSE_SCROLL  dx=%.1f  dy=%.1f\n",
                           e->mouse_scroll.dx, e->mouse_scroll.dy);
                    break;
                default: break;
            }
        }
    }

    qwerty_shutdown(ctx);
    printf("\nShutdown clean.\n");
    return 0;
}
