#include "ui.h"

#include <SDL2/SDL_ttf.h>

#include <math.h>
#include <stdio.h>

static const int FONT_SIZES[] = {14, 18, 24};
static const char* FONT_PATHS[] = {
    "res/fonts/AdwaitaSans-Regular.ttf",
    "assets/fonts/AdwaitaSans-Regular.ttf",
    "/opt/muos/share/font/muterm.ttf"
};
static TTF_Font* fonts[sizeof(FONT_SIZES) / sizeof(FONT_SIZES[0])];

static TTF_Font* font_for_size(int size) {
    if (size <= FONT_SIZES[0]) return fonts[0];
    if (size <= FONT_SIZES[1]) return fonts[1];
    return fonts[2];
}

int ui_init(void) {
    const size_t font_count = sizeof(FONT_SIZES) / sizeof(FONT_SIZES[0]);
    const size_t path_count = sizeof(FONT_PATHS) / sizeof(FONT_PATHS[0]);
    const char* selected_path = NULL;

    for (size_t path = 0; path < path_count && !selected_path; ++path) {
        TTF_Font* probe = TTF_OpenFont(FONT_PATHS[path], FONT_SIZES[0]);
        if (probe) {
            TTF_CloseFont(probe);
            selected_path = FONT_PATHS[path];
        }
    }

    if (!selected_path) {
        fprintf(stderr, "No usable font found (tried res/, assets/, and muOS system font)\n");
        return 0;
    }

    for (size_t i = 0; i < font_count; ++i) {
        fonts[i] = TTF_OpenFont(selected_path, FONT_SIZES[i]);
        if (!fonts[i]) {
            fprintf(stderr, "TTF_OpenFont(%s, %d) failed: %s\n",
                    selected_path, FONT_SIZES[i], TTF_GetError());
            ui_shutdown();
            return 0;
        }
    }

    fprintf(stderr, "Using font: %s\n", selected_path);
    return 1;
}

void ui_shutdown(void) {
    const size_t font_count = sizeof(FONT_SIZES) / sizeof(FONT_SIZES[0]);
    for (size_t i = 0; i < font_count; ++i) {
        if (fonts[i]) {
            TTF_CloseFont(fonts[i]);
            fonts[i] = NULL;
        }
    }
}

static int scale_x(const AppRenderer* renderer, int x) {
    return x * renderer->width / 640;
}

static int scale_y(const AppRenderer* renderer, int y) {
    return y * renderer->height / 480;
}

void ui_fill(AppRenderer* renderer, SDL_Rect rect, SDL_Color color) {
    int left = scale_x(renderer, rect.x);
    int top = scale_y(renderer, rect.y);
    int right = scale_x(renderer, rect.x + rect.w);
    int bottom = scale_y(renderer, rect.y + rect.h);
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > renderer->width) right = renderer->width;
    if (bottom > renderer->height) bottom = renderer->height;
    if (left >= right || top >= bottom) return;

    uint32_t pixel = (uint32_t)color.a << 24 |
                     (uint32_t)color.r << 16 |
                     (uint32_t)color.g << 8 |
                     color.b;
    for (int y = top; y < bottom; ++y) {
        uint32_t* row = renderer->pixels + (size_t)y * renderer->width;
        for (int x = left; x < right; ++x) row[x] = pixel;
    }
}

void ui_round_rect(AppRenderer* renderer, SDL_Rect rect, int radius, SDL_Color color) {
    if (radius <= 0) {
        ui_fill(renderer, rect, color);
        return;
    }
    if (radius * 2 > rect.w) radius = rect.w / 2;
    if (radius * 2 > rect.h) radius = rect.h / 2;
    for (int y = 0; y < radius; ++y) {
        /* Distance from the row to the straight part; the top row (t = radius)
         * touches the corner circle at x = radius, the last corner row
         * (t = 1) is nearly full width. */
        int t = radius - y;
        int inset = radius - (int)sqrt((double)(radius * radius - t * t));
        ui_fill(renderer, (SDL_Rect){rect.x + inset, rect.y + y, rect.w - inset * 2, 1}, color);
        ui_fill(renderer, (SDL_Rect){rect.x + inset, rect.y + rect.h - y - 1,
                                     rect.w - inset * 2, 1}, color);
    }
    ui_fill(renderer, (SDL_Rect){rect.x, rect.y + radius, rect.w, rect.h - radius * 2}, color);
}

void ui_text(AppRenderer* renderer, const char* text, int x, int y, int size, SDL_Color color) {
    TTF_Font* font = font_for_size(size);
    if (!font || !text || !text[0]) return;

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) {
        fprintf(stderr, "TTF_RenderUTF8_Blended failed: %s\n", TTF_GetError());
        return;
    }

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
    if (!converted) {
        fprintf(stderr, "SDL_ConvertSurfaceFormat failed: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }

    int dest_x = scale_x(renderer, x);
    int dest_y = scale_y(renderer, y);
    int dest_width = surface->w * renderer->width / 640;
    int dest_height = surface->h * renderer->height / 480;
    if (dest_width < 1) dest_width = 1;
    if (dest_height < 1) dest_height = 1;
    const uint32_t* source = (const uint32_t*)converted->pixels;

    for (int dy = 0; dy < dest_height; ++dy) {
        int target_y = dest_y + dy;
        if (target_y < 0 || target_y >= renderer->height) continue;
        int source_y = dy * converted->h / dest_height;
        const uint32_t* source_row = (const uint32_t*)((const uint8_t*)source +
                                     (size_t)source_y * converted->pitch);
        uint32_t* target_row = renderer->pixels + (size_t)target_y * renderer->width;
        for (int dx = 0; dx < dest_width; ++dx) {
            int target_x = dest_x + dx;
            if (target_x < 0 || target_x >= renderer->width) continue;
            int source_x = dx * converted->w / dest_width;
            uint32_t foreground = source_row[source_x];
            unsigned alpha = foreground >> 24;
            if (alpha == 0) continue;
            uint32_t background = target_row[target_x];
            unsigned inverse = 255 - alpha;
            unsigned red = (((foreground >> 16) & 0xff) * alpha +
                            ((background >> 16) & 0xff) * inverse + 127) / 255;
            unsigned green = (((foreground >> 8) & 0xff) * alpha +
                              ((background >> 8) & 0xff) * inverse + 127) / 255;
            unsigned blue = ((foreground & 0xff) * alpha +
                             (background & 0xff) * inverse + 127) / 255;
            target_row[target_x] = 0xff000000u | red << 16 | green << 8 | blue;
        }
    }

    SDL_FreeSurface(converted);
    SDL_FreeSurface(surface);
}

int ui_text_width(const char* text, int size) {
    int width = 0;
    int height = 0;
    TTF_Font* font = font_for_size(size);
    if (!font || !text || TTF_SizeUTF8(font, text, &width, &height) < 0) return 0;
    return width;
}
