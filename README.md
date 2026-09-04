# Night City Online

[![Latest release](https://img.shields.io/github/v/release/ofmiceandcam98-eng/CyberpunkMP?label=launcher&color=e5484d)](https://github.com/ofmiceandcam98-eng/CyberpunkMP/releases/latest)
[![Build windows](https://github.com/ofmiceandcam98-eng/CyberpunkMP/actions/workflows/windows.yml/badge.svg)](https://github.com/ofmiceandcam98-eng/CyberpunkMP/actions/workflows/windows.yml)
[![Build linux](https://github.com/ofmiceandcam98-eng/CyberpunkMP/actions/workflows/linux.yml/badge.svg)](https://github.com/ofmiceandcam98-eng/CyberpunkMP/actions/workflows/linux.yml)
[![Discord](https://img.shields.io/badge/Discord-Night%20City%20Online-5865F2?logo=discord&logoColor=white)](https://discord.gg/M9NSWsndC7)
[![Game patch](https://img.shields.io/badge/Cyberpunk%202077-2.31-f3c50f)](https://github.com/ofmiceandcam98-eng/CyberpunkMP#readme)

A fork of [CyberpunkMP](https://github.com/tiltedphoques/CyberpunkMP) carrying the changes
needed to run on **Cyberpunk 2077 patch 2.31**, plus a launcher, Discord-backed
permissions, chat with range, and server-side persistence. Upstream targets patch 2.2 and
will not start on current game versions.

### ⬇ [Download the launcher — one click, always the newest](https://github.com/ofmiceandcam98-eng/CyberpunkMP/releases/latest/download/NightCityOnline-Setup.exe)

That link never goes stale — it always serves the current release's installer directly.
The launcher installs everything else (the mod, the frameworks) and keeps itself updated.
Browsing instead: [all releases](https://github.com/ofmiceandcam98-eng/CyberpunkMP/releases/latest)
· **Discord:** https://discord.gg/M9NSWsndC7

**Contribute:** read [CONTRIBUTING.md](CONTRIBUTING.md) first — the build has version pins
that are load-bearing and a clean checkout of upstream does not compile. What is currently
broken is tracked in [publish/TODO.md](publish/TODO.md).

This is a **beta**. It works well enough for a group to play together and there are still
rough edges; the to-do list is honest about which.

---

## About the upstream project

**CyberpunkMP** is a multiplayer mod for Cyberpunk 2077, created by Tilted 
Phoques SRL. This mod brings multiplayer functionality to the game, allowing 
players to synchronize their appearances, equipment, movements, and basic 
animations seamlessly. Additionally, vehicles and their passengers are fully 
synchronized, enabling cooperative or competitive experiences involving the 
game's dynamic vehicular systems.

CyberpunkMP also includes powerful tools for developers. We provide a .NET SDK 
for creating server-side plugins and support client-side plugins through an 
exposed Redscript SDK. The mod features a robust Remote Procedure Call (RPC) 
system, allowing plugins to invoke server-side functions from the client and 
vice versa. This system is completely automatic, requiring no additional code 
to handle RPC functionality.

## Building it yourself

**[CONTRIBUTING.md](CONTRIBUTING.md) is the build guide** — machine setup, the exact
toolchain pins and why each exists, the server's Docker build, and the ship tooling.
The upstream project's generic build steps do not work on a clean checkout of this
fork; that is precisely why the guide exists.

For the code's geography — what lives where and the gotcha that bites in each area —
see [docs/MAP.md](docs/MAP.md).
