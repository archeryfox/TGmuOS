#pragma once

#include <linux/fb.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    int fd;
    int headless;
    int width;
    int height;
    size_t map_length;
    uint8_t* framebuffer;
    struct fb_fix_screeninfo fixed;
    struct fb_var_screeninfo variable;
    uint32_t* pixels;
} AppRenderer;

int renderer_init(AppRenderer* renderer, int headless);
void renderer_shutdown(AppRenderer* renderer);
void renderer_clear(AppRenderer* renderer, uint32_t color);
void renderer_present(AppRenderer* renderer);
