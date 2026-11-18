#include "srp.h"

#include <string.h>

#include <mbedtls/bignum.h>
#include <mbedtls/sha256.h>

#include "srp_group.h"

namespace srp {
namespace {

// g = 2 as an MPI is reused by every public computation.
bool setGenerator(mbedtls_mpi &g) {
  return mbedtls_mpi_lset(&g, 2) == 0;
}

// k = H(N | PAD(g)) per RFC 5054 section 2.6.
bool computeMultiplier(mbedtls_mpi &k, const mbedtls_mpi &n) {
  uint8_t buffer[kGroupBytes + kGroupBytes];
  if (mbedtls_mpi_write_binary(&n, buffer, kGroupBytes) != 0) {
    return false;
  }
  memset(buffer + kGroupBytes, 0, kGroupBytes);
  buffer[kGroupBytes * 2 - 1] = 2;
  uint8_t digest[kHashBytes];
  hash(buffer, sizeof(buffer), digest);
  return mbedtls_mpi_read_binary(&k, digest, kHashBytes) == 0;
}

// u = H(PAD(A) | PAD(B)).
bool computeScramble(mbedtls_mpi &u, const mbedtls_mpi &clientPublic, const mbedtls_mpi &serverPublic) {
  uint8_t buffer[kGroupBytes * 2];
  if (mbedtls_mpi_write_binary(&clientPublic, buffer, kGroupBytes) != 0 ||
      mbedtls_mpi_write_binary(&serverPublic, buffer + kGroupBytes, kGroupBytes) != 0) {
    return false;
  }
  uint8_t digest[kHashBytes];
  hash(buffer, sizeof(buffer), digest);
  return mbedtls_mpi_read_binary(&u, digest, kHashBytes) == 0;
}

}  // namespace

// PAD() for the fixed generator, used when hashing g.
static void padGenerator(uint8_t out[kGroupBytes]) {
  memset(out, 0, kGroupBytes);
  out[kGroupBytes - 1] = 2;
}

void hash(const uint8_t *data, size_t length, uint8_t out[kHashBytes]) {
  mbedtls_sha256_ret(data, length, out, 0);
}

bool validVerifier(const uint8_t verifier[kVerifierBytes]) {
  mbedtls_mpi group;
  mbedtls_mpi value;
  mbedtls_mpi upper;
  mbedtls_mpi_init(&group);
  mbedtls_mpi_init(&value);
  mbedtls_mpi_init(&upper);

  bool ok = false;
  do {
    if (mbedtls_mpi_read_binary(&group, srp_group::kPrime, kGroupBytes) != 0 ||
        mbedtls_mpi_read_binary(&value, verifier, kVerifierBytes) != 0) {
      break;
    }
    // v must satisfy 2 <= v <= N - 2; anything else is a degenerate group
    // element that would weaken or break the handshake.
    if (mbedtls_mpi_cmp_int(&value, 2) < 0) {
      break;
    }
    if (mbedtls_mpi_sub_int(&upper, &group, 2) != 0 ||
        mbedtls_mpi_cmp_mpi(&value, &upper) > 0) {
      break;
    }
    ok = true;
  } while (false);

  mbedtls_mpi_free(&upper);
  mbedtls_mpi_free(&value);
  mbedtls_mpi_free(&group);
  return ok;
}

bool beginHandshake(const uint8_t verifier[kVerifierBytes],
                    const uint8_t salt[kSaltBytes],
                    const uint8_t clientPublic[kGroupBytes],
                    const uint8_t serverSecret[kSecretBytes],
                    Handshake &handshake) {
  clearHandshake(handshake);

  mbedtls_mpi group;
  mbedtls_mpi generator;
  mbedtls_mpi multiplier;
  mbedtls_mpi verifierValue;
  mbedtls_mpi a;
  mbedtls_mpi b;
  mbedtls_mpi u;
  mbedtls_mpi serverPublic;
  mbedtls_mpi clientMod;
  mbedtls_mpi base;
  mbedtls_mpi shared;
  bool ok = false;

  mbedtls_mpi_init(&group);
  mbedtls_mpi_init(&generator);
  mbedtls_mpi_init(&multiplier);
  mbedtls_mpi_init(&verifierValue);
  mbedtls_mpi_init(&a);
  mbedtls_mpi_init(&b);
  mbedtls_mpi_init(&u);
  mbedtls_mpi_init(&serverPublic);
  mbedtls_mpi_init(&clientMod);
  mbedtls_mpi_init(&base);
  mbedtls_mpi_init(&shared);

  do {
    if (mbedtls_mpi_read_binary(&group, srp_group::kPrime, srp::kGroupBytes) != 0 ||
        !setGenerator(generator) ||
        mbedtls_mpi_read_binary(&verifierValue, verifier, kVerifierBytes) != 0 ||
        mbedtls_mpi_read_binary(&a, clientPublic, kGroupBytes) != 0 ||
        mbedtls_mpi_read_binary(&b, serverSecret, kSecretBytes) != 0) {
      break;
    }

    // Reject A == 0 mod N, which would otherwise leak the verifier.
    if (mbedtls_mpi_mod_mpi(&clientMod, &a, &group) != 0 ||
        mbedtls_mpi_cmp_int(&clientMod, 0) == 0) {
      break;
    }

    if (!computeMultiplier(multiplier, group) ||
        mbedtls_mpi_exp_mod(&serverPublic, &generator, &b, &group, nullptr) != 0 ||
        mbedtls_mpi_mul_mpi(&base, &multiplier, &verifierValue) != 0 ||
        mbedtls_mpi_add_mpi(&base, &base, &serverPublic) != 0 ||
        mbedtls_mpi_mod_mpi(&serverPublic, &base, &group) != 0 ||
        !computeScramble(u, a, serverPublic)) {
      break;
    }

    // S = (A * v^u)^b mod N.
    if (mbedtls_mpi_exp_mod(&base, &verifierValue, &u, &group, nullptr) != 0 ||
        mbedtls_mpi_mul_mpi(&base, &base, &a) != 0 ||
        mbedtls_mpi_mod_mpi(&base, &base, &group) != 0 ||
        mbedtls_mpi_exp_mod(&shared, &base, &b, &group, nullptr) != 0 ||
        mbedtls_mpi_write_binary(&serverPublic, handshake.serverPublic, kGroupBytes) != 0) {
      break;
    }

    // K = H(PAD(S)): the full 256-byte big-endian shared secret.
    uint8_t sharedBytes[kGroupBytes];
    if (mbedtls_mpi_write_binary(&shared, sharedBytes, sizeof(sharedBytes)) != 0) {
      break;
    }
    hash(sharedBytes, sizeof(sharedBytes), handshake.sessionKey);
    memset(sharedBytes, 0, sizeof(sharedBytes));

    memcpy(handshake.salt, salt, kSaltBytes);
    memcpy(handshake.clientPublic, clientPublic, kGroupBytes);
    handshake.active = true;
    ok = true;
  } while (false);

  mbedtls_mpi_free(&shared);
  mbedtls_mpi_free(&base);
  mbedtls_mpi_free(&clientMod);
  mbedtls_mpi_free(&serverPublic);
  mbedtls_mpi_free(&u);
  mbedtls_mpi_free(&b);
  mbedtls_mpi_free(&a);
  mbedtls_mpi_free(&verifierValue);
  mbedtls_mpi_free(&multiplier);
  mbedtls_mpi_free(&generator);
  mbedtls_mpi_free(&group);

  if (!ok) {
    clearHandshake(handshake);
  }
  return ok;
}

namespace {

// Precomputed H(N) xor H(g), which depends only on the fixed group.
const uint8_t *groupDigestXor() {
  static uint8_t cached[kHashBytes] = {};
  static bool ready = false;
  if (ready) {
    return cached;
  }

  uint8_t groupHash[kHashBytes];
  uint8_t generatorHash[kHashBytes];
  uint8_t generatorBytes[kGroupBytes];
  padGenerator(generatorBytes);
  hash(srp_group::kPrime, kGroupBytes, groupHash);
  hash(generatorBytes, kGroupBytes, generatorHash);
  for (size_t index = 0; index < kHashBytes; ++index) {
    cached[index] = static_cast<uint8_t>(groupHash[index] ^ generatorHash[index]);
  }
  ready = true;
  return cached;
}

void computeClientProof(const Handshake &handshake, uint8_t out[kHashBytes]) {
  uint8_t identityHash[kHashBytes];
  hash(reinterpret_cast<const uint8_t *>(kIdentity), strlen(kIdentity), identityHash);

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  mbedtls_sha256_update_ret(&ctx, groupDigestXor(), kHashBytes);
  mbedtls_sha256_update_ret(&ctx, identityHash, kHashBytes);
  mbedtls_sha256_update_ret(&ctx, handshake.salt, kSaltBytes);
  mbedtls_sha256_update_ret(&ctx, handshake.clientPublic, kGroupBytes);
  mbedtls_sha256_update_ret(&ctx, handshake.serverPublic, kGroupBytes);
  mbedtls_sha256_update_ret(&ctx, handshake.sessionKey, kHashBytes);
  mbedtls_sha256_finish_ret(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

}  // namespace

bool finishHandshake(const Handshake &handshake,
                     const uint8_t clientProof[kHashBytes],
                     uint8_t serverProof[kHashBytes]) {
  if (!handshake.active) {
    return false;
  }

  uint8_t expected[kHashBytes];
  computeClientProof(handshake, expected);
  const bool matches = constantTimeEqual(expected, clientProof, kHashBytes);
  memset(expected, 0, sizeof(expected));
  if (!matches) {
    return false;
  }

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  mbedtls_sha256_update_ret(&ctx, handshake.clientPublic, kGroupBytes);
  mbedtls_sha256_update_ret(&ctx, clientProof, kHashBytes);
  mbedtls_sha256_update_ret(&ctx, handshake.sessionKey, kHashBytes);
  mbedtls_sha256_finish_ret(&ctx, serverProof);
  mbedtls_sha256_free(&ctx);
  return true;
}

void clearHandshake(Handshake &handshake) {
  memset(&handshake, 0, sizeof(handshake));
  handshake.active = false;
}

bool constantTimeEqual(const uint8_t *left, const uint8_t *right, size_t length) {
  uint8_t difference = 0;
  for (size_t index = 0; index < length; ++index) {
    difference = static_cast<uint8_t>(difference | (left[index] ^ right[index]));
  }
  return difference == 0;
}

}  // namespace srp
