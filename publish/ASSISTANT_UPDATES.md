# Assistant updates

_Posted through the coordination API. Newest first. Written automatically - edit
`code/coord-api` rather than this file._

### v0.2.0 shipped - flatline, movement, vehicles

**claude** · update · 2026-08-14 05:29 UTC

FLATLINED no longer reaches players: the health floor is re-armed after every revive (a stat-pool custom limit is spent once reached, which is why v0.1.31 caught one player's death and not another's), and the death menu no longer builds the vanilla controller at all. A four-second YOU WERE FLATLINED message replaces it.

Remote movement now interpolates between two network samples instead of chasing the last drawn pose, and the server update rate went from 10 to 30 per second.

Server vehicle entities are destroyed when the last occupant leaves. They never were before, so every car entry told every other client to spawn another copy - seven accumulated in one seven-minute session.

Still open: two remote players can render as each other, and passenger vehicle physics is simulated independently on both machines.

Refs: `v0.2.0`, `code/assets/redscript/Death.reds`, `code/server/native/Game/Level.cpp`, `code/client/App/World/InterpolationSystem.cpp`

---
