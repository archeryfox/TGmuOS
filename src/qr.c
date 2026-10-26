#include "qr.h"

#include <stddef.h>
#include <string.h>

#define QR_DATA_CODEWORDS 108
#define QR_ECC_CODEWORDS 26
#define QR_TOTAL_CODEWORDS (QR_DATA_CODEWORDS + QR_ECC_CODEWORDS)

typedef struct {
    unsigned char bits[QR_TOTAL_CODEWORDS * 8];
    size_t length;
} BitBuffer;

static unsigned char matrix[QR_SIZE][QR_SIZE];
static unsigned char function_modules[QR_SIZE][QR_SIZE];

static void append_bits(BitBuffer* buffer, unsigned value, int count) {
    for (int bit = count - 1; bit >= 0; --bit) {
        if (buffer->length < sizeof(buffer->bits)) {
            buffer->bits[buffer->length++] = (unsigned char)((value >> bit) & 1u);
        }
    }
}

static unsigned char gf_multiply(unsigned char x, unsigned char y) {
    unsigned char result = 0;
    for (int i = 0; i < 8; ++i) {
        if (y & 1u) result ^= x;
        unsigned char high = (unsigned char)(x & 0x80u);
        x <<= 1;
        if (high) x ^= 0x1du;
        y >>= 1;
    }
    return result;
}

static void reed_solomon_remainder(const unsigned char* data, size_t length,
                                   unsigned char result[QR_ECC_CODEWORDS]) {
    unsigned char divisor[QR_ECC_CODEWORDS] = {0};
    divisor[QR_ECC_CODEWORDS - 1] = 1;
    unsigned char root = 1;
    for (int i = 0; i < QR_ECC_CODEWORDS; ++i) {
        for (int j = 0; j < QR_ECC_CODEWORDS; ++j) {
            divisor[j] = gf_multiply(divisor[j], root);
            if (j + 1 < QR_ECC_CODEWORDS) divisor[j] ^= divisor[j + 1];
        }
        root = gf_multiply(root, 2);
    }

    memset(result, 0, QR_ECC_CODEWORDS);
    for (size_t i = 0; i < length; ++i) {
        unsigned char factor = (unsigned char)(data[i] ^ result[0]);
        memmove(result, result + 1, QR_ECC_CODEWORDS - 1);
        result[QR_ECC_CODEWORDS - 1] = 0;
        for (int j = 0; j < QR_ECC_CODEWORDS; ++j) {
            result[j] ^= gf_multiply(divisor[j], factor);
        }
    }
}

static void set_function_module(int x, int y, int dark) {
    if (x < 0 || x >= QR_SIZE || y < 0 || y >= QR_SIZE) return;
    matrix[y][x] = (unsigned char)(dark != 0);
    function_modules[y][x] = 1;
}

static void draw_finder(int center_x, int center_y) {
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            int distance = dx < 0 ? -dx : dx;
            int other = dy < 0 ? -dy : dy;
            if (other > distance) distance = other;
            set_function_module(center_x + dx, center_y + dy,
                                distance != 2 && distance <= 3);
        }
    }
}

static void draw_alignment(int center_x, int center_y) {
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            int x = dx < 0 ? -dx : dx;
            int y = dy < 0 ? -dy : dy;
            set_function_module(center_x + dx, center_y + dy,
                                (x > y ? x : y) != 1);
        }
    }
}

static unsigned format_bits(void) {
    unsigned value = 8; /* Error correction level L, mask pattern 0. */
    unsigned remainder = value;
    for (int i = 0; i < 10; ++i) {
        remainder = (remainder << 1) ^ (((remainder >> 9) & 1u) * 0x537u);
    }
    return ((value << 10) | remainder) ^ 0x5412u;
}

static void draw_format(void) {
    unsigned bits = format_bits();
    for (int i = 0; i <= 5; ++i) set_function_module(8, i, (bits >> i) & 1u);
    set_function_module(8, 7, (bits >> 6) & 1u);
    set_function_module(8, 8, (bits >> 7) & 1u);
    set_function_module(7, 8, (bits >> 8) & 1u);
    for (int i = 9; i < 15; ++i) {
        set_function_module(14 - i, 8, (bits >> i) & 1u);
    }

    for (int i = 0; i < 8; ++i) {
        set_function_module(QR_SIZE - 1 - i, 8, (bits >> i) & 1u);
    }
    for (int i = 8; i < 15; ++i) {
        set_function_module(8, QR_SIZE - 15 + i, (bits >> i) & 1u);
    }
    set_function_module(8, QR_SIZE - 8, 1);
}

static void draw_function_patterns(void) {
    memset(matrix, 0, sizeof(matrix));
    memset(function_modules, 0, sizeof(function_modules));
    draw_finder(3, 3);
    draw_finder(QR_SIZE - 4, 3);
    draw_finder(3, QR_SIZE - 4);

    for (int i = 8; i < QR_SIZE - 8; ++i) {
        set_function_module(6, i, (i & 1) == 0);
        set_function_module(i, 6, (i & 1) == 0);
    }
    draw_alignment(30, 30);
    draw_format();
}

static void draw_codewords(const unsigned char* codewords) {
    size_t bit_index = 0;
    int upward = 1;
    for (int right = QR_SIZE - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int offset = 0; offset < QR_SIZE; ++offset) {
            int y = upward ? QR_SIZE - 1 - offset : offset;
            for (int side = 0; side < 2; ++side) {
                int x = right - side;
                if (function_modules[y][x]) continue;
                int dark = 0;
                if (bit_index < QR_TOTAL_CODEWORDS * 8) {
                    dark = (codewords[bit_index >> 3] >>
                            (7 - (bit_index & 7))) & 1u;
                    ++bit_index;
                }
                if ((x + y) % 2 == 0) dark = !dark;
                matrix[y][x] = (unsigned char)dark;
            }
        }
        upward = !upward;
    }
}

int qr_encode(const char* text, unsigned char output[QR_SIZE][QR_SIZE]) {
    if (!text || !output) return 0;
    size_t length = strlen(text);
    if (length == 0 || length > 106) return 0;

    unsigned char data[QR_DATA_CODEWORDS] = {0};
    BitBuffer buffer = {{0}, 0};
    append_bits(&buffer, 0x4, 4);
    append_bits(&buffer, (unsigned)length, 8);
    for (size_t i = 0; i < length; ++i) {
        append_bits(&buffer, (unsigned char)text[i], 8);
    }
    size_t capacity = QR_DATA_CODEWORDS * 8;
    size_t terminator = capacity - buffer.length;
    if (terminator > 4) terminator = 4;
    append_bits(&buffer, 0, (int)terminator);
    while (buffer.length % 8 != 0) append_bits(&buffer, 0, 1);

    size_t byte_count = buffer.length / 8;
    for (size_t i = 0; i < byte_count; ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            data[i] |= buffer.bits[i * 8 + (size_t)bit] << (7 - bit);
        }
    }
    for (size_t i = byte_count; i < QR_DATA_CODEWORDS; ++i) {
        data[i] = ((i - byte_count) & 1u) ? 0x11 : 0xec;
    }

    unsigned char ecc[QR_ECC_CODEWORDS];
    unsigned char codewords[QR_TOTAL_CODEWORDS];
    reed_solomon_remainder(data, sizeof(data), ecc);
    memcpy(codewords, data, sizeof(data));
    memcpy(codewords + sizeof(data), ecc, sizeof(ecc));

    draw_function_patterns();
    draw_codewords(codewords);
    memcpy(output, matrix, sizeof(matrix));
    return 1;
}

int qr_self_test(void) {
    static const char payload[] =
        "tg://login?token=0123456789abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    unsigned char output[QR_SIZE][QR_SIZE];
    if (!qr_encode(payload, output)) return 0;

    unsigned hash = 2166136261u;
    for (int y = 0; y < QR_SIZE; ++y) {
        for (int x = 0; x < QR_SIZE; ++x) {
            hash = (hash ^ output[y][x]) * 16777619u;
        }
    }
    if (hash != 0xcd493565u) return 0;

    char maximum_payload[108];
    memset(maximum_payload, 'a', 106);
    maximum_payload[106] = '\0';
    if (!qr_encode(maximum_payload, output)) return 0;
    maximum_payload[106] = 'a';
    maximum_payload[107] = '\0';
    return !qr_encode(maximum_payload, output);
}
