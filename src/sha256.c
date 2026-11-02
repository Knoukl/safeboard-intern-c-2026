#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mangen.h"

typedef struct {
    uint32_t state[8];
    uint64_t total_bytes;
    unsigned char block[64];
    size_t block_len;
} Sha256Context;

static void sha256_init(Sha256Context* context);
static void sha256_update(Sha256Context* context, const unsigned char* data,
                          size_t data_len);
static uint32_t rotate_right(uint32_t value, unsigned int count);
static void sha256_transform(Sha256Context* context);
static void sha256_final(Sha256Context* context, unsigned char digest[32]);
static void digest_to_hex(const unsigned char digest[32], char output_hex[65]);

/* Открывает файл, читает его порциями и передаёт байты алгоритму SHA-256.
 * При успехе записывает 64-символьный хэш в output_hex; возвращает статус.
 */
int sha256_file(const char* file_path, char output_hex[65]) {
    int red_flag = 0;
    FILE* file = fopen(file_path, "rb");
    if (file == NULL) {
        red_flag = 1;
    }
    if (!red_flag) {
        Sha256Context context;
        sha256_init(&context);

        unsigned char buffer[4096];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            sha256_update(&context, buffer, bytes_read);
        }

        if (ferror(file)) {
            red_flag = 1;
        }

        if (fclose(file) != 0) {
            red_flag = 1;
        }

        unsigned char digest[32];
        sha256_final(&context, digest);
        digest_to_hex(digest, output_hex);
    }

    return red_flag;
}

/* Записывает восемь стандартных начальных слов в состояние SHA-256.
 * Также обнуляет длину текущего блока и общее число переданных байт.
 */
static void sha256_init(Sha256Context* context) {
    context->state[0] = 0x6a09e667;
    context->state[1] = 0xbb67ae85;
    context->state[2] = 0x3c6ef372;
    context->state[3] = 0xa54ff53a;
    context->state[4] = 0x510e527f;
    context->state[5] = 0x9b05688c;
    context->state[6] = 0x1f83d9ab;
    context->state[7] = 0x5be0cd19;

    context->block_len = 0;
    context->total_bytes = 0;
}

/* Добавляет очередную порцию байтов в контекст SHA-256.
 * Каждый полный 64-байтовый блок передаёт в sha256_transform().
 */
static void sha256_update(Sha256Context* context, const unsigned char* data,
                          size_t data_len) {
    context->total_bytes += data_len;
    while (data_len > 0) {
        size_t temp = sizeof(context->block) - context->block_len;
        size_t to_copy = data_len < temp ? data_len : temp;

        memcpy(context->block + context->block_len, data, to_copy);

        context->block_len += to_copy;
        data_len -= to_copy;

        data += to_copy;
        if (context->block_len == 64) {
            sha256_transform(context);
            context->block_len = 0;
        }
    }
}

/* Циклически сдвигает 32-битное слово вправо на count позиций.
 * Биты, вышедшие справа, возвращаются в освободившиеся позиции слева.
 */
static uint32_t rotate_right(uint32_t value, unsigned int count) {
    return (value >> count) | (value << (32U - count));
}

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

/* Преобразует 64-байтовый блок в расписание из 64 слов по 32 бита.
 * Первые 16 слов читаются из блока, остальные вычисляются по формулам SHA-256.
 */
static void sha256_prepare_schedule(const unsigned char block[64],
                                    uint32_t words[64]) {
    for (int i = 0; i < 16; i++) {
        words[i] = ((uint32_t)block[4 * i] << 24) |
                   ((uint32_t)block[4 * i + 1] << 16) |
                   ((uint32_t)block[4 * i + 2] << 8) |
                   ((uint32_t)block[4 * i + 3]);
    }

    for (int i = 16; i < 64; i++) {
        uint32_t x = words[i - 15];
        uint32_t y = words[i - 2];

        uint32_t sigma0 = rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3);

        uint32_t sigma1 = rotate_right(y, 17) ^ rotate_right(y, 19) ^ (y >> 10);

        words[i] = words[i - 16] + sigma0 + words[i - 7] + sigma1;
    }
}

typedef struct {
    uint32_t a, b, c, d, e, f, g, h;
} Sha256WorkingState;

/* Обновляет восемь рабочих слов состояния за один раунд SHA-256.
 * Использует очередное слово расписания и соответствующую константу K.
 */
static void sha256_round(Sha256WorkingState* working, uint32_t word,
                         uint32_t constant) {
    uint32_t sum1 = rotate_right(working->e, 6) ^ rotate_right(working->e, 11) ^
                    rotate_right(working->e, 25);
    uint32_t choose = (working->e & working->f) ^ (~working->e & working->g);
    uint32_t temp1 = working->h + sum1 + choose + constant + word;

    uint32_t sum0 = rotate_right(working->a, 2) ^ rotate_right(working->a, 13) ^
                    rotate_right(working->a, 22);
    uint32_t majority = (working->a & working->b) ^ (working->a & working->c) ^
                        (working->b & working->c);
    uint32_t temp2 = sum0 + majority;

    working->h = working->g;
    working->g = working->f;
    working->f = working->e;
    working->e = working->d + temp1;
    working->d = working->c;
    working->c = working->b;
    working->b = working->a;
    working->a = temp1 + temp2;
}

/* Подготавливает расписание для текущего блока и выполняет 64 раунда SHA-256.
 * Затем добавляет рабочие значения раундов к состоянию контекста.
 */
static void sha256_transform(Sha256Context* context) {
    uint32_t data[64];
    sha256_prepare_schedule(context->block, data);

    Sha256WorkingState working = {context->state[0], context->state[1],
                                  context->state[2], context->state[3],
                                  context->state[4], context->state[5],
                                  context->state[6], context->state[7]};

    for (int i = 0; i < 64; i++) {
        sha256_round(&working, data[i], K[i]);
    }

    context->state[0] += working.a;
    context->state[1] += working.b;
    context->state[2] += working.c;
    context->state[3] += working.d;
    context->state[4] += working.e;
    context->state[5] += working.f;
    context->state[6] += working.g;
    context->state[7] += working.h;
}

/* Дополняет последнее сообщение и добавляет его исходную длину в битах.
 * Обрабатывает заключительный блок или блоки и записывает 32 байта результата.
 */
static void sha256_final(Sha256Context* context, unsigned char digest[32]) {
    uint64_t message_length_bits = context->total_bytes * 8;

    context->block[context->block_len] = 0x80;
    context->block_len++;

    if (context->block_len > 56) {
        while (context->block_len < 64) {
            context->block[context->block_len] = 0x00;
            context->block_len++;
        }
        sha256_transform(context);
        context->block_len = 0;
    }
    while (context->block_len < 56) {
        context->block[context->block_len] = 0x00;
        context->block_len++;
    }
    for (size_t i = 0; i < 8; i++) {
        context->block[63 - i] =
            (unsigned char)(message_length_bits >> (8 * i));
    }

    context->block_len = 64;
    sha256_transform(context);
    context->block_len = 0;

    for (size_t i = 0; i < 8; i++) {
        digest[4 * i] = (unsigned char)(context->state[i] >> 24);
        digest[4 * i + 1] = (unsigned char)(context->state[i] >> 16);
        digest[4 * i + 2] = (unsigned char)(context->state[i] >> 8);
        digest[4 * i + 3] = (unsigned char)context->state[i];
    }
}

/* Преобразует 32 байта дайджеста в 64 строчные шестнадцатеричные цифры.
 * Добавляет завершающий '\0', чтобы результат был строкой C.
 */
static void digest_to_hex(const unsigned char digest[32], char output_hex[65]) {
    const char hex_chars[] = "0123456789abcdef";

    for (size_t i = 0; i < 32; i++) {
        unsigned char byte = digest[i];
        output_hex[2 * i] = hex_chars[byte >> 4];
        output_hex[2 * i + 1] = hex_chars[byte & 0x0f];
    }

    output_hex[64] = '\0';
}
