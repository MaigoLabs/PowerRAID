/**
 * SRP-6a client over the RFC 5054 2048-bit group with SHA-256, matching the
 * firmware's srp.cpp. The password is turned into the verifier locally; it is
 * never sent to the bridge.
 *
 * Every big-number value crosses the wire as a hex string of exactly
 * kGroupBytes bytes, big-endian.
 */
import { pbkdf2Async, randomBytes, sha256 } from './sha256'

export const SRP_GROUP_BYTES = 256
export const SRP_SALT_BYTES = 16
export const SRP_HASH_BYTES = 32
export const SRP_IDENTITY = 'powerraid'

const PRIME_HEX =
  'ac6bdb41324a9a9bf166de5e1389582faf72b6651987ee07fc3192943db56050' +
  'a37329cbb4a099ed8193e0757767a13dd52312ab4b03310dcd7f48a9da04fd50' +
  'e8083969edb767b0cf6095179a163ab3661a05fbd5faaae82918a9962f0b93b8' +
  '55f97993ec975eeaa80d740adbf4ff747359d041d5c33ea71d281e446b14773b' +
  'ca97b43a23fb801676bd207a436c6481f1d2b9078717461a5b9d32e688f87748' +
  '544523b524b0d57d5ea77a2775d2ecfa032cfbdbf52fb3786160279004e57ae6' +
  'af874e7303ce53299ccc041c7bc308d82a5698f3a8d0c38271ae35f8e9dbfbb6' +
  '94b5c803d89f7ae435de236d525f54759b65e372fcd68ef20fa7111f9e4aff73'

const GENERATOR = 2n
const PRIME = BigInt(`0x${PRIME_HEX}`)

export function bytesToHex(bytes: Uint8Array): string {
  let out = ''
  for (const byte of bytes) {
    out += byte.toString(16).padStart(2, '0')
  }
  return out
}

export function hexToBytes(hex: string): Uint8Array {
  const clean = hex.trim().toLowerCase()
  if (clean.length % 2 !== 0 || !/^[0-9a-f]*$/.test(clean)) {
    throw new Error('hex value has an invalid length or character')
  }
  const out = new Uint8Array(clean.length / 2)
  for (let index = 0; index < out.length; index += 1) {
    out[index] = Number.parseInt(clean.slice(index * 2, index * 2 + 2), 16)
  }
  return out
}

/** Big-endian, left-padded to the group size, as the firmware parses it. */
export function padHex(value: bigint): string {
  return value.toString(16).padStart(SRP_GROUP_BYTES * 2, '0')
}

export function paddedHexToBigInt(hex: string): bigint {
  return BigInt(`0x${hex.length === 0 ? '0' : hex}`)
}

function modPow(base: bigint, exponent: bigint, modulus: bigint): bigint {
  let result = 1n
  let factor = base % modulus
  let power = exponent
  while (power > 0n) {
    if (power & 1n) {
      result = (result * factor) % modulus
    }
    factor = (factor * factor) % modulus
    power >>= 1n
  }
  return result
}

/** Concatenates hash inputs without intermediate copies of the whole message. */
function hashConcat(...parts: Uint8Array[]): Uint8Array {
  const total = parts.reduce((sum, part) => sum + part.length, 0)
  const buffer = new Uint8Array(total)
  let offset = 0
  for (const part of parts) {
    buffer.set(part, offset)
    offset += part.length
  }
  return sha256(buffer)
}

let cachedMultiplier: bigint | null = null

/** k = H(N | PAD(g)), identical on both sides. */
function multiplier(): bigint {
  if (cachedMultiplier == null) {
    const digest = hashConcat(
      hexToBytes(PRIME_HEX),
      hexToBytes(GENERATOR.toString(16).padStart(SRP_GROUP_BYTES * 2, '0')),
    )
    cachedMultiplier = BigInt(`0x${bytesToHex(digest)}`)
  }
  return cachedMultiplier
}

/** K = H(PAD(S)) over the full group-width shared secret. */
function sessionKey(shared: bigint): Uint8Array {
  return sha256(hexToBytes(padHex(shared)))
}

export interface SrpVerifier {
  verifier: string
  salt: string
  iterations: number
}

/** Derives (v, salt) from the password. This is what the device stores. */
export async function deriveVerifier(
  password: string,
  options: { salt?: Uint8Array; iterations: number },
): Promise<SrpVerifier> {
  const salt = options.salt ?? randomBytes(SRP_SALT_BYTES)
  const x = await pbkdf2Async(new TextEncoder().encode(password), salt, options.iterations)
  const exponent = BigInt(`0x${bytesToHex(x)}`)
  const verifier = modPow(GENERATOR, exponent, PRIME)
  return {
    verifier: padHex(verifier),
    salt: bytesToHex(salt),
    iterations: options.iterations,
  }
}

export interface SrpClientSession {
  /** A: the client public key, hex. */
  clientPublic: string
  /** Finishes the handshake once the device replies with salt, B, and u. */
  finish(challenge: {
    salt: string
    serverPublic: string
    iterations: number
  }): Promise<{ clientProof: string; expectedServerProof: string }>
}

/**
 * Starts a login. `x` comes from deriveVerifier's inputs, so the same password
 * and salt always produce the same session proof.
 */
export async function beginSrpLogin(
  password: string,
  options: { salt: Uint8Array; iterations: number },
): Promise<SrpClientSession> {
  const exponentBytes = randomBytes(32)
  const a = BigInt(`0x${bytesToHex(exponentBytes)}`)
  const clientPublic = modPow(GENERATOR, a, PRIME)

  const xBytes = await pbkdf2Async(new TextEncoder().encode(password), options.salt, options.iterations)
  const x = BigInt(`0x${bytesToHex(xBytes)}`)

  return {
    clientPublic: padHex(clientPublic),
    async finish(challenge) {
      const salt = hexToBytes(challenge.salt)
      const serverPublic = paddedHexToBigInt(challenge.serverPublic)
      const k = multiplier()
      const u = BigInt(`0x${bytesToHex(hashConcat(
        hexToBytes(padHex(clientPublic)),
        hexToBytes(padHex(serverPublic)),
      ))}`)
      if (u === 0n) {
        throw new Error('device returned an invalid SRP scramble value')
      }

      const base = (serverPublic - (k * modPow(GENERATOR, x, PRIME)) % PRIME + PRIME) % PRIME
      const shared = modPow(base, a + u * x, PRIME)
      const key = sessionKey(shared)

      const identityHash = sha256(new TextEncoder().encode(SRP_IDENTITY))
      const groupHash = sha256(hexToBytes(PRIME_HEX))
      const generatorHash = sha256(hexToBytes(GENERATOR.toString(16).padStart(SRP_GROUP_BYTES * 2, '0')))
      const groupDigestXor = new Uint8Array(SRP_HASH_BYTES)
      for (let index = 0; index < SRP_HASH_BYTES; index += 1) {
        groupDigestXor[index] = groupHash[index] ^ generatorHash[index]
      }
      const clientProof = hashConcat(
        groupDigestXor,
        identityHash,
        salt,
        hexToBytes(padHex(clientPublic)),
        hexToBytes(padHex(serverPublic)),
        key,
      )
      const expectedServerProof = hashConcat(
        hexToBytes(padHex(clientPublic)),
        clientProof,
        key,
      )

      return {
        clientProof: bytesToHex(clientProof),
        expectedServerProof: bytesToHex(expectedServerProof),
      }
    },
  }
}
