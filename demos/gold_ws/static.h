/**
 * @file static.h
 * @brief Static file serving.
 */

#ifndef GOLD_WS_STATIC_H
#define GOLD_WS_STATIC_H

#include <stddef.h>

/**
 * @brief Read a file under the public root.
 *
 * @param root        Public root directory (e.g. "public").
 * @param url_path    Request path (e.g. "/style.css").
 * @param out_buf     Output buffer for the file contents.
 * @param out_cap     Output capacity.
 * @param content_type_out Output: content type string (points to a
 *                         static string, do not free).
 * @return File size on success, -1 if the file does not exist or
 *         the path escapes the root.
 */
long static_serve(const char *root,
                  const char *url_path,
                  char *out_buf,
                  size_t out_cap,
                  const char **content_type_out);

#endif /* GOLD_WS_STATIC_H */
