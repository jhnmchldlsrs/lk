/*
 * Copyright (c) 2015 Carlos Pizano-Uribe <cpu@chromium.org>
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

#include <app.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <lk/err.h>

#include <lib/bio.h>
#include <lib/gfx.h>
#include <lib/tga.h>
#include <dev/display.h>
#include <kernel/thread.h>

#include "config.h"

#define LOG_TAG "launchpad "

/* Macro helper for min calculation */
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* ---- Display Helpers ---- */

/**
 * Maps LK display subsystem formats to internal graphics surface formats.
 */
static gfx_format display_format_to_gfx(enum display_format fmt) {
    switch (fmt) {
        case DISPLAY_FORMAT_RGB_565:   return GFX_FORMAT_RGB_565;
        case DISPLAY_FORMAT_RGB_x888:  return GFX_FORMAT_RGB_x888;
        case DISPLAY_FORMAT_ARGB_8888: return GFX_FORMAT_ARGB_8888;
        default:                       return GFX_FORMAT_ARGB_8888;
    }
}

/**
 * Safe single-pixel fetch from an ARGB surface (expects 32-bpp surface).
 */
static inline uint32_t surface_getpixel(const gfx_surface *s, uint32_t x, uint32_t y) {
    if (!s || !s->ptr || x >= s->width || y >= s->height) {
        return 0;
    }
    const uint32_t *pixels = (const uint32_t *)s->ptr;
    return pixels[y * s->stride + x];
}

/**
 * Acquires and wraps the active display framebuffer inside a gfx_surface wrapper.
 * Caller MUST destroy the returned surface with gfx_surface_destroy().
 */
static gfx_surface *get_framebuffer(struct display_framebuffer *out_dfb) {
    if (!out_dfb) {
        return NULL;
    }

    memset(out_dfb, 0, sizeof(*out_dfb));

    if (display_get_framebuffer(out_dfb) == NO_ERROR && out_dfb->image.pixels != NULL) {
        uint32_t stride = (out_dfb->image.stride > 0) ? (uint32_t)out_dfb->image.stride : out_dfb->image.width;
        gfx_format fmt = display_format_to_gfx(out_dfb->format);

        return gfx_create_surface(out_dfb->image.pixels,
                                  out_dfb->image.width,
                                  out_dfb->image.height,
                                  stride,
                                  fmt);
    }

    return NULL;
}

/* ---- Storage I/O ---- */

/**
 * Reads a block device segment into a dynamically allocated buffer.
 */
static uint8_t *bio_read_alloc(const char *dev_name, off_t offset, size_t len, size_t *out_len) {
    if (!dev_name) {
        return NULL;
    }

    bdev_t *dev = bio_open(dev_name);
    if (!dev) {
        printf(LOG_TAG "Error: Failed to open block device '%s'\n", dev_name);
        return NULL;
    }

    uint64_t dev_size = (uint64_t)dev->block_count * dev->block_size;

    if ((uint64_t)offset >= dev_size) {
        printf(LOG_TAG "Error: Offset %lld exceeds device size (%llu)\n",
               (long long)offset, (unsigned long long)dev_size);
        bio_close(dev);
        return NULL;
    }

    /* Auto-detect remaining size if length is unassigned or out-of-bounds */
    if (len == 0 || ((uint64_t)offset + len) > dev_size) {
        len = (size_t)(dev_size - (uint64_t)offset);
    }

    uint8_t *buf = malloc(len);
    if (!buf) {
        printf(LOG_TAG "Error: Allocation failed for %zu bytes\n", len);
        bio_close(dev);
        return NULL;
    }

    ssize_t ret = bio_read(dev, buf, offset, len);
    bio_close(dev);

    if (ret < 0 || (size_t)ret != len) {
        printf(LOG_TAG "Error: bio_read failed (returned %ld, expected %zu)\n", (long)ret, len);
        free(buf);
        return NULL;
    }

    if (out_len) {
        *out_len = len;
    }

    return buf;
}

/* ---- Graphics Rendering ---- */

/**
 * Blits `src` surface onto `dst` surface with full opacity enforcement.
 * Optimized for direct memory copies when scanline formats match.
 */
static void gfx_blit(gfx_surface *dst, const gfx_surface *src, uint32_t destx, uint32_t desty) {
    if (!dst || !src || destx >= dst->width || desty >= dst->height) {
        return;
    }

    uint32_t copy_w = MIN(src->width, dst->width - destx);
    uint32_t copy_h = MIN(src->height, dst->height - desty);

    /* Fast Path: 32-bpp Direct Row Transfer with Alpha Enforcement */
    if ((dst->format == GFX_FORMAT_ARGB_8888 || dst->format == GFX_FORMAT_RGB_x888) &&
        (src->format == GFX_FORMAT_ARGB_8888 || src->format == GFX_FORMAT_RGB_x888)) {

        for (uint32_t y = 0; y < copy_h; y++) {
            const uint32_t *src_row = (const uint32_t *)src->ptr + (y * src->stride);
            uint32_t *dst_row = (uint32_t *)dst->ptr + ((desty + y) * dst->stride) + destx;

            for (uint32_t x = 0; x < copy_w; x++) {
                dst_row[x] = src_row[x] | 0xFF000000;
            }
        }
        return;
    }

    /* Fallback Path: Pixel-by-pixel translation for mismatched pixel depths */
    for (uint32_t y = 0; y < copy_h; y++) {
        uint32_t dy = desty + y;
        for (uint32_t x = 0; x < copy_w; x++) {
            uint32_t dx = destx + x;
            uint32_t pixel = surface_getpixel(src, x, y) | 0xFF000000;
            gfx_putpixel(dst, dx, dy, pixel);
        }
    }
}

/**
 * Decodes a TGA image and renders it centered on the display framebuffer.
 */
static int display_tga_centered(const void *tga_data, size_t tga_len) {
    int status = -1;
    gfx_surface *tga_surface = NULL;
    gfx_surface *fb = NULL;
    struct display_framebuffer dfb;

    if (!tga_data || tga_len == 0) {
        printf(LOG_TAG "Error: Invalid image buffer provided\n");
        return -1;
    }

    tga_surface = tga_decode(tga_data, tga_len, GFX_FORMAT_ARGB_8888);
    if (!tga_surface) {
        printf(LOG_TAG "Error: Failed to decode TGA image\n");
        return -1;
    }

    fb = get_framebuffer(&dfb);
    if (!fb) {
        printf(LOG_TAG "Error: Framebuffer device unavailable\n");
        goto cleanup;
    }

    uint32_t destx = (fb->width > tga_surface->width) ? (fb->width - tga_surface->width) / 2 : 0;
    uint32_t desty = (fb->height > tga_surface->height) ? (fb->height - tga_surface->height) / 2 : 0;

    /* 1. Clear background to black */
    gfx_clear(fb, 0xFF000000);

    /* 2. Blit TGA image centered */
    gfx_blit(fb, tga_surface, destx, desty);

    /* 3. Flush CPU cache lines to RAM */
    gfx_flush(fb);

    /* 4. Trigger hardware display flush if supported */
    if (dfb.flush != NULL) {
        dfb.flush(0, fb->height);
    }

    status = 0;

cleanup:
    if (fb) {
        gfx_surface_destroy(fb);
    }
    if (tga_surface) {
        gfx_surface_destroy(tga_surface);
    }

    return status;
}

/**
 * Draws a progress bar centered near the bottom of the screen.
 * Uses integer math to prevent floating-point overhead in kernel space.
 *
 * @param fb       Active display surface wrapper.
 * @param progress Current progress percentage (0 to 100).
 * @return 0 on success, negative integer on invalid surface.
 */
static int draw_progressbar(gfx_surface *fb, uint32_t progress) {
    if (!fb || fb->width == 0 || fb->height == 0) {
        return -1;
    }

    /* Clamp input to valid percentage range */
    if (progress > 100) {
        progress = 100;
    }

    /* Define bar geometry using integer ratio (25% screen width, 10px height) */
    const uint32_t bar_width = fb->width / 4;
    const uint32_t bar_height = 10;
    const uint32_t bar_x = (fb->width - bar_width) / 2;

    /* Position vertically at 90% screen height without floating-point math */
    const uint32_t bar_y_target = (fb->height * 90) / 100;
    const uint32_t bar_y = (bar_y_target > bar_height) ? (bar_y_target - bar_height) : 0;

    /* Color definitions (ARGB8888) */
    const uint32_t COLOR_TRACK = 0xFF333333; /* Dark Gray background track */
    const uint32_t COLOR_FILL  = 0xFFFFFFFF; /* White active progress fill */

    /* 1. Draw background track (container) */
    gfx_fillrect(fb, bar_x, bar_y, bar_width, bar_height, COLOR_TRACK);

    /* 2. Draw active progress fill */
    uint32_t filled_width = (bar_width * progress) / 100;
    if (filled_width > 0) {
        gfx_fillrect(fb, bar_x, bar_y, filled_width, bar_height, COLOR_FILL);
    }

    return 0;
}

/* ---- Application Entry Point ---- */

static void launchpad_entry(const struct app_descriptor *app, void *args) {
    printf(LOG_TAG "Initializing boot logo...\n");

    size_t logo_size = 0;
    uint8_t *logo_data = bio_read_alloc(BLOCK_DEV_CONFIG, 0, 0, &logo_size);

    if (!logo_data) {
        printf(LOG_TAG "Error: Could not load splash screen from %s\n", BLOCK_DEV_CONFIG);
    } else {
        printf(LOG_TAG "Loaded splash image (%zu bytes)\n", logo_size);

        if (display_tga_centered(logo_data, logo_size) != 0) {
            printf(LOG_TAG "Error: Failed to render splash logo\n");
        }

        /* Acquire framebuffer once for the animation loop */
        struct display_framebuffer dfb;
        gfx_surface *fb = get_framebuffer(&dfb);

        if (fb) {
            /* Smooth boot animation: 0% to 100% in integer steps */
            for (uint32_t progress = 0; progress <= 100; progress++) {
                draw_progressbar(fb, progress);

                /* Flush CPU cache lines and trigger hardware display panel refresh */
                gfx_flush(fb);
                if (dfb.flush != NULL) {
                    dfb.flush(0, fb->height);
                }

                thread_sleep(20); /* 20ms delay per 1% = smooth 2.0 second animation */
            }

            /* Clean up framebuffer surface after animation finishes */
            gfx_surface_destroy(fb);
        } else {
            printf(LOG_TAG "Error: Framebuffer unavailable for progress bar\n");
        }

        free(logo_data);
    }
}

APP_START(launchpad)
    .entry = launchpad_entry,
    .flags = APP_FLAG_CUSTOM_STACK_SIZE,
    .stack_size = 1024 * 1024,
APP_END