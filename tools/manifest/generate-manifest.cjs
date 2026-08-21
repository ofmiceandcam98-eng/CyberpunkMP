'use strict'

/*
    generate-manifest.cjs - build server-manifest.json from the staged tree and
    the curated source list (docs/MANIFEST-ARCHITECTURE.md, sections 2, 3, 9.1).

    The manifest is generated, never hand-edited - hand-edited hashes are wrong
    hashes. Everything a human decides lives in publish/manifest-source.json;
    everything measurable (payload hashes, prerequisite hashes, install order,
    version serial) is measured here, at ship time, from the bytes actually
    being shipped.

    Every refusal in this script is the doc's RELEASE BLOCKED semantics: a
    cycle in the dependency graph, a dependency naming no component, a schema
    violation, a missing prerequisite zip - each one means the ship stops here,
    before anything is signed or uploaded, because each one would otherwise
    surface as an undiagnosable failure on a player's machine.

    Output is deterministic: stable key order, 2-space indent, trailing
    newline, files sorted by path. Re-running on identical inputs yields
    identical bytes, which is what lets Ship.ps1's sign-then-verify gate and
    any later audit reason about the artifact instead of its timestamp.

    Usage:
        node generate-manifest.cjs
            --staged <dir>                  staged mod tree (ModPayload staging dir)
            --source <manifest-source.json> the curated component list
            --out <server-manifest.json>    where the manifest is written
            --release <vX.Y.Z>              the release this manifest ships on
            --channel <production|staging|development>
            --protocol-client <hex>         client kIdentifier for this build
            --protocol-server <hex>         server kIdentifier for this build
            [--min-launcher <ver>]          oldest launcher allowed to verify
            [--payload-zip <path>]          ModPayload.zip, for the archive entry
            [--previous-manifest <path>]    last manifest, for same-day serials

    Deliberately dependency-free: pure node:crypto does the hashing, so this
    can run anywhere Node 20+ exists - no install step between "checkout" and
    "a ship can generate its manifest". Signing (which does need tweetnacl) is
    sign.cjs's job, kept separate for exactly that reason.
*/

const crypto = require('node:crypto')
const fs = require('node:fs')
const path = require('node:path')

function usage (message) {
  console.error(message)
  console.error('Run with no arguments mis-set; see the header of this file for the full usage.')
  process.exit(2)
}

// The doc's RELEASE BLOCKED wording appears verbatim so a failed ship's log
// grep-matches the architecture document that explains what to do about it.
function blocked (message) {
  console.error(`RELEASE BLOCKED: ${message}`)
  process.exit(1)
}

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

const FLAGS = {
  '--staged': 'staged',
  '--source': 'source',
  '--out': 'out',
  '--release': 'release',
  '--channel': 'channel',
  '--protocol-client': 'protocolClient',
  '--protocol-server': 'protocolServer',
  '--min-launcher': 'minLauncher',
  '--payload-zip': 'payloadZip',
  '--previous-manifest': 'previousManifest'
}

const args = {}
for (let i = 2; i < process.argv.length; i += 2) {
  const flag = process.argv[i]
  const value = process.argv[i + 1]
  if (!(flag in FLAGS)) usage(`Unknown option: ${flag}`)
  if (value === undefined) usage(`${flag} needs a value.`)
  args[FLAGS[flag]] = value
}

for (const required of ['staged', 'source', 'out', 'release', 'channel', 'protocolClient', 'protocolServer']) {
  if (!args[required]) usage(`Missing required option --${required.replace(/[A-Z]/g, c => '-' + c.toLowerCase())}.`)
}

if (!/^v\d+\.\d+\.\d+$/.test(args.release)) usage(`--release must look like vX.Y.Z, got "${args.release}".`)
if (!['production', 'staging', 'development'].includes(args.channel)) usage(`--channel must be production, staging or development, got "${args.channel}".`)

// Protocol hashes arrive as hex however the caller happened to format them and
// leave normalised, because the manifest's whole point here is diagnosability:
// a support conversation comparing "0x8579FF3E" against "8579ff3e" by eye is
// the failure mode being retired.
function normaliseProtocol (flag, value) {
  const match = /^(0x)?([0-9a-fA-F]{1,16})$/.exec(value)
  if (!match) usage(`${flag} must be a hex value of up to 16 digits, got "${value}".`)
  return '0x' + match[2].toLowerCase()
}
const protocolClient = normaliseProtocol('--protocol-client', args.protocolClient)
const protocolServer = normaliseProtocol('--protocol-server', args.protocolServer)

if (!fs.existsSync(args.staged) || !fs.statSync(args.staged).isDirectory()) blocked(`--staged directory not found: ${args.staged}`)
if (!fs.existsSync(args.source)) blocked(`--source file not found: ${args.source}`)
if (args.payloadZip && !fs.existsSync(args.payloadZip)) blocked(`--payload-zip file not found: ${args.payloadZip}`)

// ---------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------

function sha256File (file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')
}

// Directory walk with sorted, forward-slash relative paths. The sort is what
// makes the output independent of filesystem enumeration order - NTFS happens
// to iterate sorted, but determinism must not lean on a filesystem habit.
function walk (dir, prefix) {
  const out = []
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const rel = prefix ? `${prefix}/${entry.name}` : entry.name
    const full = path.join(dir, entry.name)
    if (entry.isDirectory()) out.push(...walk(full, rel))
    else if (entry.isFile()) out.push(rel)
    else blocked(`the staged tree contains something that is neither a file nor a directory: ${full}. Nothing like that can ship.`)
  }
  return out.sort()
}

const stagedFiles = walk(args.staged, '')
if (stagedFiles.length === 0) blocked(`the staged tree at ${args.staged} is empty - a manifest promising zero payload files means the staging step upstream broke.`)

// Manifest paths are game-root-relative with forward slashes, the same
// convention mods-installed.json already uses, so the launcher's ownership
// index never has to translate between two path dialects.
const payloadFiles = stagedFiles.map(rel => {
  const full = path.join(args.staged, rel)
  return {
    path: 'red4ext/plugins/zzzCyberpunkMP/' + rel,
    sha256: sha256File(full),
    size: fs.statSync(full).size
  }
})

let payloadArchive = null
if (args.payloadZip) {
  payloadArchive = {
    name: path.basename(args.payloadZip),
    sha256: sha256File(args.payloadZip),
    size: fs.statSync(args.payloadZip).size
  }
}

// ---------------------------------------------------------------------------
// Source validation
//
// All violations are collected and reported together: a curator fixing a
// component list should see the whole bill at once, not one refusal per run.
// ---------------------------------------------------------------------------

const VALID_TYPES = ['red4ext_plugin', 'redscript', 'archive', 'tweak', 'input', 'native_dll', 'config', 'resource', 'multiplayer_core']
const VALID_CLASSES = ['payload', 'bundled', 'nexus']
const VALID_IMPACT = ['none', 'low', 'medium', 'high', 'critical']
const VALID_AUDIENCE = ['all', 'dev', 'server']
const VALID_ENFORCE = ['allow', 'warn', 'block']

let source
try {
  source = JSON.parse(fs.readFileSync(args.source, 'utf8'))
} catch (err) {
  blocked(`${args.source} is not valid JSON: ${err.message}`)
}

const problems = []
function problem (message) { problems.push(message) }

if (!Array.isArray(source.components) || source.components.length === 0) problem('source has no components array (or it is empty)')
if (!Array.isArray(source.loadRules)) problem('source has no loadRules array')
if (!source.compatibility || !Array.isArray(source.compatibility.entries)) problem('source has no compatibility.entries array')
if (!source.policy || !VALID_ENFORCE.includes(source.policy.unknownMods)) problem('source policy.unknownMods must be allow, warn or block')
if (!source.game || typeof source.game.id !== 'string' || typeof source.game.supportedVersion !== 'string' || !VALID_ENFORCE.includes(source.game.enforce)) {
  problem('source game section must carry id, supportedVersion and enforce (allow|warn|block)')
}

const components = Array.isArray(source.components) ? source.components : []
const seenIds = new Set()
for (const comp of components) {
  const label = comp && typeof comp.id === 'string' ? comp.id : JSON.stringify(comp).slice(0, 60)
  if (!comp || typeof comp.id !== 'string' || comp.id.length === 0) { problem(`component ${label}: missing id`); continue }
  if (seenIds.has(comp.id)) problem(`component ${comp.id}: duplicate id`)
  seenIds.add(comp.id)

  if (typeof comp.name !== 'string' || comp.name.length === 0) problem(`component ${comp.id}: missing name`)
  if (!Array.isArray(comp.types) || comp.types.length === 0) problem(`component ${comp.id}: types must be a non-empty array (primary loading mechanism first)`)
  else for (const t of comp.types) if (!VALID_TYPES.includes(t)) problem(`component ${comp.id}: unknown type "${t}" (valid: ${VALID_TYPES.join(', ')})`)
  if (!VALID_CLASSES.includes(comp.class)) problem(`component ${comp.id}: class must be one of ${VALID_CLASSES.join(', ')}`)
  if (typeof comp.required !== 'boolean') problem(`component ${comp.id}: required must be true or false, explicitly`)
  if (!VALID_IMPACT.includes(comp.networkImpact)) problem(`component ${comp.id}: networkImpact must be one of ${VALID_IMPACT.join(', ')}`)
  if (!VALID_AUDIENCE.includes(comp.audience)) problem(`component ${comp.id}: audience must be one of ${VALID_AUDIENCE.join(', ')}`)
  for (const field of ['author', 'source', 'license']) {
    if (typeof comp[field] !== 'string' || comp[field].length === 0) problem(`component ${comp.id}: missing ${field} (attribution is mandatory - we did not write most of these)`)
  }

  // The payload's version is stamped from --release below, and bundled
  // versions name zips we redistribute, so both are mandatory. A nexus
  // component may lack a version until its curation pass (doc section 3.3)
  // records what was actually served - blocking on that today would make the
  // first manifest impossible to generate before the curation tooling exists,
  // so the gap is a loud warning below instead of a refusal.
  if (comp.class === 'bundled' && (typeof comp.version !== 'string' || comp.version.length === 0)) problem(`component ${comp.id}: missing version`)
  if (comp.class === 'nexus' && comp.version !== undefined && (typeof comp.version !== 'string' || comp.version.length === 0)) problem(`component ${comp.id}: version, when present, must be a non-empty string`)
  if (comp.class === 'bundled' && (!comp.archive || typeof comp.archive.name !== 'string')) problem(`component ${comp.id}: bundled components must carry archive.name (the prerequisite zip to hash)`)
  if (comp.class === 'nexus' && (!comp.nexus || typeof comp.nexus.modId !== 'number')) problem(`component ${comp.id}: nexus components must carry nexus.modId`)

  if (comp.dependencies !== undefined) {
    if (!Array.isArray(comp.dependencies)) problem(`component ${comp.id}: dependencies must be an array`)
    else for (const dep of comp.dependencies) {
      if (!dep || typeof dep.id !== 'string') problem(`component ${comp.id}: every dependency needs an id`)
      if (dep && dep.version !== undefined && typeof dep.version !== 'string') problem(`component ${comp.id}: dependency versions are strings like ">=1.29.0"`)
    }
  }
}

const payloadComponents = components.filter(c => c && c.class === 'payload')
if (payloadComponents.length !== 1) problem(`there must be exactly one class:payload component, found ${payloadComponents.length}`)

// install_order is a generated artifact; a hand-written one in the source
// would be a second source of truth waiting to disagree with the graph.
if (Array.isArray(source.loadRules) && source.loadRules.some(r => r && r.rule === 'install_order')) {
  problem('the source carries an install_order rule - install order is DERIVED from the dependency graph at generation time, remove it from the source')
}

// Dependencies must name components that exist. A typo here would otherwise
// become a launcher that can never satisfy the graph.
for (const comp of components) {
  if (!comp || !Array.isArray(comp.dependencies)) continue
  for (const dep of comp.dependencies) {
    if (dep && typeof dep.id === 'string' && !seenIds.has(dep.id)) {
      problem(`component ${comp.id} depends on "${dep.id}", which matches no component id`)
    }
  }
}

if (problems.length > 0) {
  blocked(`the source list has ${problems.length} violation(s):\n  - ` + problems.join('\n  - '))
}

// ---------------------------------------------------------------------------
// Bundled prerequisite hashing
//
// The zips live next to the source file under fullinstall-base/prerequisites,
// which is also what Ship.ps1 packs into FullInstall.zip - so the bytes being
// hashed are the bytes a new player will actually receive.
// ---------------------------------------------------------------------------

const prereqDir = path.join(path.dirname(path.resolve(args.source)), 'fullinstall-base', 'prerequisites')

for (const comp of components) {
  if (comp.class !== 'bundled') continue
  const zip = path.join(prereqDir, comp.archive.name)
  if (!fs.existsSync(zip)) {
    blocked(`component ${comp.id} names prerequisite archive "${comp.archive.name}", but ${zip} does not exist. A manifest cannot vouch for bytes it never saw.`)
  }
  comp.archive.sha256 = sha256File(zip)
  comp.archive.size = fs.statSync(zip).size
}

// The payload component's version is the release being shipped - measured from
// the ship, not curated, because a curated copy would drift the first time
// someone forgot to bump it.
payloadComponents[0].version = args.release.slice(1)

// ---------------------------------------------------------------------------
// Install order - topological sort of the dependency graph
//
// The graph is the single source of truth; this array is a cached result
// shipped so the launcher does not re-derive it (doc section 4). Kahn's
// algorithm with a stable tie-break: among ready components, source order
// wins, so the output cannot flap between runs or Node versions.
// ---------------------------------------------------------------------------

const order = []
const placed = new Set()
const ids = components.map(c => c.id)
const depsOf = new Map(components.map(c => [c.id, (c.dependencies || []).map(d => d.id)]))

while (order.length < ids.length) {
  const ready = ids.find(id => !placed.has(id) && depsOf.get(id).every(dep => placed.has(dep)))
  if (!ready) {
    // Everything unplaced is either on a cycle or downstream of one. Stripping
    // members nothing unplaced depends on, repeatedly, leaves exactly the
    // cycle members - so the refusal names the actual loop, not its victims.
    let loop = ids.filter(id => !placed.has(id))
    for (let trimmed = true; trimmed;) {
      trimmed = false
      for (const id of [...loop]) {
        const isDependedOn = loop.some(other => other !== id && depsOf.get(other).includes(id))
        if (!isDependedOn) { loop = loop.filter(x => x !== id); trimmed = true }
      }
    }
    blocked(`the dependency graph has a cycle involving: ${loop.join(', ')}. No install order can satisfy it.`)
  }
  placed.add(ready)
  order.push(ready)
}

// ---------------------------------------------------------------------------
// Manifest version - date.serial, UTC (doc section 2)
//
// Two manifests can ship in one day; the serial disambiguates. The serial only
// counts against the previous manifest when one is supplied and is same-day,
// which is exactly the information Ship.ps1 has (it downloads the current
// release's manifest anyway for its carry-forward logic).
// ---------------------------------------------------------------------------

const now = new Date()
const day = [
  String(now.getUTCFullYear()),
  String(now.getUTCMonth() + 1).padStart(2, '0'),
  String(now.getUTCDate()).padStart(2, '0')
]

let serial = 1
if (args.previousManifest) {
  if (!fs.existsSync(args.previousManifest)) blocked(`--previous-manifest file not found: ${args.previousManifest}`)
  let previous
  try {
    previous = JSON.parse(fs.readFileSync(args.previousManifest, 'utf8'))
  } catch (err) {
    blocked(`--previous-manifest is not valid JSON (${err.message}) - if the published manifest is corrupt, that needs investigating, not skipping.`)
  }
  const match = /^(\d{4})\.(\d{2})\.(\d{2})\.(\d+)$/.exec(previous.manifestVersion || '')
  if (!match) blocked(`--previous-manifest carries no parseable manifestVersion ("${previous.manifestVersion}").`)
  if (match[1] === day[0] && match[2] === day[1] && match[3] === day[2]) serial = parseInt(match[4], 10) + 1
}

const manifestVersion = `${day.join('.')}.${String(serial).padStart(2, '0')}`

// generatedAt is truncated to the day, deliberately: a to-the-second timestamp
// would make every rerun a different artifact, defeating the determinism the
// sign-and-verify gate relies on. Day precision costs nothing - intra-day
// ordering is already the serial's job.
const generatedAt = `${day.join('-')}T00:00:00Z`

// ---------------------------------------------------------------------------
// Assembly
//
// Key order is fixed by construction, matching the schema layout in doc
// section 2, so diffs between two manifests are always about content.
// Underscore-prefixed keys are curator commentary and stay in the source; the
// shipped manifest carries data only.
// ---------------------------------------------------------------------------

function ordered (obj, keyOrder) {
  const out = {}
  for (const key of keyOrder) if (obj[key] !== undefined) out[key] = obj[key]
  for (const key of Object.keys(obj).sort()) {
    if (!(key in out) && !key.startsWith('_')) out[key] = obj[key]
  }
  return out
}

const COMPONENT_KEYS = ['id', 'name', 'types', 'class', 'version', 'required', 'networkImpact', 'audience', 'author', 'source', 'license', 'archive', 'nexus', 'files', 'dependencies']

const manifest = {
  schema: 1,
  manifestVersion,
  channel: args.channel,
  generatedAt,
  release: args.release,
  game: ordered(source.game, ['id', 'supportedVersion', 'enforce']),
  protocol: { client: protocolClient, server: protocolServer },
  client: {
    ...(args.minLauncher ? { minLauncher: args.minLauncher } : {}),
    payload: {
      ...(payloadArchive ? { archive: payloadArchive } : {}),
      files: payloadFiles
    }
  },
  components: components.map(c => ordered(c, COMPONENT_KEYS)),
  loadRules: [
    ...source.loadRules.map(r => ordered(r, ['rule', 'mechanism', 'subject', 'after', 'order', 'severity', 'reason'])),
    { rule: 'install_order', mechanism: 'installer', order }
  ],
  compatibility: {
    entries: source.compatibility.entries.map(e => ordered(e, ['a', 'b', 'status', 'testedVersions', 'severity', 'reason', 'action', 'detection']))
  },
  policy: ordered(source.policy, ['unknownMods'])
}

fs.writeFileSync(args.out, JSON.stringify(manifest, null, 2) + '\n', 'utf8')

console.log(`wrote ${args.out}`)
console.log(`  manifestVersion ${manifestVersion} (${args.channel}, release ${args.release})`)
console.log(`  payload: ${payloadFiles.length} file(s)${payloadArchive ? ` + archive ${payloadArchive.name}` : ''}`)
console.log(`  components: ${components.length}, install order: ${order.join(' -> ')}`)

// Doc section 3.3 makes version pins and curation-time hashes mandatory for
// every nexus component whose networkImpact is medium or above. Until the
// scanner exists to record them this cannot be a refusal, but it must never
// be silent either - a sync-relevant mod nobody pinned is exactly the drift
// this whole system exists to end.
const unpinned = components.filter(c =>
  c.class === 'nexus' &&
  ['medium', 'high', 'critical'].includes(c.networkImpact) &&
  (!c.version || !c.archive || !c.archive.sha256 || !c.nexus.fileId))
if (unpinned.length > 0) {
  console.log(`  WARNING: curation pending for sync-relevant nexus component(s): ${unpinned.map(c => c.id).join(', ')}`)
  console.log('           doc section 3.3 requires their version pin, fileId and hashes; record them via the curation pass before policy leaves "warn".')
}
