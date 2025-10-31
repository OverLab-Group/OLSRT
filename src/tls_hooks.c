/* OLSRT - TLS hooks */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>

/* Enable TLS on an existing stream.
 * cert_file/key_file are optional depending on role (server vs client).
 * Currently returns OL_ERR_NOTSUP as no TLS engine is embedded.
 */
int ol_stream_enable_tls(ol_stream_t *st, const char *cert_file, const char *key_file) {
    (void)st;
    (void)cert_file;
    (void)key_file;
    return OL_ERR_NOTSUP;
}

/* Disable TLS and revert to plain I/O.
 * Currently returns OL_ERR_NOTSUP as no TLS engine is embedded.
 */
int ol_stream_disable_tls(ol_stream_t *st) {
    (void)st;
    return OL_ERR_NOTSUP;
}
