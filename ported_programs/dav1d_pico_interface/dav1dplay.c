/*
 * Copyright © 2019, VideoLAN and dav1d authors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "vcs_version.h"

#include <getopt.h>
#include <stdbool.h>

#include "dav1d/dav1d.h"
#include "common/attributes.h"
#include "tools/input/input.h"
#include "dp_fifo.h"
#include "dp_renderer.h"

#include "dav1dplay.h"

#include <stdio.h>
#include <pico/stdlib.h>
#include "pico/time.h"

#include "FreeRTOS.h"
#include "task.h"
#include "pthread.h"
#include "FreeRTOS_POSIX_types.h"

#define FRAME_OFFSET_TO_PTS(foff) \
    (uint64_t)(((foff) * rd_ctx->spf) * 1000000000.0 + .5)
#define TS_TO_PTS(ts) \
    (uint64_t)(((ts) * rd_ctx->timebase) * 1000000000.0 + .5)

// Selected renderer callbacks and cookie
static const Dav1dPlayRenderInfo *renderer_info = { NULL };

/**
 * Render context structure
 * This structure contains informations necessary
 * to be shared between the decoder and the renderer
 * threads.
 */
typedef struct render_context
{
    Dav1dPlaySettings settings;
    Dav1dSettings lib_settings;

    // Renderer private data (passed to callbacks)
    void *rd_priv;

    // Lock to protect access to the context structure
    pthread_mutex_t *lock;

    // Timestamp of last displayed frame (in timebase unit)
    int64_t last_ts;
    // Timestamp of last decoded frame (in timebase unit)
    int64_t current_ts;
    // Ticks when last frame was received
    uint32_t last_ticks;
    // PTS time base
    double timebase;
    // Seconds per frame
    double spf;
    // Number of frames
    uint32_t total;

    // Fifo
    Dav1dPlayPtrFifo *fifo;

    // Custom SDL2 event types
    uint32_t event_types;

    // User pause state
    uint8_t user_paused;
    // Internal pause state
    uint8_t paused;
    // Start of internal pause state
    uint32_t pause_start;
    // Duration of internal pause state
    uint32_t pause_time;

    // Seek accumulator
    int seek;

    // Indicates if termination of the decoder thread was requested
    uint8_t dec_should_terminate;
} Dav1dPlayRenderContext;

static void dp_settings_print_usage(const char *const app,
                                    const char *const reason, ...)
{
    if (reason) {
        va_list args;

        va_start(args, reason);
        vfprintf(stderr, reason, args);
        va_end(args);
        fprintf(stderr, "\n\n");
    }
    fprintf(stderr, "Usage: %s [options]\n\n", app);
    fprintf(stderr, "Supported options:\n"
            " --input/-i  $file:    input file\n"
            " --untimed/-u:         ignore PTS, render as fast as possible\n"
            " --threads $num:       number of threads (default: 0)\n"
            " --framedelay $num:    maximum frame delay, capped at $threads (default: 0);\n"
            "                       set to 1 for low-latency decoding\n"
            " --highquality:        enable high quality rendering\n"
            " --zerocopy/-z:        enable zero copy upload path\n"
            " --gpugrain/-g:        enable GPU grain synthesis\n"
            " --fullscreen/-f:      enable full screen mode\n"
            " --version/-v:         print version and exit\n"
            " --renderer/-r:        select renderer backend (default: auto)\n");
    exit(1);
}

static unsigned parse_unsigned(const char *const optarg, const int option,
                               const char *const app)
{
    char *end;
    const unsigned res = (unsigned) strtoul(optarg, &end, 0);
    if (*end || end == optarg)
        dp_settings_print_usage(app, "Invalid argument \"%s\" for option %s; should be an integer",
          optarg, option);
    return res;
}

static void dp_rd_ctx_parse_args(Dav1dPlayRenderContext *rd_ctx,
                                 const int argc, char *const *const argv)
{
    int o;
    Dav1dPlaySettings *settings = &rd_ctx->settings;
    Dav1dSettings *lib_settings = &rd_ctx->lib_settings;

    // Short options
    static const char short_opts[] = "i:vuzgfr:";

    enum {
        ARG_THREADS = 256,
        ARG_FRAME_DELAY,
        ARG_HIGH_QUALITY,
    };

    // Long options
    static const struct option long_opts[] = {
        { "input",          1, NULL, 'i' },
        { "version",        0, NULL, 'v' },
        { "untimed",        0, NULL, 'u' },
        { "threads",        1, NULL, ARG_THREADS },
        { "framedelay",     1, NULL, ARG_FRAME_DELAY },
        { "highquality",    0, NULL, ARG_HIGH_QUALITY },
        { "zerocopy",       0, NULL, 'z' },
        { "gpugrain",       0, NULL, 'g' },
        { "fullscreen",     0, NULL, 'f'},
        { "renderer",       0, NULL, 'r'},
        { NULL,             0, NULL, 0 },
    };

    while ((o = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
        switch (o) {
            case 'i':
                settings->inputfile = optarg;
                break;
            case 'v':
                fprintf(stderr, "%s\n", dav1d_version());
                exit(0);
            case 'u':
                settings->untimed = true;
                break;
            case ARG_HIGH_QUALITY:
                settings->highquality = true;
                break;
            case 'z':
                settings->zerocopy = true;
                break;
            case 'g':
                settings->gpugrain = true;
                break;
            case 'f':
                settings->fullscreen = true;
                break;
            case 'r':
                settings->renderer_name = optarg;
                break;
            case ARG_THREADS:
                lib_settings->n_threads =
                    parse_unsigned(optarg, ARG_THREADS, argv[0]);
                break;
            case ARG_FRAME_DELAY:
                lib_settings->max_frame_delay =
                    parse_unsigned(optarg, ARG_FRAME_DELAY, argv[0]);
                break;
            default:
                dp_settings_print_usage(argv[0], NULL);
        }
    }

    if (optind < argc)
        dp_settings_print_usage(argv[0],
            "Extra/unused arguments found, e.g. '%s'\n", argv[optind]);
    if (!settings->inputfile)
        dp_settings_print_usage(argv[0], "Input file (-i/--input) is required");
    if (settings->renderer_name && strcmp(settings->renderer_name, "auto") == 0)
        settings->renderer_name = NULL;
}

/**
 * Destroy a Dav1dPlayRenderContext
 */
static void dp_rd_ctx_destroy(Dav1dPlayRenderContext *rd_ctx)
{
    if (!rd_ctx) return;
    if (renderer_info && renderer_info->destroy_renderer)
        renderer_info->destroy_renderer(rd_ctx->rd_priv);
    if (rd_ctx->fifo) dp_fifo_destroy(rd_ctx->fifo);
    if (rd_ctx->lock) {
        pthread_mutex_destroy(rd_ctx->lock);
        free(rd_ctx->lock);
    }
    free(rd_ctx);
}

/**
 * Create a Dav1dPlayRenderContext
 *
 * \note  The Dav1dPlayRenderContext must be destroyed
 *        again by using dp_rd_ctx_destroy.
 */
static Dav1dPlayRenderContext *dp_rd_ctx_create(int argc, char **argv)
{
    Dav1dPlayRenderContext *rd_ctx;

    // Alloc
    rd_ctx = calloc(1, sizeof(Dav1dPlayRenderContext));
    if (rd_ctx == NULL) {
        return NULL;
    }

    // Parse and validate arguments
    dav1d_default_settings(&rd_ctx->lib_settings);
    memset(&rd_ctx->settings, 0, sizeof(rd_ctx->settings));
    dp_rd_ctx_parse_args(rd_ctx, argc, argv);


    rd_ctx->fifo = dp_fifo_create(5);
    if (rd_ctx->fifo == NULL) {
        fprintf(stderr, "Failed to create FIFO for output pictures!\n");
        goto fail;
    }

    rd_ctx->lock = malloc(sizeof(pthread_mutex_t));
    if (!rd_ctx->lock || pthread_mutex_init(rd_ctx->lock, NULL) != 0) {
        goto fail;
    }

    // Select renderer
    renderer_info = dp_get_renderer(rd_ctx->settings.renderer_name);

    if (renderer_info == NULL) {
        printf("No suitable renderer matching %s found.\n",
            (rd_ctx->settings.renderer_name) ? rd_ctx->settings.renderer_name : "auto");
    } else {
        printf("Using %s renderer\n", renderer_info->name);
    }

    rd_ctx->rd_priv = (renderer_info) ? renderer_info->create_renderer(&rd_ctx->settings) : NULL;
    if (rd_ctx->rd_priv == NULL) {
        goto fail;
    }

    return rd_ctx;

fail:
    dp_rd_ctx_destroy(rd_ctx);
    return NULL;
}


/**
 * Update the decoder context with a new dav1d picture
 *
 * Once the decoder decoded a new picture, this call can be used
 * to update the internal texture of the render context with the
 * new picture.
 */
static void dp_rd_ctx_update_with_dav1d_picture(Dav1dPlayRenderContext *rd_ctx,
                                                Dav1dPicture *dav1d_pic)
{
    rd_ctx->current_ts = dav1d_pic->m.timestamp;
    renderer_info->update_frame(rd_ctx->rd_priv, dav1d_pic, &rd_ctx->settings);
}


/**
 * Query pause state
 */
static int dp_rd_ctx_is_paused(Dav1dPlayRenderContext *rd_ctx)
{
    int ret;
    pthread_mutex_lock(rd_ctx->lock);
    ret = rd_ctx->dec_should_terminate;
    pthread_mutex_unlock(rd_ctx->lock);
    return ret;
}


static int decode_frame(Dav1dPicture **p, Dav1dContext *c,
                        Dav1dData *data, DemuxerContext *in_ctx);
static inline void destroy_pic(void *a);


/**
 * Render the currently available texture
 *
 * Renders the currently available texture, if any.
 */
static void dp_rd_ctx_render(Dav1dPlayRenderContext *rd_ctx)
{
    pthread_mutex_lock(rd_ctx->lock);
    // Calculate time since last frame was received
    uint32_t ticks_now = to_ms_since_boot(get_absolute_time());;
    uint32_t ticks_diff = (rd_ctx->last_ticks != 0) ? ticks_now - rd_ctx->last_ticks : 0;

    // Calculate when to display the frame
    int64_t ts_diff = rd_ctx->current_ts - rd_ctx->last_ts;
    int32_t pts_diff = (ts_diff * rd_ctx->timebase) * 1000.0 + .5;
    int32_t wait_time = pts_diff - ticks_diff;

    // In untimed mode, simply don't wait
    if (rd_ctx->settings.untimed)
        wait_time = 0;

    // This way of timing the playback is not accurate, as there is no guarantee
    // that SDL_Delay will wait for exactly the requested amount of time so in a
    // accurate player this would need to be done in a better way.
    if (wait_time > 0) {
        vTaskDelay(pdMS_TO_TICKS(wait_time));
    } else if (wait_time < -10 && !rd_ctx->paused) {
        fprintf(stderr, "Frame displayed %f seconds too late\n", wait_time / 1000.0);
    }

    renderer_info->render(rd_ctx->rd_priv, &rd_ctx->settings);

    rd_ctx->last_ts = rd_ctx->current_ts;
    rd_ctx->last_ticks = to_ms_since_boot(get_absolute_time());;

    pthread_mutex_unlock(rd_ctx->lock);
}

static int decode_frame(Dav1dPicture **p, Dav1dContext *c,
                        Dav1dData *data, DemuxerContext *in_ctx)
{
    int res;
    // Send data packets we got from the demuxer to dav1d
    if ((res = dav1d_send_data(c, data)) < 0) {
        // On EAGAIN, dav1d can not consume more data and
        // dav1d_get_picture needs to be called first, which
        // will happen below, so just keep going in that case
        // and do not error out.
        if (res != DAV1D_ERR(EAGAIN)) {
            dav1d_data_unref(data);
            goto err;
        }
    }
    *p = calloc(1, sizeof(**p));
    // Try to get a decoded frame
    if ((res = dav1d_get_picture(c, *p)) < 0) {
        // In all error cases, even EAGAIN, p needs to be freed as
        // it is never added to the queue and would leak.
        free(*p);
        *p = NULL;
        // On EAGAIN, it means dav1d has not enough data to decode
        // therefore this is not a decoding error but just means
        // we need to feed it more data, which happens in the next
        // run of the decoder loop.
        if (res != DAV1D_ERR(EAGAIN))
            goto err;
    }
    return data->sz == 0 ? input_read(in_ctx, data) : 0;
err:
    fprintf(stderr, "Error decoding frame: %s\n",
            strerror(-res));
    return res;
}

static inline void destroy_pic(void *a)
{
    Dav1dPicture *p = (Dav1dPicture *)a;
    dav1d_picture_unref(p);
    free(p);
}

/* Decoder thread "main" function */
static int decoder_thread_main(void *cookie)
{
    Dav1dPlayRenderContext *rd_ctx = cookie;

    Dav1dPicture *p;
    Dav1dContext *c = NULL;
    Dav1dData data;
    DemuxerContext *in_ctx = NULL;
    int res = 0;
    unsigned total, timebase[2], fps[2];

    Dav1dPlaySettings settings = rd_ctx->settings;

    if ((res = input_open(&in_ctx, "ivf",
                          settings.inputfile,
                          fps, &total, timebase)) < 0)
    {
        fprintf(stderr, "Failed to open demuxer\n");
        res = 1;
        goto cleanup;
    }

    rd_ctx->timebase = (double)timebase[1] / timebase[0];
    rd_ctx->spf = (double)fps[1] / fps[0];
    rd_ctx->total = total;

    if ((res = dav1d_open(&c, &rd_ctx->lib_settings))) {
        fprintf(stderr, "Failed opening dav1d decoder\n");
        res = 1;
        goto cleanup;
    }

    if ((res = input_read(in_ctx, &data)) < 0) {
        fprintf(stderr, "Failed demuxing input\n");
        res = 1;
        goto cleanup;
    }

    // Decoder loop
    while (1) {
        if (p) {
            // Queue frame
            pthread_mutex_lock(rd_ctx->lock);
            int seek = rd_ctx->seek;
            pthread_mutex_unlock(rd_ctx->lock);
            if (!seek) {
                dp_fifo_push(rd_ctx->fifo, p);
            }
        }
    }

    // Release remaining data
    if (data.sz > 0)
        dav1d_data_unref(&data);
    // Do not drain in case an error occured and caused us to leave the
    // decoding loop early.
    if (res < 0)
        goto cleanup;

    // Drain decoder
    // When there is no more data to feed to the decoder, for example
    // because the file ended, we still need to request pictures, as
    // even though we do not have more data, there can be frames decoded
    // from data we sent before. So we need to call dav1d_get_picture until
    // we get an EAGAIN error.
    do {
        if (dp_rd_ctx_should_terminate(rd_ctx))
            break;
        p = calloc(1, sizeof(*p));
        res = dav1d_get_picture(c, p);
        if (res < 0) {
            free(p);
            if (res != DAV1D_ERR(EAGAIN)) {
                fprintf(stderr, "Error decoding frame: %s\n",
                        strerror(-res));
                break;
            }
        } else {
            // Queue frame
            dp_fifo_push(rd_ctx->fifo, p);
            uint32_t type = rd_ctx->event_types + DAV1D_EVENT_NEW_FRAME;
            dp_rd_ctx_post_event(rd_ctx, type);
        }
    } while (res != DAV1D_ERR(EAGAIN));

cleanup:
    // Signal main thread that decoding is complete
    dp_fifo_push(rd_ctx->fifo, NULL);

    if (in_ctx)
        input_close(in_ctx);
    if (c)
        dav1d_close(&c);

    return (res != DAV1D_ERR(EAGAIN) && res < 0);
}

// used to be main()
int dav1dplay_main(int argc, char **argv)
{
    stdio_init_all();

    TaskHandle_t *decoder_thread;

    // Create render context
    Dav1dPlayRenderContext *rd_ctx = dp_rd_ctx_create(argc, argv);
    if (rd_ctx == NULL) {
        printf("Failed creating render context\n");
        return 5;
    }

    if (rd_ctx->settings.zerocopy) {
        if (renderer_info->alloc_pic) {
            rd_ctx->lib_settings.allocator = (Dav1dPicAllocator) {
                .cookie = rd_ctx->rd_priv,
                .alloc_picture_callback = renderer_info->alloc_pic,
                .release_picture_callback = renderer_info->release_pic,
            };
        } else {
            printf(stderr, "--zerocopy unsupported by selected renderer\n");
        }
    }

    if (rd_ctx->settings.gpugrain) {
        if (renderer_info->supports_gpu_grain) {
            rd_ctx->lib_settings.apply_grain = 0;
        } else {
            printf(stderr, "--gpugrain unsupported by selected renderer\n");
        }
    }

    // Start decoder thread
    if (pthread_create(&decoder_thread, NULL, decoder_thread_main, rd_ctx) != 0) {
        fprintf(stderr, "Failed to create decoder thread\n");
        return 6;
    }


    // Main loop - Data will just be sent to a file. No events.
    uint32_t start_time = 0, n_out = 0;
    while (1) {
        Dav1dPicture *p = dp_fifo_shift(rd_ctx->fifo);

        // Null pointer sent from decoder thread means stream is done
        if (p == NULL) {
            break;
        }

        if (start_time == 0) {
            start_time = to_ms_since_boot(get_absolute_time());;
        }

        dp_rd_ctx_update_with_dav1d_picture(rd_ctx, p);
        dp_rd_ctx_render(rd_ctx);
        n_out++;

        destroy_pic(p);
    }

    // Print stats
    uint32_t time_ms = to_ms_since_boot(get_absolute_time()); - start_time - rd_ctx->pause_time;
    printf("Decoded %u frames in %d seconds, avg %.02f fps\n",
           n_out, time_ms / 1000, n_out/ (time_ms / 1000.0));

    int decoder_ret = 0;
    pthread_join(decoder_thread, &decoder_ret);

    dp_rd_ctx_destroy(rd_ctx);
    return decoder_ret;
}
