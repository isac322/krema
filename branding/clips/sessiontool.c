/*
 * sessiontool -- Wayland helper for the Krema clip pipeline (GPU session).
 *
 * Creates a zkde_screencast_unstable_v1 stream (a PipeWire source served by
 * KWin) and prints the PipeWire node id / object serial so a consumer
 * (gst-launch pipewiresrc) can attach. The stream stays alive until the
 * process exits; rec_stop kills it after the consumer has drained.
 *
 *   sessiontool region X Y W H SCALE     stream the logical rect at SCALE
 *   sessiontool output                   stream the first wl_output
 *
 * Pointer mode is "embedded" so the cursor is composited into the frames.
 *
 * SPDX-License-Identifier: MIT
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

#include "zkde-screencast-unstable-v1.h"

static struct wl_display *s_display;
static struct zkde_screencast_unstable_v1 *s_screencast;
static struct wl_output *s_output;
static volatile sig_atomic_t s_done;

static void on_created(void *data, struct zkde_screencast_stream_unstable_v1 *stream, uint32_t node)
{
    (void)data;
    (void)stream;
    printf("node %u\n", node);
    fflush(stdout);
}

static void on_serial(void *data, struct zkde_screencast_stream_unstable_v1 *stream,
                      uint32_t hi, uint32_t lo)
{
    (void)data;
    (void)stream;
    printf("serial %llu\n", ((unsigned long long)hi << 32) | lo);
    fflush(stdout);
}

static void on_failed(void *data, struct zkde_screencast_stream_unstable_v1 *stream, const char *error)
{
    (void)data;
    (void)stream;
    fprintf(stderr, "screencast failed: %s\n", error);
    exit(1);
}

static void on_closed(void *data, struct zkde_screencast_stream_unstable_v1 *stream)
{
    (void)data;
    zkde_screencast_stream_unstable_v1_close(stream);
    s_done = 1;
}

static const struct zkde_screencast_stream_unstable_v1_listener s_stream_listener = {
    .closed = on_closed,
    .created = on_created,
    .failed = on_failed,
    .serial = on_serial,
};

static void on_output_geometry(void *d, struct wl_output *o, int32_t x, int32_t y,
                               int32_t pw, int32_t ph, int32_t s, const char *make,
                               const char *model, int32_t tr)
{
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)s; (void)make; (void)tr;
    printf("output model %s\n", model);
}

static void on_output_mode(void *d, struct wl_output *o, uint32_t f, int32_t w, int32_t h, int32_t r)
{
    (void)d; (void)o; (void)f;
    printf("output mode %dx%d\n", w, h);
}

static void on_output_scale(void *d, struct wl_output *o, int32_t s)
{
    (void)d; (void)o;
    printf("output scale %d\n", s);
}

static void on_output_done(void *d, struct wl_output *o) { (void)d; (void)o; printf("output done\n"); }
static void on_output_name(void *d, struct wl_output *o, const char *n) { (void)d; (void)o; printf("output name %s\n", n); }
static void on_output_description(void *d, struct wl_output *o, const char *n) { (void)d; (void)o; }

static const struct wl_output_listener s_output_listener = {
    .geometry = on_output_geometry,
    .mode = on_output_mode,
    .done = on_output_done,
    .scale = on_output_scale,
    .name = on_output_name,
    .description = on_output_description,
};

static void on_global(void *data, struct wl_registry *registry, uint32_t name,
                      const char *interface, uint32_t version)
{
    (void)data;
    if (strcmp(interface, zkde_screencast_unstable_v1_interface.name) == 0) {
        s_screencast = wl_registry_bind(registry, name, &zkde_screencast_unstable_v1_interface,
                                      version < 6 ? version : 6);
    } else if (strcmp(interface, wl_output_interface.name) == 0 && !s_output) {
        s_output = wl_registry_bind(registry, name, &wl_output_interface, version < 4 ? version : 4);
        wl_output_add_listener(s_output, &s_output_listener, NULL);
    }
}

static void on_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener s_registry_listener = {
    .global = on_global,
    .global_remove = on_global_remove,
};

// wl_display_dispatch() retries its poll on EINTR, so leave right here; KWin
// ends the stream when the client disconnects.
static void on_signal(int sig)
{
    (void)sig;
    _exit(0);
}

int main(int argc, char **argv)
{
    int use_region = 0;
    int x = 0, y = 0, w = 0, h = 0;
    double scale = 1.0;

    if (argc >= 7 && strcmp(argv[1], "region") == 0) {
        use_region = 1;
        x = atoi(argv[2]); y = atoi(argv[3]); w = atoi(argv[4]); h = atoi(argv[5]);
        scale = atof(argv[6]);
        if (scale <= 0) scale = 1.0;
    } else if (argc >= 2 && strcmp(argv[1], "output") == 0) {
    } else {
        fprintf(stderr, "usage: %s output | region X Y W H SCALE\n", argv[0]);
        return 2;
    }

    s_display = wl_display_connect(NULL);
    if (!s_display) {
        fprintf(stderr, "cannot connect to wayland\n");
        return 1;
    }
    struct wl_registry *registry = wl_display_get_registry(s_display);
    wl_registry_add_listener(registry, &s_registry_listener, NULL);
    wl_display_roundtrip(s_display);
    wl_display_roundtrip(s_display);
    if (!s_screencast) {
        fprintf(stderr, "compositor does not offer zkde_screencast_unstable_v1\n");
        return 1;
    }

    struct zkde_screencast_stream_unstable_v1 *stream;
    const uint32_t pointer = ZKDE_SCREENCAST_UNSTABLE_V1_POINTER_EMBEDDED;
    if (use_region) {
        stream = zkde_screencast_unstable_v1_stream_region(s_screencast, x, y, w, h,
                                                         wl_fixed_from_double(scale), pointer);
    } else {
        if (!s_output) {
            fprintf(stderr, "no wl_output\n");
            return 1;
        }
        stream = zkde_screencast_unstable_v1_stream_output(s_screencast, s_output, pointer);
    }
    zkde_screencast_stream_unstable_v1_add_listener(stream, &s_stream_listener, NULL);
    wl_display_roundtrip(s_display);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    while (!s_done) {
        if (wl_display_dispatch(s_display) < 0)
            break;
    }
    wl_display_disconnect(s_display);
    return 0;
}
