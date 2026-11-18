/**
 * SHA-256 and PBKDF2-HMAC-SHA256 in plain JavaScript.
 *
 * The bridge is served over plain HTTP on a LAN address, which is not a secure
 * context, so `crypto.subtle` is unavailable exactly where this code matters.
 * WebCrypto is used when present and this implementation otherwise, so the
 * derived value is identical either way.
 *
 * This is not generic-purpose crypto glue: it is the minimum needed to derive
 * the SRP verifier and its login proof in the browser.
 */

const K = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
])

const INITIAL_STATE = new Uint32Array([
  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
])

/**
 * Incremental SHA-256 with cheap cloning, so HMAC reuses its padded key blocks
 * instead of re-hashing them on every call.
 */
class Sha256 {
  private state = new Uint32Array(INITIAL_STATE)
  private block = new Uint8Array(64)
  private blockLength = 0
  private totalLength = 0
  private readonly words = new Uint32Array(64)

  update(data: Uint8Array): this {
    this.totalLength += data.length
    let offset = 0

    if (this.blockLength > 0) {
      const needed = 64 - this.blockLength
      const take = Math.min(needed, data.length)
      this.block.set(data.subarray(0, take), this.blockLength)
      this.blockLength += take
      offset = take
      if (this.blockLength === 64) {
        this.compress(this.block, 0)
        this.blockLength = 0
      }
    }

    while (offset + 64 <= data.length) {
      this.compress(data, offset)
      offset += 64
    }

    if (offset < data.length) {
      this.block.set(data.subarray(offset), 0)
      this.blockLength = data.length - offset
    }
    return this
  }

  clone(): Sha256 {
    const copy = new Sha256()
    copy.state.set(this.state)
    copy.block.set(this.block)
    copy.blockLength = this.blockLength
    copy.totalLength = this.totalLength
    return copy
  }

  digest(): Uint8Array {
    const final = this.clone()
    const bitLength = final.totalLength * 8
    // Padding: 0x80, zeros, then the 64-bit big-endian bit length, so the
    // total lands on a block boundary.
    const paddingLength = final.blockLength < 56 ? 64 - final.blockLength : 128 - final.blockLength
    const padding = new Uint8Array(paddingLength)
    padding[0] = 0x80
    const paddingView = new DataView(padding.buffer)
    // Message length up to 2^53 bits; the high word stays zero in practice.
    paddingView.setUint32(paddingLength - 8, Math.floor(bitLength / 0x100000000), false)
    paddingView.setUint32(paddingLength - 4, bitLength >>> 0, false)
    final.update(padding)

    const out = new Uint8Array(32)
    const view = new DataView(out.buffer)
    for (let index = 0; index < 8; index += 1) {
      view.setUint32(index * 4, final.state[index], false)
    }
    return out
  }

  private compress(data: Uint8Array, offset: number): void {
    const w = this.words
    for (let index = 0; index < 16; index += 1) {
      w[index] = (data[offset + index * 4] << 24) |
        (data[offset + index * 4 + 1] << 16) |
        (data[offset + index * 4 + 2] << 8) |
        data[offset + index * 4 + 3]
    }
    for (let index = 16; index < 64; index += 1) {
      const a = w[index - 15]
      const b = w[index - 2]
      const s0 = ((a >>> 7) | (a << 25)) ^ ((a >>> 18) | (a << 14)) ^ (a >>> 3)
      const s1 = ((b >>> 17) | (b << 15)) ^ ((b >>> 19) | (b << 13)) ^ (b >>> 10)
      w[index] = (w[index - 16] + s0 + w[index - 7] + s1) | 0
    }

    let h0 = this.state[0]
    let h1 = this.state[1]
    let h2 = this.state[2]
    let h3 = this.state[3]
    let h4 = this.state[4]
    let h5 = this.state[5]
    let h6 = this.state[6]
    let h7 = this.state[7]

    for (let index = 0; index < 64; index += 1) {
      const s1 = ((h4 >>> 6) | (h4 << 26)) ^ ((h4 >>> 11) | (h4 << 21)) ^ ((h4 >>> 25) | (h4 << 7))
      const choose = (h4 & h5) ^ (~h4 & h6)
      const temp1 = (h7 + s1 + choose + K[index] + w[index]) | 0
      const s0 = ((h0 >>> 2) | (h0 << 30)) ^ ((h0 >>> 13) | (h0 << 19)) ^ ((h0 >>> 22) | (h0 << 10))
      const majority = (h0 & h1) ^ (h0 & h2) ^ (h1 & h2)
      const temp2 = (s0 + majority) | 0

      h7 = h6
      h6 = h5
      h5 = h4
      h4 = (h3 + temp1) | 0
      h3 = h2
      h2 = h1
      h1 = h0
      h0 = (temp1 + temp2) | 0
    }

    this.state[0] = (this.state[0] + h0) | 0
    this.state[1] = (this.state[1] + h1) | 0
    this.state[2] = (this.state[2] + h2) | 0
    this.state[3] = (this.state[3] + h3) | 0
    this.state[4] = (this.state[4] + h4) | 0
    this.state[5] = (this.state[5] + h5) | 0
    this.state[6] = (this.state[6] + h6) | 0
    this.state[7] = (this.state[7] + h7) | 0
  }
}

export function sha256(data: Uint8Array): Uint8Array {
  return new Sha256().update(data).digest()
}

/** HMAC-SHA256 with the padded key blocks pre-hashed once per call site. */
function prepareHmac(key: Uint8Array) {
  const padded = new Uint8Array(64)
  padded.set(key.length > 64 ? sha256(key) : key)
  const inner = new Uint8Array(64)
  const outer = new Uint8Array(64)
  for (let index = 0; index < 64; index += 1) {
    inner[index] = padded[index] ^ 0x36
    outer[index] = padded[index] ^ 0x5c
  }
  const innerState = new Sha256().update(inner)
  const outerState = new Sha256().update(outer)
  return {
    inner: innerState,
    outer: outerState,
  }
}

function hmacSha256(key: Uint8Array, data: Uint8Array): Uint8Array {
  const prepared = prepareHmac(key)
  const innerDigest = prepared.inner.clone().update(data).digest()
  return prepared.outer.clone().update(innerDigest).digest()
}

/**
 * PBKDF2-HMAC-SHA256 for a single 32-byte output block, which is all the SRP
 * verifier needs. Prefix states are computed once instead of per iteration.
 */
export function pbkdf2Sha256(password: Uint8Array, salt: Uint8Array, iterations: number): Uint8Array {
  const prepared = prepareHmac(password)
  const first = new Uint8Array(salt.length + 4)
  first.set(salt)
  first[salt.length] = 0
  first[salt.length + 1] = 0
  first[salt.length + 2] = 0
  first[salt.length + 3] = 1

  let u = prepared.outer.clone().update(prepared.inner.clone().update(first).digest()).digest()
  const out = u.slice()

  for (let round = 1; round < iterations; round += 1) {
    u = prepared.outer.clone().update(prepared.inner.clone().update(u).digest()).digest()
    for (let index = 0; index < 32; index += 1) {
      out[index] ^= u[index]
    }
  }
  return out
}

function hasWebCrypto(): boolean {
  return typeof crypto !== 'undefined' && crypto.subtle != null && crypto.subtle.digest != null
}

/**
 * PBKDF2-HMAC-SHA256 for one 32-byte block, using native WebCrypto when the
 * page is a secure context. Native wins by an order of magnitude, so it is
 * always preferred.
 */
export async function pbkdf2Async(password: Uint8Array, salt: Uint8Array, iterations: number): Promise<Uint8Array> {
  if (hasWebCrypto() && typeof crypto.subtle.importKey === 'function') {
    // WebCrypto wants ArrayBuffer-backed views specifically.
    const passwordBuffer = password.slice().buffer
    const saltBuffer = salt.slice().buffer
    const key = await crypto.subtle.importKey('raw', passwordBuffer, 'PBKDF2', false, ['deriveBits'])
    const bits = await crypto.subtle.deriveBits(
      { name: 'PBKDF2', salt: saltBuffer, iterations, hash: 'SHA-256' },
      key,
      256,
    )
    return new Uint8Array(bits)
  }
  return pbkdf2Sha256(password, salt, iterations)
}

export function randomBytes(length: number): Uint8Array {
  const out = new Uint8Array(length)
  crypto.getRandomValues(out)
  return out
}
