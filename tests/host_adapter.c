/*
 * Host adapter contract checks.
 *
 * These use a fake host backend and a headless Apostrophe instance. They do
 * not link NextUI, B5, B6, or any real system overlay.
 */

#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_HOST_IMPLEMENTATION
#include "apostrophe_host.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

typedef struct {
    int poll_result;
    int polls;
    int draws;
} fake_backend;

static int event_counts[AP_HOST_EVENT_COUNT];
static ap_host_event event_order[32];
static int event_order_count;

static void reset_events(void) {
    memset(event_counts, 0, sizeof(event_counts));
    memset(event_order, 0, sizeof(event_order));
    event_order_count = 0;
}

static void record_event(void *user, ap_host_event event) {
    (void)user;
    if (event >= 0 && event < AP_HOST_EVENT_COUNT) event_counts[event]++;
    if (event_order_count < (int)(sizeof(event_order) / sizeof(event_order[0])))
        event_order[event_order_count++] = event;
}

static int fake_poll(void *user) {
    fake_backend *backend = (fake_backend *)user;
    backend->polls++;
    return backend->poll_result;
}

static void fake_draw(void *user, SDL_Renderer *renderer) {
    (void)renderer;
    fake_backend *backend = (fake_backend *)user;
    backend->draws++;
}

static void push_key(SDL_Keycode key, Uint32 type) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key.keysym.sym = key;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    SDL_PushEvent(&event);
}

static void drain_input(void) {
    ap_input_event event;
    while (ap_poll_input(&event)) { }
}

static void test_detached(void) {
    ap_host_detach();
    reset_events();

    CHECK(!ap_host_attached());
    CHECK(!ap_host_poll());
    CHECK(!ap_host_overlay_open());
    CHECK(!ap_host_quit_requested());
    CHECK(ap_host_request_quit() == 0);

    ap_host_overlay_changed(true);
    ap_host_overlay_changed(false);
    CHECK(event_order_count == 0);

    ap_host_detach(); /* idempotent */
    CHECK(!ap_host_attached());
}

static void test_attach_contract(void) {
    fake_backend a = { .poll_result = 0 };
    fake_backend b = { .poll_result = 0 };
    ap_host_backend backend_a = { .user = &a, .poll = fake_poll, .draw = fake_draw };
    ap_host_backend backend_b = { .user = &b, .poll = fake_poll, .draw = fake_draw };
    ap_host_backend no_poll = { .user = &a, .poll = NULL, .draw = fake_draw };

    ap_host_detach();
    reset_events();

    CHECK(ap_host_attach(NULL, record_event, NULL) == AP_ERROR);
    CHECK(ap_host_attach(&no_poll, record_event, NULL) == AP_ERROR);
    CHECK(!ap_host_attached());

    CHECK(ap_host_attach(&backend_a, record_event, NULL) == AP_OK);
    CHECK(ap_host_attached());
    CHECK(ap_host_attach(&backend_a, record_event, NULL) == AP_OK); /* same */
    CHECK(ap_host_attach(&backend_a, NULL, NULL) == AP_ERROR);      /* callback differs */
    CHECK(ap_host_attach(&backend_b, record_event, NULL) == AP_ERROR); /* backend differs */

    ap_host_detach();
    CHECK(!ap_host_attached());
    CHECK(ap_host_attach(&backend_b, record_event, NULL) == AP_OK);
    ap_host_detach();
}

static void test_overlay_transitions(void) {
    fake_backend backend = { .poll_result = 0 };
    ap_host_backend hooks = { .user = &backend, .poll = fake_poll, .draw = fake_draw };

    ap_host_detach();
    reset_events();
    CHECK(ap_host_attach(&hooks, record_event, NULL) == AP_OK);

    ap_host_overlay_changed(true);
    CHECK(ap_host_overlay_open());
    CHECK(event_counts[AP_HOST_EVENT_OVERLAY_OPENED] == 1);
    CHECK(event_counts[AP_HOST_EVENT_INPUT_YIELDED] == 1);
    CHECK(event_order_count == 2);
    CHECK(event_order[0] == AP_HOST_EVENT_OVERLAY_OPENED);
    CHECK(event_order[1] == AP_HOST_EVENT_INPUT_YIELDED);

    /* Repeating the current state is not a transition and emits nothing. */
    ap_host_overlay_changed(true);
    CHECK(event_order_count == 2);

    /* A poll result alone must not invent an overlay transition. */
    backend.poll_result = 1;
    CHECK(ap_host_poll());
    CHECK(event_order_count == 2);
    CHECK(ap_host_overlay_open());

    ap_host_overlay_changed(false);
    CHECK(!ap_host_overlay_open());
    CHECK(event_counts[AP_HOST_EVENT_OVERLAY_CLOSED] == 1);
    CHECK(event_counts[AP_HOST_EVENT_INPUT_RETURNED] == 1);
    CHECK(event_order_count == 4);
    CHECK(event_order[2] == AP_HOST_EVENT_OVERLAY_CLOSED);
    CHECK(event_order[3] == AP_HOST_EVENT_INPUT_RETURNED);

    ap_host_overlay_changed(false);
    CHECK(event_order_count == 4);
    ap_host_detach();
}

static void test_poll_ownership(void) {
    fake_backend backend = { .poll_result = 0 };
    ap_host_backend hooks = { .user = &backend, .poll = fake_poll, .draw = fake_draw };

    ap_host_detach();
    reset_events();
    CHECK(ap_host_attach(&hooks, record_event, NULL) == AP_OK);

    backend.poll_result = 1;
    CHECK(ap_host_poll());
    CHECK(backend.polls == 1);
    CHECK(event_order_count == 0); /* ownership is not an overlay event */

    backend.poll_result = 0;
    CHECK(!ap_host_poll());
    CHECK(backend.polls == 2);
    CHECK(event_order_count == 0);

    ap_host_detach();
    CHECK(!ap_host_poll());
    CHECK(backend.polls == 2); /* detached never calls the backend */
}

static void test_input_isolation(void) {
    fake_backend backend = { .poll_result = 0 };
    ap_host_backend hooks = { .user = &backend, .poll = fake_poll, .draw = fake_draw };
    ap_input_event event;

    ap_host_detach();
    reset_events();
    ap_reset_input();
    CHECK(ap_host_attach(&hooks, record_event, NULL) == AP_OK);

    /* A press queued before the overlay opens must not survive the hand-off. */
    push_key(SDLK_a, SDL_KEYDOWN);
    push_key(SDLK_a, SDL_KEYUP);
    ap_host_overlay_changed(true);
    CHECK(!ap_poll_input(&event));

    /* Presses made while the overlay owns input are discarded on close. */
    push_key(SDLK_a, SDL_KEYDOWN);
    push_key(SDLK_a, SDL_KEYUP);
    backend.poll_result = 1;
    CHECK(ap_host_poll());          /* app skips ap_poll_input here */
    ap_host_overlay_changed(false);
    CHECK(!ap_poll_input(&event));

    /* After the hand-back, the app receives fresh input again. */
    push_key(SDLK_a, SDL_KEYDOWN);
    push_key(SDLK_a, SDL_KEYUP);
    backend.poll_result = 0;
    CHECK(!ap_host_poll());
    CHECK(ap_poll_input(&event));
    CHECK(event.button == AP_BTN_A);
    CHECK(event.pressed);
    drain_input();

    ap_host_detach();

    /* ap_reset_input() is a core primitive, usable with no host attached. */
    push_key(SDLK_a, SDL_KEYDOWN);
    push_key(SDLK_a, SDL_KEYUP);
    ap_reset_input();
    CHECK(!ap_poll_input(&event));
}

static void test_quit_once(void) {
    fake_backend backend = { .poll_result = 0 };
    ap_host_backend hooks = { .user = &backend, .poll = fake_poll, .draw = fake_draw };

    ap_host_detach();
    reset_events();
    CHECK(ap_host_attach(&hooks, record_event, NULL) == AP_OK);

    CHECK(ap_host_request_quit() == 1);
    CHECK(ap_host_quit_requested());
    CHECK(event_counts[AP_HOST_EVENT_QUIT_REQUESTED] == 1);
    CHECK(backend.polls == 0);

    /* Repeats are accepted but must not call the app callback twice. */
    CHECK(ap_host_request_quit() == 1);
    CHECK(event_counts[AP_HOST_EVENT_QUIT_REQUESTED] == 1);

    /* A latched quit owns the frame even when the backend reports zero. */
    CHECK(ap_host_poll());
    CHECK(ap_host_quit_requested());

    /* Detach clears the latch; a fresh attach can deliver a fresh request. */
    ap_host_detach();
    CHECK(!ap_host_quit_requested());
    CHECK(ap_host_attach(&hooks, record_event, NULL) == AP_OK);
    CHECK(ap_host_request_quit() == 1);
    CHECK(event_counts[AP_HOST_EVENT_QUIT_REQUESTED] == 2);

    ap_host_detach();
}

static void test_present_and_draw(void) {
    fake_backend backend = { .poll_result = 0 };
    ap_host_backend hooks = { .user = &backend, .poll = fake_poll, .draw = fake_draw };

    ap_host_detach();
    CHECK(ap_host_attach(&hooks, record_event, NULL) == AP_OK);

    ap_request_frame();
    ap_host_present();
    CHECK(backend.draws == 1);

    ap_host_detach();
    ap_request_frame();
    ap_host_present();
    CHECK(backend.draws == 1); /* detached present does not call draw */

    /* Attached backend with no draw is valid. */
    ap_host_backend no_draw = { .user = &backend, .poll = fake_poll, .draw = NULL };
    CHECK(ap_host_attach(&no_draw, record_event, NULL) == AP_OK);
    ap_request_frame();
    ap_host_present();
    CHECK(backend.draws == 1);
    ap_host_detach();
}

int main(void) {
    ap_config cfg = {
        .window_title = "Apostrophe host adapter test",
        .font_path = "res/font.ttf",
        .disable_background = true,
        .is_nextui = false,
    };
    if (ap_init(&cfg) != AP_OK) {
        fprintf(stderr, "ap_init failed: %s\n", ap_get_error());
        return 1;
    }

    test_detached();
    test_attach_contract();
    test_overlay_transitions();
    test_poll_ownership();
    test_input_isolation();
    test_quit_once();
    test_present_and_draw();

    ap_quit();

    if (failures) {
        fprintf(stderr, "%d checks, %d failures\n", checks, failures);
        return 1;
    }
    printf("%d checks, 0 failures\n", checks);
    return 0;
}
