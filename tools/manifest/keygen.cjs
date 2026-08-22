'use strict'

/*
    keygen.cjs - mint an ed25519 manifest-signing keypair.

    One keypair per OWNER, generated and held on that owner's machine - never in
    the repo, never on the NAS (docs/MANIFEST-ARCHITECTURE.md, section 2 and F4).
    The entire value of signing the manifest is that the always-on,
    internet-adjacent boxes that can WRITE release assets cannot also SPEAK for
    them; a secret key that ever touches one of those boxes gives that back.

    Usage:
        node keygen.cjs

    The secret is written to the file named by NCO_MANIFEST_KEY_FILE, or to
    ~/.nco-manifest-key when the variable is not set. The PUBLIC line printed on
    stdout is the only part that travels: it gets pinned in the launcher and
    posted to the coordination feed so the other owner can confirm it.
*/

const crypto = require('node:crypto')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')

// tweetnacl is borrowed from the launcher's node_modules rather than vendored a
// second time: the launcher verifies with the exact library that signed, so one
// installed copy keeps the two sides incapable of drifting apart.
let nacl
try {
  nacl = require(require('path').join(__dirname, '..', '..', 'code', 'launcher-lite', 'node_modules', 'tweetnacl'))
} catch (err) {
  console.error('tweetnacl is not installed. Open a terminal in code\\launcher-lite and run "pnpm install", then run this again.')
  process.exit(1)
}

const keyFile = process.env.NCO_MANIFEST_KEY_FILE || path.join(os.homedir(), '.nco-manifest-key')

// Refusing to overwrite is not politeness. Regenerating over an existing key
// silently retires the public half every launcher has pinned - every manifest
// signed afterwards would verify against nothing, and the failure would surface
// on players' machines, not here. Retiring a key is a launcher release, not a
// file write.
if (fs.existsSync(keyFile)) {
  console.error(`There is already a key at ${keyFile} - refusing to overwrite it.`)
  console.error('If you really mean to retire that key, move the file aside yourself, and remember that the launcher pins the PUBLIC half: a new key signs nothing anyone accepts until a launcher release pins it.')
  process.exit(1)
}

const pair = nacl.sign.keyPair()

// The key id is a fingerprint, not a secret: the first 8 hex chars of
// sha256(publicKey). It rides in every signature container so a verifier
// holding several pinned keys knows which to try first, and so a human reading
// a .sig can say whose key produced it.
const keyId = crypto.createHash('sha256').update(Buffer.from(pair.publicKey)).digest('hex').slice(0, 8)

const secretLine = `ed25519-secret:${Buffer.from(pair.secretKey).toString('base64')}:${keyId}`
const publicLine = `ed25519-public:${Buffer.from(pair.publicKey).toString('base64')}:${keyId}`

// The mode matters on POSIX and is ignored by Windows ACLs - acceptable,
// because on Windows the profile directory is already the user's own boundary.
fs.writeFileSync(keyFile, secretLine + '\n', { encoding: 'utf8', mode: 0o600 })

// The public line goes to stdout alone so `node keygen.cjs` can be piped or
// copy-pasted without trimming commentary; the guidance goes to stderr.
console.log(publicLine)
console.error('')
console.error(`Secret key written to ${keyFile} (key id ${keyId}).`)
console.error('The ed25519-public line above is what gets pinned in the launcher and posted to the coordination feed.')
console.error('The secret file must never enter the repo or the NAS. It lives on this machine and nowhere else.')
