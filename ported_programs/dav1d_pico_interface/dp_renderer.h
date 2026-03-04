/*
 * Copyright © 2020, VideoLAN and dav1d authors
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

#ifndef DP_RENDERER_H
#define DP_RENDERER_H

#include <inttypes.h>
#include <string.h>

#include "dav1d/dav1d.h"

/**
 * Settings structure
 * Hold all settings available for the player,
 * this is usually filled by parsing arguments
 * from the console.
 */
typedef struct {
    const char *inputfile;
    const char *renderer_name;
    int highquality;
    int untimed;
    int zerocopy;
    int gpugrain;
    int fullscreen;
} Dav1dPlaySettings;

#define WINDOW_WIDTH  910
#define WINDOW_HEIGHT 512

enum {
    DAV1D_EVENT_NEW_FRAME,
    DAV1D_EVENT_SEEK_FRAME,
    DAV1D_EVENT_DEC_QUIT
};

/**
 * Renderer info
 */
typedef struct rdr_info
{
    // Renderer name
    const char *name;
    // Cookie passed to the renderer implementation callbacks
    void *cookie;
    // Callback to create the renderer
    void* (*create_renderer)(const Dav1dPlaySettings *settings);
    // Callback to destroy the renderer
    void (*destroy_renderer)(void *cookie);
    // Callback to the render function that renders a prevously sent frame
    void (*render)(void *cookie, const Dav1dPlaySettings *settings);
    // Callback to the send frame function, _may_ also unref dav1d_pic!
    int (*update_frame)(void *cookie, Dav1dPicture *dav1d_pic,
                        const Dav1dPlaySettings *settings);
    // Callback for alloc/release pictures (optional)
    int (*alloc_pic)(Dav1dPicture *pic, void *cookie);
    void (*release_pic)(Dav1dPicture *pic, void *cookie);
    // Whether or not this renderer can apply on-GPU film grain synthesis
    int supports_gpu_grain;
} Dav1dPlayRenderInfo;

extern const Dav1dPlayRenderInfo rdr_pico;

// Available renderes ordered by priority
static const Dav1dPlayRenderInfo* const dp_renderers[] = {
    &rdr_pico,
};

static inline const Dav1dPlayRenderInfo *dp_get_renderer(const char *name)
{
    for (size_t i = 0; i < (sizeof(dp_renderers)/sizeof(*dp_renderers)); ++i)
    {
        if (dp_renderers[i]->name == NULL)
            continue;

        if (name == NULL || strcmp(name, dp_renderers[i]->name) == 0) {
            return dp_renderers[i];
        }
    }
    return NULL;
}

#endif // DP_RENDERER_H