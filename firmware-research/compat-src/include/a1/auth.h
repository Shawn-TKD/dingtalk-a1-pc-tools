#ifndef A1_AUTH_H
#define A1_AUTH_H

#include <stddef.h>
#include <stdint.h>

#include "a1/platform.h"

#define A1_DEVICE_SECRET_LENGTH 32u
#define A1_CHALLENGE_LENGTH 32u
#define A1_TOKEN_LENGTH 64u
#define A1_HARDWARE_UID_LENGTH 16u

typedef struct {
    uint8_t device_secret[A1_DEVICE_SECRET_LENGTH];
    uint32_t expected_token_hash;
    int configured;
    int challenge_pending;
} a1_auth_state_t;

void a1_auth_init(
    a1_auth_state_t *state,
    const uint8_t *device_secret,
    size_t device_secret_length);

/* V1.6.88 derives the 32-character deviceSecret as lowercase hex(MD5(
 * lowercase hex(16-byte hardware UID))). This deterministic protocol key is
 * distinct from the random DTIOT binding secret stored in vendor data. */
int a1_device_secret_derive(
    const uint8_t hardware_uid[A1_HARDWARE_UID_LENGTH],
    char device_secret[A1_DEVICE_SECRET_LENGTH + 1u]);

uint32_t a1_djb2_bytes(const uint8_t *bytes, size_t length);

int a1_auth_create_challenge(
    a1_auth_state_t *state,
    const a1_platform_t *platform,
    char challenge[A1_CHALLENGE_LENGTH + 1u]);

int a1_auth_verify_token(
    a1_auth_state_t *state,
    const char *token,
    size_t token_length);

#endif
