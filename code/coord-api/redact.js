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
  [/\b172\.(1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}\b/g, '<private-address redacted>']
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
