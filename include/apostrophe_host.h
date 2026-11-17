/*
 * Apostrophe host integration — optional, header-only.
 *
 * A host desktop can provide a system overlay around an Apostrophe app without
 * linking its renderer or overlay implementation into Apostrophe. The app
 * calls ap_host_poll() once per frame, skips its own input when it returns
 * true, draws its frame, then calls ap_host_present(). The host backend draws
 * its overlay over that frame and Apostrophe presents it.
 *
 * The backend is a plain callback table. Its lifetime and the lifetime of any
 * overlay hooks/strings it uses stay with the host application. There is no
 * link-time dependency on a particular desktop, and no overlay is implemented
 * here.
 *
 * Define AP_HOST_IMPLEMENTATION in exactly one .c file before including this
 * header, after AP_IMPLEMENTATION for apostrophe.h in that same file (or in a
 * different file that is linked with it):
 *
 *     #define AP_IMPLEMENTATION
 *     #include "apostrophe.h"
 *     #define AP_HOST_IMPLEMENTATION
 *     #include "apostrophe_host.h"
 *
 * License: MIT (same as Apostrophe).
 */

#ifndef APOSTROPHE_HOST_H
#define APOSTROPHE_HOST_H

#include "apostrophe.h"

/* State/event notifications for the application.
 *
 * Overlay transitions are edge-triggered: one opened/closed pair per real
 * transition, never per frame. INPUT_YIELDED follows OVERLAY_OPENED and
 * INPUT_RETURNED follows OVERLAY_CLOSED. Input is already reset by the time
 * the callback runs, so the app must not poll input from inside the callback.
 * QUIT_REQUESTED is delivered once per attach. */
typedef enum {
    AP_HOST_EVENT_OVERLAY_OPENED = 0,
    AP_HOST_EVENT_OVERLAY_CLOSED,
    AP_HOST_EVENT_INPUT_YIELDED,
    AP_HOST_EVENT_INPUT_RETURNED,
    AP_HOST_EVENT_QUIT_REQUESTED,
    AP_HOST_EVENT_COUNT
} ap_host_event;

typedef void (*ap_host_event_cb)(void *user, ap_host_event event);

/*
 * Host-provided callbacks.
 *
 * poll:  called once per frame by ap_host_poll(). Return true when the host
 *        consumed this frame (overlay focused or a system key was handled), so
 *        the app must skip its own input for this frame.
 * draw:  optional. Called by ap_host_present() after the app has drawn its
 *        frame and before Apostrophe presents it. The host draws its overlay
 *        into Apostrophe's renderer here. Do not call ap_present().
 *
 * The struct is copied. The host owns the pointed-to user data and any SDL
 * surface/texture it draws from.
 */
typedef struct {
    void *user;
    int (*poll)(void *user);
    void (*draw)(void *user, SDL_Renderer *renderer);
} ap_host_backend;

/*
 * Attach a host backend. Call after ap_init(). Attaching the same backend
 * twice is idempotent (AP_OK); attaching a different one without detaching
 * first fails with AP_ERROR. A NULL backend fails with AP_ERROR and leaves any
 * existing attachment untouched.
 *
 * The adapter does not create, open, close or destroy the host overlay; the
 * host backend owns that lifecycle.
 */
int  ap_host_attach(const ap_host_backend *backend, ap_host_event_cb on_event, void *user);

/* Detach the backend and clear all adapter state. Idempotent. Does not call
 * the backend or change the host's overlay. Detach before destroying the host
 * object when possible. */
void ap_host_detach(void);
bool ap_host_attached(void);

/*
 * Pump the host once. Returns true when the app must skip its own input this
 * frame. Always false while detached. A latched quit request also returns
 * true, so input stops even if the app has not left its loop yet.
 */
bool ap_host_poll(void);

/* Draw the host overlay (if any) and present the Apostrophe frame. Call after
 * the app has finished drawing and instead of ap_present(). While detached
 * this is exactly ap_present(). */
void ap_host_present(void);

/*
 * Host -> adapter state notifications.
 *
 * overlay_changed: call on real transitions only, normally from the host
 * overlay's focus callback (focused ? open : closed). Repeating the current
 * state is a no-op. On a transition the adapter calls ap_reset_input(),
 * updates ap_host_overlay_open(), and emits the opened/closed and
 * yielded/returned events. The host should still call ap_reset_input() from
 * its own input-reset callback, matching a focus + input-reset hand-off; the
 * reset is idempotent.
 *
 * request_quit: the host's normal application-exit request. Returns 1 when
 * accepted, latches the request, and emits QUIT_REQUESTED once. It never
 * calls exit(), raises a signal, kills a process, or touches launcher files.
 * The app must leave its main loop from the callback or after checking
 * ap_host_quit_requested(). Repeating the call returns 1 without a second
 * event. The host still owns closing its overlay.
 */
void ap_host_overlay_changed(bool open);
int  ap_host_request_quit(void);

bool ap_host_overlay_open(void);
bool ap_host_quit_requested(void);

#endif /* APOSTROPHE_HOST_H */

#ifdef AP_HOST_IMPLEMENTATION
#ifndef APOSTROPHE_HOST_IMPLEMENTED
#define APOSTROPHE_HOST_IMPLEMENTED

typedef struct {
    bool attached;
    ap_host_backend backend;
    ap_host_event_cb on_event;
    void *event_user;
    bool overlay_open;
    bool quit_requested;
} ap__host_state;

static ap__host_state ap__host;

static void ap__host_emit(ap_host_event event) {
    if (ap__host.on_event) ap__host.on_event(ap__host.event_user, event);
}

int ap_host_attach(const ap_host_backend *backend, ap_host_event_cb on_event, void *user) {
    if (!backend || !backend->poll) return AP_ERROR;

    if (ap__host.attached) {
        bool same = ap__host.backend.user == backend->user &&
                    ap__host.backend.poll == backend->poll &&
                    ap__host.backend.draw == backend->draw &&
                    ap__host.on_event == on_event &&
                    ap__host.event_user == user;
        return same ? AP_OK : AP_ERROR;
    }

    ap__host.backend = *backend;
    ap__host.on_event = on_event;
    ap__host.event_user = user;
    ap__host.overlay_open = false;
    ap__host.quit_requested = false;
    ap__host.attached = true;
    return AP_OK;
}

void ap_host_detach(void) {
    if (!ap__host.attached) return;

    memset(&ap__host, 0, sizeof(ap__host));
}

bool ap_host_attached(void) {
    return ap__host.attached;
}

bool ap_host_poll(void) {
    if (!ap__host.attached) return false;
    if (ap__host.quit_requested) return true;

    int owned = ap__host.backend.poll ? ap__host.backend.poll(ap__host.backend.user) : 0;
    return owned || ap__host.overlay_open || ap__host.quit_requested;
}

void ap_host_present(void) {
    if (ap__host.attached && ap__host.backend.draw)
        ap__host.backend.draw(ap__host.backend.user, ap_get_renderer());
    ap_present();
}

void ap_host_overlay_changed(bool open) {
    if (!ap__host.attached || open == ap__host.overlay_open) return;

    ap__host.overlay_open = open;
    ap_reset_input();

    if (open) {
        ap__host_emit(AP_HOST_EVENT_OVERLAY_OPENED);
        ap__host_emit(AP_HOST_EVENT_INPUT_YIELDED);
    } else {
        ap__host_emit(AP_HOST_EVENT_OVERLAY_CLOSED);
        ap__host_emit(AP_HOST_EVENT_INPUT_RETURNED);
    }
}

int ap_host_request_quit(void) {
    if (!ap__host.attached) return 0;
    if (ap__host.quit_requested) return 1;

    ap__host.quit_requested = true;
    ap__host_emit(AP_HOST_EVENT_QUIT_REQUESTED);
    return 1;
}

bool ap_host_overlay_open(void) {
    return ap__host.attached && ap__host.overlay_open;
}

bool ap_host_quit_requested(void) {
    return ap__host.attached && ap__host.quit_requested;
}

#endif /* APOSTROPHE_HOST_IMPLEMENTED */
#endif /* AP_HOST_IMPLEMENTATION */
