#include "clipboard-content.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

static gchar *write_temp_bytes(const gchar *pattern, GBytes *bytes)
{
    gchar *path = NULL;
    gint fd = g_file_open_tmp(pattern, &path, NULL);
    gconstpointer data;
    gsize size;

    g_assert_cmpint(fd, >=, 0);
    data = g_bytes_get_data(bytes, &size);
    g_assert_cmpint(write(fd, data, size), ==, (ssize_t)size);
    g_assert_cmpint(close(fd), ==, 0);
    return path;
}

static gboolean offers_mime(SpiceClipboardContent *content, const gchar *mime)
{
    const gchar *const *offers = spice_clipboard_content_get_offers(content, NULL);

    for (const gchar *const *offer = offers; *offer != NULL; offer++) {
        if (g_strcmp0(*offer, mime) == 0)
            return TRUE;
    }
    return FALSE;
}

static GBytes *make_tiff_image(void)
{
    static const gchar encoded[] =
        "SUkqACgAAAAzM2ZmzMyAgDMzZmbMzICAMzNmZszMgIAzM2ZmzMyAgBAAAAEDAAEA"
        "AAACAAAAAQEDAAEAAAACAAAAAgEDAAQAAADuAAAAAwEDAAEAAAABAAAABgEDAAEA"
        "AAACAAAACgEDAAEAAAABAAAAEQEEAAEAAAAIAAAAEgEDAAEAAAABAAAAFQEDAAEA"
        "AAAEAAAAFgEDAAEAAAACAAAAFwEEAAEAAAAgAAAAHAEDAAEAAAABAAAAKQEDAAIA"
        "AAAAAAEAPgEFAAIAAAAmAQAAPwEFAAYAAAD2AAAAUgEDAAEAAAACAAAAAAAAABAA"
        "EAAQABAAhetRAAAAgADD9agAAAAAAs3MTAAAAAABzcxMAAAAgADNzEwAAAAAAo/C"
        "9QAAAAAQNxqgAAAAAAIrhwoAAAAgAA==";
    gsize size = 0;
    guchar *data = g_base64_decode(encoded, &size);

    return g_bytes_new_take(data, size);
}

static void text_offers_aliases_without_image_conversion(void)
{
    GBytes *source = g_bytes_new_static("hello", 5);
    gchar *path = write_temp_bytes("spice-content-text-XXXXXX", source);
    GError *error = NULL;
    SpiceClipboardContent *content = spice_clipboard_content_new(
        path, "text/plain", TRUE, 1024, 1024, &error);
    GBytes *alias;

    g_assert_no_error(error);
    g_assert_nonnull(content);
    g_assert_true(offers_mime(content, "text/plain;charset=utf-8"));
    g_assert_true(offers_mime(content, "UTF8_STRING"));
    g_assert_true(offers_mime(content, SPICE_CLIPBOARD_MARKER_MIME));
    g_assert_false(offers_mime(content, "image/png"));

    alias = spice_clipboard_content_get(content, "UTF8_STRING", &error);
    g_assert_no_error(error);
    g_assert_true(g_bytes_equal(source, alias));

    g_bytes_unref(alias);
    spice_clipboard_content_free(content);
    g_bytes_unref(source);
    g_unlink(path);
    g_free(path);
}

static void tiff_preserves_original_and_derives_png_lazily(void)
{
    GBytes *source = make_tiff_image();
    gchar *path = write_temp_bytes("spice-content-tiff-XXXXXX", source);
    GError *error = NULL;
    SpiceClipboardContent *content = spice_clipboard_content_new(
        path, "image/tiff", TRUE, 1024 * 1024, 1024, &error);
    GBytes *original;
    GBytes *png;
    gconstpointer png_data;
    gsize png_size;

    g_assert_no_error(error);
    g_assert_nonnull(content);
    g_assert_true(offers_mime(content, "image/tiff"));
    g_assert_true(offers_mime(content, "image/png"));
    g_assert_true(offers_mime(content, SPICE_CLIPBOARD_MARKER_MIME));
    g_assert_false(spice_clipboard_content_png_is_cached(content));

    original = spice_clipboard_content_get(content, "image/tiff", &error);
    g_assert_no_error(error);
    g_assert_true(g_bytes_equal(source, original));
    g_assert_false(spice_clipboard_content_png_is_cached(content));

    png = spice_clipboard_content_get(content, "image/png", &error);
    g_assert_no_error(error);
    g_assert_nonnull(png);
    g_assert_true(spice_clipboard_content_png_is_cached(content));
    png_data = g_bytes_get_data(png, &png_size);
    g_assert_cmpuint(png_size, >=, 8);
    g_assert_cmpmem(png_data, 8, "\x89PNG\r\n\x1a\n", 8);

    g_bytes_unref(png);
    g_bytes_unref(original);
    spice_clipboard_content_free(content);
    g_bytes_unref(source);
    g_unlink(path);
    g_free(path);
}

static void conversion_enforces_pixel_limit(void)
{
    GBytes *source = make_tiff_image();
    gchar *path = write_temp_bytes("spice-content-tiff-limit-XXXXXX", source);
    GError *error = NULL;
    SpiceClipboardContent *content = spice_clipboard_content_new(
        path, "image/tiff", TRUE, 1024 * 1024, 1, &error);
    GBytes *png;

    g_assert_no_error(error);
    g_assert_nonnull(content);
    png = spice_clipboard_content_get(content, "image/png", &error);
    g_assert_null(png);
    g_assert_error(error, g_quark_from_static_string("spice-clipboard-content-error"), 1);

    g_clear_error(&error);
    spice_clipboard_content_free(content);
    g_bytes_unref(source);
    g_unlink(path);
    g_free(path);
}

static void derived_png_can_be_disabled(void)
{
    GBytes *source = make_tiff_image();
    gchar *path = write_temp_bytes("spice-content-no-derived-XXXXXX", source);
    GError *error = NULL;
    SpiceClipboardContent *content = spice_clipboard_content_new(
        path, "image/tiff", FALSE, 1024 * 1024, 1024, &error);

    g_assert_no_error(error);
    g_assert_nonnull(content);
    g_assert_true(offers_mime(content, "image/tiff"));
    g_assert_false(offers_mime(content, "image/png"));
    g_assert_true(offers_mime(content, SPICE_CLIPBOARD_MARKER_MIME));

    spice_clipboard_content_free(content);
    g_bytes_unref(source);
    g_unlink(path);
    g_free(path);
}

static void input_enforces_byte_limit(void)
{
    GBytes *source = g_bytes_new_static("too large", 9);
    gchar *path = write_temp_bytes("spice-content-large-XXXXXX", source);
    GError *error = NULL;
    SpiceClipboardContent *content = spice_clipboard_content_new(
        path, "text/plain", TRUE, 4, 1024, &error);

    g_assert_null(content);
    g_assert_error(error, g_quark_from_static_string("spice-clipboard-content-error"), 1);

    g_clear_error(&error);
    g_bytes_unref(source);
    g_unlink(path);
    g_free(path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/clipboard/text-offers",
                    text_offers_aliases_without_image_conversion);
    g_test_add_func("/clipboard/tiff-lazy-png",
                    tiff_preserves_original_and_derives_png_lazily);
    g_test_add_func("/clipboard/pixel-limit",
                    conversion_enforces_pixel_limit);
    g_test_add_func("/clipboard/no-derived-png",
                    derived_png_can_be_disabled);
    g_test_add_func("/clipboard/byte-limit",
                    input_enforces_byte_limit);
    return g_test_run();
}
