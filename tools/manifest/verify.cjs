'use strict'

/*
    verify.cjs - check a detached ed25519 signature against pinned public keys.

    Usage:
        node verify.cjs <file> <pubkey-line-or-file-of-lines>

    Reads <file> and <file>.sig (the "ed25519:<base64>:<keyid>" container that
    sign.cjs writes) and accepts the signature if ANY of the supplied
    "ed25519-public:<base64>:<keyid>" lines verifies it - the same any-of rule
    the launcher applies to its pinned key set (docs/MANIFEST-ARCHITECTURE.md,
    section 2). The second argument is either one such line or the path to a
    file holding one per line; unrelated lines in such a file are ignored so a
    pin list can carry comments.

    Two jobs, one implementation: Ship.ps1 runs this immediately after signing
    as its publish gate (a ship that cannot verify its own signature must not
    upload), and the launcher's verification code treats this file as the
    reference for what "valid" means.

    Exit codes: 0 = valid (the matching key id is printed), anything else = not
    proven valid. There is deliberately no "probably fine" exit.
*/

const fs = require('node:fs')

// Shared installed copy - see keygen.cjs for why this is not vendored twice.
let nacl
try {
  nacl = require(require('path').join(__dirname, '..', '..', 'code', 'launcher-lite', 'node_modules', 'tweetnacl'))
} catch (err) {
  console.error('tweetnacl is not installed. Open a terminal in code\\launcher-lite and run "pnpm install", then run this again.')
  process.exit(1)
}

const target = process.argv[2]
const keysArg = process.argv[3]
if (!target || !keysArg) {
  console.error('Usage: node verify.cjs <file> <pubkey-line-or-file-of-lines>')
  process.exit(2)
}
if (!fs.existsSync(target)) {
  console.error(`Nothing to verify: ${target} does not exist.`)
  process.exit(1)
}
const sigPath = target + '.sig'
if (!fs.existsSync(sigPath)) {
  console.error(`No signature: ${sigPath} does not exist. An unsigned file does not verify - it fails.`)
  process.exit(1)
}

// The container is one line; trimming tolerates the CRLF a Windows editor or
// git checkout may have appended without weakening what is actually checked.
const sigLine = fs.readFileSync(sigPath, 'utf8').trim()
const sigParts = sigLine.split(':')
if (sigParts.length !== 3 || sigParts[0] !== 'ed25519') {
  console.error(`${sigPath} is not in the expected "ed25519:<base64>:<keyid>" format.`)
  process.exit(1)
}
const signature = Buffer.from(sigParts[1], 'base64')
if (signature.length !== 64) {
  console.error(`${sigPath} does not decode to a 64-byte ed25519 signature.`)
  process.exit(1)
}
const sigKeyId = sigParts[2]

// The argument is a key line if it looks like one, otherwise a path. A literal
// line can never be mistaken for a path (":" is not legal in Windows filenames
// past the drive letter), so the ambiguity is theoretical.
const keysText = keysArg.startsWith('ed25519-public:') ? keysArg
  : fs.existsSync(keysArg) ? fs.readFileSync(keysArg, 'utf8')
    : null
if (keysText === null) {
  console.error(`${keysArg} is neither an ed25519-public line nor a file that exists.`)
  process.exit(1)
}

const keys = []
for (const line of keysText.split(/\r?\n/)) {
  const trimmed = line.trim()
  if (!trimmed.startsWith('ed25519-public:')) continue
  const parts = trimmed.split(':')
  if (parts.length !== 3) continue
  const key = Buffer.from(parts[1], 'base64')
  if (key.length !== 32) continue
  keys.push({ keyId: parts[2], key })
}
if (keys.length === 0) {
  console.error('No usable ed25519-public lines were supplied - nothing to verify against.')
  process.exit(1)
}

// The signature's key id is a hint for humans and a fast path, never an
// authority: every supplied key is tried (id matches first), because a
// mislabeled id must not be able to reject a signature a pinned key would
// accept - and equally must not skip the check.
keys.sort((a, b) => (a.keyId === sigKeyId ? 0 : 1) - (b.keyId === sigKeyId ? 0 : 1))

const bytes = new Uint8Array(fs.readFileSync(target))
const sig = new Uint8Array(signature)
for (const candidate of keys) {
  if (nacl.sign.detached.verify(bytes, sig, new Uint8Array(candidate.key))) {
    console.log(`signature valid: ${target} verified by key ${candidate.keyId}`)
    process.exit(0)
  }
}

console.error(`signature INVALID: ${target} is not signed by any of the ${keys.length} supplied key(s) (signature claims key ${sigKeyId}).`)
process.exit(1)
