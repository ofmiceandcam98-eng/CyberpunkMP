'use strict'

/*
    selftest.cjs - prove the manifest tooling's gates actually close.

    These four scripts are ship gates: the generator refuses broken component
    graphs, sign/verify decide whether a manifest is trusted at all. A gate
    that silently stopped closing is worse than no gate, because everyone
    downstream believes it. So this runs the real scripts as child processes -
    the same way Ship.ps1 will - against a throwaway tree, and checks both
    directions: the good path passes AND the bad paths refuse.

    Usage:
        node selftest.cjs

    Everything happens in a temp directory; the real key path is never touched
    (keygen is pointed at a temp file via NCO_MANIFEST_KEY_FILE).
*/

const { spawnSync } = require('node:child_process')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')

const here = __dirname
const failures = []

function check (name, ok, detail) {
  if (ok) {
    console.log(`PASS  ${name}`)
  } else {
    console.log(`FAIL  ${name}${detail ? ` - ${String(detail).trim()}` : ''}`)
    failures.push(name)
  }
}

function run (script, args, env) {
  return spawnSync(process.execPath, [path.join(here, script), ...args], {
    encoding: 'utf8',
    env: { ...process.env, ...(env || {}) }
  })
}

// Every test component carries the full mandatory field set so the only
// schema violation in any refusal test is the one that test plants.
function component (overrides) {
  return Object.assign({
    id: 'component', name: 'Component', types: ['red4ext_plugin'], class: 'nexus',
    version: '1.0.0', required: false, networkImpact: 'none', audience: 'all',
    author: 'selftest', source: 'https://example.invalid/selftest', license: 'MIT',
    nexus: { modId: 1 }
  }, overrides)
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'nco-manifest-selftest-'))

try {
  // --- a staged tree of two dummy files, mirroring ModPayload's shape --------
  const staged = path.join(tmp, 'staged')
  fs.mkdirSync(path.join(staged, 'assets', 'redscript'), { recursive: true })
  fs.writeFileSync(path.join(staged, 'CyberpunkMP.dll'), 'not a real dll - selftest bytes one\n')
  fs.writeFileSync(path.join(staged, 'assets', 'redscript', 'Selftest.reds'), '// selftest bytes two\n')

  // The generator finds prerequisite zips relative to the source file, so the
  // temp source gets the same directory shape publish/ has. The zip's content
  // is irrelevant - only its bytes are hashed.
  const pub = path.join(tmp, 'publish')
  const prereqs = path.join(pub, 'fullinstall-base', 'prerequisites')
  fs.mkdirSync(prereqs, { recursive: true })
  fs.writeFileSync(path.join(prereqs, 'FakeFramework-1.0.0.zip'), 'not a real zip - selftest prerequisite\n')

  // Every real ship passes --payload-zip (Ship.ps1). The fixture did not, so it was
  // generating a manifest no ship would ever produce - and the payload component's
  // missing archive.sha256, which disables every manifest check on BOTH sides, sailed
  // straight through this selftest for three releases. Test the shipping path, or the
  // test is theatre.
  const payloadZip = path.join(tmp, 'ModPayload.zip')
  fs.writeFileSync(payloadZip, 'not a real zip - selftest payload')

  const goodSource = path.join(pub, 'manifest-source.json')
  fs.writeFileSync(goodSource, JSON.stringify({
    game: { id: 'cyberpunk2077', supportedVersion: '2.31', enforce: 'warn' },
    components: [
      // The fixture framework borrows the real red4ext id: required:true is an
      // allowlist in the generator (the helper rule), and a made-up id carrying
      // it would be refused - which is itself tested below.
      component({ id: 'cyberpunk_multiplayer', name: 'CyberpunkMP', class: 'payload', version: undefined, required: true, networkImpact: 'critical', nexus: undefined, dependencies: [{ id: 'red4ext' }] }),
      component({ id: 'red4ext', name: 'Fake Framework', class: 'bundled', required: true, networkImpact: 'critical', nexus: undefined, archive: { name: 'FakeFramework-1.0.0.zip' } })
    ],
    loadRules: [],
    compatibility: { entries: [] },
    policy: { unknownMods: 'warn' }
  }, null, 2))

  const genArgs = out => [
    '--staged', staged, '--source', goodSource, '--out', out,
    '--release', 'v0.0.1', '--channel', 'development',
    '--protocol-client', '0x8579ff3e88d82943', '--protocol-server', 'bfc3bfaab24320a0',
    '--payload-zip', payloadZip
  ]

  // --- generation + determinism ---------------------------------------------
  const out1 = path.join(tmp, 'server-manifest.json')
  const out2 = path.join(tmp, 'server-manifest-rerun.json')

  const g1 = run('generate-manifest.cjs', genArgs(out1))
  check('generator: clean source generates', g1.status === 0, g1.stderr || g1.stdout)

  const g2 = run('generate-manifest.cjs', genArgs(out2))
  check('generator: rerun exits 0', g2.status === 0, g2.stderr || g2.stdout)
  check('generator: two runs are byte-identical',
    g1.status === 0 && g2.status === 0 && fs.readFileSync(out1).equals(fs.readFileSync(out2)))

  if (g1.status === 0) {
    const manifest = JSON.parse(fs.readFileSync(out1, 'utf8'))
    check('generator: both staged files hashed under the plugin prefix',
      manifest.client.payload.files.length === 2 &&
      manifest.client.payload.files.every(f => f.path.startsWith('red4ext/plugins/zzzCyberpunkMP/') && /^[0-9a-f]{64}$/.test(f.sha256)))
    const installOrder = manifest.loadRules.find(r => r.rule === 'install_order')
    check('generator: install order derived from the graph',
      !!installOrder && installOrder.order.join(',') === 'red4ext,cyberpunk_multiplayer')
    check('generator: bundled prerequisite zip hashed',
      /^[0-9a-f]{64}$/.test(manifest.components.find(c => c.id === 'red4ext').archive.sha256))

    // THE REGRESSION THAT DISABLED EVERY MANIFEST CHECK ON BOTH SIDES.
    //
    // The install digest is computed from `id:version:archive.sha256` for every component
    // with required:true and audience:"all". The payload component is required, so both
    // implementations reach it - and until 2026-09-07 the generator never gave it an
    // archive block, because only class:bundled components got hashed. Result: the
    // launcher threw (manifest.js:361) and the server logged "missing version/archive
    // hash - manifest checks stay disabled" and cleared the version (GameServer.cpp:823).
    //
    // Nothing failed loudly enough to notice. The servers simply reported ManifestVersion
    // "" forever, which read as "nobody armed it" rather than "it cannot be armed".
    const payloadComp = manifest.components.find(c => c.class === 'payload')
    check('generator: the payload component carries an archive hash',
      !!payloadComp && /^[0-9a-f]{64}$/.test(payloadComp.archive && payloadComp.archive.sha256))
    check('generator: payload component hash matches client.payload.archive',
      !!payloadComp && payloadComp.archive.sha256 === manifest.client.payload.archive.sha256)

    // The invariant both consumers actually depend on, stated once.
    const undigestible = manifest.components
      .filter(c => c.required === true && (c.audience || 'all') === 'all')
      .filter(c => !c.version || !(c.archive && c.archive.sha256))
      .map(c => c.id)
    check('generator: every required component can be digested', undigestible.length === 0,
      undigestible.join(', '))

    // Same-day serial arithmetic: a second manifest generated against the
    // first must advance NN, nothing else.
    const g3 = run('generate-manifest.cjs', [...genArgs(path.join(tmp, 'serial-check.json')), '--previous-manifest', out1])
    const v1 = manifest.manifestVersion
    const v3 = g3.status === 0 ? JSON.parse(fs.readFileSync(path.join(tmp, 'serial-check.json'), 'utf8')).manifestVersion : '(generation failed)'
    check('generator: same-day serial increments',
      g3.status === 0 && v3 === v1.replace(/\.(\d+)$/, (_, n) => '.' + String(parseInt(n, 10) + 1).padStart(2, '0')),
      `previous ${v1}, next ${v3}`)
  }

  // --- refusal: dependency cycle ---------------------------------------------
  const cycleSource = path.join(pub, 'cycle-source.json')
  fs.writeFileSync(cycleSource, JSON.stringify({
    game: { id: 'cyberpunk2077', supportedVersion: '2.31', enforce: 'warn' },
    components: [
      component({ id: 'cyberpunk_multiplayer', name: 'CyberpunkMP', class: 'payload', version: undefined, required: true, networkImpact: 'critical', nexus: undefined }),
      component({ id: 'loop_a', name: 'Loop A', dependencies: [{ id: 'loop_b' }] }),
      component({ id: 'loop_b', name: 'Loop B', nexus: { modId: 2 }, dependencies: [{ id: 'loop_a' }] })
    ],
    loadRules: [],
    compatibility: { entries: [] },
    policy: { unknownMods: 'warn' }
  }, null, 2))

  const gc = run('generate-manifest.cjs', ['--staged', staged, '--source', cycleSource, '--out', path.join(tmp, 'never.json'),
    '--release', 'v0.0.1', '--channel', 'development', '--protocol-client', '1', '--protocol-server', '2', '--payload-zip', payloadZip])
  check('generator: refuses a dependency cycle', gc.status !== 0)
  check('generator: the refusal names the loop members',
    gc.status !== 0 && gc.stderr.includes('RELEASE BLOCKED') && gc.stderr.includes('loop_a') && gc.stderr.includes('loop_b'),
    gc.stderr)

  // --- refusal: dependency on a component that does not exist ----------------
  const ghostSource = path.join(pub, 'ghost-source.json')
  fs.writeFileSync(ghostSource, JSON.stringify({
    game: { id: 'cyberpunk2077', supportedVersion: '2.31', enforce: 'warn' },
    components: [
      component({ id: 'cyberpunk_multiplayer', name: 'CyberpunkMP', class: 'payload', version: undefined, required: true, networkImpact: 'critical', nexus: undefined, dependencies: [{ id: 'ghost' }] })
    ],
    loadRules: [],
    compatibility: { entries: [] },
    policy: { unknownMods: 'warn' }
  }, null, 2))

  const gg = run('generate-manifest.cjs', ['--staged', staged, '--source', ghostSource, '--out', path.join(tmp, 'never2.json'),
    '--release', 'v0.0.1', '--channel', 'development', '--protocol-client', '1', '--protocol-server', '2', '--payload-zip', payloadZip])
  check('generator: refuses a dependency naming no component',
    gg.status !== 0 && gg.stderr.includes('ghost'), gg.stderr)

  // --- refusals: the helper rule (crew decree 2026-08-22) ---------------------
  // Content mods are never load-bearing. Three doors, each planted and each
  // expected slammed: a required nexus component (the digest admission test),
  // a required flag on an id outside the allowlist (the reclassification
  // bypass), and a load-bearing component depending on a nexus one (building
  // the system on a mod).
  const decreeCases = [
    ['a class:nexus component marked required',
      [component({ id: 'cyberpunk_multiplayer', name: 'CyberpunkMP', class: 'payload', version: undefined, required: true, networkImpact: 'critical', nexus: undefined }),
        component({ id: 'sneaky_mod', name: 'Sneaky Mod', required: true })],
      'sneaky_mod', 'helpers, not variables'],
    ['required:true on an id outside the load-bearing allowlist',
      [component({ id: 'cyberpunk_multiplayer', name: 'CyberpunkMP', class: 'payload', version: undefined, required: true, networkImpact: 'critical', nexus: undefined }),
        component({ id: 'not_a_framework', name: 'Not A Framework', class: 'bundled', required: true, nexus: undefined, archive: { name: 'FakeFramework-1.0.0.zip' } })],
      'not_a_framework', 'reserved for the payload'],
    ['a load-bearing component depending on a nexus one',
      [component({ id: 'cyberpunk_multiplayer', name: 'CyberpunkMP', class: 'payload', version: undefined, required: true, networkImpact: 'critical', nexus: undefined, dependencies: [{ id: 'helper_mod' }] }),
        component({ id: 'helper_mod', name: 'Helper Mod' })],
      'helper_mod', 'cannot build on content mods']
  ]
  for (const [label, comps, needle, phrase] of decreeCases) {
    const src = path.join(pub, `decree-${needle}.json`)
    fs.writeFileSync(src, JSON.stringify({
      game: { id: 'cyberpunk2077', supportedVersion: '2.31', enforce: 'warn' },
      components: comps, loadRules: [], compatibility: { entries: [] }, policy: { unknownMods: 'warn' }
    }, null, 2))
    const gd = run('generate-manifest.cjs', ['--staged', staged, '--source', src, '--out', path.join(tmp, `never-${needle}.json`),
      '--release', 'v0.0.1', '--channel', 'development', '--protocol-client', '1', '--protocol-server', '2', '--payload-zip', payloadZip])
    check(`generator: refuses ${label}`,
      gd.status !== 0 && gd.stderr.includes(needle) && gd.stderr.includes(phrase), gd.stderr)
  }

  // --- keygen + sign + verify roundtrip --------------------------------------
  const keyPath = path.join(tmp, 'signing-key')
  const keyEnv = { NCO_MANIFEST_KEY_FILE: keyPath }

  const kg = run('keygen.cjs', [], keyEnv)
  const pubLine = (kg.stdout || '').split(/\r?\n/)[0]
  check('keygen: generates a key and prints the public line',
    kg.status === 0 && pubLine.startsWith('ed25519-public:') && fs.existsSync(keyPath) &&
    fs.readFileSync(keyPath, 'utf8').startsWith('ed25519-secret:'),
    kg.stderr || kg.stdout)

  const kg2 = run('keygen.cjs', [], keyEnv)
  check('keygen: refuses to overwrite an existing key', kg2.status !== 0)

  const sMissing = run('sign.cjs', [out1], { NCO_MANIFEST_KEY_FILE: path.join(tmp, 'no-such-key') })
  check('sign: refuses when the key file is missing', sMissing.status !== 0)

  const sg = run('sign.cjs', [out1], keyEnv)
  check('sign: signs the manifest', sg.status === 0 && fs.existsSync(out1 + '.sig'), sg.stderr || sg.stdout)

  const vLiteral = run('verify.cjs', [out1, pubLine])
  check('verify: accepts the signature (public key passed as a literal line)',
    vLiteral.status === 0 && vLiteral.stdout.includes(pubLine.split(':')[2]),
    vLiteral.stderr || vLiteral.stdout)

  // A pin file the way the launcher will hold one - with unrelated lines that
  // must be ignored, not tripped over.
  const pins = path.join(tmp, 'pinned-keys.txt')
  fs.writeFileSync(pins, '# pinned manifest signing keys\n' + pubLine + '\n')
  const vFile = run('verify.cjs', [out1, pins])
  check('verify: accepts the signature (public keys read from a file)', vFile.status === 0, vFile.stderr || vFile.stdout)

  // --- verify must fail on tamper --------------------------------------------
  const tampered = path.join(tmp, 'tampered.json')
  const bytes = fs.readFileSync(out1)
  bytes[Math.floor(bytes.length / 2)] ^= 0x01
  fs.writeFileSync(tampered, bytes)
  fs.copyFileSync(out1 + '.sig', tampered + '.sig')
  const vTampered = run('verify.cjs', [tampered, pubLine])
  check('verify: rejects a tampered file', vTampered.status !== 0)
} finally {
  fs.rmSync(tmp, { recursive: true, force: true })
}

console.log('')
if (failures.length === 0) {
  console.log('selftest: all checks passed')
} else {
  console.log(`selftest: ${failures.length} check(s) FAILED`)
  process.exit(1)
}
