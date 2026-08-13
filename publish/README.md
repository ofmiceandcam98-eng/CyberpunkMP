# publish/ — the outward-facing stuff

Sources for everything the community sees. **Keep these current.** Cam's standing
instruction, 2026-08-11: the status page and the GitHub release must track reality — stale
issues removed as they are fixed, new issues added as they are found.

A contributor acting on a fixed bug wastes their evening, and a tester who hits an unlisted
crash assumes the project is abandoned. Both are worse than a slightly rough page.

| File | Publishes to |
|---|---|
| `status-page.html` | the public status page (artifact) |
| `INSTALL.txt` | ships inside the release zip |
| `release-notes.md` | the GitHub release body |

---

## Shipping: use `tools\Ship.ps1`

```
.\tools\Ship.ps1              # everything that changed
.\tools\Ship.ps1 -Launcher    # launcher only
.\tools\Ship.ps1 -Mod         # client mod only
.\tools\Ship.ps1 -WhatIf      # dry run
```

One command replaces build → package → upload → verify. It refuses to publish anything
that fails a check, because a broken build reaching the release is worse than no build.

**What it checks, and why each one exists** — every item below is a mistake that was
actually made and cost a full round trip to find:

| Check | The failure it prevents |
|---|---|
| Secrets gitignored | Publishing a credential is unrecoverable — it is public the moment it uploads |
| Server not running | The DLL cannot relink while it is loaded; the error looks like a compile failure |
| Launcher closed | electron-builder cannot overwrite a running exe |
| `node --check` | A syntax error packages happily and shows up as a blank window |
| Every `$('id')` exists in the markup | A missing id is a dead button with no error at all |
| Balanced `<div>` | An unclosed div silently swallows the rest of the layout |
| Read the release back | Confirms the upload landed rather than assuming it did |

Add a check whenever something breaks twice. That is the point of the file.

## The live targets

- **Status page:** https://claude.ai/code/artifact/8eabe1f0-60dc-4899-8688-376a2549b129
- **Download:** https://github.com/ofmiceandcam98-eng/CyberpunkMP/releases/latest
- **Discord:** https://discord.gg/M9NSWsndC7 — updates go to `#server-update`

The download button on the page points at `/releases/latest`, **not** at a version tag. A new
release is picked up automatically; do not change that link to a specific version.

---

## Updating the status page

Edit `status-page.html`, then republish it to the **same URL**. From Claude Code:

> Update the status page artifact at
> `https://claude.ai/code/artifact/8eabe1f0-60dc-4899-8688-376a2549b129`
> using `publish/status-page.html`.

Passing that URL is what keeps the link stable. Publishing without it mints a **new** URL, and
every link already shared in Discord goes stale.

### What to check every time

- [ ] Bump the `Updated` date in the masthead.
- [ ] Move any fixed subsystem from **Blocking**/**Broken** to **Working** in the status grid.
- [ ] Add anything newly broken — including regressions.
- [ ] Add newly disproved theories to the *Already ruled out* table, **with the evidence that
      killed them**. That table is the single most valuable thing on the page: it is what stops
      a new contributor re-proposing a dead theory.
- [ ] Delete resolved items from *Known cosmetic issues*.
- [ ] Advance the roadmap stage chips if a stage actually moved.
- [ ] If the crash is solved: rewrite the whole *blocker* section. Do not leave a fixed bug
      described in the present tense.

---

## Cutting a new release

1. Build and deploy, confirming the DLL timestamp actually moved:

   ```
   xmake build -j 4 Client
   xmake install -o distrib Client
   ```

2. Rebuild the zip. It must contain, at minimum:
   - `mod/` — `CyberpunkMP.dll`, `assets/`, `Rpc/`. **Exclude `logs/` and `*.log`.**
   - `prerequisites/` — the six mod zips
   - `LICENSES/` — one license file per prerequisite
   - `INSTALL.txt`

3. Update `INSTALL.txt` and `release-notes.md` — especially **Known issues**.

4. Create the release:

   ```
   gh release create nightcity-YYYY.MM.DD \
     --repo ofmiceandcam98-eng/CyberpunkMP \
     --target <branch> \
     --title "Night City Online - YYYY-MM-DD build (game patch 2.31)" \
     --notes-file publish/release-notes.md \
     --latest \
     <path-to-zip>
   ```

5. Update the status page (above), then announce it:

   ```
   powershell -File tools\AnnounceRelease.ps1
   ```

   That reads the live release from GitHub and posts it to `#server-update`, so the Discord
   message can never disagree with what people actually download. Add `-Highlights "..."` to
   lead with what changed, or `-DryRun` to see the message without sending it.

   It refuses to post a draft release, or one with no file attached — both would send people
   to a dead download.

### Packaging traps, both already hit once

- The older share zip stores entry paths with **backslashes**. A
  `FullName.StartsWith("prerequisites/")` filter silently matches nothing and produces a
  package with **zero prerequisites** that still looks fine. Normalise separators first.
- Copying `distrib\launcher\mod` wholesale now sweeps in the per-session `logs\` folder. Copy
  the three payload items explicitly.
- Always verify the finished zip by counting entries before uploading.

---

## Licensing — do not drop this

The release bundles six independent mods. All six are **MIT**, which makes redistribution fine
*on the condition* that their license text and attribution ship with it. `LICENSES/` and the
credits section in `INSTALL.txt` are what satisfy that. Any regenerated package must keep both.

Both the release notes and `INSTALL.txt` must keep stating that this is an unofficial
community build and **not released by Tilted Phoques**.
