# Server Manifest & Compatibility Architecture

**Status: DESIGN — nothing here is implemented yet.** This is the architecture and
data model for making the launcher the authoritative client distribution and
compatibility system, per the spec zeldfep brought on 2026-08-21. Implementation
is milestoned at the end; milestones 0 and 1 are concrete enough to start from.

Grounding: a full inspection of the codebase (7 parallel readers over server,
client, launcher, distribution, mod systems, versioning, Nexus constraints),
followed by an adversarial review pass (3 independent reviewers: spec coverage,
codebase feasibility, security) whose 32 findings are incorporated below.
Claims about current behavior carry file:line evidence. Where the spec assumed
something this codebase contradicts, the contradiction is called out and the
design follows reality.

---

## 0. Goal, restated against reality

> Every player connected to the server is running the exact client-side
> multiplayer environment required by the server.

The spec's pipeline (manifest → launcher → verify → handshake → allow/deny) maps
onto systems that mostly already exist here in embryonic form:

| Spec concept | What exists today | Where |
|---|---|---|
| Canonical manifest | Four separate release assets: `server.json`, `modlist.json`, `roles.json`, `assistant-updates.json` | `publish/`, fetched from `releases/latest/download/` (main.js:238, 3308, 2702, 4138) |
| Mod download | Full Nexus integration: zip/rar/7z, premium API + free nxm:// route | main.js:125-156, 4885-4985 |
| File verification | **Existence checks only.** SHA-256 exists solely in the dev-only prerelease paths | main.js:1064-1156 (verifyInstall), 3788-3826 (mods:verify), 4546-4550 (the only real hashing) |
| Version checking | Asset-identity stamps (`assetId:size`), not versions; the DLL itself is unstamped | main.js:1038-1045; Main.cpp:130 (frozen 0.1.0) |
| Dependencies | `requires` topo-sort exists — but only the browse-to-Nexus path checks it (mods:open, main.js:3835-3851); `installMissing` and the nxm:// route install with no dependency check, and no entry populates `requires` anyway | main.js:3676-3717, 177-194, 4998-5035 |
| Conflict detection | None. No path→owner index; two mods writing one file is invisible | gap — record shape at main.js:4972-4979 has no reverse index |
| Load order | One honest constraint exists (the `zzz` prefix) — asserted in comments, verified by nothing | main.js:1187, publish/INSTALL.txt §2 |
| Client/server handshake | A strong protocol-shape gate (netpack kIdentifier) and Discord identity — but no version, mod, or file concept | GameServer.cpp:549-617; client.proto:7-12 |

So this is not a green-field build. It is a unification: one signed manifest
that the ship pipeline generates, the launcher enforces, and the server checks
at the door.

---

## 1. Facts that constrain the design

These override the spec where they conflict.

**F1 — Nexus mods cannot come from our CDN, and Nexus does not hand us hashes.**
Third-party mods are not ours to redistribute; the six MIT prerequisites in
FullInstall.zip are the deliberate exception (modlist.json:34-36,
main.js:3291-3306). Nexus's API refuses download links to free accounts (403 =
policy, main.js:118-124), and the sanctioned free route is the nxm://
handshake. Additionally — corrected by review — the v1 `files.json` response
carries **no md5/hash field** (its md5 surface is only the reverse
`md5_search` lookup), so there is no authoritative Nexus-supplied hash to
verify a download against. **Design consequence:** the manifest is
authoritative for *identity, version, and hash* of every component, but the
*transport* stays split: GitHub Releases for what we own, Nexus for what we
don't — and hashes for Nexus-sourced components are **ours**, recorded at
curation time (§3.3): a dev installs the pinned file once through the scanner,
the launcher hashes the archive and every extracted file, and those hashes go
into the manifest. Spec §36 is satisfied in spirit: every download is either
our release asset or the mod author's own Nexus listing; no third mirror.

**F2 — The protocol version is a shape-hash, not a number — better than a
number, with one scoped hole.** netpack hashes each .proto's own message
surface plus the *names* of its dependencies into `client::kIdentifier` /
`server::kIdentifier` (FNV1a64, netpack/main.cpp:92-154, 339-345), regenerated
unconditionally every build (tools/codegen/xmake.lua:28-43). Any change to
client.proto or server.proto moves the hashes. A hand-maintained
`protocolVersion: 12` (spec §27-28) would be strictly weaker. **The hole
(review finding):** imported message *content* is not hashed — a field added
to `Vector3` in common.proto changes the wire format of both sides while
moving neither identifier. Rule until fixed: **a common.proto change is
treated as a protocol change by convention** (both sides ship together, the
manifest's protocol note records it); milestone 2 extends HashProtocol to hash
dependency content and closes it structurally. **Design consequence:** keep
the hash as the wire gate; add the missing halves — a human-readable mapping
(manifest records which hashes belong to which release, fixing the
undiagnosable `Invalid protocol version!`) and a *readable* denial (today the
transport-layer check kicks silently before the application-layer check that
carries the error string ever runs — Server.cpp:289-328 vs
GameServer.cpp:553-572).

**F3 — "Load order" here is not ESP-style, and mostly is not an order at all.**
Verified per mechanism:
- **RED4ext plugins**: loaded per-folder; the only ordering lever anyone uses is
  alphabetical folder naming — asserted in our comments and INSTALL.txt, but the
  loader is not vendored and the SDK exposes no ordering API
  (vendor/RED4ext.SDK Api/v0 has none). The one real constraint:
  `zzzCyberpunkMP` must sort after `ArchiveXL`/`Codeware`/`TweakXL`/`input_loader`.
- **redscript**: one global compilation, all-or-nothing. There is no order; the
  invariant is *the set compiles*. One broken mod = game boots with NO scripts
  (main.js:1132-1138; the compiler config ships inside
  publish/fullinstall-base/prerequisites/redscript-0.5.31.zip as
  `r6/config/cybercmd/scc.toml` — a game-install file, not a repo file).
- **Archives**: ArchiveXL processes registered dirs in registration order with
  an unsorted directory iterator (vendor/ArchiveXL ArchiveService.cpp:24-107);
  the game's own `archive/pc/mod` scope loads alphabetically. No modlist.txt
  exists anywhere in this project.
- **Tweaks**: registration order, last-write-wins per record.

**Design consequence:** the manifest carries *checkable rules*, not a global
ordered list. Spec §9's flat list survives only as *install* order, derived
from the dependency graph. Spec §46's "load order verification" becomes
verification of the small set of real invariants above.

**F4 — Release assets are mutable and unsigned, and the writer set is wider
than it looks.** Ship.ps1, the publish-server-json workflow, and coord-api all
`--clobber` onto existing tags (Ship.ps1:801 etc.). The coord-api runs on the
NAS holding gh credentials with release-write — so the NAS, an always-on
internet-adjacent box, is currently inside the trust boundary of every asset
including `server.json`, which directs every launcher's game-server AND
coord-API host and is fetched with zero authenticity check. One tag can name
two different mod builds over time (mod-only ships clobber under an unchanged
tag); only the `assetId:size` stamp distinguishes them. **Design consequence:**
the manifest is *signed* with keys that never touch the NAS, which genuinely
removes the NAS from the data-asset trust boundary. What signing does and does
not buy is stated honestly in §10 — the launcher auto-update channel remains
the residual hole until it verifies signatures too (§10, milestone 2).

**F5 — The DLL doesn't know what it is.** No version resource, RED4ext version
frozen at 0.1.0 (Main.cpp:130), `BuildInfo.h` generated with real
branch/commit but #included by nothing, `Settings::Version` declared and never
written. The server's status API has no version field at all
(WebApi.cs:203-208). **Design consequence:** stamp the build (milestone 1);
until the DLL can say what it is, the handshake cannot check it.

**F6 — Trust boundaries: this is compatibility, not anticheat.** The server is
a Discord-gated RP server for a small trusted community. A modified client can
lie in any client-computed field (spec §23 concedes this). The design gets its
real assurance from three things that are hard to fake *accidentally*: the
protocol shape-hash (wrong build = wrong hash), the launcher's pre-flight file
verification, and the server-side manifest checks. Detecting a *deliberately*
hostile client is a non-goal — and every check that relies on client-supplied
data is explicitly labeled advisory below, never sold as a security gate.

---

## 2. The canonical manifest: `server-manifest.json`

One file, **generated by Ship.ps1 at ship time** (never hand-edited —
hand-edited hashes are wrong hashes), published as a release asset on the
existing channel, signed. The existing four JSONs stay published for shipped
launchers; §2.1 defines exactly when new code may fall back to them.

**What it deliberately does NOT contain:** the server address. `server.json`
moves hosts via its own republish workflow without a ship
(publish-server-json.yml); folding the address into a ship-generated file
would couple "the server moved" to "reship". Addressing stays operational —
but because a tampered `server.json` redirects both the game connection and
the Discord-token-bearing coord call, it gets its own signature and a host
allow-check in milestone 1 (§10; this was misstated as "tailnet-gated" in an
earlier draft — review established `requiresTailscale` is read by no code).

```jsonc
{
  "schema": 1,
  // Independent of the launcher version and the release tag (spec §29).
  // date.serial - two manifests can ship in one day; serial disambiguates.
  // Monotonic: the launcher refuses any manifest older than the last one it
  // accepted (§10, replay defense).
  "manifestVersion": "2026.08.21.01",
  "channel": "production",            // production | staging | development
  "generatedAt": "2026-08-21T21:16:08Z",
  "release": "v0.3.96",               // the release this manifest shipped on

  "game": {
    "id": "cyberpunk2077",
    "supportedVersion": "2.31",
    // 'warn' matches Main.cpp:57-71 ("a later patch may well work, and
    // blocking it would be presumptuous"). Flip to 'block' after a patch
    // that actually breaks us.
    "enforce": "warn"
  },

  // The shape-hashes this build was generated with, in hex - so a protocol
  // denial can finally name which side is stale (F2). Diagnostic, not a gate;
  // the wire gate is the hash comparison itself.
  "protocol": {
    "client": "0x8579ff3e88d82943",
    "server": "0xbfc3bfaab24320a0"
  },

  "client": {
    "minLauncher": "0.3.93",          // oldest launcher allowed to verify against this
    // Identity of the payload on this release, pinned BY CONTENT. No asset ids
    // here: GitHub mints a new asset id on every upload (including byte-identical
    // carry-forwards, Ship.ps1:655-681), so ids cannot be known before upload and
    // cannot name content. sha256 can, and is computable from the staged files
    // BEFORE anything is uploaded - which is what keeps manifest generation a
    // single pass (§9.1). The launcher's assetId:size stamp remains a private
    // freshness cache, not manifest data.
    "payload": {
      "archive": { "name": "ModPayload.zip", "sha256": "hex...", "size": 8813587 },
      "files": [
        { "path": "red4ext/plugins/zzzCyberpunkMP/CyberpunkMP.dll", "sha256": "hex...", "size": 4361216 },
        { "path": "red4ext/plugins/zzzCyberpunkMP/assets/redscript/MainMenu.reds", "sha256": "hex...", "size": 48213 }
        // ... every file Ship.ps1 stages into ModPayload.zip
      ]
    }
  },

  "components": [ /* §3 */ ],
  "loadRules":  [ /* §4 */ ],
  "compatibility": { /* §5 */ },

  "policy": {
    // allow | warn | block (spec §18). Start at 'warn': blocking on day one
    // punishes every existing install with cosmetic mods before we have a
    // whitelist. Move to 'block' once clientOnly whitelisting (§5) exists.
    // NOTE: 'block' is advisory-strength only - see §7.3.
    "unknownMods": "warn"
  }
}
```

**Signing.** `server-manifest.json.sig` published beside it: a **raw detached
ed25519 signature over the exact file bytes, base64, in a one-line container**
`ed25519:<base64 signature>:<key id>`. Deliberately NOT minisign format —
review established that a stock `.minisig` is ed25519 over a Blake2b-512
prehash plus a second global signature over the trusted comment, which
`tweetnacl` alone cannot verify. Raw detached ed25519 keeps both sides trivial:
Ship.ps1 signs via a 20-line Node script using `tweetnacl` (pure JS, no native
build step — the same reason 7zip-bin/node-unrar-js were chosen), the launcher
verifies with the same library. **Keys:** one keypair per owner (Cam, zeldfep),
generated and held on their machines — never in the repo, never on the NAS
(F4). The launcher pins the **set** of public keys with key ids; a manifest is
valid if any pinned key verifies it. That answers "who can ship": each owner
signs with their own key; a contributor without a key cannot ship a manifest
(and since every ship regenerates the manifest — §9.1 — cannot ship at all,
which matches how shipping actually works today). Rotation = launcher release
adding/removing a pinned key. Key-holding machines already hold the GitHub
release-write token, so a workstation compromise was already inside the trust
boundary; signing does not widen it (§10).

**Fetch path.** `releases/latest/download/server-manifest.json` — the existing
asset channel, a web redirect not subject to the 60/hr anonymous API limit that
already bit us (main.js:1512-1515). Staging manifests ride pre-releases exactly
like test builds do today (main.js:4460-4461).

### 2.1 Manifest availability states

Review found the earlier draft self-contradictory ("prefers" vs "fail
closed"). The states, explicitly:

| State | Behavior |
|---|---|
| **Fetched, signature valid, version ≥ last accepted** | Normal path. Persist as last-accepted (version + bytes) in userData. |
| **Fetched, signature valid, version < last accepted** | Rollback evidence. Refuse it, keep using the cached last-accepted manifest, surface a themed warning. An attacker replaying an old signed manifest gains nothing (§10). |
| **Fetched, signature INVALID or malformed** | Tampering evidence. Refuse Ready, show exactly what failed and say "report this" — never fall back to the legacy JSONs, because a downgrade-to-unverified fallback would make the signature worthless. The cached manifest is NOT used either: live tampering on the channel is the one state where refusing to play is the point. |
| **Unreachable (offline, GitHub down)** | Use the cached last-accepted manifest; a previously verified install may Play. This preserves today's deliberate offline-tolerant behavior (main.js:3774-3776, and `filesReady = installed && (upToDate || offline)`, index.html:1547-1563). Never verified before + unreachable = can't verify, can't Play — same as today's first-install-offline. |
| **Absent (release has no manifest at all)** | Migration state only: fall back to the legacy four-JSON path with a visible "environment unverified" note. This window exists so a new launcher works against pre-manifest releases; it closes N releases after manifests start shipping, after which absent-where-cached-expects-one is treated as unreachable (use cache), and absent-with-no-cache refuses Ready. |

The bad-ship scenario review raised (a forgotten `.sig` bricking everyone)
lands in "absent" during migration (harmless, legacy fallback) and in
"unreachable-equivalent" after (players ride the cached manifest; only
brand-new installs wait) — a bad ship inconveniences, it does not brick. A bad
*signature* on a real ship blocks Ready by design; Ship.ps1 verifies its own
signature as a publish gate (§9.1) so producing one requires the gate itself
to be bypassed.

---

## 3. Component model

Every managed thing gets a stable internal id (spec §4) — never a display name,
never a bare Nexus number in code paths.

Two canonical examples, one per interesting class (an earlier draft mixed them
into one self-contradictory example — review caught it):

```jsonc
// class:bundled - a redistributed MIT prerequisite
{
  "id": "codeware",
  "name": "Codeware",
  // Primary loading mechanism first; every mechanism the component touches is
  // listed. Consumers that care about mechanism (loadRules §4, scanner §5)
  // read "types"; UI reads types[0].
  "types": ["red4ext_plugin", "redscript"],
  "class": "bundled",                  // §3.2 - decides transport + verify depth
  "version": "1.18.0",
  "required": true,                    // required blocks Play; optional never does
  "networkImpact": "critical",         // none|low|medium|high|critical (spec §44)
  "audience": "all",                   // all | dev | server (spec §45)
  // Attribution (spec §37-38). We did not write most of these.
  "author": "psiberx",
  "source": "https://github.com/psiberx/cp2077-codeware",
  "license": "MIT",
  "archive": { "name": "Codeware-1.18.0.zip", "sha256": "hex...", "size": 1234567 },
  // File ownership (spec §5): every path this component writes, relative to
  // game root, forward slashes - same convention as mods-installed.json.
  "files": [
    { "path": "red4ext/plugins/Codeware/Codeware.dll", "sha256": "hex...", "size": 999 }
  ],
  "dependencies": [ { "id": "red4ext", "version": ">=1.29.0" } ]
}

// class:nexus - a curated third-party mod, transported by Nexus, hashed by us
{
  "id": "audioware",
  "name": "Audioware",
  "types": ["red4ext_plugin", "redscript"],
  "class": "nexus",
  "version": "1.4.2",
  "required": false,
  "networkImpact": "medium",
  "audience": "all",
  "author": "psiberx",
  "source": "https://www.nexusmods.com/cyberpunk2077/mods/12001",
  "license": "see Nexus page",
  // The pointer the transport needs, plus OUR hash of the pinned archive,
  // recorded at curation time (§3.3). Nexus's v1 API supplies no hash (F1).
  "nexus": { "modId": 12001, "fileId": 98765 },
  "archive": { "name": "Audioware-1.4.2.7z", "sha256": "hex...", "size": 2411520 },
  // For class:nexus, per-file lists are ALSO recorded at curation time by the
  // scanner install (§5), so ownership/conflict derivation covers them. A
  // player's install record (§3.4) re-learns them locally and must match.
  "files": [
    { "path": "red4ext/plugins/audioware/audioware.dll", "sha256": "hex...", "size": 999 }
  ],
  "dependencies": [ { "id": "red_data", "version": ">=0.9.0" } ]
}
```

### 3.1 Component types (spec §10)

`red4ext_plugin | redscript | archive | tweak | input | native_dll | config |
resource | multiplayer_core`. A type names the *loading mechanism*, which
decides which invariants from F3 apply. The field is `types` (array, primary
mechanism first) — the singular reads in earlier drafts are gone.

### 3.2 Distribution classes

The class decides transport, verification depth, and update authority:

| class | what | transport | hash source | verify depth |
|---|---|---|---|---|
| `payload` | CyberpunkMP itself (DLL + assets + Rpc) | ModPayload.zip / FullInstall.zip release assets | Ship.ps1 hashes every staged file | per-file sha256 |
| `bundled` | the six MIT prerequisites | inside FullInstall.zip | Ship.ps1 hashes the prerequisite zips + their contents | per-file sha256 |
| `nexus` | everything on the curated list | Nexus API (premium) or nxm:// (free) — never mirrored (F1) | **curation-time hashing by us** (§3.3): the scanner install records archive sha256 + per-file sha256 into the manifest entry. The launcher computes sha256 of every downloaded archive anyway (the buffer is already in memory in installModArchive, main.js:4885) and compares when a pin exists | archive sha256 at download + per-file sha256 at verify |

(Earlier drafts verified Nexus downloads against a `files.json` `md5_hash`
field. **That field does not exist in the v1 API** — review verified against
Nexus's own API client types. The md5 surface in v1 is only the reverse
`md5_search` lookup. Hence: our hashes, recorded at curation.)

The initial id set: `cyberpunk_multiplayer` (payload), `red4ext`, `redscript`,
`codeware`, `archive_xl`, `tweak_xl`, `input_loader` (bundled), and one id per
modlist entry (`fast_launch`, `native_settings_ui`, `dlc_call_off`,
`audioware`, `red_data`, …). The two entries with unconfirmed names
(nexusModId 4198, 22114) must be resolved before they get ids — and 4198 is
suspected to be a duplicate ArchiveXL install over the pinned prerequisite,
which the conflict engine (§5) would flag as its first real catch.

### 3.3 Server-approved version, not latest (spec §39)

Already half-true today via `nexusFileId` pinning (documented in
modlist.json:14-17) — but **no entry pins, and the install record's
fileId/version fields are populated from those absent modlist fields, so every
install records `undefined`** while the fileId actually downloaded via nxm://
is parsed and then discarded (main.js:5002 vs 4885). The manifest makes the
pin + curation-time hashes mandatory for every `nexus` component with
`networkImpact` ≥ medium, and the install record captures what Nexus *actually
served* (the served fileId, the computed archive sha256).

Note the double-encoding trap review flagged: "Audioware needs RedData" lives
in exactly one place — the `dependencies` edge. A compatibility entry
(§5) may additionally record that a pair was *tested together*, but its
`reason` never restates a requirement; requirements are graph edges, period.

### 3.4 Install records grow hashes

`mods-installed.json` per-mod record today: `{ name, fileId, version, files[],
at }` (main.js:4972-4979). It becomes:

```jsonc
{
  "id": "audioware",                    // manifest id, not just the Nexus number
  "nexusModId": 12001,
  "fileId": 98765,                      // what was ACTUALLY downloaded (nxm or API)
  "version": "1.4.2",
  "archiveSha256": "hex...",            // computed from the downloaded buffer
  "files": [
    { "path": "red4ext/plugins/audioware/audioware.dll", "sha256": "hex...", "size": 2411520 }
  ],
  "at": 1755812168000
}
```

Hashing happens in `installModArchive` where every entry already flows through
memory one at a time — the sha256 is nearly free at that point. This single
change turns `mods:verify` from "does a file exist and is it non-empty" into
real integrity checking, with a `[REPAIR]` path (spec §26): re-download from
the recorded source, or for payload files re-extract from ModPayload.zip.

---

## 4. Load rules (spec §9, §11 — reinterpreted per F3; verification per §46-47)

The manifest carries the checkable invariants, each tagged with its mechanism:

```jsonc
"loadRules": [
  {
    // The one real RED4ext ordering constraint in this project. Checkable by
    // pure string comparison of folder names present in red4ext/plugins.
    "rule": "folder_sorts_after",
    "mechanism": "red4ext_plugin",
    "subject": "zzzCyberpunkMP",
    "after": ["ArchiveXL", "Codeware", "TweakXL", "input_loader"],
    "severity": "critical",
    "reason": "RED4ext loads plugin folders alphabetically; the mod must load after the frameworks it binds at load time."
  },
  {
    // redscript has no order - the invariant is that the set compiles.
    // Enforced at ship time by the existing scc.exe staged-compile gate
    // (Ship.ps1:272-325); at verify time the launcher checks set completeness
    // against client.payload.files.
    "rule": "compilation_unit",
    "mechanism": "redscript",
    "severity": "critical",
    "reason": "One broken .reds aborts the entire modded compilation - the game boots with no scripts at all."
  },
  {
    // Install order. DERIVED: Ship.ps1 generates this array by topo-sorting
    // the components' dependency edges at manifest-generation time. The graph
    // is the single source of truth; this array is a cached result shipped so
    // the launcher does not re-derive it. If they ever disagree, the graph wins
    // and manifest generation fails loudly.
    "rule": "install_order",
    "mechanism": "installer",
    "order": ["red4ext", "redscript", "codeware", "archive_xl", "tweak_xl", "input_loader", "cyberpunk_multiplayer"]
  }
]
```

Dependency relationships form the graph (spec §11). Precision about the
existing resolver, per review: `sortByDependencies` (main.js:3687-3717)
**tolerates** cycles by appending unplaced members unsorted — it does not
refuse them. The manifest pipeline upgrades this in two places: manifest
*generation* refuses to emit a manifest whose graph has a cycle, naming the
loop members (spec §12); the *launcher* reports a cycle in whatever list it is
given the same way. And the `requires` gate must cover **all three install
paths** — today it guards only mods:open, not installMissing and not the
nxm:// route (milestone 1).

Verification honesty rule (spec §46): the launcher **verifies** rules; it never
silently reorders anything it doesn't have a mechanism-level basis to reorder.
The one exception that already exists and stays: duplicate mod copies are
parked to `red4ext/disabled-by-launcher/` before launch (main.js:824-866) —
that's remediation of a broken state, not reordering of a valid one.

---

## 5. Compatibility engine (spec §6, §13-17, §43, §62-63)

A section of the manifest, not a separate fetch (one signed artifact):

```jsonc
"compatibility": {
  "entries": [
    {
      "a": { "id": "cyberpunk_multiplayer" },
      "b": { "id": "cyber_engine_tweaks" },
      "status": "known_incompatible",
      "severity": "critical",
      "reason": "Running CET alongside caused a GPU hard-lock (INSTALL.txt §0); the mod ships its own ImGui backend for exactly this reason (ImGuiService.cpp:29-32).",
      "action": "Disable or remove CET before playing.",
      // How the launcher recognizes a component that will never have an
      // install record (review: without this, the one incompatibility we have
      // actually observed would be undetectable - CET lives in bin/x64, which
      // no install record and no plugin-folder scan covers).
      "detection": {
        "anyOf": [
          "bin/x64/version.dll",
          "bin/x64/plugins/cyber_engine_tweaks/"
        ]
      }
    },
    {
      "a": { "id": "audioware", "version": ">=1.0.0" },
      "b": { "id": "red_data",  "version": ">=0.9.0" },
      "status": "tested_compatible",
      "testedVersions": [["1.4.2", "0.9.1"]],
      "severity": "info",
      "reason": "Verified running together in a live session, 2026-08-21."
    }
  ]
}
```

- **Status vocabulary:** `tested_compatible | known_incompatible | potential |
  unknown`. Anything the engine cannot determine is **unknown, never
  compatible** (spec §62). Version-ranged on both sides (spec §63).
- **Severity:** `info | warning | error | critical` (spec §43). `error`+
  blocks Play for required components; `warning` shows but doesn't block.
- **Detection signatures:** a compatibility entry may carry `detection`
  (filesystem markers) so the engine can see components that are not — and
  never will be — launcher-managed. CET is the archetype.
- **File conflicts** (spec §5-6) are *derived*, not declared: the ownership
  index is the union of every manifest component's `files[]` (all classes —
  nexus components carry curation-time file lists, §3) plus every install
  record's `files[]`. Two owners for one path = conflict, reported with both
  owners named, then classified against `compatibility.entries` (a matching
  entry can mark a known-benign overlap); no entry = `potential`, and the file
  is **never silently overwritten** — `installModArchive` gains a
  pre-extraction ownership check (milestone 1).
- **Explanations** (spec §14): every entry carries `reason` and `action`.
  The UI never says just "INCOMPATIBLE".
- **The matrix** (spec §16) is a rendering of `entries` filtered to components
  that are installed *or detected via signatures* — dev-tab UI, generated,
  never hand-maintained.
- **Unmanaged mods** (spec §17): the launcher scans **four** surfaces it can
  reason about — `red4ext/plugins/*` folder names, `archive/pc/mod/*.archive`
  files, `r6/scripts/*` (unmanaged .reds — the mechanism F3 identifies as able
  to kill every script in the game), and `bin/x64/plugins/*` plus the
  `detection` markers above — and diffs against the ownership index. Unknown =
  reported as `UNMANAGED`, status unknown, with the policy (`allow|warn|block`)
  applied from the manifest. Never deleted, never touched (spec §58) —
  remediation for a blocked unmanaged mod is a message naming the file and
  where it is, full stop. A future `clientOnly` whitelist (spec §19) is a
  manifest `components` entry with `networkImpact: "none", required: false` —
  the schema needs nothing new.

**Developer mod scanner** (spec §42): a dev-tab tool that takes a Nexus mod id
or a local archive, extracts to a temp dir (never the game dir), classifies
every file by path convention (`red4ext/plugins/` → red4ext_plugin,
`r6/scripts|assets .reds` → redscript, `archive/pc/mod` → archive, `r6/tweaks`
→ tweak, `bin/` → native_dll — the layout knowledge already in F3), hashes the
archive and every file (this is where curation-time hashes come from, §3.2),
diffs against the ownership index for conflicts, and emits the §42 report with
`networkImpact: UNKNOWN` until a human classifies it. Its output is a
ready-to-review manifest component block — the on-ramp of the §61 workflow.

---

## 6. Client verification pipeline (spec §30-35, §50, §57-59)

Launcher states, in order, before Play unlocks:

```
fetch manifest → §2.1 state machine (signature, monotonicity, cache)
  → game found → game version read
  → diff installed vs manifest (stamps first, hashes on demand)
  → REMOVALS: components present in install records but gone from the manifest
     are retired - their recorded owned files removed (backup first, §6.1),
     shared/unknown paths never touched (spec §59). Exactly the existing
     mods:delete semantics (per-file, never directories, main.js:3908-3916),
     driven by the manifest diff instead of a click
  → download missing/outdated (only the delta - spec §30)
  → verify each download (sha256) BEFORE it touches the game dir
  → install (§6.1) → post-verify → record
  → dependency check → conflict check → load rules check
  → unmanaged scan + policy
  → READY (player UI: "✓ Ready to play · manifest 2026.08.21.01" - spec §52-53)
```

Every step writes one line into the existing rotating trail log
(launcher-trail.log, credential-presence-only policy unchanged) in the spec
§57 shape — `[18:32:12] manifest 2026.08.21.01 verified (key: cam)` — so "send
me your logs" keeps working for the new pipeline too (milestone 1, not an
afterthought).

Full hashing of every file on every boot is not free; the existing stamp
comparison stays as the fast path, and full hash verification runs on: first
install, after any update, on explicit Verify, and when the manifest version
changes. (Spec §23's "launcher verifies fully before launch, server checks
lightly at connect" — exactly this split.)

### 6.1 Transactional install (spec §33-35) — milestone 2, window acknowledged

Extends the pattern the prerelease path already uses (first-write-wins
`CyberpunkMP.dll.shipped` backup at main.js:4552-4561; restore at 4611-4648)
to every managed write:

```
download → staging file (userData/staging, never the game dir)
→ verify hash → backup: move each about-to-be-replaced file to
   userData/backup/<manifestVersion>/<path>
→ move staged files into place → post-verify hashes on disk
→ commit: write install record + stamp; delete backup on next successful boot
FAIL at any step → restore every backed-up file, delete staged, report
```

Rollback (spec §34) = re-applying the backup dir, kept for the previous
manifest version only (disk-bounded). The staging dir is transient; spec §56's
*cache* (reusing verified downloads across reinstalls) is a separate,
deliberate non-goal for now — payloads are small and Nexus links expire.
**Honest window (review):** until milestone 2, `installEverything`
(extractAllTo over gameDir, main.js:1250), `applyUpdate` (main.js:1531) and
the prerelease path keep overwrite semantics. Mitigation in milestone 1: those
paths write only paths the payload/bundled components own by construction, and
the new ownership index warns when that stops being true — but
backup-and-rollback protection genuinely does not exist until milestone 2, and
anyone implementing should know that.

"Resumable downloads" (spec §32): axios range-resume for our release assets is
cheap (milestone 2); Nexus links expire (`expires` param) so a Nexus resume is
a re-request — accepted limitation.

### 6.2 Repair

`[REPAIR]` re-fetches exactly the files whose hashes fail, from their recorded
class transport. The existing "Deep clean" (launcher footprint residue) is
unrelated and keeps its name — repair means *file integrity*, deep clean means
*uninstall hygiene* (they were already confused once; naming is load-bearing).

---

## 7. Handshake (spec §21-28, §50-51)

### 7.1 Protocol changes

Extending `AuthenticationRequest` changes client.proto/server.proto and
therefore moves both kIdentifiers — the flag-day is built in and
self-enforcing (verified against netpack/main.cpp:132-137, 339-343): old
clients are refused by the existing hash gate the moment the new server
deploys. (Note the F2 caveat: this self-enforcement holds for these files;
common.proto changes need the convention rule until milestone 2.)

```proto
message AuthenticationRequest {
  string username = 1;
  string token = 2;
  uint64 client_protocol = 3;
  uint64 server_protocol = 4;
  string build_stamp = 5;       // BUILD_COMMIT, finally #included (F5)
  string manifest_version = 6;  // what the launcher verified against
  string install_digest = 7;    // §7.2 - 64 lowercase hex chars
  // Unmanaged findings from all four scan surfaces (§5), type-prefixed:
  // "plugin:FlightControl", "archive:weather.archive", "script:foo.reds",
  // "marker:cyber_engine_tweaks". Advisory - see §7.3.
  repeated string unmanaged = 8;
}

message AuthenticationResponse {
  bool success = 1;
  string error = 2;                    // stays: human-readable, launcher-actionable
  Settings settings = 3;
  repeated CharacterSummary characters = 4;
  uint32 denial_code = 5;              // §7.3
  string required_manifest = 6;        // so the client can say WHICH version to update to
}
```

The launcher passes `--manifest-version=` and `--install-digest=` as launch
arguments — the established launcher→DLL channel (Settings.cpp:4-152).
**Encoding is mandated lowercase hex** (64 chars, `[0-9a-f]`): the game's own
argument parser is a black box we cannot inspect (RED4ext only exposes the
parsed map), the `=`-form is already load-bearing (Settings.cpp:123-128,
INSTALL.txt §4), and hex — unlike base64 — cannot collide with `=`/`/`
tokenization. `build_stamp` comes from the DLL itself: `BuildInfo.h` exists
with real values and is included by nothing (F5) — milestone 1 includes it in
`NetworkService.cpp` and stamps `RED4EXT_SEMVER` from the release at ship time.

### 7.2 install_digest — definition, and what it is not

**Input: manifest-declared fields only.** SHA-256 over the canonical string
built from the sorted list of `(id, version, archive.sha256)` of every
component with `required: true` and `audience: "all"`, plus
`client.payload.archive.sha256` and `manifestVersion`. Nothing
launcher-learned enters the digest — review established that per-file hashes
learned at install time (class:nexus, §3) would make the digest
irreproducible server-side. Both sides can compute this from the manifest
alone; the launcher asserts it only after its own deeper per-file verification
passed. Optional and dev-only components never enter it, so per-player
variance cannot break equality.

It is a *compatibility attestation*, not proof (F6): a hostile client can echo
the right digest, but an out-of-date, half-installed, or wrong-branch client
cannot produce it by accident — and accident is the entire observed failure
population so far (six-failure join sagas, protocol-mismatch fresh installs,
half-updated test builds).

### 7.3 Structured denial + the silent-kick fix

```
DENIAL_NONE = 0            PROTOCOL_MISMATCH = 1      CLIENT_OUTDATED = 2
MANIFEST_MISMATCH = 3      DIGEST_MISMATCH = 4        DISCORD_EXPIRED = 5
NOT_A_MEMBER = 6           DISCORD_UNREACHABLE = 7    BANNED = 8
SERVER_FULL = 9            UNMANAGED_BLOCKED = 10
```

Existing denial strings (GameServer.cpp:633-676) get codes; new checks slot in
beside them. **Server-side check order:** protocol → manifest_version equality
(with an explicit allow-list of N previous manifest versions for rolling
updates; **default N=0**, and every N>0 admits clients verified against an
older environment — a deliberate, temporary operator choice, never a standing
config) → **install_digest comparison** (computed once at manifest load,
compared per-join; DIGEST_MISMATCH) → Discord identity → ban → capacity →
unmanaged policy. The server loads `config/server-manifest.json` — the
established config-dir pattern (GameServer.cpp:30-103) — and computes the
expected digest at boot.

**UNMANAGED_BLOCKED is advisory, and labeled so.** The unmanaged list is
client-computed; a client that wants to hide a mod omits it (F6). The check
exists to stop *accidents* — the player who forgot FlightControl was installed
before joining an RP session — not adversaries. It is not a security gate and
is not presented as one; the privacy note also stands: the list discloses mod
folder names to the server operator, acceptable in this community, worth a
line in the launcher's About text.

Two long-standing holes get fixed as part of this work because the handshake is
being touched anyway:

- **The silent transport kick** (F2): `Server::HandleHandshake` gains a tiny
  `kRefused` reply (opcode + denial code) sent before the kick — deliverable:
  Kick's CloseConnection lingers and reliable sends flush (Server.cpp:167),
  and the AuthenticationResponse-then-Kick pattern already demonstrably
  arrives (NetworkService.cpp:151-158). Two caveats from review, both
  design-relevant: the *new* client must drain pending received messages
  before acting on a close notification (today's callback order can destroy a
  same-tick message unread — Client.cpp:187-208, 281-303), and old clients
  ignore the unknown opcode silently in release builds
  (`default: assert(false)`, Client.cpp:338-340) — so the readable transport
  denial only pays off from the *next* divergence after both sides carry it.
- **The invisible denial reason**: `AuthenticationResponse.error` currently
  dies in a log file (NetworkService.cpp:151-158 — spdlog + Close, nothing
  shown; the numeric disconnect reason is ignored by script,
  NetworkWorldSystem.reds:260-272). The denial code + error string get stored
  where script can read them (the `GetCharacterError` pattern,
  NetworkWorldSystem.cpp:1110-1129, already demonstrates the plumbing) and
  shown as an in-game popup with the spec §51 shape: what's wrong, which
  version is needed, "open the launcher and update". Also fixed en route:
  `MaxPlayer` is currently never enforced (a full server admits everyone) and
  `Config.Password` is checked nowhere — both get real checks with real codes.

### 7.4 What the server does NOT do

No file transfer at connect (spec §55) — the server's new jobs are exactly
two: the manifest-version equality check and the digest comparison, both
against one JSON read at boot. No per-file hash exchange in the handshake
(spec §23's freeze warning): the launcher did the heavy verification before
the game ever started; the handshake carries one digest and two strings.

---

## 8. Version surfaces, unified (spec §27, §29)

| Surface | Today | Target |
|---|---|---|
| Game version | 2.31 warn-only in DLL; asserted in 4 hand-maintained places | manifest `game.supportedVersion` is canonical; DLL constants stay as the runtime check; README/CONTRIBUTING/AnnounceRelease cite the manifest |
| Launcher version | package.json, self-updates via electron-updater | unchanged + `minLauncher` gate: the launcher refuses to verify against a manifest requiring a newer launcher, and says so (milestone 1) |
| Mod build | unstamped (F5) | `BUILD_COMMIT` in the DLL + `RED4EXT_SEMVER` from release; `.nco-version` + stamp records stay |
| Server build | none anywhere; status API has no version | version + manifestVersion fields in `/api/v1/status/` so the launcher can detect skew *before* launching the game |
| Protocol | kIdentifier hash pair, unmapped | unchanged as gate + hex recorded in manifest per release (diagnosable); common.proto convention per F2 |
| Manifest | — | `manifestVersion` date.serial, monotonic client-side, the client-facing identity of the environment |

Known versioning debts this design retires: the tag-clobber ambiguity (two
builds, one tag) stops mattering because the manifest hash-pins content; test
builds not updating `.nco-version`/stamps (prerelease:install writes only
`testBuildTag`, main.js:4575) gets fixed in milestone 1 so the verify pipeline
sees test bytes as what they are.

---

## 9. Developer workflow & channels (spec §40-42, §48-49, §61)

**Channels map to what exists:** production = the promoted latest release
(Ship.ps1 already creates as prerelease → verifies assets → promotes,
Ship.ps1:771-852); staging = pre-releases (the test-build channel devs already
opt into via prerelease:list/install); development = a local manifest
generated against the working tree by the same generation code Ship.ps1 uses,
runnable standalone (milestone 2, with the scanner). No new infrastructure.

### 9.1 Ship-time generation order

Review caught that manifest generation cannot be a side effect of the existing
upload step if it references asset ids — so it doesn't (§2: content hashes
only), and the order is explicit:

```
build + stage (existing) → hash every staged file + prerequisite zip
→ generate server-manifest.json (components from the curated source list,
   payload files from the staged tree, install_order derived from the graph,
   REFUSING cycles/missing deps/schema violations - spec §49's RELEASE BLOCKED)
→ sign (owner key; the ship dies if no key is available - §2)
→ verify own signature (gate)
→ upload ALL assets including manifest + .sig (existing --clobber loop)
→ verify assets by name AND the manifest+sig pair by re-download + re-verify
→ promote (existing gh release edit --prerelease=false --latest)
```

Every ship regenerates and re-signs the manifest — including launcher-only
ships that carry mod bytes forward (Ship.ps1:655-681), because carried bytes
are re-hashed from the local copies the carry step already downloads. A ship
that fails after upload but before promote leaves an unpromoted prerelease —
exactly today's failure semantics (Ship.ps1:815-847), invisible to players.

**Release validation (spec §49)** is thereby the existing gate battery
(secrets check, staged scc compile, hash-match-source checks, node --check,
DOM checks, dependency check, smoke test — Ship.ps1:185-487) plus the
generation-time refusals above.

**The §61 pipeline** (find mod → import to dev manifest → scans → staging →
test → production) is the §5 scanner feeding the manifest components list,
tested on the staging channel, promoted by ship. The dev dashboard (spec §48)
is a dev-tab rendering of the manifest + verification state — counts,
warnings, protocol hashes, channel.

---

## 10. Security model (spec §54) — stated honestly

**What signing buys, and from whom.** The manifest (and `server.json`, which
gets the same `.sig` treatment in milestone 1 — it directs the game connection
AND the coord call that POSTs the player's Discord token,
main.js:4394-4402, so it is the single most security-relevant asset we
publish) becomes verifiable against keys that exist only on the two owners'
machines. That **removes the NAS from the data-asset trust boundary** — real,
because the coord-api's gh credentials on the NAS can clobber any release
asset today (F4) — and makes any tamper or rollback of signed assets evident
(signature check + manifestVersion monotonicity, §2.1).

**The residual, named plainly (review's central finding):** the launcher
auto-update channel. electron-updater trusts latest.yml's sha512, and both the
installer and latest.yml are clobberable release assets; the launcher binary
is not code-signed (package.json build block has no signing config). So
today, anyone with release-write can ship a launcher that pins different keys
— the signing scheme does not defend against that attacker until the update
loop is closed. Closing it, milestone 2, no certificate required: the launcher
verifies its *own* update before applying — autoDownload stays on, but
quitAndInstall is gated on verifying `NightCityOnline-Setup.exe.sig` (same
ed25519 keys, downloaded installer file hashed locally). After that, replacing
the pinned keys requires compromising an owner's machine — which already holds
the release-write token and was always inside the boundary. Windows
code-signing (SmartScreen reputation) remains out of scope for cost reasons
and is orthogonal to this integrity chain.

**Until milestone 2 lands:** the honest statement is that release-write access
is the trust boundary, signing narrows day-to-day exposure (NAS, coord-api,
workflow token) rather than eliminating the determined-attacker case, and the
design says so instead of overselling.

The rest:
- **Monotonic manifestVersion** client-side (§2.1) → replay/downgrade of old
  signed manifests is refused; server-side allow-list defaults to N=0 (§7.3).
- **Own-asset downloads** verified against **manifest sha256** (the signed
  values — not GitHub's `asset.digest`, which whoever replaces an asset also
  replaces; review caught the earlier draft leaning on it. `asset.digest`
  remains a cheap early-abort for truncated downloads, nothing more).
- **Nexus downloads** verified against curation-time archive sha256 (F1).
- **server.json host check**: with `requiresTailscale: true`, the launcher
  additionally refuses hosts outside the tailnet CGNAT range (100.64.0.0/10)
  — making the field real code instead of the documentation it is today
  (grep: no reader exists), so even a tampered-but-unsigned legacy server.json
  on an old launcher can't silently point at a public harvester. New launchers
  require the signature.
- **No secrets in the manifest** — it is public by design.
- **Out of scope, stated:** Windows Authenticode signing (cost), anticheat
  (F6 — client-supplied fields are labeled advisory everywhere they appear),
  and Nexus ToS workarounds (F1 — the 403 stays respected; nothing new is
  fetched from Nexus beyond what the launcher already uses).

---

## 11. Milestones

### Milestone 0 — repairs the design builds on (small, immediate)
Defects found during inspection that milestone 1 would otherwise inherit:
1. `installEverything` writes `.nco-version` against an undefined `modDir` —
   ReferenceError swallowed by try/catch; fresh installs never get the marker
   (main.js:1308-1312, local is `modTarget`).
2. Record the nxm-served `fileId` in install records instead of discarding it
   (main.js:5002 → installModArchive signature).
3. `mods:list` filters devOnly by hardcoded admin ids while `installMissing`
   honors the role map — unify on the role map (main.js:3730-3731 vs 178-179).
4. `installNexusMod` takes `files[0]` unfiltered; `resolveMainFile` filters
   MAIN + newest — unify on `resolveMainFile` (main.js:134-138 vs 3636-3646).
5. `tools/UpdateMod.ps1` default URL pinned to a dead test tag (lines 14-20).

### Milestone 1 — the spec's first milestone, mapped to concrete work
- **Manifest**: Ship.ps1 generates + signs `server-manifest.json` per §9.1
  (per-file hashing, derived install order, generation-time refusals);
  `server.json.sig` beside it; signing/verify scripts (tweetnacl, raw ed25519).
- **Launcher**: §2.1 state machine (signature verify, monotonicity, cached
  manifest, migration fallback); per-file sha256 recording in
  `installModArchive` + archive sha256 compare when pinned; hash verification
  + `[REPAIR]`; own-asset verification against manifest sha256 (asset.digest
  kept as early-abort only); dependency check on **all three** install paths;
  cycle reporting; ownership index + pre-extraction conflict check in
  installModArchive; component-removal retirement in the verify diff;
  unmanaged scan (four surfaces + detection markers) with `warn` policy;
  minLauncher gate; requiresTailscale host check; §57-shape trail logging;
  prerelease installs update `.nco-version`/stamps.
- **Versions**: BuildInfo.h into the DLL; RED4EXT_SEMVER stamped at ship;
  server version + manifestVersion in the status API.
- **Handshake**: proto fields + denial codes + server manifest load +
  manifest-version equality (N=0) + install_digest computation and comparison
  + MaxPlayer/password enforcement + kRefused transport reply (with
  drain-before-close client fix) + in-game denial popup. (One protocol
  flag-day, announced.)

### Milestone 2 — the spec's second wave
Transactional install with rollback (closing the §6.1 overwrite window);
launcher self-update signature gate (closing the §10 residual); HashProtocol
dependency-content hashing (closing the F2 common.proto hole); full
compatibility DB + matrix UI; developer mod scanner + local dev-channel
manifest generation; staging-channel manifests on pre-releases; resumable
downloads; `clientOnly` whitelist + `block` policy; download cache semantics
if reinstall traffic ever warrants it (spec §56, deliberately deferred).

---

## 12. Spec coverage map

| Spec § | Disposition |
|---|---|
| 1-3 manifest & entries | §2-3 — new, generated at ship |
| 4 mod ids | §3 — ids introduced; Nexus numbers demoted to pointer fields |
| 5-6 ownership/conflicts | §5 — derived ownership index; never overwrite silently (window until M2 acknowledged in §6.1) |
| 7-8 dependencies + versions | §3 — manifest `dependencies` (richer than modlist's `requires`); enforced on all install paths (M1) |
| 9 install order | §4 — derived flat order, graph is source of truth |
| 10 mod types | §3.1 — `types` array per component |
| 11-12 graph/cycles | §4 — dependency edges; generation refuses cycles, launcher reports them |
| 13-16 conflict DB/explanations/report/matrix | §5 — manifest section; CET entry with detection markers is the first row |
| 17-20 unknown mods/policy/client-only/network impact | §5 (scan + whitelist), §3 (`networkImpact`), §7.3 (policy semantics, advisory-labeled) |
| 21-26 handshake/mismatch/repair | §6-7 — structured denials, repair path, silent kick fixed |
| 27-29 protocol/game/manifest version separation | §8 + F2 — hash kept over number, scoped honestly; manifestVersion independent + monotonic |
| 30-35 download/verify/atomic/rollback | §6 — delta downloads exist; hashing M1; transactions + rollback M2 (window stated) |
| 36-39 distribution/metadata/authorship/approved versions | F1, §3 — split transport is deliberate; attribution + pins mandatory for sync-relevant mods |
| 40-42 testing workflow/channels/scanner | §9 (workflow + channels), §5 (scanner) |
| 43 severity levels | §5 |
| 44-45 sync categories / owned list | §3 (`networkImpact`, `audience`, `class`) |
| 46-47 load-order validation / multiple load systems | §4 + F3 — verified rules per mechanism, no fictional global order |
| 48-49 dashboard / release validation | §9 — dev-tab rendering; §9.1 generation-time refusals + existing gate battery |
| 50-53 join flow/UX split | §6-7 — player sees Ready/simple; dev sees everything |
| 54-56 security/no-file-serving/cache | §10 (honest trust model), §7.4 (server serves no files), §6.1 (staging ≠ cache; cache deferred, stated) |
| 57 logging | §6 — pipeline logs into launcher-trail.log in the §57 shape (M1) |
| 58-59 never delete unknown / removal by ownership | §5 (never touch unmanaged), §6 (manifest-diff retirement via per-file records) |
| 60 final architecture | §2-10 assembled; §0 maps the pipeline |
| 61 development workflow | §9 |
| 62-63 unknown-default / version-ranged compat | §5 — `unknown` is the default verdict everywhere; ranges on both sides |
| 64 inspect first | this document |
