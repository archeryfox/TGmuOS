#define _GNU_SOURCE

#include "renderer.h"

#include <SDL2/SDL.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define LOGICAL_WIDTH 640
#define LOGICAL_HEIGHT 480

static int scale_channel(unsigned value, unsigned length) {
    if (length == 0) return 0;
    unsigned maximum = (1u << length) - 1u;
    return (int)((value * maximum + 127u) / 255u);
}

int renderer_init(AppRenderer* renderer, int headless) {
    memset(renderer, 0, sizeof(*renderer));
    renderer->fd = -1;
    renderer->headless = headless;

    if (headless) {
        renderer->width = LOGICAL_WIDTH;
        renderer->height = LOGICAL_HEIGHT;
    } else {
        renderer->fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
        if (renderer->fd < 0) {
            perror("open /dev/fb0");
            return 0;
        }
        if (ioctl(renderer->fd, FBIOGET_FSCREENINFO, &renderer->fixed) < 0 ||
            ioctl(renderer->fd, FBIOGET_VSCREENINFO, &renderer->variable) < 0) {
            perror("read framebuffer information");
            renderer_shutdown(renderer);
            return 0;
        }
        if (renderer->variable.bits_per_pixel != 16 &&
            renderer->variable.bits_per_pixel != 24 &&
            renderer->variable.bits_per_pixel != 32) {
            fprintf(stderr, "Unsupported framebuffer depth: %u bits\n",
                    renderer->variable.bits_per_pixel);
            renderer_shutdown(renderer);
            return 0;
        }

        renderer->width = (int)renderer->variable.xres;
        renderer->height = (int)renderer->variable.yres;
        renderer->map_length = renderer->fixed.smem_len;
        renderer->framebuffer = mmap(NULL, renderer->map_length,
                                     PROT_READ | PROT_WRITE, MAP_SHARED,
                                     renderer->fd, 0);
        if (renderer->framebuffer == MAP_FAILED) {
            renderer->framebuffer = NULL;
            perror("mmap /dev/fb0");
            renderer_shutdown(renderer);
            return 0;
        }
        fprintf(stderr, "Framebuffer: %dx%d, %u bpp, stride %u\n",
                renderer->width, renderer->height,
                renderer->variable.bits_per_pixel, renderer->fixed.line_length);
    }

    renderer->pixels = calloc((size_t)renderer->width * renderer->height,
                              sizeof(*renderer->pixels));
    if (!renderer->pixels) {
        perror("allocate framebuffer canvas");
        renderer_shutdown(renderer);
        return 0;
    }
    return 1;
}

void renderer_shutdown(AppRenderer* renderer) {
    free(renderer->pixels);
    renderer->pixels = NULL;
    if (renderer->framebuffer) {
        munmap(renderer->framebuffer, renderer->map_length);
        renderer->framebuffer = NULL;
    }
    if (renderer->fd >= 0) {
        close(renderer->fd);
        renderer->fd = -1;
    }
}

void renderer_clear(AppRenderer* renderer, uint32_t color) {
    size_t count = (size_t)renderer->width * renderer->height;
    for (size_t i = 0; i < count; ++i) renderer->pixels[i] = color;
}

void renderer_present(AppRenderer* renderer) {
    if (renderer->headless) return;

    const unsigned bytes_per_pixel = renderer->variable.bits_per_pixel / 8;
    const size_t page_offset =
        (size_t)renderer->variable.yoffset * renderer->fixed.line_length +
        (size_t)renderer->variable.xoffset * bytes_per_pixel;
    const size_t required = page_offset +
        (size_t)(renderer->height - 1) * renderer->fixed.line_length +
        (size_t)renderer->width * bytes_per_pixel;
    if (required > renderer->map_length) {
        fprintf(stderr, "Framebuffer page is outside mapped memory\n");
        return;
    }

    for (int y = 0; y < renderer->height; ++y) {
        uint8_t* row = renderer->framebuffer + page_offset +
                       (size_t)y * renderer->fixed.line_length;
        for (int x = 0; x < renderer->width; ++x) {
            uint32_t color = renderer->pixels[(size_t)y * renderer->width + x];
            uint32_t pixel =
                ((uint32_t)scale_channel((color >> 16) & 0xff, renderer->variable.red.length)
                 << renderer->variable.red.offset) |
                ((uint32_t)scale_channel((color >> 8) & 0xff, renderer->variable.green.length)
                 << renderer->variable.green.offset) |
                ((uint32_t)scale_channel(color & 0xff, renderer->variable.blue.length)
                 << renderer->variable.blue.offset);
            if (renderer->variable.transp.length != 0) {
                uint32_t alpha = (1u << renderer->variable.transp.length) - 1u;
                pixel |= alpha << renderer->variable.transp.offset;
            }
            memcpy(row + (size_t)x * bytes_per_pixel, &pixel, bytes_per_pixel);
        }
    }
}
