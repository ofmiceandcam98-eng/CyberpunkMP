/*
 * Selftest for the publish-time address redaction.
 *
 *   node code/coord-api/redact.selftest.mjs
 *
 * Beside its module rather than in tools/tests/ because it needs no build and no server -
 * same shape as manifest.selftest.mjs in the launcher. It exists because the cost of this
 * function being subtly wrong is infrastructure addresses on a public release asset, and
 * "I read the regex and it looked right" is not evidence.
 */

import { createRequire } from 'node:module'
const require = createRequire(import.meta.url)
const { redact } = require('./redact.js')

let pass = 0
let fail = 0

function check (name, actual, expected) {
  if (actual === expected) {
    pass++
  } else {
    fail++
    console.log(`  FAIL  ${name}`)
    console.log(`        expected: ${expected}`)
    console.log(`        actual:   ${actual}`)
  }
}

const TN = '<tailnet-address redacted>'
const PV = '<private-address redacted>'

// --- the addresses this project actually uses -------------------------------
check('live server', redact('game at 100.109.52.23:11778'), `game at ${TN}:11778`)
check('test server', redact('test at 100.106.1.67:11778'), `test at ${TN}:11778`)
check('server host', redact('ssh zeldfep@100.74.122.79'), `ssh zeldfep@${TN}`)
check('retired live', redact('was 100.80.243.29'), `was ${TN}`)
check('retired test', redact('was 100.125.74.56'), `was ${TN}`)
check('old NAS LAN', redact('ssh truenas_admin@10.27.27.223'), `ssh truenas_admin@${PV}`)

// --- CGNAT boundaries. 100.64 and 100.127 are IN, 100.63 and 100.128 are OUT.
check('lower bound in', redact('100.64.0.0'), TN)
check('upper bound in', redact('100.127.255.255'), TN)
check('below range out', redact('100.63.255.255'), '100.63.255.255')
check('above range out', redact('100.128.0.0'), '100.128.0.0')

// --- public addresses MUST survive. Redacting these would corrupt real content.
check('cloudflare dns', redact('1.1.1.1'), '1.1.1.1')
check('google dns', redact('8.8.8.8'), '8.8.8.8')
check('public 100.x', redact('100.200.1.1'), '100.200.1.1')
check('discord ip', redact('162.159.128.233'), '162.159.128.233')

// --- other private space --------------------------------------------------
check('rfc1918 192', redact('192.168.1.5'), PV)
check('rfc1918 172 in', redact('172.20.0.4'), PV)
check('172.15 is public', redact('172.15.0.1'), '172.15.0.1')
check('172.32 is public', redact('172.32.0.1'), '172.32.0.1')

// --- several in one string, which is what a real post body looks like -------
check('multiple in one body',
  redact('feed moved 100.80.243.29 -> 100.109.52.23, NAS 10.27.27.223 retired'),
  `feed moved ${TN} -> ${TN}, NAS ${PV} retired`)

// --- shape: an update is a nested object and must round-trip ----------------
const update = {
  id: '20260906-abc',
  at: '2026-09-06T22:00:00Z',
  kind: 'status',
  title: 'live at 100.109.52.23',
  body: 'ssh truenas_admin@10.27.27.223 for the old box',
  refs: ['100.106.1.67', 'abc1234'],
  count: 95,
  ok: true,
  nothing: null
}
const out = redact(update)
check('nested title', out.title, `live at ${TN}`)
check('nested body', out.body, `ssh truenas_admin@${PV} for the old box`)
check('array element redacted', out.refs[0], TN)
check('array element untouched', out.refs[1], 'abc1234')
check('number survives', out.count, 95)
check('boolean survives', out.ok, true)
check('null survives', out.nothing, null)
check('id survives', out.id, '20260906-abc')

// --- the input must not be mutated: the server keeps the real record --------
check('input not mutated', update.title, 'live at 100.109.52.23')

console.log(`\nredact: ${pass} passed, ${fail} failed`)
process.exit(fail === 0 ? 0 : 1)
