/*
 * H3/WebTransport session negotiation. See wt_session.h.
 *
 * Call sequence, grounded against private-octopus/picoquic's own
 * reference server (picoquic@790e973b, the exact commit vendored in
 * thirdparty/picoquic):
 *
 * 1. webtransport_server.c's picoquic_create() is given default_alpn =
 *    "h3", default_callback_fn = h3zero_callback, default_callback_ctx =
 *    what zone_wt_create_context() returns here. h3zero_callback (from
 *    h3zero_common.h) is picoquic's own HTTP/3 request-parsing callback,
 *    not written by us -- we only supply its context and a path table.
 *
 * 2. When a client's H3 request path matches ZONE_WT_PATH, h3zero_callback
 *    fires the path table's path_callback (zone_wt_connect_callback below)
 *    with wt_event = picohttp_callback_connect
 *    (picohttp_call_back_event_t, h3zero_common.h) -- mirrors
 *    wt_baton.c's wt_baton_callback's picohttp_callback_connect case
 *    exactly: call picowt_select_wt_protocol() to negotiate the
 *    WT_AVAILABLE_PROTOCOLS header, then hand the *session* off by
 *    setting stream_ctx->path_callback / path_callback_ctx (also per
 *    wt_baton.c, around its wt_baton_accept() call site).
 *
 * 3. From then on, datagrams for this session arrive as
 *    picohttp_callback_post_datagram on the callback stream_ctx->path_callback
 *    now points at (zone_wt_session_callback below), and
 *    picohttp_callback_deregister fires on session teardown.
 */

#include "wt_session.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    zone_wt_datagram_cb on_datagram;
    void *app_ctx;
} zone_wt_session_ctx_t;

static int zone_wt_session_callback(picoquic_cnx_t *cnx, uint8_t *bytes, size_t length,
                                     picohttp_call_back_event_t wt_event,
                                     h3zero_stream_ctx_t *stream_ctx, void *path_app_ctx)
{
    (void)cnx;
    (void)bytes;
    (void)length;
    (void)stream_ctx;
    zone_wt_session_ctx_t *sctx = (zone_wt_session_ctx_t *)path_app_ctx;

    switch (wt_event) {
    case picohttp_callback_post_datagram:
        if (sctx != NULL && sctx->on_datagram != NULL) {
            sctx->on_datagram(sctx->app_ctx);
        }
        break;
    case picohttp_callback_deregister:
        /* sctx itself is the path table's static entry (see
         * zone_wt_create_context) -- not freed here, only per-connection
         * state would be, and this server keeps none yet. */
        break;
    default:
        break;
    }

    return 0;
}

static int zone_wt_connect_callback(picoquic_cnx_t *cnx, uint8_t *bytes, size_t length,
                                     picohttp_call_back_event_t wt_event,
                                     h3zero_stream_ctx_t *stream_ctx, void *path_app_ctx)
{
    (void)bytes;
    (void)length;

    if (wt_event == picohttp_callback_connect) {
        h3zero_callback_ctx_t *h3_ctx;

        /* picowt_select_wt_protocol's own doc comment: it returns -1 "if
         * no common protocol is found, including if the peer did not
         * provide a WT_AVAILABLE_PROTOCOLS header" -- i.e. a plain
         * WebTransport client (unclear yet whether Godot's
         * WebTransportPeer, quic_picoquic_backend.cpp, sends this header
         * at all) may always get -1 here regardless of what "zone" (our
         * placeholder protocol name) matches against. The return value is
         * deliberately not checked/gated on below -- ZONE_WT_PATH has
         * exactly one purpose, so an unnegotiated protocol name is not
         * fatal, only unrecorded. Whether that is the right call for a
         * real client is unverified; flagged, not silently assumed. */
        (void)picowt_select_wt_protocol(stream_ctx, "zone");
        stream_ctx->path_callback = zone_wt_session_callback;
        stream_ctx->path_callback_ctx = path_app_ctx;

        /* Accepting the CONNECT is not the same as joining the session to
         * its streams, and stopping at the two lines above is the second
         * bug this commit fixes. Every stream a client opens inside the
         * session carries the control stream's id as its prefix, and
         * h3zero routes an arriving stream by looking that prefix up
         * (h3zero_find_stream_prefix). Undeclared, the lookup misses and
         * h3zero resets each stream on arrival -- observed as
         * "Received RESET_STREAM" in Chromium, on a session that had
         * otherwise negotiated cleanly.
         *
         * wt_baton.c does exactly this in wt_baton_ctx_init, under the
         * comment "Register the control stream and the stream id". */
        h3_ctx = (h3zero_callback_ctx_t *)picoquic_get_callback_context(cnx);
        if (h3_ctx == NULL) {
            return -1;
        }
        stream_ctx->ps.stream_state.control_stream_id = stream_ctx->stream_id;
        return h3zero_declare_stream_prefix(h3_ctx, stream_ctx->stream_id,
                                            zone_wt_session_callback, path_app_ctx);
    }

    return 0;
}

picohttp_server_parameters_t *zone_wt_create_context(zone_wt_datagram_cb on_datagram,
                                                     void *app_ctx)
{
    zone_wt_session_ctx_t *sctx = calloc(1, sizeof(*sctx));
    if (sctx == NULL) {
        return NULL;
    }
    sctx->on_datagram = on_datagram;
    sctx->app_ctx = app_ctx;

    picohttp_server_path_item_t *path_table = calloc(1, sizeof(*path_table));
    if (path_table == NULL) {
        free(sctx);
        return NULL;
    }
    path_table[0].path = ZONE_WT_PATH;
    path_table[0].path_length = strlen(ZONE_WT_PATH);
    path_table[0].path_callback = zone_wt_connect_callback;
    path_table[0].path_app_ctx = sctx;

    /* Heap, not a local: h3zero reads these fields every time it builds a
     * per-connection context, which is long after this function returns. */
    picohttp_server_parameters_t *params = calloc(1, sizeof(*params));
    if (params == NULL) {
        free(path_table);
        free(sctx);
        return NULL;
    }
    params->path_table = path_table;
    params->path_table_nb = 1;

    return params;
}

void zone_wt_free_context(picohttp_server_parameters_t *params)
{
    if (params == NULL) {
        return;
    }
    if (params->path_table != NULL) {
        /* path_app_ctx is the zone_wt_session_ctx_t this file allocated. */
        free(params->path_table[0].path_app_ctx);
        free((void *)params->path_table);
    }
    free(params);
}
