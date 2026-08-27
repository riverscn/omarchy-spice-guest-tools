#define _POSIX_C_SOURCE 200809L

#include "clipboard-content.h"
#include "ext-data-control-v1-client-protocol.h"

#include <errno.h>
#include <gtk/gtk.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>

#define DEFAULT_MAX_BYTES (100ULL * 1024ULL * 1024ULL)
#define DEFAULT_MAX_PIXELS (8192ULL * 8192ULL)

typedef struct {
    const gchar *mime;
    const gchar *path;
    guint64 max_bytes;
    guint64 max_pixels;
    gboolean derive_png;
    gboolean foreground;
    gboolean print_types;
} Options;

typedef struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct ext_data_control_manager_v1 *manager;
    struct ext_data_control_device_v1 *device;
    struct ext_data_control_source_v1 *source;
    struct ext_data_control_offer_v1 *selection_offer;
    struct ext_data_control_offer_v1 *primary_offer;
    GtkClipboard *x11_clipboard;
    SpiceClipboardContent *content;
    gboolean running;
} Provider;

static volatile sig_atomic_t stop_requested = 0;

static void provider_stop(Provider *provider)
{
    provider->running = FALSE;
}

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage: spice-clipboard-provider --mime MIME [options] FILE\n"
            "\n"
            "Own the Wayland and X11 clipboards and serve one logical item in multiple MIME types.\n"
            "\n"
            "Options:\n"
            "  --mime MIME          Original clipboard MIME type (required)\n"
            "  --derive-png         Offer a lazily generated image/png fallback\n"
            "  --max-bytes N        Maximum source and derived size (default: %llu)\n"
            "  --max-pixels N       Maximum decoded image pixel count (default: %llu)\n"
            "  --foreground         Do not fork after acquiring the clipboard\n"
            "  --print-types        Print MIME types that would be offered and exit\n"
            "  -h, --help           Show this help\n",
            DEFAULT_MAX_BYTES, DEFAULT_MAX_PIXELS);
}

static gboolean parse_uint64(const gchar *text, guint64 *value)
{
    gchar *end = NULL;
    guint64 parsed;

    errno = 0;
    parsed = g_ascii_strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0)
        return FALSE;
    *value = parsed;
    return TRUE;
}

static gboolean parse_options(int argc, char **argv, Options *options)
{
    options->max_bytes = DEFAULT_MAX_BYTES;
    options->max_pixels = DEFAULT_MAX_PIXELS;

    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--mime") == 0 && i + 1 < argc) {
            options->mime = argv[++i];
        } else if (g_strcmp0(argv[i], "--max-bytes") == 0 && i + 1 < argc) {
            if (!parse_uint64(argv[++i], &options->max_bytes)) {
                fprintf(stderr, "invalid --max-bytes value: %s\n", argv[i]);
                return FALSE;
            }
        } else if (g_strcmp0(argv[i], "--max-pixels") == 0 && i + 1 < argc) {
            if (!parse_uint64(argv[++i], &options->max_pixels)) {
                fprintf(stderr, "invalid --max-pixels value: %s\n", argv[i]);
                return FALSE;
            }
        } else if (g_strcmp0(argv[i], "--derive-png") == 0) {
            options->derive_png = TRUE;
        } else if (g_strcmp0(argv[i], "--foreground") == 0) {
            options->foreground = TRUE;
        } else if (g_strcmp0(argv[i], "--print-types") == 0) {
            options->print_types = TRUE;
        } else if (g_strcmp0(argv[i], "-h") == 0 ||
                   g_strcmp0(argv[i], "--help") == 0) {
            usage(stdout);
            exit(EXIT_SUCCESS);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return FALSE;
        } else if (options->path == NULL) {
            options->path = argv[i];
        } else {
            fprintf(stderr, "unexpected argument: %s\n", argv[i]);
            return FALSE;
        }
    }

    if (options->mime == NULL || options->path == NULL) {
        fprintf(stderr, "--mime and FILE are required\n");
        return FALSE;
    }
    return TRUE;
}

static gboolean write_all(int fd, GBytes *bytes)
{
    gconstpointer data;
    gsize size;
    gsize offset = 0;

    data = g_bytes_get_data(bytes, &size);
    while (offset < size) {
        ssize_t written = write(fd, (const guint8 *)data + offset, size - offset);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return FALSE;
        }
        if (written == 0)
            return FALSE;
        offset += (gsize)written;
    }
    return TRUE;
}

static void source_send(void *data,
                        struct ext_data_control_source_v1 *source,
                        const char *mime_type,
                        int32_t fd)
{
    Provider *provider = data;
    GError *error = NULL;
    GBytes *bytes;

    (void)source;
    bytes = spice_clipboard_content_get(provider->content, mime_type, &error);
    if (bytes == NULL) {
        fprintf(stderr, "spice-clipboard-provider: cannot provide %s: %s\n",
                mime_type, error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        close(fd);
        return;
    }
    if (!write_all(fd, bytes) && errno != EPIPE) {
        fprintf(stderr, "spice-clipboard-provider: write %s failed: %s\n",
                mime_type, g_strerror(errno));
    }
    g_bytes_unref(bytes);
    close(fd);
}

static void source_cancelled(void *data,
                             struct ext_data_control_source_v1 *source)
{
    Provider *provider = data;

    if (provider->source == source) {
        ext_data_control_source_v1_destroy(source);
        provider->source = NULL;
    }
    provider_stop(provider);
}

static const struct ext_data_control_source_v1_listener source_listener = {
    .send = source_send,
    .cancelled = source_cancelled,
};

static void offer_mime(void *data,
                       struct ext_data_control_offer_v1 *offer,
                       const char *mime_type)
{
    (void)data;
    (void)offer;
    (void)mime_type;
}

static const struct ext_data_control_offer_v1_listener offer_listener = {
    .offer = offer_mime,
};

static void device_data_offer(void *data,
                              struct ext_data_control_device_v1 *device,
                              struct ext_data_control_offer_v1 *offer)
{
    (void)device;
    ext_data_control_offer_v1_add_listener(offer, &offer_listener, data);
}

static void replace_offer(struct ext_data_control_offer_v1 **current,
                          struct ext_data_control_offer_v1 *replacement)
{
    if (*current != NULL && *current != replacement)
        ext_data_control_offer_v1_destroy(*current);
    *current = replacement;
}

static void device_selection(void *data,
                             struct ext_data_control_device_v1 *device,
                             struct ext_data_control_offer_v1 *offer)
{
    Provider *provider = data;

    (void)device;
    replace_offer(&provider->selection_offer, offer);
}

static void device_finished(void *data,
                            struct ext_data_control_device_v1 *device)
{
    Provider *provider = data;

    (void)device;
    provider_stop(provider);
}

static void device_primary_selection(void *data,
                                     struct ext_data_control_device_v1 *device,
                                     struct ext_data_control_offer_v1 *offer)
{
    Provider *provider = data;

    (void)device;
    replace_offer(&provider->primary_offer, offer);
}

static const struct ext_data_control_device_v1_listener device_listener = {
    .data_offer = device_data_offer,
    .selection = device_selection,
    .finished = device_finished,
    .primary_selection = device_primary_selection,
};

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
    Provider *provider = data;

    if (g_strcmp0(interface, wl_seat_interface.name) == 0 &&
        provider->seat == NULL) {
        provider->seat = wl_registry_bind(registry, name, &wl_seat_interface,
                                          MIN(version, 1U));
    } else if (g_strcmp0(interface,
                        ext_data_control_manager_v1_interface.name) == 0 &&
               provider->manager == NULL) {
        provider->manager = wl_registry_bind(
            registry, name, &ext_data_control_manager_v1_interface,
            MIN(version, 1U));
    }
}

static void registry_global_remove(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void x11_clipboard_get(GtkClipboard *clipboard,
                              GtkSelectionData *selection_data,
                              guint info,
                              gpointer data)
{
    Provider *provider = data;
    GdkAtom target;
    gchar *mime;
    GError *error = NULL;
    GBytes *bytes;
    gconstpointer bytes_data;
    gsize size;

    (void)clipboard;
    (void)info;
    target = gtk_selection_data_get_target(selection_data);
    mime = gdk_atom_name(target);
    bytes = spice_clipboard_content_get(provider->content, mime, &error);
    if (bytes == NULL) {
        fprintf(stderr, "spice-clipboard-provider: cannot provide X11 %s: %s\n",
                mime, error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        g_free(mime);
        return;
    }
    bytes_data = g_bytes_get_data(bytes, &size);
    if (size > G_MAXINT) {
        fprintf(stderr, "spice-clipboard-provider: X11 %s representation is too large\n",
                mime);
    } else {
        gtk_selection_data_set(selection_data, target, 8, bytes_data, (gint)size);
    }
    g_bytes_unref(bytes);
    g_free(mime);
}

static void x11_clipboard_clear(GtkClipboard *clipboard, gpointer data)
{
    Provider *provider = data;

    (void)clipboard;
    provider_stop(provider);
}

static gboolean provider_setup_x11(Provider *provider)
{
    const gchar *const *offers;
    gsize offer_count;
    GtkTargetEntry *targets;
    gboolean claimed;

    g_setenv("GDK_BACKEND", "x11", TRUE);
    if (!gtk_init_check(NULL, NULL)) {
        fprintf(stderr, "spice-clipboard-provider: cannot connect to X11 display\n");
        return FALSE;
    }
    offers = spice_clipboard_content_get_offers(provider->content, &offer_count);
    targets = g_new0(GtkTargetEntry, offer_count);
    for (gsize i = 0; i < offer_count; i++) {
        targets[i].target = g_strdup(offers[i]);
        targets[i].flags = 0;
        targets[i].info = (guint)i;
    }

    provider->x11_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    claimed = gtk_clipboard_set_with_data(provider->x11_clipboard, targets,
                                          (guint)offer_count,
                                          x11_clipboard_get,
                                          x11_clipboard_clear,
                                          provider);
    for (gsize i = 0; i < offer_count; i++)
        g_free(targets[i].target);
    g_free(targets);
    if (!claimed) {
        fprintf(stderr, "spice-clipboard-provider: cannot own X11 clipboard\n");
        provider->x11_clipboard = NULL;
        return FALSE;
    }
    return TRUE;
}

static void provider_cleanup(Provider *provider)
{
    if (provider->selection_offer != NULL)
        ext_data_control_offer_v1_destroy(provider->selection_offer);
    if (provider->primary_offer != NULL)
        ext_data_control_offer_v1_destroy(provider->primary_offer);
    if (provider->source != NULL)
        ext_data_control_source_v1_destroy(provider->source);
    if (provider->device != NULL)
        ext_data_control_device_v1_destroy(provider->device);
    if (provider->manager != NULL)
        ext_data_control_manager_v1_destroy(provider->manager);
    if (provider->seat != NULL)
        wl_seat_destroy(provider->seat);
    if (provider->registry != NULL)
        wl_registry_destroy(provider->registry);
    if (provider->display != NULL)
        wl_display_disconnect(provider->display);
}

static gboolean provider_setup(Provider *provider)
{
    const gchar *const *offers;

    provider->running = TRUE;
    provider->display = wl_display_connect(NULL);
    if (provider->display == NULL) {
        fprintf(stderr, "spice-clipboard-provider: cannot connect to Wayland display\n");
        return FALSE;
    }
    provider->registry = wl_display_get_registry(provider->display);
    wl_registry_add_listener(provider->registry, &registry_listener, provider);
    if (wl_display_roundtrip(provider->display) < 0)
        return FALSE;
    if (provider->seat == NULL || provider->manager == NULL) {
        fprintf(stderr,
                "spice-clipboard-provider: compositor lacks ext-data-control-v1 or a seat\n");
        return FALSE;
    }

    provider->device = ext_data_control_manager_v1_get_data_device(
        provider->manager, provider->seat);
    ext_data_control_device_v1_add_listener(provider->device, &device_listener,
                                            provider);
    if (wl_display_roundtrip(provider->display) < 0)
        return FALSE;

    provider->source = ext_data_control_manager_v1_create_data_source(
        provider->manager);
    ext_data_control_source_v1_add_listener(provider->source, &source_listener,
                                            provider);
    offers = spice_clipboard_content_get_offers(provider->content, NULL);
    for (const gchar *const *offer = offers; *offer != NULL; offer++)
        ext_data_control_source_v1_offer(provider->source, *offer);
    ext_data_control_device_v1_set_selection(provider->device, provider->source);
    if (wl_display_flush(provider->display) < 0)
        return FALSE;
    if (!provider_setup_x11(provider))
        return FALSE;
    while (g_main_context_iteration(NULL, FALSE))
        ;
    if (!provider->running) {
        fprintf(stderr, "spice-clipboard-provider: clipboard ownership was replaced during setup\n");
        return FALSE;
    }
    return TRUE;
}

static int provider_run(SpiceClipboardContent *content, int ready_fd)
{
    Provider provider = {
        .content = content,
    };
    char ready = '0';
    int result = EXIT_FAILURE;

    if (!provider_setup(&provider))
        goto out;

    ready = '1';
    if (ready_fd >= 0) {
        if (write(ready_fd, &ready, 1) != 1)
            goto out;
        close(ready_fd);
        ready_fd = -1;
    }

    while (provider.running && !stop_requested) {
        struct pollfd wayland_poll = {
            .fd = wl_display_get_fd(provider.display),
            .events = POLLIN,
        };
        int poll_result;

        if (wl_display_dispatch_pending(provider.display) < 0)
            goto dispatch_failed;
        if (wl_display_flush(provider.display) < 0 && errno != EAGAIN)
            goto dispatch_failed;
        while (g_main_context_iteration(NULL, FALSE))
            ;
        poll_result = poll(&wayland_poll, 1, 50);
        if (poll_result < 0) {
            if (errno == EINTR)
                continue;
            goto dispatch_failed;
        }
        if (poll_result > 0 && (wayland_poll.revents & POLLIN) != 0 &&
            wl_display_dispatch(provider.display) < 0)
            goto dispatch_failed;
        if ((wayland_poll.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            goto dispatch_failed;
    }
    result = EXIT_SUCCESS;
    goto out;

dispatch_failed:
    fprintf(stderr, "spice-clipboard-provider: Wayland dispatch failed: %s\n",
            g_strerror(errno));

out:
    if (ready_fd >= 0) {
        ssize_t written;

        do {
            written = write(ready_fd, &ready, 1);
        } while (written < 0 && errno == EINTR);
        close(ready_fd);
    }
    provider_cleanup(&provider);
    return result;
}

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void install_signal_handlers(void)
{
    struct sigaction action = {
        .sa_handler = handle_signal,
    };

    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
}

static int run_background(SpiceClipboardContent *content)
{
    int ready_pipe[2];
    pid_t child;
    char ready = '0';

    if (pipe(ready_pipe) < 0) {
        fprintf(stderr, "spice-clipboard-provider: pipe failed: %s\n",
                g_strerror(errno));
        return EXIT_FAILURE;
    }
    child = fork();
    if (child < 0) {
        fprintf(stderr, "spice-clipboard-provider: fork failed: %s\n",
                g_strerror(errno));
        close(ready_pipe[0]);
        close(ready_pipe[1]);
        return EXIT_FAILURE;
    }
    if (child == 0) {
        int result;

        close(ready_pipe[0]);
        (void)setsid();
        result = provider_run(content, ready_pipe[1]);
        spice_clipboard_content_free(content);
        _exit(result);
    }

    close(ready_pipe[1]);
    while (read(ready_pipe[0], &ready, 1) < 0 && errno == EINTR)
        ;
    close(ready_pipe[0]);
    if (ready != '1') {
        (void)waitpid(child, NULL, 0);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    Options options = { 0 };
    SpiceClipboardContent *content;
    GError *error = NULL;
    int result;

    if (!parse_options(argc, argv, &options)) {
        usage(stderr);
        return EXIT_FAILURE;
    }
    content = spice_clipboard_content_new(options.path, options.mime,
                                          options.derive_png,
                                          options.max_bytes,
                                          options.max_pixels, &error);
    if (content == NULL) {
        fprintf(stderr, "spice-clipboard-provider: %s\n",
                error != NULL ? error->message : "cannot load clipboard item");
        g_clear_error(&error);
        return EXIT_FAILURE;
    }

    if (options.print_types) {
        const gchar *const *offers = spice_clipboard_content_get_offers(content, NULL);
        for (const gchar *const *offer = offers; *offer != NULL; offer++)
            puts(*offer);
        spice_clipboard_content_free(content);
        return EXIT_SUCCESS;
    }

    install_signal_handlers();
    if (options.foreground) {
        result = provider_run(content, -1);
        spice_clipboard_content_free(content);
        return result;
    }
    result = run_background(content);
    spice_clipboard_content_free(content);
    return result;
}
