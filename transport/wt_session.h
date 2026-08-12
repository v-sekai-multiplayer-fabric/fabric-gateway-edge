#pragma once

#include <picoquic.h>
#include <h3zero_common.h>
#include <pico_webtransport.h>

/*
 * H3/WebTransport session negotiation, on top of the raw QUIC bridge in
 * webtransport_server.c -- task #12. Every function here is grounded
 * directly against private-octopus/picoquic's real server reference
 * (picohttp/demoserver.c's ALPN/callback wiring, picohttp/wt_baton.c's
 * wt_baton_callback's picohttp_callback_connect handling), not invented
 * -- see wt_session.c's header comment for the exact call sequence each
 * function mirrors.
 *
 * Simplification versus demoserver.c: that reference juggles multiple
 * ALPNs (h3, http/0.9, quicperf) via a custom alpn_select_fn.
 * zone-server-h2o only ever wants "h3" -- so picoquic_create is called
 * with default_alpn = "h3" directly (picoquic.h's own doc comment: the
 * alpn_select_fn "is only called if no default ALPN is specified"),
 * skipping that indirection entirely rather than porting code for ALPNs
 * this server will never negotiate.
 *
 * Path: zone-server-h2o serves exactly one WebTransport path,
 * ZONE_WT_PATH below -- there is no multi-endpoint routing need yet
 * (matches multiplayer-fabric-manuals rfd/0086's single-zone
 * scope).
 */

#define ZONE_WT_PATH "/zone"

/* Callback fired once per received WebTransport datagram, after a
 * session has been negotiated on ZONE_WT_PATH. No payload is passed
 * through yet -- task #11/#14's "one fixed tick per datagram" semantics
 * carry over unchanged; parsing datagram contents into per-entity
 * commands is follow-up work once this session layer itself is proven. */
typedef void (*zone_wt_datagram_cb)(void *app_ctx);

/* Builds the path table wiring ZONE_WT_PATH to the WebTransport session
 * callbacks, and returns the picohttp_server_parameters_t naming it --
 * NOT an h3zero_callback_ctx_t.
 *
 * That distinction is the bug this replaces, and it is picoquic's own
 * contract rather than a preference: h3zero_callback, on the first
 * packet of a new connection, takes the branch
 *
 *     if (callback_ctx == NULL ||
 *         callback_ctx == picoquic_get_default_callback_context(cnx->quic)) {
 *         ctx = h3zero_callback_create_context(
 *             (picohttp_server_parameters_t *)callback_ctx);
 *
 * (h3zero_common.c). So whatever is handed to picoquic_create as
 * default_callback_ctx is cast to picohttp_server_parameters_t* and read
 * for path_table/path_table_nb/web_folder. Handing it a ready-made
 * h3zero_callback_ctx_t*, as this file did, reads the path table from
 * the wrong offset and segfaults on the first packet. h3zero builds a
 * context per connection itself; nothing here should build one.
 *
 * The returned pointer must outlive every connection. Returns NULL on
 * failure. */
picohttp_server_parameters_t *zone_wt_create_context(zone_wt_datagram_cb on_datagram,
                                                     void *app_ctx);

/* Frees the parameters and the path table under them. The per-connection
 * h3zero contexts are not ours: picoquic_free closes every connection,
 * and each one deletes the context it made for itself. */
void zone_wt_free_context(picohttp_server_parameters_t *params);
