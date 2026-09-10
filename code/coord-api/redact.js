'use strict'

/*
 * Redact internal addresses on the way OUT of the coordination feed.
 *
 * WHY THIS EXISTS. publish/ is not a local folder: assistant-updates.json is uploaded as a
 * GitHub release asset and ASSISTANT_UPDATES.md is committed to a public repo. So every
 * address either assistant stream writes into a post body ends up on a public URL - and
 * both streams write addresses constantly, correctly, because a diagnosis without the
 * address it applies to is useless to the other stream.
 *
 * So the RECORD keeps them and the PUBLICATION does not. coord-data/updates.jsonl on the
 * server is untouched and remains the source of truth; only the slice that leaves the
 * machine is scrubbed. Redacting at write time would have destroyed the thing that makes
 * the feed worth reading in the first place.
 *
 * Deliberately blunt: a false positive costs a reader one lookup, a false negative puts
 * infrastructure on a public URL. Those are not comparable.
 */

const REDACTIONS = [
  // Tailscale hands out CGNAT space: 100.64.0.0/10, i.e. 100.64.x.x - 100.127.x.x.
  // Anchored so that 100.200.x.x and 100.7.x.x - ordinary public addresses - are left alone.
  [/\b100\.(6[4-9]|[7-9]\d|1[01]\d|12[0-7])\.\d{1,3}\.\d{1,3}\b/g, '<tailnet-address redacted>'],
  [/\b10\.\d{1,3}\.\d{1,3}\.\d{1,3}\b/g, '<private-address redacted>'],
  [/\b192\.168\.\d{1,3}\.\d{1,3}\b/g, '<private-address redacted>'],
  [/\b172\.(1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}\b/g, '<private-address redacted>'],

  // MagicDNS HOSTNAMES. The IP rules above miss the NAMES, and the docs treat those names as
  // not-for-publication - docs/CLAUDE-HANDOFF.md placeholders them as <server-host> /
  // <live-server>. But a post that said "officialcutstudios01:11782" or
  // "nco-test-server-1.tail1de33b.ts.net" reached the public asset intact until this was
  // added (found 2026-09-08 while writing an Atlas announcement that would have leaked it).
  //
  // The node set is small and known, so bare names are an EXPLICIT list, never a pattern -
  // a hostname-shaped pattern would scrub half the English in a post body. The one pattern
  // is the FQDN, because nothing but a real MagicDNS name is shaped like `<anything>.ts.net`,
  // and it runs FIRST so a fully-qualified name is taken whole before the bare-name rule can
  // leave a dangling `.tailXXXX.ts.net`.
  //
  // Deliberately NOT listed: `stream`. It is the word both assistant streams use for
  // themselves in nearly every post; redacting it bare would scrub the feed to noise. Its
  // FQDN form (`stream.tailXXXX.ts.net`) is still caught by the rule above it.
  [/\b[a-z0-9.-]+\.ts\.net\b/gi, '<tailnet-host redacted>'],
  [/\b(?:nco-test-server-1|nco-server-1|nco-test-server|nco-server|officialcutstudios01|desktop-jebd9rn|zeldfep-pc)\b/gi, '<tailnet-host redacted>']
]

/**
 * Walks strings, arrays and plain objects. Anything else is returned untouched, so numbers,
 * booleans and nulls survive a round trip unchanged - an update's `at` and `kind` must come
 * out the far side identical or the published slice stops matching the record.
 */
function redact (value) {
  if (typeof value === 'string') {
    let out = value
    for (const [pattern, replacement] of REDACTIONS) out = out.replace(pattern, replacement)
    return out
  }

  if (Array.isArray(value)) return value.map(redact)

  if (value && typeof value === 'object') {
    const out = {}
    for (const [k, v] of Object.entries(value)) out[k] = redact(v)
    return out
  }

  return value
}

module.exports = { redact, REDACTIONS }
