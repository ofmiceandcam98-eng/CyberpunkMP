/**
 * manifest.js - the signed server-manifest, as pure functions.
 *
 * Everything docs/MANIFEST-ARCHITECTURE.md asks the launcher to decide about a
 * manifest - is the signature real, is this a rollback, what does the install
 * digest come out to, who owns which file - lives here, deliberately free of
 * Electron and of main.js's globals. main.js does the fetching, the caching and
 * the UI; this file only ever answers questions about bytes it is handed. That
 * split is what makes the manifest logic testable at all (manifest.selftest.mjs
 * runs it under plain node), and it keeps the security-relevant decisions in
 * one small file that can be read top to bottom.
 *
 * Nothing here touches the network, and only the two hashing helpers at the
 * bottom touch the filesystem.
 */

import crypto from 'node:crypto'
import { readFileSync } from 'node:fs'
// Pure JS ed25519, no native build step - the same reason 7zip-bin and
// node-unrar-js were chosen over their faster native cousins. Ship.ps1 signs
// with this library; verifying with the same one means the two sides can never
// disagree about what a signature is.
import nacl from 'tweetnacl'

// ---------------------------------------------------------------------------
// Signature containers
// ---------------------------------------------------------------------------

// The one-line container formats from the architecture doc §2. Deliberately NOT
// minisign: a stock .minisig is ed25519 over a Blake2b prehash plus a second
// signature over a trusted comment, which tweetnacl alone cannot verify. Raw
// detached ed25519 in a line of text keeps both the signer and this verifier
// trivial enough to audit by eye.
//
//   signature:  ed25519:<base64 signature>:<key id>
//   public key: ed25519-public:<base64 key>:<key id>
//
// Parsing is strict on purpose. These lines are the trust anchor of the whole
// pipeline; a parser that "helpfully" accepts near-misses is a parser that
// accepts things nobody signed. Anything that does not match exactly is null,
// and null is treated as tampering upstream, not as a shrug.

const SIG_LINE = /^ed25519:([A-Za-z0-9+/]+={0,2}):([A-Za-z0-9_.-]+)$/
const PUBKEY_LINE = /^ed25519-public:([A-Za-z0-9+/]+={0,2}):([A-Za-z0-9_.-]+)$/

// An ed25519 signature is exactly 64 bytes and a public key exactly 32. A
// base64 blob of any other length is not a truncation to tolerate - it is
// evidence the line was mangled or forged.
const SIGNATURE_BYTES = 64
const PUBKEY_BYTES = 32

function parseContainerLine (text, pattern, expectedBytes) {
  if (typeof text !== 'string') return null

  // A file that ends in a newline is still one line. Anything with structure
  // beyond that fails the regex and is rejected.
  const match = pattern.exec(text.trim())
  if (!match) return null

  const bytes = Buffer.from(match[1], 'base64')
  if (bytes.length !== expectedBytes) return null

  return { algorithm: 'ed25519', bytes, keyid: match[2] }
}

/** `ed25519:<base64 sig>:<keyid>` -> { algorithm, bytes, keyid }, or null. */
export function parseSigLine (text) {
  return parseContainerLine(text, SIG_LINE, SIGNATURE_BYTES)
}

/** `ed25519-public:<base64 key>:<keyid>` -> { algorithm, bytes, keyid }, or null. */
export function parsePubkeyLine (text) {
  return parseContainerLine(text, PUBKEY_LINE, PUBKEY_BYTES)
}

/**
 * Verifies a manifest against the pinned key set.
 *
 * A manifest is valid if ANY pinned key verifies it - each owner signs with
 * their own key, and the launcher does not care which owner shipped. The key id
 * in the signature line is a routing hint, tried first because it usually
 * names the right key, but it proves nothing: the signature math decides, and a
 * mislabeled-but-genuine signature still verifies. The keyid returned is the
 * PINNED key's id, because that is the name the launcher actually trusts and
 * the one the trail log should print.
 *
 * Malformed pinned lines are skipped rather than fatal - a key that cannot be
 * parsed can never verify anything, so skipping it only ever narrows what is
 * accepted, never widens it.
 */
export function verifyManifestSignature (manifestBytes, sigText, pinnedPubkeyLines) {
  const sig = parseSigLine(sigText)
  if (!sig) return { ok: false, keyid: null }

  const message = typeof manifestBytes === 'string'
    ? Buffer.from(manifestBytes, 'utf8')
    : manifestBytes
  if (!(message instanceof Uint8Array)) return { ok: false, keyid: null }

  const keys = (Array.isArray(pinnedPubkeyLines) ? pinnedPubkeyLines : [])
    .map((line) => parsePubkeyLine(line))
    .filter(Boolean)
    .sort((a, b) => (a.keyid === sig.keyid ? -1 : 0) - (b.keyid === sig.keyid ? -1 : 0))

  for (const key of keys) {
    // Signature over the EXACT file bytes - no canonicalisation, no reformat.
    // The .sig signs what was published, byte for byte, so what gets verified
    // is what was fetched, byte for byte.
    if (nacl.sign.detached.verify(message, sig.bytes, key.bytes)) {
      return { ok: true, keyid: key.keyid }
    }
  }

  return { ok: false, keyid: null }
}

// ---------------------------------------------------------------------------
// Manifest versions
// ---------------------------------------------------------------------------

/**
 * Compares two manifestVersion strings (date.serial, e.g. "2026.08.21.01").
 * Returns -1, 0 or 1.
 *
 * Zero-padded date.serial strings would compare correctly as plain strings -
 * today. The whole point of monotonicity is defending against inputs that are
 * not what we expected, so the comparison is numeric per dot-separated field
 * and does not depend on anyone remembering to zero-pad. A missing field
 * counts as zero, so "2026.08.21" and "2026.08.21.00" are the same version.
 */
export function compareManifestVersion (a, b) {
  const parts = (v) => String(v ?? '').split('.').map((p) => {
    const n = parseInt(p, 10)
    return Number.isFinite(n) ? n : 0
  })

  const pa = parts(a)
  const pb = parts(b)

  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const x = pa[i] ?? 0
    const y = pb[i] ?? 0
    if (x !== y) return x < y ? -1 : 1
  }
  return 0
}

// ---------------------------------------------------------------------------
// Availability state machine (architecture doc §2.1)
// ---------------------------------------------------------------------------

function describeError (err) {
  return (err && err.message) ? err.message : String(err)
}

// The cache the caller persists on a 'valid' verdict is { manifestVersion,
// bytes } (doc §2.1: "version + bytes"), but a caller that keeps the parsed
// object around may hand that in instead - both are accepted, because this
// function should not dictate main.js's storage format.
function readCachedManifest (cached) {
  if (!cached) return null
  if (cached.manifest && typeof cached.manifest === 'object') return cached.manifest
  if (cached.bytes != null) {
    try { return JSON.parse(Buffer.isBuffer(cached.bytes) ? cached.bytes.toString('utf8') : String(cached.bytes)) } catch { return null }
  }
  return null
}

/**
 * The §2.1 state table, as one decision. Pure: the caller fetches, the caller
 * persists; this only judges.
 *
 * Inputs:
 *   fetchedBytes      Buffer of server-manifest.json as fetched, or null/undefined
 *                     when the release simply has no manifest asset (a real 404
 *                     on the asset, not a network failure)
 *   fetchedSigText    contents of server-manifest.json.sig, or null when absent
 *   fetchError        set when the channel could not be reached at all
 *   cached            the last-accepted manifest: { manifestVersion, bytes } or
 *                     { manifestVersion, manifest }
 *   pinnedPubkeyLines the launcher's pinned `ed25519-public:...` lines
 *
 * Returns { state, manifest, warning, keyid }:
 *
 *   'valid'              signature verified, version >= last accepted. The
 *                        caller persists (manifestVersion, bytes) as the new
 *                        cache. keyid names the pinned key that verified.
 *   'rollback'           signature verified but the version went BACKWARDS.
 *                        An old signed manifest is replay material, not an
 *                        update - refused, and the cached manifest stays in
 *                        force. Warning explains.
 *   'invalid'            bytes arrived but the signature is missing, wrong, or
 *                        the signed bytes are not a manifest. This is the one
 *                        state that returns NO manifest even when a cache
 *                        exists: live tampering on the channel is exactly when
 *                        refusing to play is the point, and falling back to
 *                        anything would make the signature theatre.
 *   'cached'             channel unreachable, but a previously accepted
 *                        manifest exists - use it, may Play. Preserves today's
 *                        deliberate offline tolerance.
 *   'unverified-offline' channel unreachable and nothing ever accepted -
 *                        can't verify, can't Play, same as a first install
 *                        with no internet today.
 *   'absent'             the release genuinely has no manifest. Migration
 *                        window only: the caller falls back to the legacy
 *                        four-JSON path with an "environment unverified" note.
 */
export function evaluateManifestState ({ fetchedBytes, fetchedSigText, fetchError, cached, pinnedPubkeyLines }) {
  const cachedManifest = readCachedManifest(cached)
  const cachedVersion = cached?.manifestVersion ?? cachedManifest?.manifestVersion ?? null

  if (fetchError) {
    if (cachedManifest) {
      return {
        state: 'cached',
        manifest: cachedManifest,
        warning: `Could not reach the release channel (${describeError(fetchError)}) - ` +
                 `using the last verified manifest (${cachedVersion}).`,
        keyid: null
      }
    }
    return {
      state: 'unverified-offline',
      manifest: null,
      warning: `Could not reach the release channel (${describeError(fetchError)}) and no ` +
               'manifest has ever been verified on this machine. The environment cannot be verified offline.',
      keyid: null
    }
  }

  if (fetchedBytes == null || fetchedBytes.length === 0) {
    return {
      state: 'absent',
      manifest: null,
      warning: 'This release carries no manifest - environment unverified, using the legacy update path.',
      keyid: null
    }
  }

  // Bytes arrived, so from here every failure is tampering evidence, not an
  // availability problem. Each rejection says exactly what failed, because the
  // doc's contract is "show exactly what failed and say report this".
  const invalid = (what) => ({
    state: 'invalid',
    manifest: null,
    warning: `The manifest failed verification: ${what}. This can mean the release channel was ` +
             'tampered with. Not falling back to anything - report this.',
    keyid: null
  })

  if (typeof fetchedSigText !== 'string' || fetchedSigText.trim() === '') {
    return invalid('a manifest is published but its signature is missing')
  }

  const verdict = verifyManifestSignature(fetchedBytes, fetchedSigText, pinnedPubkeyLines)
  if (!verdict.ok) {
    return invalid(parseSigLine(fetchedSigText)
      ? 'the signature does not verify against any pinned key'
      : 'the signature file is malformed')
  }

  let manifest
  try {
    manifest = JSON.parse(Buffer.isBuffer(fetchedBytes) ? fetchedBytes.toString('utf8') : String(fetchedBytes))
  } catch {
    return invalid('the signed bytes are not valid JSON')
  }

  if (typeof manifest?.manifestVersion !== 'string' || manifest.manifestVersion === '') {
    return invalid('the manifest carries no manifestVersion')
  }

  // Monotonicity (§10 replay defense): an attacker replaying an OLD signed
  // manifest gains nothing, because older-than-accepted is refused even with a
  // perfect signature. Equal is fine - re-fetching the current manifest is the
  // common case, not a rollback.
  if (cachedVersion && compareManifestVersion(manifest.manifestVersion, cachedVersion) < 0) {
    return {
      state: 'rollback',
      manifest: cachedManifest,
      warning: `The published manifest (${manifest.manifestVersion}) is OLDER than the last accepted ` +
               `one (${cachedVersion}). Refusing the downgrade and keeping the current manifest. ` +
               'If this persists, report it - it can mean the release channel was rolled back.',
      keyid: null
    }
  }

  return { state: 'valid', manifest, warning: null, keyid: verdict.keyid }
}

// ---------------------------------------------------------------------------
// Install digest (architecture doc §7.2)
// ---------------------------------------------------------------------------

/**
 * The install_digest the handshake carries: 64 lowercase hex chars.
 *
 * Input is manifest-declared fields ONLY. Nothing the launcher learned at
 * install time enters the digest, because the server computes the same value
 * from the same manifest and anything launcher-learned would make the two
 * sides irreproducible. Optional and dev-only components never enter it, so
 * per-player variance cannot break equality.
 *
 * THE canonical form - the C++ server mirrors this byte for byte, so it is
 * fixed and any change to it is a protocol change:
 *
 *   For every component with required === true AND audience === "all",
 *   sorted by id ascending (plain byte/lexicographic order on the id string),
 *   emit one line:
 *     id + ":" + version + ":" + archive.sha256
 *   Then one line:
 *     "payload:" + client.payload.archive.sha256
 *   Then one line:
 *     "manifest:" + manifestVersion
 *   Join the lines with "\n" (no trailing newline after the last line).
 *   The digest is the lowercase-hex SHA-256 of the UTF-8 bytes of that string.
 *
 * Note the sort is on the ID, not on the whole line - the two differ once ids
 * contain digits ("mod2" sorts before "mod24" by id, after it by line, because
 * ':' outranks '4'). Field values are used verbatim; the generator emits
 * lowercase hex and canonical ids, and this function does not paper over a
 * generator that stopped doing so.
 *
 * A qualifying component missing its version or archive.sha256 is a hard
 * error. The generator guarantees both for required components, so absence
 * means a malformed manifest - and a digest computed around a hole would
 * "verify" an environment nobody defined.
 */
export function computeInstallDigest (manifest) {
  const components = Array.isArray(manifest?.components) ? manifest.components : []

  const included = components.filter((c) => c?.required === true && c?.audience === 'all')

  const lines = included
    .slice()
    .sort((a, b) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0))
    .map((c) => {
      const id = c.id
      const version = c.version
      const sha = c.archive?.sha256

      if (typeof id !== 'string' || id === '') {
        throw new Error('install digest: a required component has no id')
      }
      if (typeof version !== 'string' || version === '') {
        throw new Error(`install digest: required component "${id}" has no version`)
      }
      if (typeof sha !== 'string' || sha === '') {
        throw new Error(`install digest: required component "${id}" has no archive.sha256`)
      }
      // The canonical string is colon- and newline-delimited; an id or version
      // containing either is not encodable, and escaping would complicate the
      // C++ mirror for inputs the generator never legally produces.
      if (/[:\n]/.test(id) || /[:\n]/.test(version)) {
        throw new Error(`install digest: component "${id}" has ':' or newline in its id/version`)
      }

      return `${id}:${version}:${sha}`
    })

  const payloadSha = manifest?.client?.payload?.archive?.sha256
  if (typeof payloadSha !== 'string' || payloadSha === '') {
    throw new Error('install digest: manifest has no client.payload.archive.sha256')
  }

  const manifestVersion = manifest?.manifestVersion
  if (typeof manifestVersion !== 'string' || manifestVersion === '') {
    throw new Error('install digest: manifest has no manifestVersion')
  }

  lines.push(`payload:${payloadSha}`)
  lines.push(`manifest:${manifestVersion}`)

  return sha256Hex(Buffer.from(lines.join('\n'), 'utf8'))
}

// ---------------------------------------------------------------------------
// Ownership index and conflicts (architecture doc §5)
// ---------------------------------------------------------------------------

/**
 * One canonical spelling per path, for comparison only - never for touching
 * the disk. Windows paths are case-insensitive and arrive with whichever
 * separators and casing the archive or the manifest author used, so
 * "Red4ext\Plugins\Foo\a.dll" and "red4ext/plugins/foo/a.dll" must land on the
 * same index key or a real conflict hides behind a spelling difference (the
 * same reasoning as samePath in main.js, minus the filesystem - this index
 * compares declared paths, not live ones).
 */
export function normalizeOwnedPath (p) {
  return String(p ?? '')
    .replace(/\\/g, '/')
    .replace(/^\.\//, '')
    .replace(/^\/+/, '')
    .toLowerCase()
}

// files[] entries appear in two shapes: bare relative-path strings (every
// record written so far) and { path, sha256, size } objects (manifest entries
// and the §3.4 records that grow hashes). Both must keep working - upgrading
// the launcher must not blind it to what the old launcher installed.
function fileEntryPath (entry) {
  if (typeof entry === 'string') return entry
  if (entry && typeof entry.path === 'string') return entry.path
  return null
}

/**
 * The path -> owners index everything in §5 derives from: the union of every
 * manifest component's files[], the payload's own file list, and every install
 * record's files[].
 *
 * The subtlety worth a comment: a mod the launcher installed exists in BOTH
 * sources - the manifest declares audioware's files, and the install record
 * for Nexus mod 12001 lists the same paths. Those are one owner wearing two
 * names, not a conflict, so records are unified with their manifest component
 * (by the record's own manifest id when it has one, else by nexusModId) before
 * anything is counted. Without that, every correctly installed mod would be
 * reported as conflicting with itself.
 *
 * installedModsRecord is the mods-installed.json object: keys are Nexus mod
 * ids as strings, values are { name, fileId, version, files, at } (plus id /
 * nexusModId / hashes once records grow per §3.4).
 */
export function buildOwnershipIndex (manifest, installedModsRecord) {
  const index = new Map()

  const add = (rawPath, owner) => {
    const key = normalizeOwnedPath(fileEntryPath(rawPath))
    if (!key || !owner) return
    const owners = index.get(key)
    if (!owners) index.set(key, [owner])
    else if (!owners.includes(owner)) owners.push(owner)
  }

  const components = Array.isArray(manifest?.components) ? manifest.components : []

  for (const component of components) {
    for (const file of component?.files || []) add(file, component.id)
  }

  // The payload's authoritative file list lives at client.payload.files (§2),
  // not necessarily repeated on the payload-class component. Owned by that
  // component's id when one exists, so zzzCyberpunkMP's own files never show
  // up as unmanaged or as a stranger in a conflict report.
  const payloadOwner = components.find((c) => c?.class === 'payload')?.id || 'client_payload'
  for (const file of manifest?.client?.payload?.files || []) add(file, payloadOwner)

  // Map a record back to its manifest identity where possible (see above).
  const byNexusId = new Map()
  for (const component of components) {
    const modId = component?.nexus?.modId
    if (modId != null) byNexusId.set(String(modId), component.id)
  }

  for (const [key, record] of Object.entries(installedModsRecord || {})) {
    const owner = record?.id ||
      byNexusId.get(String(record?.nexusModId ?? key)) ||
      String(key)
    for (const file of record?.files || []) add(file, owner)
  }

  return index
}

/**
 * Every path with two or more distinct owners. Derived, never declared (§5):
 * nobody has to remember to write down a conflict, because two file lists
 * naming one path IS the conflict.
 */
export function findConflicts (ownershipIndex) {
  const conflicts = []
  for (const [path, owners] of ownershipIndex) {
    if (owners.length >= 2) conflicts.push({ path, owners: [...owners] })
  }
  return conflicts
}

// ---------------------------------------------------------------------------
// Unmanaged scan classification (architecture doc §5)
// ---------------------------------------------------------------------------

/**
 * The detection markers compatibility entries carry, for components that will
 * never have an install record - CET is the archetype: it lives in bin/x64,
 * which no record and no plugin-folder scan covers, yet it is the one
 * incompatibility this project has actually observed (a GPU hard-lock).
 *
 * Each entry names two sides; the marker identifies the side that is NOT a
 * manifest component, because a managed component is recognised by its files,
 * not by markers. If neither side is a component the entry is about two
 * outsiders and b is used by the doc's convention (a = ours, b = theirs).
 */
export function extractDetectionMarkers (manifest) {
  const componentIds = new Set((manifest?.components || []).map((c) => c?.id))
  const markers = []

  for (const entry of manifest?.compatibility?.entries || []) {
    const anyOf = entry?.detection?.anyOf
    if (!Array.isArray(anyOf) || anyOf.length === 0) continue

    const sides = [entry?.a?.id, entry?.b?.id].filter(Boolean)
    const id = sides.find((s) => !componentIds.has(s)) || sides[sides.length - 1]
    if (id) markers.push({ id, anyOf: [...anyOf] })
  }

  return markers
}

/**
 * Diffs what the scanner saw on disk against what the manifest and the install
 * records own. Anything left over is UNMANAGED - reported, never touched
 * (spec §58: remediation for a blocked unmanaged mod is a message, full stop).
 *
 * The caller scans the four surfaces (main.js knows where the game lives);
 * this decides what the findings mean:
 *
 *   pluginFolders  folder names under red4ext/plugins. A folder is managed if
 *                  any owned path lives inside it - ownership is per-file, but
 *                  RED4ext loads per-folder, so one owned file legitimises the
 *                  folder it ships in.
 *   archiveFiles   *.archive names (or paths) under archive/pc/mod
 *   scriptFiles    *.reds names (or paths) under r6/scripts - the surface F3
 *                  singles out, because one stray broken script kills every
 *                  script in the game
 *   markerHits     detection-marker matches from extractDetectionMarkers,
 *                  as ids or { id } objects
 *
 * Names in the result keep the scanner's original spelling - the report is for
 * a human who has to find the file, and a lowercased name can be hard to spot
 * in an Explorer window.
 */
export function classifyUnmanaged ({ pluginFolders, archiveFiles, scriptFiles, markerHits } = {}, manifest, ownershipIndex) {
  const index = ownershipIndex instanceof Map ? ownershipIndex : new Map()
  const ownedKeys = [...index.keys()]
  const findings = []

  for (const folder of pluginFolders || []) {
    const prefix = `red4ext/plugins/${normalizeOwnedPath(folder)}/`
    if (!ownedKeys.some((key) => key.startsWith(prefix))) {
      findings.push({ surface: 'plugin', name: folder, status: 'unmanaged' })
    }
  }

  // Archives and scripts may arrive as bare names or as paths relative to the
  // game root - the scanner should not have to care which this function wants.
  const fileFinding = (surface, roots) => (raw) => {
    const rel = normalizeOwnedPath(raw)
    const key = roots.some((root) => rel.startsWith(root)) ? rel : `${roots[0]}${rel}`
    if (!index.has(key)) findings.push({ surface, name: raw, status: 'unmanaged' })
  }

  for (const file of archiveFiles || []) fileFinding('archive', ['archive/pc/mod/'])(file)
  for (const file of scriptFiles || []) fileFinding('script', ['r6/scripts/'])(file)

  // Marker hits name components, not files. A hit whose id IS a manifest
  // component is managed by definition and not a finding; the rest are
  // reported once each, however many of their anyOf paths matched.
  const componentIds = new Set((manifest?.components || []).map((c) => c?.id))
  const seenMarkers = new Set()

  for (const hit of markerHits || []) {
    const id = typeof hit === 'string' ? hit : hit?.id
    if (!id || componentIds.has(id) || seenMarkers.has(id)) continue
    seenMarkers.add(id)
    findings.push({ surface: 'marker', name: id, status: 'unmanaged' })
  }

  return findings
}

// ---------------------------------------------------------------------------
// Host and version gates
// ---------------------------------------------------------------------------

/**
 * True iff host is an IPv4 literal inside 100.64.0.0/10 - the CGNAT range
 * Tailscale assigns from. This is the requiresTailscale check from §10: a
 * tampered server.json pointing the game (and the Discord-token-bearing coord
 * call) at a public harvester fails this before anything connects.
 *
 * Pure string parsing, deliberately no DNS: a hostname that RESOLVES into the
 * tailnet range is still a name somebody else controls the resolution of, so
 * only a literal address passes. Leading zeros are rejected too - "0100" is
 * octal to some parsers and decimal to others, and an address that different
 * software reads differently is exactly the ambiguity an allow-check must not
 * have.
 */
export function isTailnetHost (host) {
  const match = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(String(host ?? '').trim())
  if (!match) return false

  const octets = []
  for (const part of match.slice(1)) {
    if (part.length > 1 && part.startsWith('0')) return false
    const n = Number(part)
    if (n > 255) return false
    octets.push(n)
  }

  // 100.64.0.0/10: first octet 100, second octet's top two bits are 01 -
  // that is, 64 through 127 inclusive.
  return octets[0] === 100 && octets[1] >= 64 && octets[1] <= 127
}

/**
 * The minLauncher gate (§8): a manifest may require a newer launcher than the
 * one reading it, and the honest response is "update me first", not a verify
 * pass against rules this build does not know how to check. No minLauncher in
 * the manifest means no gate - old manifests must keep working.
 *
 * Comparison is numeric triples ("0.3.93"), nothing fancier - launcher
 * versions have never carried prerelease tags and the comparison should not
 * pretend to handle what does not exist.
 */
export function checkMinLauncher (manifest, appVersion) {
  const required = manifest?.client?.minLauncher || null
  if (!required) return { ok: true, required: null }

  const triple = (v) => {
    const parts = String(v ?? '').trim().replace(/^v/i, '').split('.')
    return [0, 1, 2].map((i) => {
      const n = parseInt(parts[i], 10)
      return Number.isFinite(n) ? n : 0
    })
  }

  const app = triple(appVersion)
  const min = triple(required)

  for (let i = 0; i < 3; i++) {
    if (app[i] !== min[i]) return { ok: app[i] > min[i], required }
  }
  return { ok: true, required }
}

// ---------------------------------------------------------------------------
// Hashing helpers
// ---------------------------------------------------------------------------

/** Lowercase hex sha256 of a Buffer/Uint8Array (node's hex digest is lowercase). */
export function sha256Hex (buffer) {
  return crypto.createHash('sha256').update(buffer).digest('hex')
}

/**
 * Lowercase hex sha256 of a file, read synchronously. The verify pipeline
 * hashes payload files a few megabytes at a time between UI updates; a sync
 * read keeps the call sites trivial, and the largest thing this will ever
 * hash (ModPayload.zip, ~9 MB) is well inside what a sync read handles
 * without anyone noticing.
 */
export function hashFileSha256 (path) {
  return sha256Hex(readFileSync(path))
}
