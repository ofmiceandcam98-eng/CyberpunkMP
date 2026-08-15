# How this codebase behaves

Durable project knowledge — the things that are true about this codebase regardless of who
is working on it or on which machine. [CONTRIBUTING.md](../CONTRIBUTING.md) covers getting a
build running; this covers what to expect once you are in.

This lived in a gitignored scratchpad on one PC until 2026-08-14. That was fine with one
contributor and became a single point of failure the moment there were two — and this repo
has already lost single-copy work to a stray `git reset` once.

---

## The failure mode this codebase produces

**Silent fallbacks, not errors.** Almost every hard bug here has looked like "the mod does
nothing" rather than like a crash or an error message. Budget your debugging time
accordingly: assume something quietly took a different code path, not that something threw.

Three examples, all real:

- A **failing C++ concept** produces no error and no warning — just a different overload.
- A **single bad `.reds` file** aborts the whole redscript compilation, so the game starts
  with *no* scripts at all. No menu entry, no chat, no HUD, nothing naming the cause.
- **`xmake install` without `-o distrib`** prints `install ok!` and writes to the wrong
  prefix. The game keeps loading the previous DLL and the build "did not work".

---

## The 2.31 loading bug, as a worked example

Worth reading in full even though it is fixed, because it is the shape of the problems here.

`RTTI_DEFINE_CLASS` supplies a type name via `nameof::nameof_short_type<T>()`, which returns
`nameof::cstring<N>` — *convertible* to `std::string_view` but not *identical* to it. The
concept `IsTypeNameConst` demanded an exact `string_view`, so it silently failed and name
resolution fell through to `U::NAME`.

Because `NetworkWorldSystem` derives from `RED4ext::IGameSystem`, which declares
`static NAME = "gameIGameSystem"`, **C++ static member inheritance handed the derived class
its base's name.** The system registered as `gameIGameSystem`, RedLib generated the accessor
`GetGameIGameSystem` instead of `GetNetworkWorldSystem`, redscript validation failed, and
RED4ext aborted startup.

This was latent on 2.2 — the SDK's `IGameSystem` had no `NAME` to inherit — and became fatal
the moment the vendored SDK gained one. Nothing in the codebase changed.

The fix needed both hunks:

```cpp
// concept IsTypeNameConst
- std::is_convertible_v<T, const char*> || std::is_same_v<T, std::string_view>
+ std::is_same_v<std::remove_cvref_t<T>, const char*> || std::is_convertible_v<T, std::string_view>

// GetTypeNameStr: invert the branch to test const char* first,
// with the name.size()/name.data() path in the else
```

---

## Diagnostic techniques that actually work here

- **Add temporary `spdlog::info` probes and read the log.** By a wide margin the
  highest-value technique in this codebase. It ended the guessing on the 2.31 bug after
  three wrong hypotheses, and it is what closed the remote-spawn crash.
- **`tools\CheckScripts.ps1` is an API oracle**, not just a gate. There is no RTTI dump;
  `scc` reports every unresolved symbol in one pass, so a file full of candidate calls
  answers a batch of "does this method exist" questions in a single run. This is how the
  death-menu and god-mode work got done without guessing.
- **Read RED4ext's own source** — the release zip contains it. `src/dll/Hooks/ValidateScripts.cpp`
  revealed that only errors carrying a source-file reference trigger the startup abort,
  which turned "nine errors" into "one real bug and eight downstream artifacts".
- **`Codeware.Global.reds`**, shipped with Codeware, is a generated dump of the entire game
  type hierarchy for the installed patch — authoritative for what any class extends on 2.31.

---

## What is actually synchronised

**The world is not shared.** Thirteen message types sync players, vehicles, chat and
teleports. NPCs, doors, time of day, weather and combat are simulated per-client and will
not match between players.

Read every feature request against that. "Can we do a heist together" means synchronising
NPCs, which does not exist. "Can we shoot each other" means damage replication, which does
not exist yet either.

**`UpdateRate`** in the server config is the ceiling on how smooth remote players can look.
The client's interpolation delay is derived from it (`50ms + 1500/rate`), so a low rate costs
both smoothness *and* responsiveness. It was 10 and everyone described the result as other
players teleporting rather than walking. It is 30 now.

---

## Redscript is not the game's script dialect

They look alike. They are not the same language, and the difference is invisible until the
game refuses to run any script at all.

- Class parameters need `ref<T>`. The game's own sources write
  `data: PauseMenuListItemData`; redscript requires `ref<PauseMenuListItemData>`.
- `const label: String` becomes `label: script_ref<String>`.

**Never invoke `scc.exe` directly.** It is the same binary the game's loader uses, so on
failure it pops the player-facing "REDScript compilation has failed" dialog and *blocks
until somebody clicks OK* — on whoever happens to be at the machine. `CheckScripts.ps1` runs
it detached and kills it as soon as an error reaches stdout.

---

## Death, and why it took three attempts

Players must never see the vanilla FLATLINED screen: its only real option is Load Last
Checkpoint, and loading rebuilds the world while the server still holds the old puppet, so
one person dying ends the session for everyone.

Two attempts failed for the same reason. Wrapping `OnDeath` and putting a custom floor under
the health pool are both **conventions the game's own scripts follow** — anything that sets
the dead state directly walks straight past them.

`gameGodModeType.Immortal` is not a convention. The engine checks it *before* the dead state
is entered, which is why the game itself uses it during scripted sequences. Damage still
lands and health still drops, so the stat-pool listener still fires and still gives us the
moment to respawn on.

The general lesson: **prefer the thing the engine enforces over the thing the scripts agree
to.**

---

## Things that have bitten more than once

- **Launch flags must use the `=` form**: `--online --ip=<addr> --port=11778`. The
  space-separated form silently does nothing, because `Settings::Load` only assigns when the
  parsed value is non-empty — so it keeps the `127.0.0.1` default and times out against your
  own PC.
- **`tailscale status` hides shared devices.** Use `tailscale status --json`. Plain `status`
  showing only your own machine does not mean nobody has joined.
- **Windows blocks inbound ICMP by default**, so `ping` failing proves nothing. Use
  `tailscale ping`. Firewall rules auto-created for `server.loader.exe` default to the
  **Public** profile while Tailscale is **Private** — the rule must be created with
  `-Profile Any`.
- **PowerShell 5.1**: `-Encoding UTF8` writes a BOM, which `scc` reads as garbage;
  `ConvertTo-Json -Depth` recurses into a String's own .NET members and produced a 46MB
  payload from a 3,400-character file; native stderr under `$ErrorActionPreference='Stop'`
  becomes a terminating error that jumps clean over a retry loop.
- **`SdkGenerator` swallows its own failures.** `Program.cs` wraps its body in
  `catch { Console.WriteLine(e); }`, so codegen errors **exit 0** and the build fails much
  later with a confusing `namespace 'Internal' does not exist`.
