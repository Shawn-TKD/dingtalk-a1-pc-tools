#include "a1/session.h"

#include <string.h>

#include "a1/runtime.h"

void a1_session_init(
    a1_session_t *session,
    const uint8_t *device_secret,
    size_t device_secret_length)
{
    if (session == NULL) {
        return;
    }
    memset(session, 0, sizeof(*session));
    a1_auth_init(&session->auth, device_secret, device_secret_length);
}

int a1_session_create_challenge(
    a1_session_t *session,
    const a1_platform_t *platform,
    char challenge[A1_CHALLENGE_LENGTH + 1u])
{
    if (session == NULL) {
        return -1;
    }
    return a1_auth_create_challenge(&session->auth, platform, challenge);
}

a1_session_code_t a1_session_connect(
    a1_session_t *session,
    a1_runtime_t *runtime,
    const a1_connect_request_t *request)
{
    int verify_result;

    if (session == NULL || runtime == NULL || request == NULL) {
        return A1_SESSION_CODE_AUTH_FAILED;
    }
    if (session->has_active_did || runtime->logical_session_connected) {
        return A1_SESSION_CODE_ALREADY_CONNECTED;
    }
    if (request->token == NULL) {
        return A1_SESSION_CODE_TOKEN_MISSING;
    }

    verify_result = a1_auth_verify_token(
        &session->auth,
        request->token,
        request->token_length);
    if (verify_result == -2) {
        return A1_SESSION_CODE_TOKEN_LENGTH;
    }
    if (verify_result != 0) {
        return A1_SESSION_CODE_TOKEN_MISMATCH;
    }

    session->active_did = request->did;
    session->has_active_did = 1;
    a1_runtime_set_authenticated(runtime, true);
    a1_runtime_set_logical_session(runtime, true);
    return A1_SESSION_CODE_OK;
}

a1_session_code_t a1_session_disconnect(
    a1_session_t *session,
    a1_runtime_t *runtime,
    int64_t did)
{
    if (session == NULL || runtime == NULL || !session->has_active_did ||
        session->active_did != did) {
        return A1_SESSION_CODE_DID_MISMATCH;
    }

    session->active_did = 0;
    session->has_active_did = 0;
    a1_runtime_set_logical_session(runtime, false);
    a1_runtime_set_authenticated(runtime, false);
    return A1_SESSION_CODE_OK;
}

void a1_session_transport_lost(
    a1_session_t *session,
    a1_runtime_t *runtime)
{
    if (session != NULL) {
        session->active_did = 0;
        session->has_active_did = 0;
        session->auth.challenge_pending = 0;
    }
    if (runtime != NULL) {
        a1_runtime_set_transport(runtime, false);
    }
}
