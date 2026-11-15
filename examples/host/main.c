/*
 * Apostrophe Host Adapter Example
 *
 * Shows the optional ap_host_* seam from an application main loop. The host
 * backend in this file is FAKE: it draws a panel and consumes input, but it is
 * not NextUI, B5, B6, or any system overlay. A real desktop supplies the same
 * three callbacks (poll, overlay_changed, draw) from its own overlay module.
 *
 * Host keys:
 *   F1  toggle the fake overlay
 *   F2  request a normal application quit
 *   A   counted by the app, or by the fake overlay while it is open
 */

#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_HOST_IMPLEMENTATION
#include "apostrophe_host.h"

typedef struct {
    bool overlay;
    bool f1_down;
    bool f2_down;
    bool a_down;
    int opens;
    int closes;
    int captures;
} fake_host;

typedef struct {
    bool quit;
    int a_presses;
    int yields;
    int returns;
    int quit_events;
} demo_app;

static fake_host g_host;
static demo_app g_app;

static void host_event(void *user, ap_host_event event) {
    (void)user;
    switch (event) {
        case AP_HOST_EVENT_OVERLAY_OPENED: g_host.opens++; break;
        case AP_HOST_EVENT_OVERLAY_CLOSED: g_host.closes++; break;
        case AP_HOST_EVENT_INPUT_YIELDED: g_app.yields++; break;
        case AP_HOST_EVENT_INPUT_RETURNED: g_app.returns++; break;
        case AP_HOST_EVENT_QUIT_REQUESTED:
            g_app.quit_events++;
            g_app.quit = true;
            break;
        default: break;
    }
}

static int fake_poll(void *user) {
    (void)user;

    /* The app may skip ap_poll_input() while the overlay owns the frame, so
     * this fake backend pumps SDL itself to see key state. A real backend
     * receives its own system input path here. */
    SDL_PumpEvents();
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    bool f1 = keys[SDL_SCANCODE_F1] != 0;
    bool f2 = keys[SDL_SCANCODE_F2] != 0;
    bool a = keys[SDL_SCANCODE_A] != 0;

    if (f1 && !g_host.f1_down) {
        g_host.overlay = !g_host.overlay;
        ap_host_overlay_changed(g_host.overlay);
    }
    if (f2 && !g_host.f2_down) {
        ap_host_request_quit();
    }
    if (g_host.overlay && a && !g_host.a_down) {
        g_host.captures++;
    }

    g_host.f1_down = f1;
    g_host.f2_down = f2;
    g_host.a_down = a;

    return g_host.overlay || ap_host_quit_requested();
}

static void fake_draw(void *user, SDL_Renderer *renderer) {
    (void)user;
    (void)renderer;
    if (!g_host.overlay) return;

    int sw = ap_get_screen_width();
    int sh = ap_get_screen_height();
    TTF_Font *large = ap_get_font(AP_FONT_LARGE);
    TTF_Font *medium = ap_get_font(AP_FONT_MEDIUM);
    TTF_Font *small = ap_get_font(AP_FONT_SMALL);
    int header_h = TTF_FontHeight(large) + AP_S(14);
    int line_h = TTF_FontHeight(small) + AP_S(8);
    int w = AP_S(700);
    int h = header_h + AP_S(16) + TTF_FontHeight(medium) + AP_S(12) +
            3 * line_h + AP_S(16);
    if (w > sw - AP_S(48)) w = sw - AP_S(48);
    if (h > sh - AP_S(48)) h = sh - AP_S(48);
    int x = (sw - w) / 2;
    int y = (sh - h) / 2;
    int text_x = x + AP_S(22);

    ap_draw_rounded_rect(x, y, w, h, AP_S(10), (ap_color){ 20, 23, 29, 235 });
    ap_draw_rounded_rect(x, y, w, header_h, AP_S(10), ap_get_theme()->accent);
    ap_draw_text(large, "FAKE SYSTEM OVERLAY",
                 text_x, y + AP_S(7), ap_get_theme()->button_label);

    int ty = y + header_h + AP_S(16);

    char line[128];
    snprintf(line, sizeof(line), "Host captured A presses: %d", g_host.captures);
    ap_draw_text(medium, line, text_x, ty, ap_get_theme()->text);
    ty += TTF_FontHeight(medium) + AP_S(12);

    ap_draw_text(small, "Fake panel: not NextUI, B5, or B6.",
                 text_x, ty, ap_get_theme()->hint);
    ty += line_h;
    ap_draw_text(small, "F1 closes it; app input is reset.",
                 text_x, ty, ap_get_theme()->hint);
    ty += line_h;
    ap_draw_text(small, "F2 requests a normal quit.",
                 text_x, ty, ap_get_theme()->hint);
}

static void draw_app(void) {
    ap_clear_screen();
    ap_draw_screen_title("Host adapter (fake backend)", NULL);

    SDL_Rect content = ap_get_content_rect(true, true, false);
    int x = content.x + AP_S(12);
    int y = content.y + AP_S(12);
    TTF_Font *medium = ap_get_font(AP_FONT_MEDIUM);
    TTF_Font *small = ap_get_font(AP_FONT_SMALL);
    int medium_h = TTF_FontHeight(medium);
    int small_h = TTF_FontHeight(small);
    int line_h = medium_h + AP_S(10);

    ap_draw_text(small, "F1 fake overlay   F2 normal quit   A counts in app",
                 x, y, ap_get_theme()->hint);
    y += small_h + AP_S(14);

    ap_draw_text(medium, "The app owns its frame; the host draws last.",
                 x, y, ap_get_theme()->text);
    y += line_h;
    ap_draw_text(small, "No NextUI/B5/B6 code is linked into this example.",
                 x, y, ap_get_theme()->hint);
    y += small_h + AP_S(20);

    char line[128];
    bool overlay_open = ap_host_overlay_open();
    snprintf(line, sizeof(line), "overlay: %s", overlay_open ? "OPEN" : "closed");
    ap_draw_text(medium, line, x, y,
                 overlay_open ? ap_get_theme()->accent : ap_get_theme()->text);
    y += line_h;

    snprintf(line, sizeof(line), "app A presses: %d", g_app.a_presses);
    ap_draw_text(medium, line, x, y, ap_get_theme()->text);
    y += line_h;

    snprintf(line, sizeof(line), "host captured: %d", g_host.captures);
    ap_draw_text(medium, line, x, y, ap_get_theme()->text);
    y += line_h;

    snprintf(line, sizeof(line), "input yield/return: %d/%d",
             g_app.yields, g_app.returns);
    ap_draw_text(medium, line, x, y, ap_get_theme()->text);
    y += line_h;

    snprintf(line, sizeof(line), "overlay open/close: %d/%d, quit events: %d",
             g_host.opens, g_host.closes, g_app.quit_events);
    ap_draw_text(small, line, x, y, ap_get_theme()->hint);

    ap_footer_item footer[] = {
        { .button = AP_BTN_A, .label = "COUNT", .is_confirm = true },
    };
    ap_draw_footer(footer, 1);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    ap_config cfg = {
        .window_title = "Apostrophe Host Adapter (fake backend)",
        .log_path     = NULL,
        .is_nextui    = false,
    };
    if (ap_init(&cfg) != AP_OK) {
        fprintf(stderr, "ap_init failed: %s\n", ap_get_error());
        return 1;
    }

    ap_host_backend backend = {
        .user = &g_host,
        .poll = fake_poll,
        .draw = fake_draw,
    };
    if (ap_host_attach(&backend, host_event, NULL) != AP_OK) {
        fprintf(stderr, "ap_host_attach failed\n");
        ap_quit();
        return 1;
    }

    while (!g_app.quit) {
        /* The host pump runs first. When it owns the frame, skip app input. */
        if (!ap_host_poll()) {
            ap_input_event ev;
            while (ap_poll_input(&ev)) {
                if (ev.pressed && ev.button == AP_BTN_A) g_app.a_presses++;
            }
        }

        draw_app();
        ap_host_present();   /* host overlay first (inside), then present */
    }

    ap_host_detach();
    ap_quit();
    return 0;
}
