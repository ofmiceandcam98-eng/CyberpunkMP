**Legend** — 🧍 can be done alone · 👥 needs two people online · ✅ done

## 👥 Needs a second player

*The most useful thing anyone can offer right now. Ten minutes online unblocks all of these.*

- **Confirm the spawn crash is fixed.** A cause was found and fixed — a movement update arriving before the puppet finished building — but two real players have never been connected at once to prove it. **This is the most important open item.**
- **Confirm chat ranges filter.** Local carries 30m, `/yell` 60m, `/whisper` 5m. Stand ~40m apart: local should be silent, `/yell` should carry.
- **Confirm remote players show their gear.** Equipment used to be sent as item *names* built by a debug helper that returns empty strings in release builds, so everything arrived blank. It now travels as the real item id and the sending half is confirmed — whether it *appears* on someone else's screen is not.
- **Confirm `/tp` and `/return`.** They work on paper. Whether they move the right player to the right spot facing the right way is a two-person question.

## 🧍 Can be done alone

- **Report crashes with the log.** `red4ext\plugins\zzzCyberpunkMP\logs\` — post the whole file, not a screenshot. This is how the last three bugs were found.
- **Try a fresh install.** First-time install was broken outright from v0.1.4 to v0.1.9 and nobody noticed, because it only fails for people who *don't* already have the mod.
- **Check the launcher updates itself.** It should offer "Restart to update" rather than asking you to reinstall.
- **Try the chat commands.** `/help`, `/yell`, `/whisper`, `/advert`, and the mouse wheel to scroll.
- **Check position saving.** Walk somewhere distinctive, quit, rejoin — you should come back where you left off.

## In progress

- 🧍 **In-game admin tools.** Done: `/tp`, `/return`, `/kick`, `/ban`, `/unban`, `/bans`, `/who`, `/help`.

## Next up

- 👥 **Character slots.** Your identity should be a character you made, not whichever singleplayer save you loaded. Server-side position saving landed first and is the foundation.
- 🧍 **The `/` chat hotkey.** The mod overrides a 2.2-era interface file, which is what registers the key. Needs the game asset rebuilding — not a code fix.
- 🧍 **Movement speed arrives as nonsense** (~3e8 for someone walking), so remote animations look wrong. Suspected struct offset change on 2.31.

## Not right now

- **Hosting the server somewhere other than Cam's PC.** It would mean the server is up whether or not he is. Shelved deliberately — the cloud setup is fiddly and there are better things to spend the time on while we are still finding crashes. The server runs on Cam's machine in the meantime.

## Known issues

- ⚠️ **Keep Cyber Engine Tweaks disabled.** CET alongside this mod caused a full GPU hard-lock.

## ✅ Recently done

- ~~Join from the main menu~~ — MULTIPLAYER sits beside Continue and Load Game
- ~~Chat with range, colour and `/help`~~ — yells red, whispers pink-purple, adverts yellow
- ~~Launcher updates itself~~ — no more reinstalling every version
- ~~Server remembers where you were~~ — reconnect and you are put back
- ~~First-time install~~ — was broken since v0.1.4, fixed in v0.1.9
- ~~Remote players sending blank equipment~~ — real item ids now go over the wire
- ~~Mod drawing an empty overlay every frame~~ — no GPU work at all unless the dev overlay is on
- ~~Input Loader's licence~~ — found and included; all six bundled mods now ship their licence text
