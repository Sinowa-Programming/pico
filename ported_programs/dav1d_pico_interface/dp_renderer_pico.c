#include "dp_renderer.h"
#include <stdio.h>

// 1. Initialization
static void* pico_create(const Dav1dPlaySettings *settings) {
    // dav1dplay.c checks if the returned "cookie" is NULL to detect failure.
    // We return a dummy non-null value so it knows we "initialized" successfully.
    return (void*)1;
}

// 2. Cleanup
static void pico_destroy(void *cookie) {
    // We didn't allocate any memory, so there's nothing to clean up!
}

// 3. Frame Reception (YOUR FILE SAVING HOOK)
static int pico_update_frame(void *cookie, Dav1dPicture *dav1d_pic, const Dav1dPlaySettings *settings) {
    if (!dav1d_pic) return 0;

    // TODO: This is where you grab the decoded pixel data!
    // For example, dav1d_pic->data[0] is usually your Y (Luma) plane.
    // You can write your file saving logic right here.
    
    // printf("Received a frame to save! Width: %d, Height: %d\n", dav1d_pic->p.w, dav1d_pic->p.h);

    return 0; // 0 indicates success
}

// 4. Screen Rendering (Ignored)
static void pico_render(void *cookie, const Dav1dPlaySettings *settings) {
    // Since you aren't outputting to a screen, this does absolutely nothing.
}

// 5. The Struct the Linker is Looking For
const Dav1dPlayRenderInfo rdr_pico = {
    .name = "pico",
    .create_renderer = pico_create,
    .destroy_renderer = pico_destroy,
    .render = pico_render,
    .update_frame = pico_update_frame,
    .alloc_pic = NULL,            // Not using zero-copy GPU memory
    .release_pic = NULL,          // Not using zero-copy GPU memory
    .supports_gpu_grain = 0       // No GPU grain synthesis
};