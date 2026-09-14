/*
 * Headless mode — for use inside Chlorlite sandbox.
 * No display, no /dev/input. Events are injected programmatically.
 * Claude can drive input for headless game testing.
 */
#include <qwerty/qwerty.h>
#include <stdio.h>
#include <time.h>

/* Inject a synthetic event directly into the ring buffer via dispatch */
extern void qwerty_dispatch(void* ctx, QEvent* ev);

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void) {
    QConfig cfg = qwerty_default_config();
    cfg.backend = QBACKEND_HEADLESS;

    QContext* ctx = qwerty_init(&cfg);
    if (!ctx) { fprintf(stderr, "init failed\n"); return 1; }

    printf("Qwerty headless — injecting synthetic input events\n\n");

    /* Simulate: press W, press Space, release W, release Space */
    QEvent events[] = {
        { .type = QEVENT_KEY_DOWN, .timestamp_ns = now_ns(),
          .key = { .key = QKEY_W, .mods = QMOD_NONE } },
        { .type = QEVENT_KEY_DOWN, .timestamp_ns = now_ns(),
          .key = { .key = QKEY_SPACE, .mods = QMOD_NONE } },
        { .type = QEVENT_MOUSE_MOVE, .timestamp_ns = now_ns(),
          .mouse_move = { .x = 400, .y = 300, .dx = 10, .dy = -5 } },
        { .type = QEVENT_KEY_UP, .timestamp_ns = now_ns(),
          .key = { .key = QKEY_W, .mods = QMOD_NONE } },
        { .type = QEVENT_KEY_UP, .timestamp_ns = now_ns(),
          .key = { .key = QKEY_SPACE, .mods = QMOD_NONE } },
    };

    for (int i = 0; i < 5; i++) {
        qwerty_dispatch(ctx, &events[i]);
    }

    /* Poll and print all events */
    QEvent out[16];
    uint32_t n = qwerty_poll(ctx, out, 16);
    printf("Drained %u events:\n", n);
    for (uint32_t i = 0; i < n; i++) {
        QEvent* e = &out[i];
        switch (e->type) {
            case QEVENT_KEY_DOWN:
                printf("  KEY_DOWN  %s\n", qwerty_key_name(e->key.key)); break;
            case QEVENT_KEY_UP:
                printf("  KEY_UP    %s\n", qwerty_key_name(e->key.key)); break;
            case QEVENT_MOUSE_MOVE:
                printf("  MOUSE_MOVE (%d,%d) delta=(%d,%d)\n",
                       e->mouse_move.x, e->mouse_move.y,
                       e->mouse_move.dx, e->mouse_move.dy);
                break;
            default: break;
        }
    }

    qwerty_shutdown(ctx);
    printf("\nHeadless test complete.\n");
    return 0;
}
