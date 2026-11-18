#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "srp.h"

// Device login state: the stored SRP verifier, short-lived login challenges,
// bearer session tokens, and brute-force throttling.
//
// A password is optional. With no password stored every request is allowed, so
// the bridge behaves exactly as an open device. Once a password is stored,
// every API except authentication itself, the static frontend, and (when the
// user opts in) read-only telemetry requires a session token.
namespace auth {

enum class LoginResult {
  Ok,
  BadProof,
  UnknownChallenge,
  LockedOut,
  NotConfigured,
};

// Loads the persisted verifier, iteration count, and telemetry policy.
void configureAuth();

bool passwordConfigured();
bool telemetryPublic();
uint32_t iterations();

// Remaining lockout delay after too many failed logins, in milliseconds.
uint32_t lockoutRemainingMs();

// Starts an SRP handshake for a client public key. Fails when no password is
// stored or the client public key is not a valid group element.
bool beginChallenge(const uint8_t clientPublic[srp::kGroupBytes],
                    String &challengeId,
                    uint8_t salt[srp::kSaltBytes],
                    uint8_t serverPublic[srp::kGroupBytes]);

// Verifies the client proof. On success issues a session token and the server
// proof; the challenge is consumed either way.
LoginResult finishChallenge(const char *challengeId,
                            const uint8_t clientProof[srp::kHashBytes],
                            String &token,
                            uint8_t serverProof[srp::kHashBytes],
                            uint32_t &retryAfterMs);

// Session handling. Tokens are 64 hex characters and live in RAM only, so a
// reboot forces a fresh login.
void createSession(String &token);
bool sessionValid(const char *token);
void revokeSession(const char *token);
void revokeAllSessions();

// Replaces the stored verifier. The client derives it, so the password itself
// never reaches the device.
bool setPassword(const uint8_t verifier[srp::kVerifierBytes],
                 const uint8_t salt[srp::kSaltBytes],
                 uint32_t iterationCount,
                 String &token);
void clearPassword();
bool setTelemetryPublic(bool isPublic);

// JSON fragments shared by /api/v1/auth and /api/v1/info.
String statusJson();

}  // namespace auth
