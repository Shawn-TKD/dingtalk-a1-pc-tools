#ifndef A1_SESSION_H
#define A1_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "a1/auth.h"

struct a1_runtime;

typedef enum {
    A1_SESSION_CODE_OK = 200,
    A1_SESSION_CODE_AUTH_FAILED = 401,
    A1_SESSION_CODE_DID_MISMATCH = 405,
    A1_SESSION_CODE_TOKEN_MISMATCH = 501,
    A1_SESSION_CODE_TOKEN_LENGTH = 502,
    A1_SESSION_CODE_TOKEN_MISSING = 503,
    A1_SESSION_CODE_ALREADY_CONNECTED = 541
} a1_session_code_t;

typedef struct {
    int64_t did;
    const char *token;
    size_t token_length;
    int64_t timestamp;
    const char *model;
    const char *sdk_version;
} a1_connect_request_t;

typedef struct {
    a1_auth_state_t auth;
    int64_t active_did;
    int has_active_did;
} a1_session_t;

void a1_session_init(
    a1_session_t *session,
    const uint8_t *device_secret,
    size_t device_secret_length);

int a1_session_create_challenge(
    a1_session_t *session,
    const a1_platform_t *platform,
    char challenge[A1_CHALLENGE_LENGTH + 1u]);

a1_session_code_t a1_session_connect(
    a1_session_t *session,
    struct a1_runtime *runtime,
    const a1_connect_request_t *request);

a1_session_code_t a1_session_disconnect(
    a1_session_t *session,
    struct a1_runtime *runtime,
    int64_t did);

void a1_session_transport_lost(
    a1_session_t *session,
    struct a1_runtime *runtime);

#endif
