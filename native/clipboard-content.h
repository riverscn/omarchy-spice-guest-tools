#ifndef SPICE_CLIPBOARD_CONTENT_H
#define SPICE_CLIPBOARD_CONTENT_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define SPICE_CLIPBOARD_MARKER_MIME "application/x-spice-guest-tools"

typedef struct SpiceClipboardContent SpiceClipboardContent;

SpiceClipboardContent *spice_clipboard_content_new(const gchar *path,
                                                    const gchar *mime,
                                                    gboolean derive_png,
                                                    guint64 max_bytes,
                                                    guint64 max_pixels,
                                                    GError **error);

void spice_clipboard_content_free(SpiceClipboardContent *content);

const gchar *const *spice_clipboard_content_get_offers(
    const SpiceClipboardContent *content,
    gsize *length);

GBytes *spice_clipboard_content_get(SpiceClipboardContent *content,
                                    const gchar *mime,
                                    GError **error);

gboolean spice_clipboard_content_png_is_cached(
    const SpiceClipboardContent *content);

G_END_DECLS

#endif
