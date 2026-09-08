#include "a1/auth.h"

#include <string.h>

static const char HEX_DIGITS[] = "0123456789abcdef";

static const uint32_t MD5_CONSTANTS[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

static const uint8_t MD5_ROTATIONS[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t rotate_left(uint32_t value, uint8_t shift)
{
    return (value << shift) | (value >> (32u - shift));
}

/* The only firmware use reconstructed here hashes exactly the 32 ASCII bytes
 * of a hex-encoded UID, so one MD5 block is sufficient and avoids exposing a
 * misleading general-purpose hash API. */
static void md5_uid_hex(const uint8_t uid_hex[32], uint8_t digest[16])
{
    uint8_t block[64] = {0};
    uint32_t words[16];
    uint32_t a = 0x67452301u;
    uint32_t b = 0xefcdab89u;
    uint32_t c = 0x98badcfeu;
    uint32_t d = 0x10325476u;
    uint32_t initial_a = a;
    uint32_t initial_b = b;
    uint32_t initial_c = c;
    uint32_t initial_d = d;
    uint32_t index;

    memcpy(block, uid_hex, 32u);
    block[32] = 0x80u;
    block[57] = 1u; /* 32 bytes == 256 bits, little-endian. */
    for (index = 0; index < 16u; ++index) {
        words[index] = read_le32(block + index * 4u);
    }

    for (index = 0; index < 64u; ++index) {
        uint32_t function_value;
        uint32_t word_index;
        uint32_t previous_d = d;

        if (index < 16u) {
            function_value = (b & c) | ((~b) & d);
            word_index = index;
        } else if (index < 32u) {
            function_value = (d & b) | ((~d) & c);
            word_index = (5u * index + 1u) & 15u;
        } else if (index < 48u) {
            function_value = b ^ c ^ d;
            word_index = (3u * index + 5u) & 15u;
        } else {
            function_value = c ^ (b | (~d));
            word_index = (7u * index) & 15u;
        }
        d = c;
        c = b;
        b += rotate_left(a + function_value + MD5_CONSTANTS[index] +
                         words[word_index], MD5_ROTATIONS[index]);
        a = previous_d;
    }

    a += initial_a;
    b += initial_b;
    c += initial_c;
    d += initial_d;
    write_le32(digest, a);
    write_le32(digest + 4, b);
    write_le32(digest + 8, c);
    write_le32(digest + 12, d);
    memset(block, 0, sizeof(block));
    memset(words, 0, sizeof(words));
}

static void encode_hex(const uint8_t *input, size_t length, char *output)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        output[index * 2] = HEX_DIGITS[input[index] >> 4];
        output[index * 2 + 1] = HEX_DIGITS[input[index] & 0x0f];
    }
    output[length * 2] = '\0';
}

int a1_device_secret_derive(
    const uint8_t hardware_uid[A1_HARDWARE_UID_LENGTH],
    char device_secret[A1_DEVICE_SECRET_LENGTH + 1u])
{
    uint8_t uid_hex[A1_HARDWARE_UID_LENGTH * 2u + 1u];
    uint8_t digest[16];

    if (hardware_uid == NULL || device_secret == NULL) {
        return -1;
    }
    encode_hex(hardware_uid, A1_HARDWARE_UID_LENGTH, (char *)uid_hex);
    md5_uid_hex(uid_hex, digest);
    encode_hex(digest, sizeof(digest), device_secret);
    memset(uid_hex, 0, sizeof(uid_hex));
    memset(digest, 0, sizeof(digest));
    return 0;
}

void a1_auth_init(
    a1_auth_state_t *state,
    const uint8_t *device_secret,
    size_t device_secret_length)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    if (device_secret != NULL && device_secret_length == A1_DEVICE_SECRET_LENGTH) {
        memcpy(state->device_secret, device_secret, A1_DEVICE_SECRET_LENGTH);
        state->configured = 1;
    }
}

uint32_t a1_djb2_bytes(const uint8_t *bytes, size_t length)
{
    uint32_t value = 0;
    size_t index;

    if (bytes == NULL) {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        value = value * 33u + bytes[index];
    }
    return value;
}

int a1_auth_create_challenge(
    a1_auth_state_t *state,
    const a1_platform_t *platform,
    char challenge[A1_CHALLENGE_LENGTH + 1u])
{
    uint8_t random_bytes[16];
    uint8_t encrypted[32];
    char expected_token[A1_TOKEN_LENGTH + 1u];
    uint8_t key[16];
    int result;

    if (state == NULL || !state->configured || platform == NULL || challenge == NULL ||
        platform->random_bytes == NULL ||
        platform->aes_128_cbc_encrypt == NULL) {
        return -1;
    }
    memcpy(key, state->device_secret, sizeof(key));
    result = platform->random_bytes(platform->context, random_bytes, sizeof(random_bytes));
    if (result != 0) {
        return result;
    }
    encode_hex(random_bytes, sizeof(random_bytes), challenge);
    result = platform->aes_128_cbc_encrypt(
        platform->context,
        key,
        key,
        (const uint8_t *)challenge,
        A1_CHALLENGE_LENGTH,
        encrypted);
    if (result != 0) {
        return result;
    }
    encode_hex(encrypted, sizeof(encrypted), expected_token);
    state->expected_token_hash =
        a1_djb2_bytes((const uint8_t *)expected_token, A1_TOKEN_LENGTH);
    state->challenge_pending = 1;
    memset(random_bytes, 0, sizeof(random_bytes));
    memset(key, 0, sizeof(key));
    memset(encrypted, 0, sizeof(encrypted));
    memset(expected_token, 0, sizeof(expected_token));
    return 0;
}

int a1_auth_verify_token(
    a1_auth_state_t *state,
    const char *token,
    size_t token_length)
{
    uint32_t received_hash;

    if (state == NULL || token == NULL || !state->challenge_pending) {
        return -1;
    }
    state->challenge_pending = 0;
    if (token_length != A1_TOKEN_LENGTH) {
        return -2;
    }
    received_hash = a1_djb2_bytes((const uint8_t *)token, token_length);
    return received_hash == state->expected_token_hash ? 0 : -3;
}
