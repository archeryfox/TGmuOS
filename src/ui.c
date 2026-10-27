#include "ui.h"

#include <SDL2/SDL_ttf.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const int FONT_SIZES[] = {14, 18, 24};
static const char* FONT_PATHS[] = {
    "res/fonts/AdwaitaSans-Regular.ttf",
    "assets/fonts/AdwaitaSans-Regular.ttf",
    "/opt/muos/share/font/muterm.ttf"
};
static const char* EMOJI_FONT_PATHS[] = {
    "res/fonts/NotoEmoji-Regular.ttf",
    "assets/fonts/NotoEmoji-Regular.ttf"
};
static TTF_Font* fonts[sizeof(FONT_SIZES) / sizeof(FONT_SIZES[0])];
static TTF_Font* emoji_fonts[sizeof(FONT_SIZES) / sizeof(FONT_SIZES[0])];

static TTF_Font* font_for_size(int size) {
    if (size <= FONT_SIZES[0]) return fonts[0];
    if (size <= FONT_SIZES[1]) return fonts[1];
    return fonts[2];
}

static TTF_Font* emoji_font_for_size(int size) {
    if (size <= FONT_SIZES[0]) return emoji_fonts[0];
    if (size <= FONT_SIZES[1]) return emoji_fonts[1];
    return emoji_fonts[2];
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

    const size_t emoji_path_count =
        sizeof(EMOJI_FONT_PATHS) / sizeof(EMOJI_FONT_PATHS[0]);
    const char* emoji_path = NULL;
    for (size_t path = 0; path < emoji_path_count && !emoji_path; ++path) {
        TTF_Font* probe = TTF_OpenFont(EMOJI_FONT_PATHS[path], FONT_SIZES[0]);
        if (probe) {
            TTF_CloseFont(probe);
            emoji_path = EMOJI_FONT_PATHS[path];
        }
    }
    if (emoji_path) {
        for (size_t i = 0; i < font_count; ++i) {
            emoji_fonts[i] = TTF_OpenFont(emoji_path, FONT_SIZES[i]);
        }
        fprintf(stderr, "Using emoji font: %s\n", emoji_path);
    } else {
        fprintf(stderr, "No emoji font found; emoji glyphs will be skipped\n");
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
        if (emoji_fonts[i]) {
            TTF_CloseFont(emoji_fonts[i]);
            emoji_fonts[i] = NULL;
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

/* Decode one UTF-8 code point; returns byte length (0 on malformed input). */
static int utf8_next(const char* text, uint32_t* codepoint) {
    const unsigned char* bytes = (const unsigned char*)text;
    if (!bytes[0]) return 0;
    if (bytes[0] < 0x80) {
        *codepoint = bytes[0];
        return 1;
    }
    if ((bytes[0] & 0xe0) == 0xc0 && (bytes[1] & 0xc0) == 0x80) {
        *codepoint = ((uint32_t)(bytes[0] & 0x1f) << 6) | (bytes[1] & 0x3f);
        return 2;
    }
    if ((bytes[0] & 0xf0) == 0xe0 && (bytes[1] & 0xc0) == 0x80 &&
            (bytes[2] & 0xc0) == 0x80) {
        *codepoint = ((uint32_t)(bytes[0] & 0x0f) << 12) |
                     ((uint32_t)(bytes[1] & 0x3f) << 6) | (bytes[2] & 0x3f);
        return 3;
    }
    if ((bytes[0] & 0xf8) == 0xf0 && (bytes[1] & 0xc0) == 0x80 &&
            (bytes[2] & 0xc0) == 0x80 && (bytes[3] & 0xc0) == 0x80) {
        *codepoint = ((uint32_t)(bytes[0] & 0x07) << 18) |
                     ((uint32_t)(bytes[1] & 0x3f) << 12) |
                     ((uint32_t)(bytes[2] & 0x3f) << 6) | (bytes[3] & 0x3f);
        return 4;
    }
    *codepoint = bytes[0];
    return 1;
}

/* Emoji font is used when the primary font lacks the glyph. */
static TTF_Font* run_font_for(TTF_Font* primary, TTF_Font* emoji, uint32_t cp) {
    if (emoji && !TTF_GlyphIsProvided32(primary, cp) &&
            TTF_GlyphIsProvided32(emoji, cp)) {
        return emoji;
    }
    return primary;
}

static void blit_rgba(AppRenderer* renderer, const SDL_Surface* converted,
                      int dest_x, int dest_y, int dest_width, int dest_height) {    const uint32_t* source = (const uint32_t*)converted->pixels;
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
}

static int render_run(AppRenderer* renderer, TTF_Font* font,
                      const char* text, size_t length,
                      int dest_x, int dest_y, SDL_Color color) {
    char buffer[512];
    if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
    memcpy(buffer, text, length);
    buffer[length] = '\0';

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, buffer, color);
    if (!surface) {
        fprintf(stderr, "TTF_RenderUTF8_Blended failed: %s\n", TTF_GetError());
        return 0;
    }
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(surface);
    if (!converted) {
        fprintf(stderr, "SDL_ConvertSurfaceFormat failed: %s\n", SDL_GetError());
        return 0;
    }
    int dest_width = converted->w * renderer->width / 640;
    int dest_height = converted->h * renderer->height / 480;
    if (dest_width < 1) dest_width = 1;
    if (dest_height < 1) dest_height = 1;
    blit_rgba(renderer, converted, dest_x, dest_y, dest_width, dest_height);
    SDL_FreeSurface(converted);
    return dest_width;
}

void ui_text(AppRenderer* renderer, const char* text, int x, int y, int size, SDL_Color color) {
    TTF_Font* font = font_for_size(size);
    TTF_Font* emoji = emoji_font_for_size(size);
    if (!font || !text || !text[0]) return;

    int pen_x = scale_x(renderer, x);
    int dest_y = scale_y(renderer, y);
    const char* run_start = text;
    const char* cursor = text;
    int run_emoji = -1;
    while (*cursor) {
        uint32_t cp = 0;
        int length = utf8_next(cursor, &cp);
        if (length <= 0) break;
        int use_emoji = run_font_for(font, emoji, cp) == emoji && emoji;
        if (cp == 0xfe0f && run_emoji >= 0) {
            /* Variation selector stays with the preceding run. */
            cursor += length;
            continue;
        }
        if (run_emoji < 0) {
            run_emoji = use_emoji;
        } else if (use_emoji != run_emoji) {
            TTF_Font* run_font = run_emoji && emoji ? emoji : font;
            pen_x += render_run(renderer, run_font, run_start,
                                (size_t)(cursor - run_start),
                                pen_x, dest_y, color);
            run_start = cursor;
            run_emoji = use_emoji;
        }
        cursor += length;
    }
    if (cursor > run_start) {
        TTF_Font* run_font = run_emoji && emoji ? emoji : font;
        render_run(renderer, run_font, run_start, (size_t)(cursor - run_start),
                   pen_x, dest_y, color);
    }
}

static int run_text_width(TTF_Font* font, const char* text, size_t length) {
    char buffer[512];
    if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
    memcpy(buffer, text, length);
    buffer[length] = '\0';
    int width = 0;
    int height = 0;
    if (TTF_SizeUTF8(font, buffer, &width, &height) < 0) return 0;
    return width;
}

int ui_text_width(const char* text, int size) {
    TTF_Font* font = font_for_size(size);
    TTF_Font* emoji = emoji_font_for_size(size);
    if (!font || !text || !text[0]) return 0;
    int total = 0;
    const char* run_start = text;
    const char* cursor = text;
    int run_emoji = -1;
    while (*cursor) {
        uint32_t cp = 0;
        int length = utf8_next(cursor, &cp);
        if (length <= 0) break;
        int use_emoji = run_font_for(font, emoji, cp) == emoji && emoji;
        if (cp == 0xfe0f && run_emoji >= 0) {
            cursor += length;
            continue;
        }
        if (run_emoji < 0) {
            run_emoji = use_emoji;
        } else if (use_emoji != run_emoji) {
            TTF_Font* run_font = run_emoji && emoji ? emoji : font;
            total += run_text_width(run_font, run_start,
                                    (size_t)(cursor - run_start));
            run_start = cursor;
            run_emoji = use_emoji;
        }
        cursor += length;
    }
    if (cursor > run_start) {
        TTF_Font* run_font = run_emoji && emoji ? emoji : font;
        total += run_text_width(run_font, run_start,
                                (size_t)(cursor - run_start));
    }
    return total;
}

void ui_blit_surface(AppRenderer* renderer, const SDL_Surface* surface,
                     int x, int y, int w, int h) {
    if (!renderer || !surface || w <= 0 || h <= 0) return;
    int dest_x = scale_x(renderer, x);
    int dest_y = scale_y(renderer, y);
    int dest_width = w * renderer->width / 640;
    int dest_height = h * renderer->height / 480;
    if (dest_width < 1) dest_width = 1;
    if (dest_height < 1) dest_height = 1;
    blit_rgba(renderer, surface, dest_x, dest_y, dest_width, dest_height);
}
