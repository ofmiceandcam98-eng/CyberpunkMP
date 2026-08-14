**Legend** — 🧍 can be done alone · 👥 needs two people online · ✅ done

## 👥 Needs a second player

*The most useful thing anyone can offer right now. Ten minutes online unblocks all of these.*

- **Confirm FLATLINED is gone for everyone.** v0.1.31 caught it for one player and missed it for another. The reason: a health floor is *spent* once it is reached, so only the first death of a session was protected. v0.2.0 re-arms it after every revive and blocks the death menu outright. **Please die on purpose, twice, and say whether the vanilla screen appeared either time.**
- **Confirm the new "YOU WERE FLATLINED" message.** Brief, centre screen, gone in four seconds. No menu, no Load Last Checkpoint.
- **Confirm other players move smoothly.** Update rate went from 10 to 30 a second and the interpolation was rewritten to run between two real samples instead of chasing the last drawn pose. Walking should look like walking, not teleporting.
- **Confirm cars stop duplicating.** Getting in and out of the same car used to spawn a fresh copy in everyone else's world every single time, and nothing ever removed them. One session left seven.
- **Confirm chat ranges filter.** Local carries 30m, `/yell` 60m, `/whisper` 5m. Stand ~40m apart: local should be silent, `/yell` should carry.

## 🧍 Can be done alone

- **Report crashes with the log.** `red4ext\plugins\zzzCyberpunkMP\logs\` — post the whole file, not a screenshot. This is how the last three bugs were found.
- **Check `/tp` leaves the car behind.** Summoning a driver now takes them out of the vehicle first.
- **Check `/return`** puts you back a few metres short of where you were, facing the same way.
- **Try a fresh install.** It was broken outright from v0.1.4 to v0.1.9 and nobody noticed, because it only fails for people who *don't* already have the mod.

## Known issues

- **Clothing does not always match.** Wardrobe outfits were never being read — only the individual clothing slots underneath them, which on most saves is the starting outfit. v0.2.0 reads the outfit slot too. Still unconfirmed whether that covers every case.
- **Two remote players can look like each other.** Reported, not yet diagnosed. Their appearance data does arrive distinct, so this is in how it gets applied.
- **Vehicles are only loosely synchronised.** Riding as a passenger is buggy and cars can bounce, because each machine simulates the physics independently. Fixing it properly means one machine owning each vehicle and the others following — a real piece of work, not a patch.
- **Frame rate while driving.** Some of it was ours — the duplicated cars above, and a lock on the animation thread that every NPC in the city was queueing on. Both fixed in v0.2.0.

## In progress

- 👥 **Damage between players.** Shooting each other does not register. The puppet record other players spawn as is a launch flag so it can be tried without a release.

## Next up

- 👥 **Character slots.** Your identity should be a character you made, not whichever singleplayer save you loaded. Server-side position saving landed first and is the foundation.
- 🧍 **The `/` chat hotkey.** The mod overrides a 2.2-era interface file, which is what registers the key. Needs the game asset rebuilding — not a code fix.

## Not right now

- **Hosting the server somewhere other than Cam's PC.** It would mean the server is up whether or not he is. Shelved deliberately — the cloud setup is fiddly and there are better things to spend the time on while we are still finding crashes. The server runs on Cam's machine in the meantime.

## ✅ Recently done

- ~~**Cyber Engine Tweaks works — and the console is back.**~~ The old instruction to disable it is withdrawn. Press `~` in game
- ~~The spawn crash~~ — confirmed fixed with real players
- ~~Join from the main menu~~ — MULTIPLAYER sits beside Continue and Load Game
- ~~Chat with range, colour and `/help`~~ — yells red, whispers pink-purple, adverts yellow
- ~~`/tp`, `/return`, `/kill`, `/jail`, `/setspawn`~~ — in-game admin tools
- ~~Launcher updates itself~~ · ~~Server remembers where you were~~ · ~~Mouse wheel scrolls chat~~
- ~~Player cap~~ — was 4, exactly the size of the group. Now 16
