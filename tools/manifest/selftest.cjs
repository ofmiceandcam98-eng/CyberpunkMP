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

  const goodSource = path.join(pub, 'manifest-source.json')
  fs.writeFileSync(goodSource, JSON.stringify({
    game: { id: 'cyberpunk2077', supportedVersion: '2.31', enforce: 'warn' },
    components: [
      component({ id: 'cyberpunk_multiplayer', name: 'CyberpunkMP', class: 'payload', version: undefined, required: true, networkImpact: 'critical', nexus: undefined, dependencies: [{ id: 'fake_framework' }] }),
      component({ id: 'fake_framework', name: 'Fake Framework', class: 'bundled', required: true, networkImpact: 'critical', nexus: undefined, archive: { name: 'FakeFramework-1.0.0.zip' } })
    ],
    loadRules: [],
    compatibility: { entries: [] },
    policy: { unknownMods: 'warn' }
  }, null, 2))

  const genArgs = out => [
    '--staged', staged, '--source', goodSource, '--out', out,
    '--release', 'v0.0.1', '--channel', 'development',
    '--protocol-client', '0x8579ff3e88d82943', '--protocol-server', 'bfc3bfaab24320a0'
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
      !!installOrder && installOrder.order.join(',') === 'fake_framework,cyberpunk_multiplayer')
    check('generator: bundled prerequisite zip hashed',
      /^[0-9a-f]{64}$/.test(manifest.components.find(c => c.id === 'fake_framework').archive.sha256))

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
    '--release', 'v0.0.1', '--channel', 'development', '--protocol-client', '1', '--protocol-server', '2'])
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
    '--release', 'v0.0.1', '--channel', 'development', '--protocol-client', '1', '--protocol-server', '2'])
  check('generator: refuses a dependency naming no component',
    gg.status !== 0 && gg.stderr.includes('ghost'), gg.stderr)

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
