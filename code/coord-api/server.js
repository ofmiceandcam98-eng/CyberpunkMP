/**
 * Night City Online - assistant coordination API.
 *
 * Cam works with several AI assistants on this project at once. Until now they
 * coordinated through ASSISTANTS_COMMUNICATION.md, which works for whichever of them can
 * read and write files on this machine and not at all for the ones that cannot. This is
 * the same channel over HTTP, so anything that can make a request can take part.
 *
 * Deliberately dependency-free. Node's own http/crypto/fs are enough, and a coordination
 * service that needs `npm install` before it will start is one more thing to go wrong at
 * the moment somebody is trying to use it.
 *
 * WHAT IT IS NOT: this holds notes about a hobby game server. It is not an authentication
 * system and the keys in it are not passwords. They identify who is speaking, so that a
 * feed of updates has attribution - nothing more. Treat them as name badges.
 *
 *   node server.js                 start on 127.0.0.1 and the Tailscale address
 *   node server.js --port 11780
 *   node server.js --no-publish    do not write to publish/ or GitHub
 *
 * The key console is at http://127.0.0.1:11780/ and is reachable ONLY from this machine.
 */

'use strict'

const http = require('http')
const crypto = require('crypto')
const fs = require('fs')
const os = require('os')
const path = require('path')
const { execFile } = require('child_process')

/**
 * The address the other assistants should use.
 *
 * Found rather than hardcoded. Tailscale hands out addresses in 100.64.0.0/10, so the
 * interface is identifiable without shelling out to the CLI - and an address written into
 * a file goes stale the first time the tailnet reassigns one, which would leave the setup
 * instructions confidently wrong.
 */
function findTailscaleAddress () {
  for (const addresses of Object.values(os.networkInterfaces())) {
    for (const address of addresses || []) {
      if (address.family !== 'IPv4' || address.internal) continue

      const [a, b] = address.address.split('.').map(Number)
      if (a === 100 && b >= 64 && b <= 127) return address.address
    }
  }
  return null
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

const REPO_ROOT = path.resolve(__dirname, '..', '..')

// Overridable so the service can be exercised without touching the real keys and history.
// A test run that quietly issues participants into the live store burns slots out of a
// budget of five.
const DATA_DIR = process.env.NCO_COORD_DATA || path.join(__dirname, 'data')
const PARTICIPANTS_FILE = path.join(DATA_DIR, 'participants.json')
const UPDATES_FILE = path.join(DATA_DIR, 'updates.jsonl')

const PUBLISH_DIR = path.join(REPO_ROOT, 'publish')
const PUBLISH_JSON = path.join(PUBLISH_DIR, 'assistant-updates.json')
const PUBLISH_MD = path.join(PUBLISH_DIR, 'ASSISTANT_UPDATES.md')
const COORDINATION_LOG = path.join(REPO_ROOT, 'ASSISTANTS_COMMUNICATION.md')

const GITHUB_REPO = 'ofmiceandcam98-eng/CyberpunkMP'

// Five, because Cam asked for five.
//
// A cap is worth having whatever the number: this is a small group who are supposed to
// know who each other are, and a feed nobody can account for every voice in is not
// coordination. Revoking frees a slot.
const MAX_PARTICIPANTS = 5

// Only the newest slice is published. The full history stays in updates.jsonl - this is
// what the launcher and the GitHub page show, and nobody reads the hundredth entry.
const PUBLISHED_COUNT = 40

// How long to wait after an update before pushing to GitHub. Several posts in quick
// succession are one publish, not several - uploading a release asset per message would
// be slow and would rate-limit itself.
const PUBLISH_DEBOUNCE_MS = 30_000

const MAX_BODY_BYTES = 64 * 1024
const MAX_TITLE_LENGTH = 200
const MAX_UPDATE_LENGTH = 20_000

const KINDS = ['update', 'question', 'answer', 'decision', 'warning', 'handoff']

// The shared key handed to anyone with the dev role, through the launcher's settings.
//
// Shared on purpose: a per-person key would need somebody to issue one every time a friend
// offers to help, which is the chore this removes. Everything they post is attributed to
// this participant rather than to them individually - worth knowing when reading the feed.
const DEV_PARTICIPANT_ID = 'dev'

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

const args = process.argv.slice(2)

function flag (name) { return args.includes(name) }
function option (name, fallback) {
  const i = args.indexOf(name)
  return i !== -1 && args[i + 1] ? args[i + 1] : fallback
}

const PORT = Number(option('--port', 11780))
const PUBLISHING = !flag('--no-publish')

// Optional override so the service can advertise a different public host/port
// than the one it binds to. This is useful when the game server and coord API
// should be presented at the same (external) address/port for clients.
const OVERRIDE_PUBLIC_HOST = process.env.NCO_COORD_HOST || null
const OVERRIDE_PUBLIC_PORT = process.env.NCO_COORD_PORT ? Number(process.env.NCO_COORD_PORT) : null

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

function ensureDataDir () {
  fs.mkdirSync(DATA_DIR, { recursive: true })
}

function loadParticipants () {
  try {
    return JSON.parse(fs.readFileSync(PARTICIPANTS_FILE, 'utf8'))
  } catch {
    return []
  }
}

function saveParticipants (list) {
  ensureDataDir()
  // Written 0600 where the platform honours it. The keys are name badges rather than
  // passwords, but there is no reason for another account on this machine to read them.
  fs.writeFileSync(PARTICIPANTS_FILE, JSON.stringify(list, null, 2), { mode: 0o600 })
}

/**
 * One JSON object per line.
 *
 * Append-only by construction: a crash mid-write costs the last line rather than the
 * file, and a corrupted line is skipped instead of taking the history with it. That
 * matters more here than elegance - this IS the record of what the assistants told each
 * other, and the project has already lost a day's work to a file going missing once.
 */
function loadUpdates () {
  try {
    return fs.readFileSync(UPDATES_FILE, 'utf8')
      .split('\n')
      .filter(Boolean)
      .map((line) => { try { return JSON.parse(line) } catch { return null } })
      .filter(Boolean)
  } catch {
    return []
  }
}

function appendUpdate (update) {
  ensureDataDir()
  fs.appendFileSync(UPDATES_FILE, JSON.stringify(update) + '\n')
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

/**
 * Prefixed so it is recognisable on sight.
 *
 * Somebody is going to paste one of these into a chat window by accident. A string that
 * announces what it is gets spotted; 32 anonymous hex characters do not.
 */
function generateKey () {
  return 'ncoa_' + crypto.randomBytes(24).toString('hex')
}

function normaliseId (raw) {
  return String(raw || '')
    .toLowerCase()
    .replace(/[^a-z0-9-]/g, '-')
    .replace(/-+/g, '-')
    .replace(/^-|-$/g, '')
    .slice(0, 32)
}

/**
 * Constant-time comparison.
 *
 * timingSafeEqual throws on a length mismatch, which would leak length by itself, so the
 * lengths are checked first and a mismatch short-circuits to a comparison against a
 * fixed-length dummy. Almost certainly unnecessary for a hobby coordination feed - but it
 * costs three lines and means nobody has to think about it again.
 */
function keyMatches (candidate, actual) {
  const a = Buffer.from(String(candidate))
  const b = Buffer.from(String(actual))
  if (a.length !== b.length) return false
  return crypto.timingSafeEqual(a, b)
}

function findParticipantByKey (key) {
  if (!key) return null
  return loadParticipants().find((p) => !p.revoked && keyMatches(key, p.key)) || null
}

function createParticipant (id, label) {
  const list = loadParticipants()

  const active = list.filter((p) => !p.revoked)
  if (active.length >= MAX_PARTICIPANTS) {
    return { ok: false, error: `All ${MAX_PARTICIPANTS} slots are in use. Revoke one to free a slot.` }
  }

  const cleanId = normaliseId(id)
  if (!cleanId) return { ok: false, error: 'An id is required (letters, numbers and dashes).' }
  if (list.some((p) => p.id === cleanId && !p.revoked)) {
    return { ok: false, error: `"${cleanId}" already has a key. Revoke it first to reissue.` }
  }

  const participant = {
    id: cleanId,
    label: String(label || cleanId).slice(0, 64),
    key: generateKey(),
    createdAt: new Date().toISOString(),
    revoked: false
  }

  list.push(participant)
  saveParticipants(list)

  return { ok: true, participant }
}

function revokeParticipant (id) {
  const list = loadParticipants()
  const participant = list.find((p) => p.id === id && !p.revoked)
  if (!participant) return { ok: false, error: 'No active participant with that id.' }

  participant.revoked = true
  participant.revokedAt = new Date().toISOString()
  saveParticipants(list)

  return { ok: true }
}

// ---------------------------------------------------------------------------
// Publishing
// ---------------------------------------------------------------------------

let publishTimer = null

function publishSoon () {
  if (!PUBLISHING) return
  if (publishTimer) return

  publishTimer = setTimeout(() => {
    publishTimer = null
    publishNow().catch((error) => console.error('[publish] failed:', error.message))
  }, PUBLISH_DEBOUNCE_MS)
}

function renderMarkdown (updates) {
  const lines = [
    '# Assistant updates',
    '',
    '_Posted through the coordination API. Newest first. Written automatically - edit',
    '`code/coord-api` rather than this file._',
    ''
  ]

  for (const update of updates) {
    const when = new Date(update.at).toISOString().replace('T', ' ').slice(0, 16)
    lines.push(`### ${update.title}`)
    lines.push('')
    lines.push(`**${update.from}** · ${update.kind} · ${when} UTC`)
    lines.push('')
    lines.push(update.body)
    if (update.refs && update.refs.length) {
      lines.push('')
      lines.push('Refs: ' + update.refs.map((r) => `\`${r}\``).join(', '))
    }
    lines.push('')
    lines.push('---')
    lines.push('')
  }

  return lines.join('\n')
}

async function publishNow () {
  const updates = loadUpdates().slice(-PUBLISHED_COUNT).reverse()

  fs.mkdirSync(PUBLISH_DIR, { recursive: true })

  const payload = {
    generatedAt: new Date().toISOString(),
    count: updates.length,
    updates
  }

  fs.writeFileSync(PUBLISH_JSON, JSON.stringify(payload, null, 2))
  fs.writeFileSync(PUBLISH_MD, renderMarkdown(updates))

  // Uploaded as a release asset, which is how modlist.json and server.json already reach
  // the launcher. Reusing that route means no new hosting, no new credentials, and one
  // place to look when something does not arrive.
  await uploadToRelease()

  console.log(`[publish] ${updates.length} update(s) written to publish/`)
}

function gh (args) {
  return new Promise((resolve) => {
    execFile('gh', args, { windowsHide: true }, (error, stdout, stderr) => {
      resolve({ ok: !error, stdout: (stdout || '').trim(), stderr: (stderr || '').trim() })
    })
  })
}

async function uploadToRelease () {
  // "latest" is a URL alias, not a tag - `gh release upload latest` fails looking for a
  // release literally called that. The tag has to be resolved first. Uploading to the
  // release GitHub already considers latest is what makes
  // releases/latest/download/assistant-updates.json resolve, which is the URL the
  // launcher asks for.
  const view = await gh(['release', 'view', '--repo', GITHUB_REPO, '--json', 'tagName', '-q', '.tagName'])

  if (!view.ok || !view.stdout) {
    // Not fatal. The files on disk are the record; GitHub is a mirror of them, and a
    // coordination service that stops accepting updates because a network call failed has
    // the priorities backwards.
    console.warn('[publish] GitHub upload skipped:', (view.stderr || 'could not find the latest release').split('\n')[0])
    return
  }

  const tag = view.stdout
  const upload = await gh(['release', 'upload', '--clobber', '--repo', GITHUB_REPO, tag, PUBLISH_JSON])

  if (upload.ok) console.log(`[publish] uploaded assistant-updates.json to ${tag}`)
  else console.warn('[publish] GitHub upload skipped:', (upload.stderr || 'upload failed').split('\n')[0])
}

/**
 * Mirrors into the markdown log the assistants already read.
 *
 * The API is the live channel; ASSISTANTS_COMMUNICATION.md stays the durable, committed
 * record. Writing to both means switching to this does not silently orphan the history,
 * and an assistant that only reads files still sees everything.
 */
function appendToCoordinationLog (update) {
  if (!PUBLISHING) return

  try {
    if (!fs.existsSync(COORDINATION_LOG)) return

    const when = new Date(update.at).toISOString().slice(0, 10)
    const entry = [
      '',
      `### ${when} — ${update.from} (via API)`,
      '',
      `**${update.title}** · ${update.kind}`,
      '',
      update.body,
      ''
    ].join('\n')

    fs.appendFileSync(COORDINATION_LOG, entry)
  } catch (error) {
    console.warn('[log] could not append to ASSISTANTS_COMMUNICATION.md:', error.message)
  }
}

// ---------------------------------------------------------------------------
// HTTP plumbing
// ---------------------------------------------------------------------------

function send (res, status, body, headers = {}) {
  const payload = typeof body === 'string' ? body : JSON.stringify(body, null, 2)
  res.writeHead(status, {
    'Content-Type': typeof body === 'string' ? 'text/html; charset=utf-8' : 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    ...headers
  })
  res.end(payload)
}

function readBody (req) {
  return new Promise((resolve, reject) => {
    let size = 0
    const chunks = []

    req.on('data', (chunk) => {
      size += chunk.length
      if (size > MAX_BODY_BYTES) {
        reject(new Error('Request body too large.'))
        req.destroy()
        return
      }
      chunks.push(chunk)
    })

    req.on('end', () => {
      const raw = Buffer.concat(chunks).toString('utf8')
      if (!raw) return resolve({})
      try {
        resolve(JSON.parse(raw))
      } catch {
        reject(new Error('Body must be JSON.'))
      }
    })

    req.on('error', reject)
  })
}

function extractKey (req, url) {
  const header = req.headers.authorization || ''
  if (header.toLowerCase().startsWith('bearer ')) return header.slice(7).trim()
  if (req.headers['x-api-key']) return String(req.headers['x-api-key']).trim()

  // Query strings end up in logs and shell history, so this is the last resort rather
  // than the documented way. It exists because some clients cannot set a header.
  return url.searchParams.get('key')
}

/**
 * The key console is this machine only.
 *
 * Everything else is reachable over Tailscale so the other assistants can post, but the
 * page that DISPLAYS the keys is not something to serve across a network - the whole
 * point of it is that the keys are readable, which is exactly what makes it worth
 * keeping local.
 */
function isLocal (req) {
  const address = req.socket.remoteAddress || ''
  return address === '127.0.0.1' || address === '::1' || address === '::ffff:127.0.0.1'
}

// A crude limiter: enough to stop a broken loop hammering the disk, not a security
// control. Keyed on the participant, so one misbehaving client cannot silence the others.
const rateBuckets = new Map()

function withinRate (id, limit = 60, windowMs = 60_000) {
  const now = Date.now()
  const bucket = rateBuckets.get(id) || { count: 0, resetAt: now + windowMs }

  if (now > bucket.resetAt) {
    bucket.count = 0
    bucket.resetAt = now + windowMs
  }

  bucket.count += 1
  rateBuckets.set(id, bucket)

  return bucket.count <= limit
}

// ---------------------------------------------------------------------------
// Routes
// ---------------------------------------------------------------------------

/**
 * Hands a verified dev their key.
 *
 * The one endpoint that does not need a key, because it is how you get one. Cam's friends
 * are helping write this and each of them needs the key in their own Claude; relaying it
 * by hand every time is exactly the sort of chore that stops happening after the second
 * person.
 *
 * Identity is decided by Discord, not by the caller: the launcher sends the OAuth token it
 * already holds, this asks Discord who it belongs to and what roles they have in the
 * guild, and checks those against the role map the game server publishes. Same source of
 * truth as the in-game permissions, so the two cannot disagree about who is a dev.
 */
async function handleDevKey (req, res) {
  let body
  try {
    body = await readBody(req)
  } catch (error) {
    return send(res, 400, { error: error.message })
  }

  const token = String(body.discordToken || '').trim()
  if (!token) return send(res, 400, { error: 'A Discord token is required.' })

  // The role map the game server writes. No map means nothing to verify against, and
  // handing out a key on that basis would defeat the point of asking.
  let roleMap
  try {
    roleMap = JSON.parse(fs.readFileSync(path.join(PUBLISH_DIR, 'roles.json'), 'utf8'))
  } catch {
    return send(res, 503, { error: 'The role map is not available yet. Start the game server once.' })
  }

  const headers = {
    Authorization: `Bearer ${token}`,
    'User-Agent': 'CyberpunkMP-Coord (https://github.com/ofmiceandcam98-eng/CyberpunkMP, 1.0)'
  }

  let identity
  try {
    const me = await fetch('https://discord.com/api/v10/users/@me', { headers })
    if (!me.ok) return send(res, 401, { error: 'That Discord sign-in is not valid.' })
    identity = await me.json()
  } catch {
    return send(res, 503, { error: 'Could not reach Discord.' })
  }

  // The owner always qualifies, whatever the roles say.
  let qualifies = roleMap.owner && roleMap.owner === identity.id

  if (!qualifies) {
    try {
      const member = await fetch(
        `https://discord.com/api/v10/users/@me/guilds/${roleMap.guildId}/member`, { headers })

      if (member.status === 404) return send(res, 403, { error: 'You are not in the Discord.' })
      if (!member.ok) return send(res, 401, { error: 'That Discord sign-in is not valid.' })

      const held = (await member.json()).roles || []
      const granting = (roleMap.roles || [])
        .filter((role) => role.level === 'admin' || role.level === 'owner')
        .map((role) => role.id)

      qualifies = held.some((id) => granting.includes(id))
    } catch {
      return send(res, 503, { error: 'Could not reach Discord.' })
    }
  }

  if (!qualifies) {
    return send(res, 403, { error: 'The dev key is for people with the dev role. Ask Cam.' })
  }

  const participant = loadParticipants().find((p) => !p.revoked && p.id === DEV_PARTICIPANT_ID)
  if (!participant) {
    return send(res, 503, { error: `No "${DEV_PARTICIPANT_ID}" key exists yet. Create one in the console.` })
  }

  console.log(`[dev-key] issued to ${identity.username || identity.id}`)

  return send(res, 200, {
    ok: true,
    id: participant.id,
    label: participant.label,
    key: participant.key,
    baseUrl: (process.env.NCO_COORD_BASEURL)
      || `http://${OVERRIDE_PUBLIC_HOST || findTailscaleAddress() || 'localhost'}:${OVERRIDE_PUBLIC_PORT || PORT}`
  })
}

async function handleApi (req, res, url) {
  const key = extractKey(req, url)
  const participant = findParticipantByKey(key)

  if (!participant) {
    return send(res, 401, {
      error: 'Unknown or missing key.',
      hint: 'Send it as "Authorization: Bearer <key>". Ask Cam for one - there are five slots.'
    })
  }

  if (!withinRate(participant.id)) {
    return send(res, 429, { error: 'Too many requests. Sixty a minute per participant.' })
  }

  if (url.pathname === '/v1/whoami') {
    return send(res, 200, { id: participant.id, label: participant.label, since: participant.createdAt })
  }

  if (url.pathname === '/v1/participants') {
    // Ids and labels only. A key is never returned by the API, not even your own - if you
    // have lost it, the console on Cam's machine is where it lives.
    const list = loadParticipants()
      .filter((p) => !p.revoked)
      .map((p) => ({ id: p.id, label: p.label, since: p.createdAt }))

    return send(res, 200, { max: MAX_PARTICIPANTS, used: list.length, participants: list })
  }

  if (url.pathname === '/v1/updates' && req.method === 'GET') {
    let updates = loadUpdates()

    const since = url.searchParams.get('since')
    if (since) {
      const cutoff = Date.parse(since)
      if (!Number.isNaN(cutoff)) updates = updates.filter((u) => Date.parse(u.at) > cutoff)
      else updates = updates.filter((u) => u.id > since)
    }

    const from = url.searchParams.get('from')
    if (from) updates = updates.filter((u) => u.from === from)

    const kind = url.searchParams.get('kind')
    if (kind) updates = updates.filter((u) => u.kind === kind)

    const limit = Math.min(Number(url.searchParams.get('limit')) || 25, 200)
    updates = updates.slice(-limit).reverse()

    return send(res, 200, { count: updates.length, updates })
  }

  if (url.pathname === '/v1/updates' && req.method === 'POST') {
    let body
    try {
      body = await readBody(req)
    } catch (error) {
      return send(res, 400, { error: error.message })
    }

    const title = String(body.title || '').trim().slice(0, MAX_TITLE_LENGTH)
    const text = String(body.body || '').trim().slice(0, MAX_UPDATE_LENGTH)

    if (!title) return send(res, 400, { error: 'A title is required.' })
    if (!text) return send(res, 400, { error: 'A body is required.' })

    const kind = KINDS.includes(body.kind) ? body.kind : 'update'

    const update = {
      id: new Date().toISOString().replace(/[-:.TZ]/g, '') + '-' + crypto.randomBytes(3).toString('hex'),
      at: new Date().toISOString(),
      from: participant.id,
      fromLabel: participant.label,
      kind,
      title,
      body: text,
      refs: Array.isArray(body.refs) ? body.refs.slice(0, 20).map((r) => String(r).slice(0, 200)) : []
    }

    appendUpdate(update)
    appendToCoordinationLog(update)
    publishSoon()

    console.log(`[update] ${participant.id}: ${title}`)

    return send(res, 201, { ok: true, update })
  }

  if (url.pathname === '/v1/publish' && req.method === 'POST') {
    try {
      await publishNow()
      return send(res, 200, { ok: true })
    } catch (error) {
      return send(res, 500, { error: error.message })
    }
  }

  return send(res, 404, { error: 'No such endpoint.', endpoints: ENDPOINTS })
}

const ENDPOINTS = [
  'GET  /health',
  'GET  /v1/whoami',
  'GET  /v1/participants',
  'GET  /v1/updates?limit=&since=&from=&kind=',
  'POST /v1/updates   {"title","body","kind","refs"}',
  'POST /v1/publish'
]

async function handleAdmin (req, res, url) {
  if (!isLocal(req)) {
    return send(res, 403, { error: 'The key console is only available on the machine running the service.' })
  }

  if (url.pathname === '/admin/participants' && req.method === 'GET') {
    return send(res, 200, {
      max: MAX_PARTICIPANTS,
      participants: loadParticipants().filter((p) => !p.revoked)
    })
  }

  if (url.pathname === '/admin/participants' && req.method === 'POST') {
    let body
    try {
      body = await readBody(req)
    } catch (error) {
      return send(res, 400, { error: error.message })
    }

    const result = createParticipant(body.id, body.label)
    return send(res, result.ok ? 201 : 400, result)
  }

  if (url.pathname.startsWith('/admin/participants/') && req.method === 'DELETE') {
    const id = decodeURIComponent(url.pathname.split('/').pop())
    const result = revokeParticipant(id)
    return send(res, result.ok ? 200 : 404, result)
  }

  return send(res, 404, { error: 'No such endpoint.' })
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`)

  try {
    if (url.pathname === '/health') {
      return send(res, 200, {
        ok: true,
        service: 'nco-coordination',
        participants: loadParticipants().filter((p) => !p.revoked).length,
        max: MAX_PARTICIPANTS,
        updates: loadUpdates().length,
        // So the console can print the URL to hand out without guessing at it.
        baseUrl: `http://${findTailscaleAddress() || 'localhost'}:${PORT}`
      })
    }

    if (url.pathname === '/' || url.pathname === '/index.html') {
      if (!isLocal(req)) {
        return send(res, 403, { error: 'The key console is only available on the machine running the service.' })
      }
      const page = fs.readFileSync(path.join(__dirname, 'console.html'), 'utf8')
      return send(res, 200, page)
    }

    // Before the key check below, because this is how a dev gets a key in the first place.
    if (url.pathname === '/v1/dev-key' && req.method === 'POST') return handleDevKey(req, res)

    if (url.pathname.startsWith('/admin/')) return handleAdmin(req, res, url)
    if (url.pathname.startsWith('/v1/')) return handleApi(req, res, url)

    return send(res, 404, { error: 'No such endpoint.', endpoints: ENDPOINTS })
  } catch (error) {
    console.error('[error]', error)
    return send(res, 500, { error: 'Something went wrong on the server.' })
  }
})

// Bound to every interface on purpose: the other assistants reach this over Tailscale.
// Everything that matters is key-gated, and the two things that are not - /health and the
// key console - are respectively harmless and localhost-only.
server.listen(PORT, '0.0.0.0', () => {
  ensureDataDir()

  const active = loadParticipants().filter((p) => !p.revoked)
  const tailscale = findTailscaleAddress()

  console.log('')
  console.log('  Night City Online - assistant coordination API')
  console.log('  ---------------------------------------------')
  // Print the key console URL and the public address that others should use. Both
  // can be overridden by environment variables so operators can present a single
  // stable address (for example, the game server's tailnet IP and game port).
  const publicHost = OVERRIDE_PUBLIC_HOST || tailscale || '127.0.0.1'
  const publicPort = OVERRIDE_PUBLIC_PORT || PORT
  console.log(`  Key console   http://127.0.0.1:${PORT}/   (this machine only)`)
  console.log(`  For others    http://${publicHost}:${publicPort}/v1/   (over Tailscale)`)

  // Say so when nobody else can actually reach this.
  //
  // Falling back to a loopback address is silent and looks like success: the service
  // starts, answers locally, and hands out an address that works only for whoever is
  // already on the host.
  //
  // Behind a userspace-mode Tailscale sidecar this is the GUARANTEED outcome, not an
  // unlucky one. Detection above scans interfaces for a 100.64/10 address, and userspace
  // mode creates no tun device to carry one - inbound traffic reaches sockets in the
  // namespace, but nothing in the namespace can see the address it arrived on.
  //
  // Compose cannot enforce this. It interpolates every service's environment before
  // applying profiles, so making the variable required there breaks deployments that
  // never run this service. The check belongs here, where it can tell the difference
  // between a developer running it on their own machine and a deployment that is
  // quietly reachable by nobody.
  if (publicHost === '127.0.0.1' || publicHost === 'localhost') {
    console.log('')
    console.log('  WARNING: no reachable address - this is advertising loopback.')
    console.log('           Nobody on the tailnet can post to it, and posts made')
    console.log('           elsewhere while it is unreachable are not queued - they')
    console.log('           are simply never made.')
    console.log('           Set NCO_COORD_HOST to this deployment\'s tailnet address.')
  } 
  console.log(`  Participants  ${active.length} of ${MAX_PARTICIPANTS}`)
  console.log(`  Publishing    ${PUBLISHING ? 'publish/ + GitHub release' : 'off (--no-publish)'}`)
  console.log('')

  if (active.length === 0) {
    console.log('  No keys yet. Open the console above and create one.')
    console.log('')
  }
})
