/**
 * manifest.selftest.mjs - exercises every function in manifest.js under plain
 * node, no Electron. Run with:  node manifest.selftest.mjs
 *
 * The signing tests use a throwaway keypair generated in memory for this run
 * only - no key material is ever written to disk, because a test key that
 * exists as a file is a test key somebody will eventually trust by accident.
 *
 * The digest test carries a fixed vector whose expected hash is computed here,
 * independently, from the canonical string spelled out by hand - so if
 * computeInstallDigest ever drifts from THE canonical form the C++ server
 * mirrors, this file fails before the handshake does.
 */

import crypto from 'node:crypto'
import { writeFileSync, unlinkSync, mkdtempSync, rmSync } from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import nacl from 'tweetnacl'

import {
  parseSigLine,
  parsePubkeyLine,
  verifyManifestSignature,
  evaluateManifestState,
  compareManifestVersion,
  computeInstallDigest,
  buildOwnershipIndex,
  findConflicts,
  classifyUnmanaged,
  extractDetectionMarkers,
  isTailnetHost,
  sha256Hex,
  hashFileSha256,
  checkMinLauncher,
  normalizeOwnedPath
} from './manifest.js'

let passed = 0
let failed = 0

function check (name, actual, expected) {
  const a = JSON.stringify(actual)
  const e = JSON.stringify(expected)
  if (a === e) {
    passed++
  } else {
    failed++
    console.error(`FAIL ${name}\n  expected: ${e}\n  actual:   ${a}`)
  }
}

function checkThrows (name, fn, messagePart) {
  try {
    fn()
    failed++
    console.error(`FAIL ${name}: expected a throw, got a return`)
  } catch (err) {
    if (messagePart && !String(err.message).includes(messagePart)) {
      failed++
      console.error(`FAIL ${name}: threw, but message "${err.message}" lacks "${messagePart}"`)
    } else {
      passed++
    }
  }
}

// ---------------------------------------------------------------------------
// Throwaway keypair + container lines
// ---------------------------------------------------------------------------

const keypair = nacl.sign.keyPair()
const otherKeypair = nacl.sign.keyPair()

const pubkeyLine = (kp, keyid) => `ed25519-public:${Buffer.from(kp.publicKey).toString('base64')}:${keyid}`
const sigLine = (bytes, kp, keyid) => `ed25519:${Buffer.from(nacl.sign.detached(bytes, kp.secretKey)).toString('base64')}:${keyid}`

const pinned = [pubkeyLine(keypair, 'cam'), pubkeyLine(otherKeypair, 'zeldfep')]

// --- parseSigLine / parsePubkeyLine ----------------------------------------

const manifestBytesForSig = Buffer.from('{"probe":true}', 'utf8')
const goodSig = sigLine(manifestBytesForSig, keypair, 'cam')

check('parseSigLine: roundtrip keyid', parseSigLine(goodSig)?.keyid, 'cam')
check('parseSigLine: roundtrip length', parseSigLine(goodSig)?.bytes.length, 64)
check('parseSigLine: tolerates trailing newline', parseSigLine(goodSig + '\n')?.keyid, 'cam')
check('parseSigLine: rejects wrong algorithm', parseSigLine(goodSig.replace('ed25519:', 'rsa:')), null)
check('parseSigLine: rejects pubkey prefix', parseSigLine('ed25519-public:' + goodSig.slice('ed25519:'.length)), null)
check('parseSigLine: rejects short signature (32 bytes)',
  parseSigLine(`ed25519:${Buffer.alloc(32).toString('base64')}:cam`), null)
check('parseSigLine: rejects missing keyid', parseSigLine(goodSig.slice(0, goodSig.lastIndexOf(':') + 1)), null)
check('parseSigLine: rejects extra field', parseSigLine(goodSig + ':extra'), null)
check('parseSigLine: rejects bad base64 chars', parseSigLine('ed25519:not base64!!:cam'), null)
check('parseSigLine: rejects empty string', parseSigLine(''), null)
check('parseSigLine: rejects non-string', parseSigLine(null), null)

const goodPubkey = pubkeyLine(keypair, 'cam')
check('parsePubkeyLine: roundtrip keyid', parsePubkeyLine(goodPubkey)?.keyid, 'cam')
check('parsePubkeyLine: roundtrip length', parsePubkeyLine(goodPubkey)?.bytes.length, 32)
check('parsePubkeyLine: rejects sig prefix', parsePubkeyLine(goodSig), null)
check('parsePubkeyLine: rejects 64-byte blob',
  parsePubkeyLine(`ed25519-public:${Buffer.alloc(64).toString('base64')}:cam`), null)

// --- verifyManifestSignature ------------------------------------------------

check('verify: genuine signature verifies, names the pinned key',
  verifyManifestSignature(manifestBytesForSig, goodSig, pinned), { ok: true, keyid: 'cam' })

check('verify: second owner key also verifies',
  verifyManifestSignature(manifestBytesForSig, sigLine(manifestBytesForSig, otherKeypair, 'zeldfep'), pinned),
  { ok: true, keyid: 'zeldfep' })

check('verify: tampered bytes fail',
  verifyManifestSignature(Buffer.from('{"probe":false}', 'utf8'), goodSig, pinned), { ok: false, keyid: null })

check('verify: unpinned key fails',
  verifyManifestSignature(manifestBytesForSig, sigLine(manifestBytesForSig, nacl.sign.keyPair(), 'cam'), pinned),
  { ok: false, keyid: null })

// The keyid in the sig line is a hint, not proof - the math decides, and the
// PINNED key's id is what gets reported.
check('verify: mislabeled keyid still verifies by math',
  verifyManifestSignature(manifestBytesForSig, sigLine(manifestBytesForSig, keypair, 'wrong-label'), pinned),
  { ok: true, keyid: 'cam' })

check('verify: malformed pinned line is skipped, not fatal',
  verifyManifestSignature(manifestBytesForSig, goodSig, ['garbage', pubkeyLine(keypair, 'cam')]),
  { ok: true, keyid: 'cam' })

check('verify: no pinned keys fails',
  verifyManifestSignature(manifestBytesForSig, goodSig, []), { ok: false, keyid: null })

check('verify: malformed sig line fails',
  verifyManifestSignature(manifestBytesForSig, 'nonsense', pinned), { ok: false, keyid: null })

// ---------------------------------------------------------------------------
// §2.1 state machine
// ---------------------------------------------------------------------------

function signedManifest (manifest, kp = keypair, keyid = 'cam') {
  const bytes = Buffer.from(JSON.stringify(manifest), 'utf8')
  return { bytes, sig: sigLine(bytes, kp, keyid) }
}

const current = signedManifest({ schema: 1, manifestVersion: '2026.08.21.02' })
const older = signedManifest({ schema: 1, manifestVersion: '2026.08.21.01' })
const cachedEntry = { manifestVersion: '2026.08.21.02', bytes: current.bytes }

// valid, first ever manifest - nothing cached
check('state: valid with no cache',
  (() => {
    const r = evaluateManifestState({ fetchedBytes: older.bytes, fetchedSigText: older.sig, pinnedPubkeyLines: pinned })
    return [r.state, r.manifest?.manifestVersion, r.warning, r.keyid]
  })(),
  ['valid', '2026.08.21.01', null, 'cam'])

// valid, newer than cache
check('state: valid, newer than cache',
  (() => {
    const newer = signedManifest({ schema: 1, manifestVersion: '2026.08.21.03' })
    const r = evaluateManifestState({ fetchedBytes: newer.bytes, fetchedSigText: newer.sig, cached: cachedEntry, pinnedPubkeyLines: pinned })
    return [r.state, r.manifest?.manifestVersion]
  })(),
  ['valid', '2026.08.21.03'])

// valid, same version as cache - a re-fetch, not a rollback
check('state: valid, equal to cache',
  evaluateManifestState({ fetchedBytes: current.bytes, fetchedSigText: current.sig, cached: cachedEntry, pinnedPubkeyLines: pinned }).state,
  'valid')

// rollback - older signed manifest refused, cache stays in force
check('state: rollback keeps cached manifest',
  (() => {
    const r = evaluateManifestState({ fetchedBytes: older.bytes, fetchedSigText: older.sig, cached: cachedEntry, pinnedPubkeyLines: pinned })
    return [r.state, r.manifest?.manifestVersion, typeof r.warning]
  })(),
  ['rollback', '2026.08.21.02', 'string'])

// invalid: bad signature, and crucially NO fallback to the cache
check('state: invalid on bad signature, no cache fallback',
  (() => {
    const r = evaluateManifestState({
      fetchedBytes: Buffer.from(current.bytes.toString('utf8') + ' '),
      fetchedSigText: current.sig,
      cached: cachedEntry,
      pinnedPubkeyLines: pinned
    })
    return [r.state, r.manifest, typeof r.warning]
  })(),
  ['invalid', null, 'string'])

check('state: invalid on missing signature',
  evaluateManifestState({ fetchedBytes: current.bytes, cached: cachedEntry, pinnedPubkeyLines: pinned }).state,
  'invalid')

check('state: invalid when signed bytes are not JSON',
  (() => {
    const bytes = Buffer.from('not json at all', 'utf8')
    const sig = sigLine(bytes, keypair, 'cam')
    return evaluateManifestState({ fetchedBytes: bytes, fetchedSigText: sig, cached: cachedEntry, pinnedPubkeyLines: pinned }).state
  })(),
  'invalid')

check('state: invalid when manifest has no manifestVersion',
  (() => {
    const { bytes, sig } = signedManifest({ schema: 1 })
    return evaluateManifestState({ fetchedBytes: bytes, fetchedSigText: sig, pinnedPubkeyLines: pinned }).state
  })(),
  'invalid')

// unreachable: cached manifest rides, offline-tolerant
check('state: unreachable with cache -> cached',
  (() => {
    const r = evaluateManifestState({ fetchError: new Error('ETIMEDOUT'), cached: cachedEntry, pinnedPubkeyLines: pinned })
    return [r.state, r.manifest?.manifestVersion]
  })(),
  ['cached', '2026.08.21.02'])

check('state: unreachable with no cache -> unverified-offline',
  (() => {
    const r = evaluateManifestState({ fetchError: new Error('ETIMEDOUT'), pinnedPubkeyLines: pinned })
    return [r.state, r.manifest]
  })(),
  ['unverified-offline', null])

// absent: migration fallback, caller uses the legacy path
check('state: absent',
  (() => {
    const r = evaluateManifestState({ fetchedBytes: null, pinnedPubkeyLines: pinned })
    return [r.state, r.manifest, typeof r.warning]
  })(),
  ['absent', null, 'string'])

// ---------------------------------------------------------------------------
// compareManifestVersion
// ---------------------------------------------------------------------------

check('compare: serial increments', compareManifestVersion('2026.08.21.01', '2026.08.21.02'), -1)
check('compare: numeric not string ("10" > "9")', compareManifestVersion('2026.08.21.10', '2026.08.21.9'), 1)
check('compare: equal', compareManifestVersion('2026.08.21.01', '2026.08.21.01'), 0)
check('compare: missing field is zero', compareManifestVersion('2026.08.21', '2026.08.21.00'), 0)
check('compare: year rolls over', compareManifestVersion('2027.01.01.01', '2026.12.31.99'), 1)

// ---------------------------------------------------------------------------
// computeInstallDigest - fixed vector against THE canonical form
// ---------------------------------------------------------------------------

const shaA = 'a'.repeat(64)
const shaB = 'b'.repeat(64)
const shaC = 'c'.repeat(64)
const shaD = 'd'.repeat(64)
const shaPayload = 'e'.repeat(64)

const digestManifest = {
  manifestVersion: '2026.08.21.01',
  client: { payload: { archive: { name: 'ModPayload.zip', sha256: shaPayload, size: 1 } } },
  components: [
    // Deliberately NOT in id order - the function must sort.
    { id: 'codeware', version: '1.18.0', required: true, audience: 'all', archive: { sha256: shaA } },
    { id: 'archive_xl', version: '1.5.0', required: true, audience: 'all', archive: { sha256: shaB } },
    // Optional and dev-only never enter the digest.
    { id: 'audioware', version: '1.4.2', required: false, audience: 'all', archive: { sha256: shaC } },
    { id: 'dev_tool', version: '0.1.0', required: true, audience: 'dev', archive: { sha256: shaD } }
  ]
}

// The canonical string, spelled out by hand exactly as the doc (and the C++
// mirror) define it - if this and the implementation ever disagree, the
// implementation is wrong.
const canonical = [
  `archive_xl:1.5.0:${shaB}`,
  `codeware:1.18.0:${shaA}`,
  `payload:${shaPayload}`,
  'manifest:2026.08.21.01'
].join('\n')

const expectedDigest = crypto.createHash('sha256').update(Buffer.from(canonical, 'utf8')).digest('hex')

check('digest: matches independently computed canonical vector', computeInstallDigest(digestManifest), expectedDigest)
check('digest: 64 lowercase hex chars', /^[0-9a-f]{64}$/.test(computeInstallDigest(digestManifest)), true)
check('digest: deterministic', computeInstallDigest(digestManifest), computeInstallDigest(digestManifest))

check('digest: component order in the manifest is irrelevant',
  computeInstallDigest({ ...digestManifest, components: [...digestManifest.components].reverse() }),
  expectedDigest)

check('digest: optional component changes do not move it',
  computeInstallDigest({
    ...digestManifest,
    components: digestManifest.components.map((c) => c.id === 'audioware' ? { ...c, version: '9.9.9' } : c)
  }),
  expectedDigest)

check('digest: dev-only component changes do not move it',
  computeInstallDigest({
    ...digestManifest,
    components: digestManifest.components.filter((c) => c.id !== 'dev_tool')
  }),
  expectedDigest)

check('digest: a required version bump moves it',
  computeInstallDigest({
    ...digestManifest,
    components: digestManifest.components.map((c) => c.id === 'codeware' ? { ...c, version: '1.18.1' } : c)
  }) !== expectedDigest,
  true)

// Sort is on the ID, not the whole line: by id, mod2 < mod24; by whole line
// "mod2:" > "mod24:" because ':' outranks '4'.
check('digest: sorted by id, not by line',
  computeInstallDigest({
    manifestVersion: '2026.08.21.01',
    client: { payload: { archive: { sha256: shaPayload } } },
    components: [
      { id: 'mod24', version: '1.0', required: true, audience: 'all', archive: { sha256: shaB } },
      { id: 'mod2', version: '1.0', required: true, audience: 'all', archive: { sha256: shaA } }
    ]
  }),
  crypto.createHash('sha256').update(Buffer.from(
    [`mod2:1.0:${shaA}`, `mod24:1.0:${shaB}`, `payload:${shaPayload}`, 'manifest:2026.08.21.01'].join('\n'), 'utf8'
  )).digest('hex'))

checkThrows('digest: qualifying component without archive.sha256 throws',
  () => computeInstallDigest({
    ...digestManifest,
    components: [...digestManifest.components, { id: 'broken', version: '1.0', required: true, audience: 'all' }]
  }),
  'archive.sha256')

checkThrows('digest: qualifying component without version throws',
  () => computeInstallDigest({
    ...digestManifest,
    components: [...digestManifest.components, { id: 'broken', required: true, audience: 'all', archive: { sha256: shaA } }]
  }),
  'version')

checkThrows('digest: missing payload hash throws',
  () => computeInstallDigest({ ...digestManifest, client: {} }),
  'client.payload.archive.sha256')

checkThrows('digest: missing manifestVersion throws',
  () => computeInstallDigest({ ...digestManifest, manifestVersion: undefined }),
  'manifestVersion')

// ---------------------------------------------------------------------------
// Ownership index + conflicts
// ---------------------------------------------------------------------------

const ownershipManifest = {
  manifestVersion: '2026.08.21.01',
  client: {
    payload: {
      archive: { sha256: shaPayload },
      files: [{ path: 'red4ext/plugins/zzzCyberpunkMP/CyberpunkMP.dll', sha256: shaA, size: 1 }]
    }
  },
  components: [
    {
      id: 'cyberpunk_multiplayer',
      class: 'payload',
      required: true,
      audience: 'all',
      version: '0.3.96',
      archive: { sha256: shaA },
      files: []
    },
    {
      id: 'audioware',
      class: 'nexus',
      required: false,
      audience: 'all',
      version: '1.4.2',
      nexus: { modId: 12001, fileId: 98765 },
      archive: { sha256: shaC },
      files: [{ path: 'red4ext/plugins/audioware/audioware.dll', sha256: shaC, size: 1 }]
    },
    {
      // Declares the same file as audioware, in different casing and
      // separators - this must surface as a conflict, not hide behind spelling.
      id: 'clasher',
      class: 'nexus',
      required: false,
      audience: 'all',
      version: '1.0.0',
      archive: { sha256: shaB },
      files: [{ path: 'Red4ext\\Plugins\\Audioware\\audioware.dll', sha256: shaB, size: 1 }]
    }
  ],
  compatibility: {
    entries: [
      {
        a: { id: 'cyberpunk_multiplayer' },
        b: { id: 'cyber_engine_tweaks' },
        status: 'known_incompatible',
        severity: 'critical',
        detection: { anyOf: ['bin/x64/version.dll', 'bin/x64/plugins/cyber_engine_tweaks/'] }
      }
    ]
  }
}

const installedRecords = {
  // Old-shape record (string files) for a mod the manifest also declares - the
  // record and its component are ONE owner, unified by nexusModId, no conflict.
  12001: {
    name: 'Audioware',
    fileId: 98765,
    version: '1.4.2',
    files: ['red4ext/plugins/audioware/audioware.dll'],
    at: 1755812168000
  },
  // New-shape record ({path} objects) for a mod the manifest does not know.
  34567: {
    id: 'red_data',
    name: 'RedData',
    fileId: 11111,
    version: '0.9.1',
    files: [{ path: 'red4ext/plugins/red_data/red_data.dll', sha256: shaD, size: 1 }],
    at: 1755812168000
  }
}

const index = buildOwnershipIndex(ownershipManifest, installedRecords)

check('index: payload files owned by the payload component',
  index.get('red4ext/plugins/zzzcyberpunkmp/cyberpunkmp.dll'), ['cyberpunk_multiplayer'])

check('index: record unified with its manifest component by nexusModId',
  index.get('red4ext/plugins/audioware/audioware.dll'), ['audioware', 'clasher'])

check('index: new-shape record files land under the record id',
  index.get('red4ext/plugins/red_data/red_data.dll'), ['red_data'])

check('index: path normalization folds case and separators',
  normalizeOwnedPath('Red4ext\\Plugins\\Audioware\\audioware.dll'), 'red4ext/plugins/audioware/audioware.dll')

const conflicts = findConflicts(index)
check('conflicts: exactly the clash, both owners named',
  conflicts, [{ path: 'red4ext/plugins/audioware/audioware.dll', owners: ['audioware', 'clasher'] }])

check('conflicts: none on a clean index',
  findConflicts(buildOwnershipIndex({
    ...ownershipManifest,
    components: ownershipManifest.components.filter((c) => c.id !== 'clasher')
  }, installedRecords)),
  [])

// ---------------------------------------------------------------------------
// Unmanaged classification + detection markers
// ---------------------------------------------------------------------------

check('markers: CET entry extracted with the non-component side',
  extractDetectionMarkers(ownershipManifest),
  [{ id: 'cyber_engine_tweaks', anyOf: ['bin/x64/version.dll', 'bin/x64/plugins/cyber_engine_tweaks/'] }])

const unmanaged = classifyUnmanaged({
  pluginFolders: ['Audioware', 'zzzCyberpunkMP', 'FlightControl'],
  archiveFiles: ['stray_weather.archive'],
  scriptFiles: ['stray.reds', 'r6/scripts/orphan/orphan.reds'],
  markerHits: ['cyber_engine_tweaks', 'cyberpunk_multiplayer', 'cyber_engine_tweaks']
}, ownershipManifest, index)

check('unmanaged: full classification', unmanaged, [
  { surface: 'plugin', name: 'FlightControl', status: 'unmanaged' },
  { surface: 'archive', name: 'stray_weather.archive', status: 'unmanaged' },
  { surface: 'script', name: 'stray.reds', status: 'unmanaged' },
  { surface: 'script', name: 'r6/scripts/orphan/orphan.reds', status: 'unmanaged' },
  { surface: 'marker', name: 'cyber_engine_tweaks', status: 'unmanaged' }
])

check('unmanaged: owned archive filtered out',
  classifyUnmanaged({
    archiveFiles: ['known.archive']
  }, ownershipManifest, new Map([['archive/pc/mod/known.archive', ['some_mod']]])),
  [])

check('unmanaged: empty scan yields nothing', classifyUnmanaged({}, ownershipManifest, index), [])

// ---------------------------------------------------------------------------
// isTailnetHost - the CGNAT /10 edges
// ---------------------------------------------------------------------------

check('tailnet: 100.63.255.255 (below range)', isTailnetHost('100.63.255.255'), false)
check('tailnet: 100.64.0.0 (first address)', isTailnetHost('100.64.0.0'), true)
check('tailnet: 100.127.255.255 (last address)', isTailnetHost('100.127.255.255'), true)
check('tailnet: 100.128.0.0 (above range)', isTailnetHost('100.128.0.0'), false)
check('tailnet: typical tailscale address', isTailnetHost('100.99.12.7'), true)
check('tailnet: hostname rejected, no DNS', isTailnetHost('nas.tailnet.example.com'), false)
check('tailnet: leading zero rejected (octal ambiguity)', isTailnetHost('100.064.0.1'), false)
check('tailnet: too few octets', isTailnetHost('100.64.0'), false)
check('tailnet: too many octets', isTailnetHost('100.64.0.0.0'), false)
check('tailnet: octet over 255', isTailnetHost('100.64.0.256'), false)
check('tailnet: surrounding whitespace tolerated', isTailnetHost(' 100.64.0.1 '), true)
check('tailnet: null rejected', isTailnetHost(null), false)

// ---------------------------------------------------------------------------
// checkMinLauncher
// ---------------------------------------------------------------------------

const gated = { client: { minLauncher: '0.3.93' } }
check('minLauncher: older launcher refused', checkMinLauncher(gated, '0.3.92'), { ok: false, required: '0.3.93' })
check('minLauncher: exact version passes', checkMinLauncher(gated, '0.3.93'), { ok: true, required: '0.3.93' })
check('minLauncher: newer minor passes', checkMinLauncher(gated, '0.4.0'), { ok: true, required: '0.3.93' })
check('minLauncher: newer major passes', checkMinLauncher(gated, '1.0.0'), { ok: true, required: '0.3.93' })
check('minLauncher: v-prefix tolerated', checkMinLauncher(gated, 'v0.3.93'), { ok: true, required: '0.3.93' })
check('minLauncher: no gate in manifest passes', checkMinLauncher({ client: {} }, '0.0.1'), { ok: true, required: null })

// ---------------------------------------------------------------------------
// Hash helpers
// ---------------------------------------------------------------------------

check('sha256Hex: empty-input test vector',
  sha256Hex(Buffer.alloc(0)), 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855')

check('sha256Hex: "abc" test vector',
  sha256Hex(Buffer.from('abc', 'utf8')), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')

const tmpDir = mkdtempSync(path.join(os.tmpdir(), 'nco-manifest-selftest-'))
const tmpFile = path.join(tmpDir, 'probe.bin')
try {
  writeFileSync(tmpFile, 'abc')
  check('hashFileSha256: matches the buffer hash',
    hashFileSha256(tmpFile), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
} finally {
  try { unlinkSync(tmpFile) } catch { /* best effort */ }
  try { rmSync(tmpDir, { recursive: true, force: true }) } catch { /* best effort */ }
}

// ---------------------------------------------------------------------------

console.log(`\n${passed} passed, ${failed} failed`)
process.exitCode = failed === 0 ? 0 : 1
