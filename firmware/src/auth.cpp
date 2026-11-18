#include "auth.h"

#include <Preferences.h>
#include <esp_random.h>
#include <string.h>

#include "util.h"

namespace auth {
namespace {

constexpr char kNamespace[] = "pramon_auth";
constexpr size_t kMaxSessions = 4;
constexpr size_t kMaxChallenges = 4;
constexpr uint32_t kSessionIdleMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kChallengeLifetimeMs = 120UL * 1000UL;
constexpr uint8_t kMaxFailedLogins = 5;
constexpr uint32_t kLockoutBaseMs = 30UL * 1000UL;
constexpr uint32_t kLockoutMaxMs = 15UL * 60UL * 1000UL;
constexpr size_t kPasswordMinLength = 8;
constexpr size_t kPasswordMaxLength = 128;

struct Session {
  bool used;
  uint8_t token[srp::kTokenBytes];
  uint32_t lastSeenMs;
};

struct Challenge {
  bool used;
  String id;
  srp::Handshake handshake;
  uint32_t issuedMs;
};

Preferences authPrefs;
bool configured = false;
bool publicTelemetry = false;
uint32_t storedIterations = srp::kDefaultIterations;
uint8_t salt[srp::kSaltBytes] = {};
uint8_t verifier[srp::kVerifierBytes] = {};
Session sessions[kMaxSessions] = {};
Challenge challenges[kMaxChallenges] = {};
uint8_t failedLogins = 0;
uint32_t lockoutUntilMs = 0;

String toHex(const uint8_t *data, size_t length) {
  static const char digits[] = "0123456789abcdef";
  String out;
  out.reserve(length * 2);
  for (size_t index = 0; index < length; ++index) {
    out += digits[data[index] >> 4];
    out += digits[data[index] & 0x0F];
  }
  return out;
}

bool fromHex(const char *text, uint8_t *out, size_t length) {
  if (text == nullptr || strlen(text) != length * 2) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    uint8_t value = 0;
    for (size_t nibble = 0; nibble < 2; ++nibble) {
      const char ch = text[index * 2 + nibble];
      uint8_t digit = 0;
      if (ch >= '0' && ch <= '9') {
        digit = static_cast<uint8_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        digit = static_cast<uint8_t>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        digit = static_cast<uint8_t>(ch - 'A' + 10);
      } else {
        return false;
      }
      value = static_cast<uint8_t>((value << 4) | digit);
    }
    out[index] = value;
  }
  return true;
}

void releaseChallenge(Challenge &challenge) {
  srp::clearHandshake(challenge.handshake);
  challenge.used = false;
  challenge.id = "";
}

void pruneChallenges(uint32_t now) {
  for (size_t index = 0; index < kMaxChallenges; ++index) {
    if (challenges[index].used &&
        timeReached(now, challenges[index].issuedMs + kChallengeLifetimeMs)) {
      releaseChallenge(challenges[index]);
    }
  }
}

Challenge *allocateChallenge(uint32_t now) {
  pruneChallenges(now);
  for (size_t index = 0; index < kMaxChallenges; ++index) {
    if (!challenges[index].used) {
      return &challenges[index];
    }
  }
  return nullptr;
}

Challenge *findChallenge(const char *id) {
  if (id == nullptr || id[0] == '\0') {
    return nullptr;
  }
  for (size_t index = 0; index < kMaxChallenges; ++index) {
    if (challenges[index].used && challenges[index].id == id) {
      return &challenges[index];
    }
  }
  return nullptr;
}

void pruneSessions(uint32_t now) {
  for (size_t index = 0; index < kMaxSessions; ++index) {
    if (sessions[index].used &&
        timeReached(now, sessions[index].lastSeenMs + kSessionIdleMs)) {
      memset(sessions[index].token, 0, srp::kTokenBytes);
      sessions[index].used = false;
    }
  }
}

void noteLoginFailure(uint32_t now) {
  if (failedLogins < 255) {
    ++failedLogins;
  }
  if (failedLogins < kMaxFailedLogins) {
    return;
  }

  uint32_t delayMs = kLockoutBaseMs;
  for (uint8_t step = kMaxFailedLogins; step < failedLogins && delayMs < kLockoutMaxMs; ++step) {
    delayMs *= 2;
  }
  lockoutUntilMs = now + (delayMs > kLockoutMaxMs ? kLockoutMaxMs : delayMs);
}

void noteLoginSuccess() {
  failedLogins = 0;
  lockoutUntilMs = 0;
}

void forgetPassword() {
  authPrefs.remove("salt");
  authPrefs.remove("verifier");
  authPrefs.remove("iter");
  memset(salt, 0, sizeof(salt));
  memset(verifier, 0, sizeof(verifier));
  configured = false;
  for (size_t index = 0; index < kMaxChallenges; ++index) {
    if (challenges[index].used) {
      releaseChallenge(challenges[index]);
    }
  }
  noteLoginSuccess();
  revokeAllSessions();
}

}  // namespace

void configureAuth() {
  authPrefs.begin(kNamespace, false);
  publicTelemetry = authPrefs.getBool("telempub", false);
  storedIterations = authPrefs.getUInt("iter", srp::kDefaultIterations);

  const bool hasSalt = authPrefs.getBytesLength("salt") == sizeof(salt) &&
                       authPrefs.getBytes("salt", salt, sizeof(salt)) == sizeof(salt);
  const bool hasVerifier = authPrefs.getBytesLength("verifier") == sizeof(verifier) &&
                           authPrefs.getBytes("verifier", verifier, sizeof(verifier)) == sizeof(verifier);
  const bool usableIterations = storedIterations >= srp::kMinIterations &&
                                storedIterations <= srp::kMaxIterations;

  // A partially written or corrupt record must not lock the owner out; treat
  // it as "no password" so the device stays reachable over the network.
  configured = hasSalt && hasVerifier && usableIterations &&
               srp::validVerifier(verifier);
  if (!configured) {
    memset(salt, 0, sizeof(salt));
    memset(verifier, 0, sizeof(verifier));
    storedIterations = srp::kDefaultIterations;
  }
}

bool passwordConfigured() {
  return configured;
}

bool telemetryPublic() {
  return publicTelemetry;
}

uint32_t iterations() {
  return configured ? storedIterations : srp::kDefaultIterations;
}

uint32_t lockoutRemainingMs() {
  const uint32_t now = millis();
  return timeReached(now, lockoutUntilMs) ? 0 : lockoutUntilMs - now;
}

bool beginChallenge(const uint8_t clientPublic[srp::kGroupBytes],
                    String &challengeId,
                    uint8_t outSalt[srp::kSaltBytes],
                    uint8_t serverPublic[srp::kGroupBytes]) {
  if (!configured) {
    return false;
  }

  const uint32_t now = millis();
  Challenge *challenge = allocateChallenge(now);
  if (challenge == nullptr) {
    return false;
  }

  uint8_t serverSecret[srp::kSecretBytes];
  esp_fill_random(serverSecret, sizeof(serverSecret));
  const bool started = srp::beginHandshake(verifier, salt, clientPublic, serverSecret, challenge->handshake);
  memset(serverSecret, 0, sizeof(serverSecret));
  if (!started) {
    return false;
  }

  uint8_t randomId[16];
  esp_fill_random(randomId, sizeof(randomId));
  challenge->id = toHex(randomId, sizeof(randomId));
  challenge->issuedMs = now;
  challenge->used = true;

  memcpy(outSalt, salt, sizeof(salt));
  memcpy(serverPublic, challenge->handshake.serverPublic, srp::kGroupBytes);
  challengeId = challenge->id;
  return true;
}

LoginResult finishChallenge(const char *challengeId,
                            const uint8_t clientProof[srp::kHashBytes],
                            String &token,
                            uint8_t serverProof[srp::kHashBytes],
                            uint32_t &retryAfterMs) {
  retryAfterMs = 0;
  if (!configured) {
    return LoginResult::NotConfigured;
  }

  const uint32_t now = millis();
  const uint32_t remaining = lockoutRemainingMs();
  if (remaining > 0) {
    retryAfterMs = remaining;
    if (Challenge *locked = findChallenge(challengeId)) {
      releaseChallenge(*locked);
    }
    return LoginResult::LockedOut;
  }

  Challenge *challenge = findChallenge(challengeId);
  if (challenge == nullptr) {
    return LoginResult::UnknownChallenge;
  }

  const bool verified = srp::finishHandshake(challenge->handshake, clientProof, serverProof);
  releaseChallenge(*challenge);
  if (!verified) {
    noteLoginFailure(now);
    retryAfterMs = lockoutRemainingMs();
    return LoginResult::BadProof;
  }

  noteLoginSuccess();
  createSession(token);
  return LoginResult::Ok;
}

void createSession(String &token) {
  const uint32_t now = millis();
  pruneSessions(now);

  Session *slot = nullptr;
  for (size_t index = 0; index < kMaxSessions; ++index) {
    if (!sessions[index].used) {
      slot = &sessions[index];
      break;
    }
  }
  if (slot == nullptr) {
    // Recycle the least recently used session.
    slot = &sessions[0];
    for (size_t index = 1; index < kMaxSessions; ++index) {
      if (timeReached(sessions[index].lastSeenMs, slot->lastSeenMs)) {
        slot = &sessions[index];
      }
    }
  }

  esp_fill_random(slot->token, sizeof(slot->token));
  slot->lastSeenMs = now;
  slot->used = true;
  token = toHex(slot->token, sizeof(slot->token));
}

bool sessionValid(const char *token) {
  uint8_t decoded[srp::kTokenBytes];
  if (!fromHex(token, decoded, sizeof(decoded))) {
    return false;
  }

  const uint32_t now = millis();
  pruneSessions(now);
  for (size_t index = 0; index < kMaxSessions; ++index) {
    if (!sessions[index].used) {
      continue;
    }
    if (srp::constantTimeEqual(sessions[index].token, decoded, sizeof(decoded))) {
      sessions[index].lastSeenMs = now;
      return true;
    }
  }
  return false;
}

void revokeSession(const char *token) {
  uint8_t decoded[srp::kTokenBytes];
  if (!fromHex(token, decoded, sizeof(decoded))) {
    return;
  }
  for (size_t index = 0; index < kMaxSessions; ++index) {
    if (sessions[index].used && srp::constantTimeEqual(sessions[index].token, decoded, sizeof(decoded))) {
      memset(sessions[index].token, 0, srp::kTokenBytes);
      sessions[index].used = false;
      return;
    }
  }
}

void revokeAllSessions() {
  for (size_t index = 0; index < kMaxSessions; ++index) {
    memset(sessions[index].token, 0, srp::kTokenBytes);
    sessions[index].used = false;
  }
}

bool setPassword(const uint8_t newVerifier[srp::kVerifierBytes],
                 const uint8_t newSalt[srp::kSaltBytes],
                 uint32_t iterationCount,
                 String &token) {
  if (iterationCount < srp::kMinIterations || iterationCount > srp::kMaxIterations ||
      !srp::validVerifier(newVerifier)) {
    return false;
  }

  memcpy(salt, newSalt, sizeof(salt));
  memcpy(verifier, newVerifier, sizeof(verifier));
  storedIterations = iterationCount;
  authPrefs.putBytes("salt", salt, sizeof(salt));
  authPrefs.putBytes("verifier", verifier, sizeof(verifier));
  authPrefs.putUInt("iter", iterationCount);
  configured = true;

  // Changing the credential invalidates every existing login.
  for (size_t index = 0; index < kMaxChallenges; ++index) {
    if (challenges[index].used) {
      releaseChallenge(challenges[index]);
    }
  }
  noteLoginSuccess();
  revokeAllSessions();
  createSession(token);
  return true;
}

void clearPassword() {
  forgetPassword();
}

bool setTelemetryPublic(bool isPublic) {
  publicTelemetry = isPublic;
  authPrefs.putBool("telempub", isPublic);
  return true;
}

String statusJson() {
  uint8_t emptySalt[srp::kSaltBytes] = {};
  String body = "{\"configured\":";
  body += jsonBool(configured);
  body += ",\"identity\":";
  body += jsonString(srp::kIdentity);
  body += ",\"salt\":";
  body += jsonString(toHex(configured ? salt : emptySalt, srp::kSaltBytes));
  body += ",\"iterations\":";
  body += String(iterations());
  body += ",\"telemetryPublic\":";
  body += jsonBool(publicTelemetry);
  body += ",\"lockoutMs\":";
  body += String(lockoutRemainingMs());
  body += ",\"sessionTtlMs\":";
  body += String(kSessionIdleMs);
  body += ",\"passwordMinLength\":";
  body += String(kPasswordMinLength);
  body += ",\"passwordMaxLength\":";
  body += String(kPasswordMaxLength);
  body += "}";
  return body;
}

}  // namespace auth
