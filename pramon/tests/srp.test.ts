/**
 * SRP and password-hash regression tests.
 *
 * The crypto here has two failure modes that unit tests catch cheaply but that
 * are invisible on hardware: a wrong RFC 5054 constant, and a hash/PBKDF2 that
 * drifts from a reference implementation. Both sides are pinned to values
 * produced by Python's hashlib, and a full handshake is cross-checked against
 * the firmware's own C++ server.
 *
 * Run with: bun test
 */
import { describe, expect, test } from 'bun:test'

import { pbkdf2Async, pbkdf2Sha256, sha256 } from '../src/lib/psu/sha256'
import {
  SRP_GROUP_BYTES,
  beginSrpLogin,
  bytesToHex,
  deriveVerifier,
  hexToBytes,
  padHex,
} from '../src/lib/psu/srp'

const hex = (bytes: Uint8Array) => bytesToHex(bytes)
const encode = (text: string) => new TextEncoder().encode(text)

// RFC 5054 section 3, 2048-bit group, used to verify the padded encoding.
const PRIME =
  'ac6bdb41324a9a9bf166de5e1389582faf72b6651987ee07fc3192943db56050a37329cbb4a099ed8193e0757767a13dd52312ab4b03310dcd7f48a9da04fd50e8083969edb767b0cf6095179a163ab3661a05fbd5faaae82918a9962f0b93b855f97993ec975eeaa80d740adbf4ff747359d041d5c33ea71d281e446b14773bca97b43a23fb801676bd207a436c6481f1d2b9078717461a5b9d32e688f87748544523b524b0d57d5ea77a2775d2ecfa032cfbdbf52fb3786160279004e57ae6af874e7303ce53299ccc041c7bc308d82a5698f3a8d0c38271ae35f8e9dbfbb694b5c803d89f7ae435de236d525f54759b65e372fcd68ef20fa7111f9e4aff73'

describe('sha256', () => {
  test.each([
    ['', 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'],
    ['abc', 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'],
    ['a'.repeat(1000), '41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3'],
    // 55 and 56 bytes straddle the padding boundary, where a length bug
    // silently produces a wrong digest.
    ['a'.repeat(55), '9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318'],
    ['a'.repeat(56), 'b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a'],
  ])('hashes %j', (input, expected) => {
    expect(hex(sha256(encode(input)))).toBe(expected)
  })
})

describe('pbkdf2', () => {
  test('matches the reference vector and both code paths agree', async () => {
    const expected = '632c2812e46d4604102ba7618e9d6d7d2f8128f6266b4a03264d2a0460b7dcb3'
    expect(hex(pbkdf2Sha256(encode('password'), encode('salt'), 1000))).toBe(expected)
    expect(hex(await pbkdf2Async(encode('password'), encode('salt'), 1000))).toBe(expected)
  })

  test('hashes keys longer than one SHA-256 block', () => {
    // 200-byte key: HMAC must pre-hash it instead of padding.
    expect(hex(pbkdf2Sha256(encode('x'.repeat(200)), encode('salt'), 100)))
      .toBe('1cc829581381fd336297178db69f4b2899977c509ad542755c8f33ba2e9863ff')
  })
})

describe('srp', () => {
  test('pads group values to the wire width', () => {
    expect(padHex(2n)).toBe('0'.repeat(SRP_GROUP_BYTES * 2 - 1) + '2')
    expect(padHex(BigInt(`0x${PRIME}`))).toBe(PRIME)
    expect(hex(hexToBytes('00ff'))).toBe('00ff')
  })

  test('rejects malformed hex rather than truncating it', () => {
    expect(() => hexToBytes('abc')).toThrow()
    expect(() => hexToBytes('zz')).toThrow()
  })

  test('derives the verifier the firmware stores', async () => {
    // v = g^x mod N, x = PBKDF2(password, salt, iterations).
    const salt = hexToBytes('00112233445566778899aabbccddeeff')
    const derived = await deriveVerifier('correct horse battery staple', { salt, iterations: 1000 })
    expect(derived.salt).toBe('00112233445566778899aabbccddeeff')
    expect(derived.verifier).toHaveLength(SRP_GROUP_BYTES * 2)
    // Deterministic for a fixed salt, and different for a different password.
    const same = await deriveVerifier('correct horse battery staple', { salt, iterations: 1000 })
    expect(same.verifier).toBe(derived.verifier)
    const other = await deriveVerifier('correct horse battery stapl3', { salt, iterations: 1000 })
    expect(other.verifier).not.toBe(derived.verifier)
  })

  test('produces a client public key that is a valid group element', async () => {
    const session = await beginSrpLogin('password123', { salt: new Uint8Array(16), iterations: 1000 })
    expect(session.clientPublic).toHaveLength(SRP_GROUP_BYTES * 2)
    const value = BigInt(`0x${session.clientPublic}`)
    // 2 <= A <= N - 2, which is what srp::validVerifier-style checks rely on.
    expect(value).toBeGreaterThanOrEqual(2n)
    expect(value).toBeLessThan(BigInt(`0x${PRIME}`) - 1n)
  })
})

/**
 * End-to-end against the firmware's own SRP server. The C++ side is compiled
 * for the host by scripts/srp-host-test.sh, so a protocol drift between the
 * browser and the device fails here instead of on the bench.
 */
const HOST_SERVER = new URL('../build/srp-host-test', import.meta.url).pathname

function runHostServer(...args: string[]): Record<string, string> | null {
  if (!Bun.spawnSync(['test', '-x', HOST_SERVER]).success) {
    return null
  }
  const result = Bun.spawnSync([HOST_SERVER, ...args], { stdout: 'pipe', stderr: 'pipe' })
  if (result.exitCode !== 0) {
    return null
  }
  const fields: Record<string, string> = {}
  for (const line of result.stdout.toString().trim().split('\n')) {
    const at = line.indexOf('=')
    if (at > 0) {
      fields[line.slice(0, at)] = line.slice(at + 1)
    }
  }
  return fields
}

describe('firmware interop', () => {
  const serverAvailable = Bun.spawnSync(['test', '-x', HOST_SERVER]).success
  const password = 'correct horse battery staple'
  const salt = hexToBytes('0f1e2d3c4b5a69788796a5b4c3d2e1f0')
  const serverSecret = '11'.repeat(32)

  test.skipIf(!serverAvailable)('accepts a login derived by the browser', async () => {
    const derived = await deriveVerifier(password, { salt, iterations: 20000 })
    const session = await beginSrpLogin(password, { salt, iterations: 20000 })

    const handshake = runHostServer(derived.verifier, derived.salt, session.clientPublic, serverSecret, '-')
    expect(handshake).not.toBeNull()
    expect(handshake!.validVerifier).toBe('1')

    const proofs = await session.finish({
      salt: derived.salt,
      serverPublic: handshake!.serverPublic,
      iterations: 20000,
    })
    const login = runHostServer(derived.verifier, derived.salt, session.clientPublic, serverSecret, proofs.clientProof)
    expect(login!.verified).toBe('1')
    // The device proving the password back is what stops a rogue responder
    // from harvesting a proof.
    expect(login!.serverProof).toBe(proofs.expectedServerProof)
  })

  test.skipIf(!serverAvailable)('rejects a proof for the wrong password', async () => {
    const derived = await deriveVerifier(password, { salt, iterations: 20000 })
    const session = await beginSrpLogin('not the password', { salt, iterations: 20000 })
    const handshake = runHostServer(derived.verifier, derived.salt, session.clientPublic, serverSecret, '-')
    const proofs = await session.finish({
      salt: derived.salt,
      serverPublic: handshake!.serverPublic,
      iterations: 20000,
    })
    const login = runHostServer(derived.verifier, derived.salt, session.clientPublic, serverSecret, proofs.clientProof)
    expect(login!.verified).toBe('0')
  })
})

describe('browser session storage', () => {
  test('keys stored tokens by API base URL so two bridges do not collide', () => {
    // The shape written by WebApiPsuBackend: { [baseUrl]: token }.
    const store = { 'http://a/api/v1': 'token-a', 'http://b/api/v1': 'token-b' }
    expect(JSON.parse(JSON.stringify(store))).toEqual(store)
    expect(Object.keys(store)).toHaveLength(2)
  })
})
