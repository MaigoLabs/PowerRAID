#pragma once

#include <stddef.h>
#include <stdint.h>

// SRP-6a server-side authentication over the RFC 5054 2048-bit group with
// SHA-256.
//
// The password never crosses the wire and is never stored. The browser derives
// x = PBKDF2-HMAC-SHA256(password, salt, iterations) and v = g^x mod N, and
// registers only (v, salt, iterations). Logging in proves knowledge of x
// without revealing it, so:
//   - a passive observer of the HTTP traffic learns nothing reusable;
//   - an attacker holding the stored verifier from flash cannot produce the
//     client proof (that needs a discrete log).
//
// Wire values are big-endian and left-padded to kGroupBytes. Values:
//   k = H(N | PAD(g)), u = H(PAD(A) | PAD(B)), K = H(PAD(S))
//   M1 = H(H(N) ^ H(g) | H(I) | salt | PAD(A) | PAD(B) | K)
//   M2 = H(PAD(A) | M1 | K)
//
// The module has no Arduino dependencies so host tests can exercise it.
namespace srp {

constexpr size_t kHashBytes = 32;
constexpr size_t kGroupBytes = 256;
constexpr size_t kSaltBytes = 16;
constexpr size_t kVerifierBytes = kGroupBytes;
constexpr size_t kSecretBytes = 32;   // ephemeral exponent b
constexpr size_t kTokenBytes = 32;

// Password key derivation work factor, advertised to clients so both sides
// agree. Only the client runs PBKDF2, so this costs the device nothing.
constexpr uint32_t kDefaultIterations = 600000;
constexpr uint32_t kMinIterations = 100000;
constexpr uint32_t kMaxIterations = 4000000;

// Account identity for the single device login.
constexpr char kIdentity[] = "powerraid";

void hash(const uint8_t *data, size_t length, uint8_t out[kHashBytes]);
bool constantTimeEqual(const uint8_t *left, const uint8_t *right, size_t length);

// True when v is a usable group element: 2 <= v <= N - 2.
bool validVerifier(const uint8_t verifier[kVerifierBytes]);

// State for one in-flight login. It holds only throwaway ephemerals and the
// session key K, so a snapshot of it cannot be replayed as a login.
struct Handshake {
  bool active;
  uint8_t salt[kSaltBytes];
  uint8_t clientPublic[kGroupBytes];
  uint8_t serverPublic[kGroupBytes];
  uint8_t sessionKey[kHashBytes];
};

// Computes B = (k*v + g^b) mod N from the client public A and the server
// ephemeral b, then K. Fails when A is not a valid group element.
bool beginHandshake(const uint8_t verifier[kVerifierBytes],
                    const uint8_t salt[kSaltBytes],
                    const uint8_t clientPublic[kGroupBytes],
                    const uint8_t serverSecret[kSecretBytes],
                    Handshake &handshake);

// Verifies the client proof M1 and, on success, writes the server proof M2.
bool finishHandshake(const Handshake &handshake,
                     const uint8_t clientProof[kHashBytes],
                     uint8_t serverProof[kHashBytes]);

void clearHandshake(Handshake &handshake);

}  // namespace srp
