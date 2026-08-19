# The node-to-node era: what it was, what it wasn't, what to take from it

Answer to: *"we had movement and driving synced up before we swapped to a server, see
what we did there."* Fifty-three readers went through all 377 commits on every branch,
the deleted era tooling in the backup, and the session transcripts; every candidate
regression was adversarially verified against the repo before it made this page.

## What the setup actually was

From the first two-client session (~Aug 9-10) until the NAS cutover (`822de7c`,
Aug 16 21:03 CDT), the server was a native Windows `Server.Loader.exe` on **Cam's own
gaming PC** (100.109.102.127), started by hand via `tools/StartServer.bat`. Cam played
at localhost-grade latency; the other player crossed one direct kernel-TUN Tailscale
hop. Same game patch (2.31) as now. The `join-and-start-server.*` scripts landed 2.5
hours before the cutover and contain no sync logic - they were never the era's
foundation.

## What verifiably worked there - the three claims, kept separate

- **Position sync: yes, but only the final ~3 days.** Until Aug 14 the crew's own
  words were "other players teleporting rather than walking" (chase-lerp at
  UpdateRate 10). The interpolation rewrite + 30Hz landed Aug 14 00:13; the one clean
  session log (Aug 14 00:35) is the baseline behind the memory - and it ran the
  **exact same interpolation math, send rates, and interest management shipping
  today**.
- **Animation sync: never worked in the era. Not once.** First the 2.31-broken speed
  offset pinned every puppet into permanent sprint (alive-looking by accident); after
  the Aug 13 fix, kWalkSpeed=3.0 meant walkers glided in idle pose the entire era.
  The blend parameter was never written until Aug 18. Same clip-poor mannequins as
  today.
- **Driving sync: the claim is inverted.** The era's contemporaneous record is
  duplicated cars ("seven of them stacked in the road"), depenetration explosions,
  passengers bouncing on independent physics, no authority model at all. The message
  attaching the era's "good" session log *asks to fix vehicle explosions*. Driving was
  first live-verified genuinely working AFTER the swap (v0.3.72, NAS-hosted).

Why the memory formed: the swap's first hours were catastrophic for unrelated,
since-fixed reasons (wrong mod build in v0.3.58, mixed-DLL frozen statues until
v0.3.64), and the Aug 18-19 collapse was the player-record experiment on the test
channel - not the server. Breakage clustered right after the swap; none of it was
caused by the swap.

## What IS genuinely different now (verified, still live)

1. **The latency reference frame.** Cam's zero-latency seat no longer exists: both
   players now cross the tailnet to a userspace-networking sidecar. Measured
   2026-08-19: Cam's leg is **direct (no DERP relay), 73ms RTT** to the NAS. The
   client renders on a fixed 100ms delay budget (`cSimulationDelay`,
   InterpolationSystem.cpp) - flight time and jitter spend from that budget.
2. **Vehicles get no extrapolation on buffer starvation** (on-foot puppets coast
   250ms; cars freeze-then-snap). Same code both eras - the longer path exposes it.
3. **The host rebuilt itself under live sessions** - cron deploy = full native compile
   on the serving box + container restart. **Fixed 2026-08-19:** the updater defers
   while players are online (`tools/deploy/update-server.sh`).
4. **Discord verification runs inline on the simulation thread** during joins (two
   blocking HTTPS calls) - every cold join hitches everyone's movement relay. Specced
   as character-sync-phase a2.
5. **Silent drops:** stale-id on-foot movement is discarded with no client signal
   (frozen mannequin after a crash-rejoin); the vehicle epoch guard briefly freezes
   cars around handoffs by design; a mounted passenger's server-side position freezes
   at the mount point, decimating vehicle updates to them at range.

**Nothing from the era's code is worth porting.** Every sync-relevant line either
survives verbatim at the tip or was verifiably worse. What we dropped at the swap was
a *network topology*, not code.

## Ordered fixes (smallest first)

1. ~~Measure the path~~ - partially done (Cam: direct, 73ms RTT). Remaining: run
   `tailscale ping 100.125.74.56` and `100.80.243.29` from **each player's game PC**
   during a session.
2. Baseline session on test.5, mannequins, no experimental launch args; vehicle
   enter/exit cycles first (the exit-path rewrite has zero live mannequin exits).
3. ~~Gate the cron deploy on player count~~ - **done**, live on main + feat/world-state.
4. Move Discord verification off the game thread (server-only, no protocol bump).
5. Informative rejection logging on HandleEnterVehicleRequest / HandleExitVehicleRequest
   (server-only).
6. Feed a mounted passenger's position from their vehicle in `ShouldSendTo`
   (server-only).
7. Kernel networking for the sidecar (`/dev/net/tun` + NET_ADMIN, drop TS_USERSPACE) -
   only worth it if measurements say the sidecar is the bottleneck; Cam's leg is
   already direct.
8. Client jitter tolerance (bounded vehicle extrapolation; adaptive simulation delay) -
   only after the numbers from (1) exist. Client change - pairs with a test build.
9. Tell a client when its on-foot puppet id went stale (crash-rejoin resync) -
   **needs a protocol bump**, pair client and server.
10. Male mannequin locomotion clips (era-identical defect: walking males glide today
    exactly as they glided all era). Asset work.
