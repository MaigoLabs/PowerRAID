#!/usr/bin/env bash
# Builds the firmware's SRP server for the host so the browser crypto can be
# tested against the real implementation instead of a second guess at it.
#
# Needs mbedtls 2.28 headers and libmbedcrypto, the same version the ESP32
# Arduino framework ships.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pramon="$(cd "$here/.." && pwd)"
firmware="$(cd "$pramon/../firmware/src" && pwd)"
build="$pramon/build"

mbedtls_include="${MBEDTLS_INCLUDE:-/usr/include}"
mbedtls_lib="${MBEDTLS_LIB:-/usr/lib}"

if [ ! -f "$mbedtls_include/mbedtls/bignum.h" ]; then
  echo "mbedtls headers not found in $mbedtls_include (set MBEDTLS_INCLUDE)" >&2
  exit 1
fi

mkdir -p "$build"

cat > "$build/srp-host-test.cpp" <<'CPP'
// Mirrors auth.cpp: a stored verifier plus a fresh random b produce B and K,
// then a client proof is accepted or rejected.
#include <stdio.h>
#include <string.h>

#include "srp.h"

static bool unhex(const char *text, uint8_t *out, size_t length) {
  if (text == nullptr || strlen(text) != length * 2) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    unsigned value = 0;
    if (sscanf(text + index * 2, "%2x", &value) != 1) {
      return false;
    }
    out[index] = static_cast<uint8_t>(value);
  }
  return true;
}

static void printHex(const char *label, const uint8_t *data, size_t length) {
  printf("%s=", label);
  for (size_t index = 0; index < length; ++index) {
    printf("%02x", data[index]);
  }
  printf("\n");
}

int main(int argc, char **argv) {
  // argv: verifier salt clientPublic serverSecret clientProof|-
  if (argc < 6) {
    fprintf(stderr, "usage: %s verifier salt clientPublic serverSecret clientProof\n", argv[0]);
    return 2;
  }

  uint8_t verifier[srp::kVerifierBytes];
  uint8_t salt[srp::kSaltBytes];
  uint8_t secret[srp::kSecretBytes];
  uint8_t clientPublic[srp::kGroupBytes];
  if (!unhex(argv[1], verifier, sizeof(verifier)) ||
      !unhex(argv[2], salt, sizeof(salt)) ||
      !unhex(argv[3], clientPublic, sizeof(clientPublic)) ||
      !unhex(argv[4], secret, sizeof(secret))) {
    fprintf(stderr, "bad arguments\n");
    return 2;
  }

  printf("validVerifier=%d\n", srp::validVerifier(verifier) ? 1 : 0);

  srp::Handshake handshake;
  if (!srp::beginHandshake(verifier, salt, clientPublic, secret, handshake)) {
    fprintf(stderr, "beginHandshake failed\n");
    return 1;
  }
  printHex("serverPublic", handshake.serverPublic, sizeof(handshake.serverPublic));
  printHex("sessionKey", handshake.sessionKey, sizeof(handshake.sessionKey));

  if (strcmp(argv[5], "-") != 0) {
    uint8_t clientProof[srp::kHashBytes];
    uint8_t serverProof[srp::kHashBytes];
    if (!unhex(argv[5], clientProof, sizeof(clientProof))) {
      fprintf(stderr, "bad client proof\n");
      return 2;
    }
    const bool verified = srp::finishHandshake(handshake, clientProof, serverProof);
    printf("verified=%d\n", verified ? 1 : 0);
    if (verified) {
      printHex("serverProof", serverProof, sizeof(serverProof));
    }
  }

  srp::clearHandshake(handshake);
  return 0;
}
CPP

g++ -std=c++17 -O2 -I "$firmware" -I "$mbedtls_include" \
  -o "$build/srp-host-test" \
  "$build/srp-host-test.cpp" "$firmware/srp.cpp" \
  -L "$mbedtls_lib" -lmbedcrypto

echo "built $build/srp-host-test"
