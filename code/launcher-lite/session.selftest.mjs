/**
 * session.selftest.mjs - a saved Discord sign-in ends only when Discord says so.
 * Run with:  node session.selftest.mjs
 *
 * WHY THIS EXISTS. Two things signed people out of the launcher, and both looked like "it
 * forgot me after a restart" because the saved sign-in is only checked when the launcher
 * starts:
 *
 *   1. Any failure of the identity check deleted the saved token - the network not being
 *      up yet after a reboot, a timeout, a rate limit, Discord having a bad minute. The
 *      catch said "Expired or revoked" and was written as if a 401 were the only way to
 *      get there. Nothing was logged.
 *   2. Discord's token lasts seven days, and the launcher threw away the refresh token that
 *      renews it. So everybody was signed out a week after signing in.
 *
 * And signing in again is the whole Discord login, password and all, because the sign-in
 * window starts from an empty session on purpose.
 *
 * This lifts the real functions out of main.js as text, the way prereq.selftest.mjs does,
 * and runs them against a stand-in for Discord that answers what each case says: 200, 401,
 * 429, 500, nothing at all. What it pins is which of those is allowed to delete the saved
 * sign-in. Exactly one is: Discord refusing the token, with no way to renew it.
 *
 * Reaches no network and writes no file: settings and encryption are stand-ins too.
 * Needs nothing from node_modules.
 */

import { readFileSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const source = readFileSync(path.join(here, 'main.js'), 'utf8').split('\r\n').join('\n')

let passed = 0
let failed = 0

function check (name, actual, expected) {
  const a = JSON.stringify(actual)
  const e = JSON.stringify(expected)
  if (a === e) {
    passed++
  } else {
    failed++
    console.log(`FAIL: ${name}\n  expected ${e}\n  got      ${a}`)
  }
}

// One top-level function of main.js, as text. They all close with a brace in column 0.
function grab (name) {
  const start = source.search(new RegExp(`^(async )?function ${name} `, 'm'))
  if (start < 0) throw new Error(`main.js has no function ${name} - update this selftest`)
  const end = source.indexOf('\n}\n', start)
  return source.slice(start, end + 2)
}

// A numeric constant of main.js, worked out from its own line, so this follows the source.
function constant (name) {
  const match = source.match(new RegExp(`^const ${name} = ([0-9 *]+)$`, 'm'))
  if (!match) throw new Error(`main.js has no plain numeric constant ${name} - update this selftest`)
  return match[1].split('*').reduce((product, part) => product * Number(part.trim()), 1)
}

const DISCORD_TIMEOUT_MS = constant('DISCORD_TIMEOUT_MS')
const RENEW_AHEAD_MS = constant('RENEW_AHEAD_MS')

const HOUR = 60 * 60 * 1000
const DAY = 24 * HOUR
const WEEK_SECONDS = 604800
const NOW = Date.UTC(2026, 10, 18, 12, 0, 0)

const FUNCTIONS = ['saveSession', 'loadSession', 'forgetSession', 'adoptTokens', 'tokenRefused',
  'describeDiscordFailure', 'renewSession', 'renewalDue', 'restoreSession',
  'hydrateUserFromToken', 'publicProfile', 'derivePlayerId']

// Encryption that is only a wrapper, so the test can read what was saved. `locked` is the
// case of a settings file written under another Windows account.
function fakeStorage ({ locked = false } = {}) {
  return {
    isEncryptionAvailable: () => true,
    encryptString: (text) => Buffer.from('sealed:' + text, 'utf8'),
    decryptString: (buffer) => {
      const text = buffer.toString('utf8')
      if (locked || !text.startsWith('sealed:')) throw new Error('Error while decrypting the ciphertext')
      return text.slice('sealed:'.length)
    }
  }
}

const seal = (text) => Buffer.from('sealed:' + text, 'utf8').toString('base64')
const unseal = (stored) => stored ? Buffer.from(stored, 'base64').toString('utf8').slice('sealed:'.length) : null
const savedSession = (session) => seal(JSON.stringify(session))

// What Discord says, per endpoint, in order. The last answer of a list repeats. An entry is
// { status, data } for an answer, or an Error for no answer at all.
const noAnswer = (code) => Object.assign(new Error(`connect ${code}`), { code })
const ME = { id: '80351110224678912', username: 'pelayo', global_name: 'Pelayo', avatar: null }
const identified = { status: 200, data: ME }
const member = { status: 200, data: { roles: [] } }
const freshPair = (n) => ({ status: 200, data: { access_token: `A${n}`, refresh_token: `R${n}`, expires_in: WEEK_SECONDS, token_type: 'Bearer' } })
const refused = { status: 401, data: { message: '401: Unauthorized', code: 0 } }
const invalidGrant = { status: 400, data: { error: 'invalid_grant', error_description: 'Invalid "refresh_token" in request' } }
const invalidClient = { status: 401, data: { error: 'invalid_client' } }

function fakeDiscord (script) {
  const calls = []
  const answers = { me: [...(script.me || [])], member: [...(script.member || [member])], token: [...(script.token || [])] }

  const answer = (endpoint, record, options) => {
    calls.push({ endpoint, ...record, timeout: options ? options.timeout : undefined })

    const list = answers[endpoint]
    const said = list.length > 1 ? list.shift() : list[0]
    if (!said) throw noAnswer('ENOTFOUND')
    if (said instanceof Error) throw said

    // axios throws for anything outside 2xx unless the caller says otherwise.
    const accepted = options && options.validateStatus
      ? options.validateStatus(said.status)
      : said.status >= 200 && said.status < 300
    if (!accepted) {
      throw Object.assign(new Error(`Request failed with status code ${said.status}`),
        { response: { status: said.status, data: said.data } })
    }
    return { status: said.status, data: said.data }
  }

  return {
    calls,
    get: async (url, options) => answer(url.endsWith('/users/@me') ? 'me' : 'member',
      { bearer: options.headers.Authorization }, options),
    post: async (url, body, options) => answer('token', { form: Object.fromEntries(new URLSearchParams(body)) }, options)
  }
}

// The launcher's sign-in code with everything it touches named here, and nothing else.
function launcher ({ saved = null, discord = {}, locked = false, now = NOW, signingIn = false } = {}) {
  const settings = saved ? { token: saved, gameDir: 'D:\\Games\\Cyberpunk 2077' } : { gameDir: 'D:\\Games\\Cyberpunk 2077' }
  const log = []
  const axios = fakeDiscord(discord)

  const deps = {
    axios,
    safeStorage: fakeStorage({ locked }),
    loadSettings: () => settings,
    saveSettings: (patch) => Object.assign(settings, patch),
    launcherLog: (line) => log.push(line),
    Date: { now: () => now },
    DISCORD_CLIENT_ID: 'the-application-id',
    DISCORD_TIMEOUT_MS,
    RENEW_AHEAD_MS,
    GUILD_ID: 'the-guild',
    DISCORD_INVITE: 'https://discord.example/invite',
    ADMIN_DISCORD_IDS: [],
    refreshUserLevel: async () => 'player',
    isAdmin: () => false,
    fitWindowToRole: () => {},
    mainWindow: null,
    // main.js keeps these five in module scope; here they are the wrapper's own.
    accessToken: null,
    refreshToken: null,
    tokenExpiresAt: 0,
    currentUser: null,
    signInInProgress: signingIn
  }

  const body = FUNCTIONS.map(grab).join('\n\n')
  const peek = 'const peek = () => ({ accessToken, refreshToken, tokenExpiresAt, signedIn: Boolean(currentUser) })'
  // eslint-disable-next-line no-new-func
  const fns = new Function(...Object.keys(deps), `'use strict'\n${body}\n${peek}\nreturn { ${FUNCTIONS.join(', ')}, peek }`)(...Object.values(deps))

  return {
    ...fns,
    settings,
    log,
    calls: axios.calls,
    onDisk: () => {
      const plain = unseal(settings.token)
      if (!plain) return null
      try { return JSON.parse(plain) } catch { return plain }
    },
    asked: () => axios.calls.map((call) => call.endpoint).join(' ')
  }
}

const goodFor = (ms) => ({ access: 'A1', refresh: 'R1', expiresAt: NOW + ms })

// ---------------------------------------------------------------------------
// Part 1 - what is saved, and what an older launcher saved.

{
  const l = launcher()
  l.adoptTokens({ access_token: 'A1', refresh_token: 'R1', expires_in: WEEK_SECONDS })
  check('a sign-in keeps the access token, the refresh token and when it runs out',
    l.peek(), { accessToken: 'A1', refreshToken: 'R1', tokenExpiresAt: NOW + 7 * DAY, signedIn: false })

  l.saveSession()
  check('all three are saved', l.onDisk(), { access: 'A1', refresh: 'R1', expiresAt: NOW + 7 * DAY })

  l.adoptTokens({ access_token: 'A2', expires_in: WEEK_SECONDS })
  check('an answer with no new refresh token keeps the one already held', l.peek().refreshToken, 'R1')

  l.forgetSession()
  check('forgetting clears memory', l.peek(), { accessToken: null, refreshToken: null, tokenExpiresAt: 0, signedIn: false })
  check('and the copy on disk', l.settings.token, null)
  check('and leaves every other setting alone', l.settings.gameDir, 'D:\\Games\\Cyberpunk 2077')
}

{
  const l = launcher({ saved: seal('BARE-TOKEN-FROM-AN-OLDER-LAUNCHER') })
  check('a bare token saved by an older launcher still loads', l.loadSession(), true)
  check('as a sign-in that cannot be renewed and has no known end',
    l.peek(), { accessToken: 'BARE-TOKEN-FROM-AN-OLDER-LAUNCHER', refreshToken: null, tokenExpiresAt: 0, signedIn: false })
}

// ---------------------------------------------------------------------------
// Part 2 - what counts as Discord saying no.

{
  const l = launcher()
  const status = (code, data) => ({ response: { status: code, data } })
  check('a 401 is Discord refusing the token', l.tokenRefused(status(401)), true)
  check('a rate limit is not', l.tokenRefused(status(429)), false)
  check('a server error is not', l.tokenRefused(status(500)), false)
  check('a 403 is not', l.tokenRefused(status(403)), false)
  check('no answer at all is not', l.tokenRefused(noAnswer('ENOTFOUND')), false)
  check('a timeout is not', l.tokenRefused(noAnswer('ECONNABORTED')), false)
  check('nothing is not', l.tokenRefused(undefined), false)

  check('the log names what Discord said', l.describeDiscordFailure(status(401, { error: 'invalid_client' })), 'Discord answered 401 invalid_client')
  check('or the status alone', l.describeDiscordFailure(status(429, {})), 'Discord answered 429')
  check('or that nothing came back, and why', l.describeDiscordFailure(noAnswer('ENOTFOUND')), 'no answer (ENOTFOUND)')
}

{
  const due = (session) => { const l = launcher({ saved: savedSession(session) }); l.loadSession(); return l.renewalDue() }
  check('two days left is not due for renewal', due(goodFor(2 * DAY)), false)
  check('23 hours left is due', due(goodFor(23 * HOUR)), true)
  check('already past its end is due', due(goodFor(-HOUR)), true)
  check('no refresh token is never due', due({ access: 'A1', refresh: null, expiresAt: NOW - HOUR }), false)
  check('no known end is never due', due({ access: 'A1', refresh: 'R1', expiresAt: 0 }), false)
}

// ---------------------------------------------------------------------------
// Part 3 - the start of the launcher, case by case.

// Nothing to restore.
{
  const l = launcher()
  check('nothing saved: not signed in, and Discord is not asked', [await l.restoreSession(), l.asked()], [{ ok: false }, ''])
}

{
  const l = launcher({ saved: savedSession(goodFor(5 * DAY)), locked: true })
  check('saved under another Windows account: not signed in, Discord not asked',
    [await l.restoreSession(), l.asked()], [{ ok: false }, ''])
  check('and the unreadable copy is left where it is', typeof l.settings.token, 'string')
}

// The ordinary start.
{
  const l = launcher({ saved: savedSession(goodFor(5 * DAY)), discord: { me: [identified] } })
  const result = await l.restoreSession()
  check('a good token signs in', [result.ok, result.user.handle], [true, 'pelayo'])
  check('with the saved token, and no renewal', [l.asked(), l.calls[0].bearer], ['me member', 'Bearer A1'])
  check('the profile handed to the window has no Discord id in it', 'id' in result.user, false)
  check('the copy on disk is unchanged', l.onDisk(), goodFor(5 * DAY))
}

// THE BUG: Discord does not answer, and the saved sign-in must survive it.
for (const [what, said] of [
  ['the network is not up yet', noAnswer('ENOTFOUND')],
  ['the connection times out', noAnswer('ECONNABORTED')],
  ['Discord rate-limits the launcher', { status: 429, data: { message: 'You are being rate limited.' } }],
  ['Discord has a server error', { status: 500, data: {} }],
  ['Discord is unavailable', { status: 503, data: {} }]
]) {
  const l = launcher({ saved: savedSession(goodFor(5 * DAY)), discord: { me: [said] } })
  const result = await l.restoreSession()

  check(`${what}: the window is told to wait, not that the sign-in is gone`,
    [result.ok, result.waiting, typeof result.message], [false, true, 'string'])
  check(`${what}: the saved sign-in is still on disk, untouched`, l.onDisk(), goodFor(5 * DAY))
  check(`${what}: nobody is treated as signed in meanwhile`, l.peek(), { accessToken: null, refreshToken: null, tokenExpiresAt: 0, signedIn: false })
  check(`${what}: the log says it was kept, and why`, l.log.some((line) => line.startsWith('saved sign-in: kept - ')), true)
}

// ...and the next ask, once Discord is back, signs in with the very same token.
{
  const l = launcher({ saved: savedSession(goodFor(5 * DAY)), discord: { me: [noAnswer('ENOTFOUND'), identified] } })
  const first = await l.restoreSession()
  const second = await l.restoreSession()
  check('asked again once Discord answers: signed in, with the token that was kept',
    [first.waiting, second.ok, l.calls[l.calls.length - 2].bearer], [true, true, 'Bearer A1'])
}

// Discord refuses the token.
{
  const l = launcher({ saved: seal('BARE-TOKEN-FROM-AN-OLDER-LAUNCHER'), discord: { me: [refused] } })
  check('refused, with nothing to renew it: signed out, not waiting', await l.restoreSession(), { ok: false })
  check('and only then is the saved sign-in deleted', l.settings.token, null)
  check('and the log says so', l.log.includes('saved sign-in: Discord refused it and it could not be renewed - signed out'), true)
}

{
  const l = launcher({ saved: savedSession({ access: 'A1', refresh: 'R1', expiresAt: 0 }), discord: { me: [refused, identified], token: [freshPair(2)] } })
  const result = await l.restoreSession()
  check('refused, but renewable: renewed and signed in without anybody signing in again', [result.ok, l.asked()], [true, 'me token me member'])
  check('the renewal sends the refresh token and the application id, and no secret',
    l.calls[1].form, { client_id: 'the-application-id', grant_type: 'refresh_token', refresh_token: 'R1' })
  check('the identity check is repeated with the NEW token', l.calls[2].bearer, 'Bearer A2')
  check('and the new pair is what is saved', l.onDisk(), { access: 'A2', refresh: 'R2', expiresAt: NOW + 7 * DAY })
}

{
  const l = launcher({ saved: savedSession({ access: 'A1', refresh: 'R1', expiresAt: 0 }), discord: { me: [refused], token: [invalidGrant] } })
  check('refused, and the refresh token is spent: signed out', [await l.restoreSession(), l.settings.token], [{ ok: false }, null])
}

{
  // Discord's answer for this application on 2026-11-18: it will not renew without a secret.
  const l = launcher({ saved: savedSession(goodFor(-HOUR)), discord: { me: [refused], token: [invalidClient] } })
  check('expired, and Discord will not renew for this application: signed out, as before the fix',
    [await l.restoreSession(), l.settings.token], [{ ok: false }, null])
  check('renewal was asked for once, not twice', l.asked(), 'token me')
  check('and the log says what would make it work',
    l.log.some((line) => line.includes('invalid_client') && line.includes('Public Client')), true)
}

// Renewing before the end.
{
  const l = launcher({ saved: savedSession(goodFor(12 * HOUR)), discord: { me: [identified], token: [freshPair(2)] } })
  const result = await l.restoreSession()
  check('within a day of its end: renewed FIRST, then signed in with the new token',
    [result.ok, l.asked(), l.calls[1].bearer], [true, 'token me member', 'Bearer A2'])
  check('and good for another week', l.onDisk(), { access: 'A2', refresh: 'R2', expiresAt: NOW + 7 * DAY })
}

{
  const l = launcher({ saved: savedSession(goodFor(12 * HOUR)), discord: { me: [identified], token: [invalidClient] } })
  const result = await l.restoreSession()
  check('due, Discord will not renew for this application, token still good: signed in anyway', [result.ok, l.calls[1].bearer], [true, 'Bearer A1'])
  check('and the refresh token is KEPT, for the day the application is allowed to use it', l.onDisk(), goodFor(12 * HOUR))
}

{
  const l = launcher({ saved: savedSession(goodFor(12 * HOUR)), discord: { me: [identified], token: [invalidGrant] } })
  const result = await l.restoreSession()
  check('due, refresh token spent, token still good: signed in', result.ok, true)
  check('and the spent refresh token is dropped, the access token kept', l.onDisk(), { access: 'A1', refresh: null, expiresAt: NOW + 12 * HOUR })
}

{
  const l = launcher({ saved: savedSession(goodFor(12 * HOUR)), discord: { me: [noAnswer('ENOTFOUND')], token: [noAnswer('ENOTFOUND')] } })
  const result = await l.restoreSession()
  check('due, and Discord answers nothing at all: waiting, saved sign-in untouched', [result.waiting, l.onDisk()], [true, goodFor(12 * HOUR)])
}

{
  const l = launcher({ saved: savedSession(goodFor(12 * HOUR)), discord: { me: [noAnswer('ECONNRESET')], token: [freshPair(2)] } })
  const result = await l.restoreSession()
  check('renewed, then Discord stops answering: waiting, and the NEW pair is what is on disk',
    [result.waiting, l.onDisk()], [true, { access: 'A2', refresh: 'R2', expiresAt: NOW + 7 * DAY }])
}

// A sign-in by hand is under way.
{
  const l = launcher({ saved: savedSession(goodFor(5 * DAY)), discord: { me: [identified] }, signingIn: true })
  check('while somebody is signing in by hand: ask again later, and touch nothing',
    [await l.restoreSession(), l.asked(), l.peek().accessToken, l.onDisk()],
    [{ ok: false, waiting: true, message: null }, '', null, goodFor(5 * DAY)])
}

// ---------------------------------------------------------------------------
// Part 4 - no call to Discord may wait for ever.

{
  const l = launcher({ saved: savedSession({ access: 'A1', refresh: 'R1', expiresAt: 0 }), discord: { me: [refused, identified], token: [freshPair(2)] } })
  await l.restoreSession()
  const waits = l.calls.map((call) => `${call.endpoint}:${call.timeout}`)
  check('the identity check, the membership check and the renewal each carry the timeout',
    waits, [`me:${DISCORD_TIMEOUT_MS}`, `token:${DISCORD_TIMEOUT_MS}`, `me:${DISCORD_TIMEOUT_MS}`, `member:${DISCORD_TIMEOUT_MS}`])
}

console.log(`${passed} passed, ${failed} failed`)
if (failed) {
  console.log('The rule: only Discord REFUSING the token (a 401), with no way to renew it, may delete a')
  console.log('saved sign-in. Run it from code\\launcher-lite: node session.selftest.mjs')
}
process.exit(failed ? 1 : 0)
