/**
 * @file static.c
 * @brief Static file serving with path-traversal protection.
 */

#include "static.h"

#include <stdio.h>
#include <string.h>

/* Extensions we recognise. */
static const struct {
    const char *ext;
    const char *mime;
} MIME_TABLE[] = {
    { ".html", "text/html" },
    { ".htm",  "text/html" },
    { ".css",  "text/css"  },
    { ".js",   "application/javascript" },
    { ".json", "application/json" },
    { ".png",  "image/png"  },
    { ".jpg",  "image/jpeg" },
    { ".jpeg", "image/jpeg" },
    { ".gif",  "image/gif"  },
    { ".svg",  "image/svg+xml" },
    { ".ico",  "image/x-icon" },
    { ".txt",  "text/plain" },
    { ".md",   "text/markdown" },
    { NULL, NULL }
};

static const char *mime_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    for (int i = 0; MIME_TABLE[i].ext; ++i) {
        if (strcasecmp(dot, MIME_TABLE[i].ext) == 0) {
            return MIME_TABLE[i].mime;
        }
    }
    return "application/octet-stream";
}

long static_serve(const char *root,
                  const char *url_path,
                  char *out_buf,
                  size_t out_cap,
                  const char **content_type_out) {
    if (strstr(url_path, "..")) {
        return -1;  /* path traversal attempt */
    }

    char full_path[1024];
    if (strcmp(url_path, "/") == 0 || strcmp(url_path, "") == 0) {
        /* Default to index.php if present, else index.html */
        snprintf(full_path, sizeof(full_path), "%s/index.php", root);
        FILE *probe = fopen(full_path, "rb");
        if (!probe) {
            snprintf(full_path, sizeof(full_path), "%s/index.html", root);
        } else {
            fclose(probe);
        }
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s", root, url_path);
    }

    FILE *f = fopen(full_path, "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    if (size < 0 || (size_t)size > out_cap) { fclose(f); return -1; }
    rewind(f);

    size_t got = fread(out_buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) return -1;

    if (content_type_out) {
        *content_type_out = mime_for(full_path);
    }
    return size;
}
