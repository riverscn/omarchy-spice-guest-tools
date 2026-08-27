#include "clipboard-content.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

typedef enum {
    SPICE_CLIPBOARD_CONTENT_ERROR_INVALID_MIME,
    SPICE_CLIPBOARD_CONTENT_ERROR_TOO_LARGE,
    SPICE_CLIPBOARD_CONTENT_ERROR_UNSUPPORTED,
    SPICE_CLIPBOARD_CONTENT_ERROR_INVALID_IMAGE,
} SpiceClipboardContentError;

#define SPICE_CLIPBOARD_CONTENT_ERROR \
    spice_clipboard_content_error_quark()

struct SpiceClipboardContent {
    gchar *mime;
    GBytes *original;
    GBytes *png;
    GPtrArray *offers;
    guint64 max_bytes;
    guint64 max_pixels;
    gboolean image;
    gboolean text;
};

typedef struct {
    guint64 max_pixels;
    gboolean rejected;
} DecodeLimits;

static GQuark spice_clipboard_content_error_quark(void)
{
    return g_quark_from_static_string("spice-clipboard-content-error");
}

static gboolean mime_is_text(const gchar *mime)
{
    return g_ascii_strcasecmp(mime, "text/plain") == 0 ||
           g_ascii_strcasecmp(mime, "text/plain;charset=utf-8") == 0 ||
           g_ascii_strcasecmp(mime, "UTF8_STRING") == 0 ||
           g_ascii_strcasecmp(mime, "TEXT") == 0 ||
           g_ascii_strcasecmp(mime, "STRING") == 0;
}

static const gchar *canonical_image_mime(const gchar *mime)
{
    if (g_ascii_strcasecmp(mime, "image/png") == 0)
        return "image/png";
    if (g_ascii_strcasecmp(mime, "image/jpeg") == 0 ||
        g_ascii_strcasecmp(mime, "image/jpg") == 0)
        return "image/jpeg";
    if (g_ascii_strcasecmp(mime, "image/tiff") == 0)
        return "image/tiff";
    if (g_ascii_strcasecmp(mime, "image/bmp") == 0 ||
        g_ascii_strcasecmp(mime, "image/x-bmp") == 0 ||
        g_ascii_strcasecmp(mime, "image/x-MS-bmp") == 0 ||
        g_ascii_strcasecmp(mime, "image/x-win-bitmap") == 0)
        return "image/bmp";
    return NULL;
}

static gboolean pixbuf_supports_mime(const gchar *mime, gboolean writable)
{
    GSList *formats = gdk_pixbuf_get_formats();
    gboolean supported = FALSE;

    for (GSList *item = formats; item != NULL && !supported; item = item->next) {
        GdkPixbufFormat *format = item->data;
        gchar **mime_types;

        if (writable && !gdk_pixbuf_format_is_writable(format))
            continue;
        mime_types = gdk_pixbuf_format_get_mime_types(format);
        for (gchar **candidate = mime_types; *candidate != NULL; candidate++) {
            if (g_ascii_strcasecmp(*candidate, mime) == 0) {
                supported = TRUE;
                break;
            }
        }
        g_strfreev(mime_types);
    }
    g_slist_free(formats);
    return supported;
}

static gboolean offers_contains(const GPtrArray *offers, const gchar *mime)
{
    for (guint i = 0; i < offers->len; i++) {
        if (g_ascii_strcasecmp(g_ptr_array_index((GPtrArray *)offers, i), mime) == 0)
            return TRUE;
    }
    return FALSE;
}

static void add_offer(SpiceClipboardContent *content, const gchar *mime)
{
    if (!offers_contains(content->offers, mime))
        g_ptr_array_add(content->offers, g_strdup(mime));
}

static void size_prepared(GdkPixbufLoader *loader,
                          gint width,
                          gint height,
                          gpointer user_data)
{
    DecodeLimits *limits = user_data;
    guint64 pixels;

    if (width <= 0 || height <= 0) {
        limits->rejected = TRUE;
        gdk_pixbuf_loader_set_size(loader, 1, 1);
        return;
    }
    pixels = (guint64)width * (guint64)height;
    if (pixels > limits->max_pixels) {
        limits->rejected = TRUE;
        /* Ask loaders that support scaling to avoid allocating the source size. */
        gdk_pixbuf_loader_set_size(loader, 1, 1);
    }
}

static GBytes *convert_to_png(SpiceClipboardContent *content, GError **error)
{
    gconstpointer source_data;
    gsize source_size;
    gsize offset = 0;
    GdkPixbufLoader *loader;
    GdkPixbuf *pixbuf;
    DecodeLimits limits = {
        .max_pixels = content->max_pixels,
        .rejected = FALSE,
    };
    gchar *png_data = NULL;
    gsize png_size = 0;
    GError *local_error = NULL;

    if (content->png != NULL)
        return g_bytes_ref(content->png);

    loader = gdk_pixbuf_loader_new_with_mime_type(content->mime, &local_error);
    if (loader == NULL) {
        g_propagate_prefixed_error(error, local_error,
                                   "cannot decode %s: ", content->mime);
        return NULL;
    }
    g_signal_connect(loader, "size-prepared", G_CALLBACK(size_prepared), &limits);
    source_data = g_bytes_get_data(content->original, &source_size);

    while (offset < source_size && !limits.rejected) {
        gsize chunk_size = MIN((gsize)4096, source_size - offset);
        if (!gdk_pixbuf_loader_write(loader,
                                     (const guchar *)source_data + offset,
                                     chunk_size,
                                     &local_error)) {
            g_propagate_prefixed_error(error, local_error,
                                       "invalid %s clipboard image: ",
                                       content->mime);
            g_object_unref(loader);
            return NULL;
        }
        offset += chunk_size;
    }

    if (limits.rejected) {
        g_set_error(error, SPICE_CLIPBOARD_CONTENT_ERROR,
                    SPICE_CLIPBOARD_CONTENT_ERROR_TOO_LARGE,
                    "clipboard image exceeds maximum pixel count of %" G_GUINT64_FORMAT,
                    content->max_pixels);
        g_object_unref(loader);
        return NULL;
    }
    if (!gdk_pixbuf_loader_close(loader, &local_error)) {
        g_propagate_prefixed_error(error, local_error,
                                   "invalid %s clipboard image: ", content->mime);
        g_object_unref(loader);
        return NULL;
    }
    if (limits.rejected) {
        g_set_error(error, SPICE_CLIPBOARD_CONTENT_ERROR,
                    SPICE_CLIPBOARD_CONTENT_ERROR_TOO_LARGE,
                    "clipboard image exceeds maximum pixel count of %" G_GUINT64_FORMAT,
                    content->max_pixels);
        g_object_unref(loader);
        return NULL;
    }

    pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    if (pixbuf == NULL) {
        g_set_error(error, SPICE_CLIPBOARD_CONTENT_ERROR,
                    SPICE_CLIPBOARD_CONTENT_ERROR_INVALID_IMAGE,
                    "decoder returned no pixels for %s", content->mime);
        g_object_unref(loader);
        return NULL;
    }
    if ((guint64)gdk_pixbuf_get_width(pixbuf) *
            (guint64)gdk_pixbuf_get_height(pixbuf) > content->max_pixels) {
        g_set_error(error, SPICE_CLIPBOARD_CONTENT_ERROR,
                    SPICE_CLIPBOARD_CONTENT_ERROR_TOO_LARGE,
                    "clipboard image exceeds maximum pixel count of %" G_GUINT64_FORMAT,
                    content->max_pixels);
        g_object_unref(loader);
        return NULL;
    }
    if (!gdk_pixbuf_save_to_buffer(pixbuf, &png_data, &png_size, "png",
                                   &local_error, NULL)) {
        g_propagate_prefixed_error(error, local_error,
                                   "cannot encode PNG clipboard fallback: ");
        g_object_unref(loader);
        return NULL;
    }
    g_object_unref(loader);

    if (png_size > content->max_bytes) {
        g_set_error(error, SPICE_CLIPBOARD_CONTENT_ERROR,
                    SPICE_CLIPBOARD_CONTENT_ERROR_TOO_LARGE,
                    "derived PNG is %zu bytes and exceeds maximum of %" G_GUINT64_FORMAT,
                    png_size, content->max_bytes);
        g_free(png_data);
        return NULL;
    }
    content->png = g_bytes_new_take(png_data, png_size);
    return g_bytes_ref(content->png);
}

SpiceClipboardContent *spice_clipboard_content_new(const gchar *path,
                                                    const gchar *mime,
                                                    gboolean derive_png,
                                                    guint64 max_bytes,
                                                    guint64 max_pixels,
                                                    GError **error)
{
    SpiceClipboardContent *content;
    const gchar *image_mime;
    gchar *data = NULL;
    gsize size = 0;

    g_return_val_if_fail(path != NULL, NULL);
    g_return_val_if_fail(mime != NULL, NULL);

    image_mime = canonical_image_mime(mime);
    if (image_mime == NULL && !mime_is_text(mime)) {
        g_set_error(error, SPICE_CLIPBOARD_CONTENT_ERROR,
                    SPICE_CLIPBOARD_CONTENT_ERROR_INVALID_MIME,
                    "unsupported clipboard MIME type: %s", mime);
        return NULL;
    }
    if (!g_file_get_contents(path, &data, &size, error))
        return NULL;
    if (size > max_bytes) {
        g_set_error(error, SPICE_CLIPBOARD_CONTENT_ERROR,
                    SPICE_CLIPBOARD_CONTENT_ERROR_TOO_LARGE,
                    "clipboard item is %zu bytes and exceeds maximum of %" G_GUINT64_FORMAT,
                    size, max_bytes);
        g_free(data);
        return NULL;
    }

    content = g_new0(SpiceClipboardContent, 1);
    content->mime = g_strdup(image_mime != NULL ? image_mime : "text/plain");
    content->original = g_bytes_new_take(data, size);
    content->offers = g_ptr_array_new_with_free_func(g_free);
    content->max_bytes = max_bytes;
    content->max_pixels = max_pixels;
    content->image = image_mime != NULL;
    content->text = image_mime == NULL;

    if (content->text) {
        add_offer(content, "text/plain;charset=utf-8");
        add_offer(content, "text/plain");
        add_offer(content, "UTF8_STRING");
        add_offer(content, "TEXT");
    } else {
        add_offer(content, content->mime);
        if (derive_png && g_strcmp0(content->mime, "image/png") != 0 &&
            pixbuf_supports_mime(content->mime, FALSE) &&
            pixbuf_supports_mime("image/png", TRUE))
            add_offer(content, "image/png");
    }
    add_offer(content, SPICE_CLIPBOARD_MARKER_MIME);
    g_ptr_array_add(content->offers, NULL);
    return content;
}

void spice_clipboard_content_free(SpiceClipboardContent *content)
{
    if (content == NULL)
        return;
    g_free(content->mime);
    g_clear_pointer(&content->original, g_bytes_unref);
    g_clear_pointer(&content->png, g_bytes_unref);
    g_ptr_array_unref(content->offers);
    g_free(content);
}

const gchar *const *spice_clipboard_content_get_offers(
    const SpiceClipboardContent *content,
    gsize *length)
{
    g_return_val_if_fail(content != NULL, NULL);
    if (length != NULL)
        *length = content->offers->len - 1;
    return (const gchar *const *)content->offers->pdata;
}

GBytes *spice_clipboard_content_get(SpiceClipboardContent *content,
                                    const gchar *mime,
                                    GError **error)
{
    static const gchar marker_value[] = "spice-guest-tools\n";

    g_return_val_if_fail(content != NULL, NULL);
    g_return_val_if_fail(mime != NULL, NULL);

    if (!offers_contains(content->offers, mime)) {
        g_set_error(error, SPICE_CLIPBOARD_CONTENT_ERROR,
                    SPICE_CLIPBOARD_CONTENT_ERROR_UNSUPPORTED,
                    "clipboard MIME type was not offered: %s", mime);
        return NULL;
    }
    if (g_ascii_strcasecmp(mime, SPICE_CLIPBOARD_MARKER_MIME) == 0)
        return g_bytes_new_static(marker_value, sizeof(marker_value) - 1);
    if (content->text || g_ascii_strcasecmp(mime, content->mime) == 0)
        return g_bytes_ref(content->original);
    if (g_ascii_strcasecmp(mime, "image/png") == 0)
        return convert_to_png(content, error);

    g_set_error(error, SPICE_CLIPBOARD_CONTENT_ERROR,
                SPICE_CLIPBOARD_CONTENT_ERROR_UNSUPPORTED,
                "clipboard MIME type has no representation: %s", mime);
    return NULL;
}

gboolean spice_clipboard_content_png_is_cached(
    const SpiceClipboardContent *content)
{
    g_return_val_if_fail(content != NULL, FALSE);
    return content->png != NULL;
}
