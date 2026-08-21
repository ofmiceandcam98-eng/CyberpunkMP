'use strict'

/*
    sign.cjs - detached ed25519 signature over one file's exact bytes.

    Usage:
        node sign.cjs <file>

    Writes <file>.sig containing the one-line container
        ed25519:<base64 signature>:<key id>
    per docs/MANIFEST-ARCHITECTURE.md section 2. Deliberately raw detached
    ed25519 and NOT minisign: a stock .minisig signs a Blake2b-512 prehash plus
    a second global signature over the trusted comment, which tweetnacl alone
    cannot verify - and the launcher verifies with tweetnacl. Raw detached
    keeps both sides a few lines each.

    The signature covers the file's bytes as they are on disk, untouched - no
    canonicalisation, no newline normalisation. What gets uploaded is what got
    signed, so the verifier never has to guess what transformation to undo.
*/

const crypto = require('node:crypto')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')

// Shared installed copy - see keygen.cjs for why this is not vendored twice.
let nacl
try {
  nacl = require(require('path').join(__dirname, '..', '..', 'code', 'launcher-lite', 'node_modules', 'tweetnacl'))
} catch (err) {
  console.error('tweetnacl is not installed. Open a terminal in code\\launcher-lite and run "pnpm install", then run this again.')
  process.exit(1)
}

const target = process.argv[2]
if (!target) {
  console.error('Usage: node sign.cjs <file>')
  process.exit(2)
}
if (!fs.existsSync(target)) {
  console.error(`Nothing to sign: ${target} does not exist.`)
  process.exit(1)
}

const keyFile = process.env.NCO_MANIFEST_KEY_FILE || path.join(os.homedir(), '.nco-manifest-key')
if (!fs.existsSync(keyFile)) {
  console.error(`No signing key at ${keyFile}.`)
  console.error('Generate one with "node tools\\manifest\\keygen.cjs" (or point NCO_MANIFEST_KEY_FILE at an existing key file).')
  console.error('A ship without a key must die here rather than publish an unsigned manifest - that is the design, not an accident.')
  process.exit(1)
}

// The key file is one line: ed25519-secret:<base64 of 64-byte secret>:<keyid>.
// Parsing is deliberately strict - a truncated or hand-edited key that "mostly
// parses" would produce signatures nothing verifies, and that failure would
// surface on players' machines instead of here.
const rawKey = fs.readFileSync(keyFile, 'utf8').trim()
const keyParts = rawKey.split(':')
if (keyParts.length !== 3 || keyParts[0] !== 'ed25519-secret') {
  console.error(`The key file at ${keyFile} is not in the expected "ed25519-secret:<base64>:<keyid>" format. Regenerate it with keygen.cjs.`)
  process.exit(1)
}
const secretKey = Buffer.from(keyParts[1], 'base64')
if (secretKey.length !== 64) {
  console.error(`The key file at ${keyFile} does not decode to a 64-byte ed25519 secret key. It is corrupted - regenerate it with keygen.cjs.`)
  process.exit(1)
}

// An ed25519 secret key carries its public half in its last 32 bytes, so the
// key id can be recomputed and cross-checked against the one stored in the
// file. A mismatch means the file was edited or spliced together; signing with
// it would stamp signatures with a key id that names the wrong public key.
const publicKey = secretKey.subarray(32)
const keyId = crypto.createHash('sha256').update(publicKey).digest('hex').slice(0, 8)
if (keyId !== keyParts[2]) {
  console.error(`The key id inside ${keyFile} (${keyParts[2]}) does not match its own key material (${keyId}). The file is corrupted - regenerate it with keygen.cjs.`)
  process.exit(1)
}

const bytes = fs.readFileSync(target)
const signature = nacl.sign.detached(new Uint8Array(bytes), new Uint8Array(secretKey))

const sigPath = target + '.sig'
fs.writeFileSync(sigPath, `ed25519:${Buffer.from(signature).toString('base64')}:${keyId}\n`, 'utf8')

console.log(`Signed ${target} (${bytes.length} bytes) with key ${keyId} -> ${sigPath}`)
