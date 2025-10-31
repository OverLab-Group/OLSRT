/* OLSRT - Actors */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>

/* Actor object */
struct ol_actor_s {
    const char       *name;
    ol_loop_t        *loop;
    ol_channel_t     *mailbox;
    ol_actor_fn       fn;
    void             *arg;
    int               running;
    int               supervise;
};

/* Internal actor entry: executes user fn; supervision hook placeholder */
static void ol_actor_entry(void *a_) {
    ol_actor_t *a = (ol_actor_t*)a_;
    a->running = 1;
    a->fn(a, a->arg);
    a->running = 0;
    if (a->supervise) {
        /* TODO: implement restart/backoff policy; currently no-op */
    }
}

/* Spawn actor: create mailbox and schedule entry on loop */
ol_actor_t* ol_actor_spawn(ol_loop_t *loop, ol_actor_fn fn, void *arg, const ol_actor_opts_t *opts) {
    if (!loop || !fn) return NULL;

    ol_actor_t *a = (ol_actor_t*)calloc(1, sizeof(*a));
    if (!a) return NULL;

    a->loop      = loop;
    a->fn        = fn;
    a->arg       = arg;
    a->name      = (opts && opts->name) ? opts->name : NULL;
    a->supervise = (opts && opts->supervise) ? 1 : 0;

    ol_channel_opts_t chopt;
    chopt.capacity = (opts && opts->mailbox_capacity) ? opts->mailbox_capacity : 1024;
    a->mailbox = ol_channel_create(&chopt);
    if (!a->mailbox) { free(a); return NULL; }

    ol_loop_post(loop, ol_actor_entry, a);
    return a;
}

/* Send message to actor mailbox (non-blocking, 0 timeout) */
int ol_actor_send(ol_actor_t *actor, void *msg) {
    if (!actor || !actor->mailbox) return OL_ERR_STATE;
    return ol_channel_send(actor->mailbox, msg, 0);
}

/* Stop actor: close mailbox; actor function should honor closure and exit */
int ol_actor_stop(ol_actor_t *actor) {
    if (!actor) return OL_ERR_STATE;
    return ol_channel_close(actor->mailbox);
}
