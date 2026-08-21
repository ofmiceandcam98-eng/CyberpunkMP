# The Songbird holocall hang

Reference for work reverted on 2026-08-20. Everything here was tried and did **not** fix it.
Kept so the next attempt starts from the end of this one rather than the beginning.

## The bug

Creating a NEW character and receiving Songbird's opening holocall freezes the game.
Existing characters never hit it. Vanilla never hits it. Other players hit it too, so it is
not one machine.

Picture stays, audio stops a beat later, Windows is fine, the process dies on its own after
about two minutes.

## What it actually is

The engine's own watchdog kills it: `Watchdog timeout! (120 seconds)`,
`engineWatchdog.cpp:198`. That is a main-thread stall, not a crash and not a rendering
fault.

Three stack samples four seconds apart, taken from the live frozen process:

- **100 of 101 threads identical** across the samples. Genuinely stuck, not merely busy.
- The one thread still moving was Wwise audio draining its buffers - which is why sound
  stops shortly after the picture does.
- The only thread spinning rather than waiting:

```
thread 41144  #00 ntdll!RtlQueryPerformanceCounter      <- busy-wait on a clock
                  Cyberpunk2077!ffxFsr2...              <- the render / upscale path
```

- **Every thread carrying CyberpunkMP frames was idle**: the networking service in a normal
  wait, four libuv pool threads parked in `uv_cond_wait`, the watchdog sleeping. None held a
  lock. None were in our game code.

So our code causes the stall indirectly. There is no lock of ours in the deadlock.

## Ruled out, with evidence

| Suspect | How it was eliminated |
|---|---|
| `HoloAudioCallLogicController.OnInitialize` refusal | Reverted; still froze |
| Time dilation returning false without notifying the listener | Fixed to pass through at 1.0; still froze |
| All six of our redscript files | Moved aside; still froze |
| `PhoneHotkeyController.reds` | Disabled alone; still froze |
| The appearance serializer | Instrumented - `CCPoll` fired and the game ran 13s longer |
| Present hook device index out of range | Guarded; the guard never fired, index was valid |
| Cam's GPU | Others reproduce it; and this is a watchdog stall, not a TDR |
| Medal.tv + Overwolf overlay hooks | Both removed from the process; still froze |
| 8 quest facts aimed at the hook (below) | Applied and confirmed client-side; call still fired |

## The quest facts that did not work

Set server-side in `worldfacts.json`, confirmed applied client-side
(`facts: applied 8 world fact(s)`), call fired anyway:

```json
[
  { "Name": "q301_hook_pulse",                     "Value": 1 },
  { "Name": "q301_00_done",                        "Value": 1 },
  { "Name": "q301_00_active",                      "Value": 0 },
  { "Name": "q301_book_scene_ended",               "Value": 1 },
  { "Name": "holo_songbird_calls_v_start_activate","Value": 0 },
  { "Name": "holo_songbird_calls_v_start_done",    "Value": 1 },
  { "Name": "holo_songbird_calls_v_end_activate",  "Value": 0 },
  { "Name": "holo_songbird_calls_v_end_done",      "Value": 1 }
]
```

## The trace, which is still good

From the real archives, not community lists. See
`reference_phantom_liberty_trace` in the assistant's memory for the full version.

- Skip to Phantom Liberty sets **`ep1_standalone = 1`**.
- `ep1/quest/main_quests/q301/phases/q301_hook.questphase` is gated on it and sets
  `holo_songbird_calls_v_start_activate = 1`.
- That fact is read by exactly one other file:
  `ep1/quest/holocalls/songbird/songbird_holocall.questphase`.
- **`ep1_standalone` is read by 69 quest phases** including the root graph, the metro,
  apartments, and `ep1_community.questphase` - Dogtown's NPC population. Do not touch it.
- Songbird's call is a **holocall** (`q301_00_holocall_hook`), delivered by
  `HoloAudioCallLogicController` - not `IncomingCallLogicController`.

## The workaround that unblocks people

The character is saved to the server **before** the call arrives - `players.json` holds the
full record. So:

1. Create the character
2. Let it freeze, force-close
3. Rejoin - now an existing character, no NEW CHARACTER path, no call

One freeze on first join, then fine forever.

## What was kept

- The **hang watchdog** (`App/HangWatchdog.*`). One atomic store per frame, fires once,
  dumps every thread's stack. It is the only reason any of the above is known.
- `stackdump.exe` - an external attach-and-dump tool, in the session scratchpad.
- The **Present bounds guard**. Reading past the end of a hand-derived array is worth
  fixing whether or not it caused this.

## Where to start next time

The stall is a wait on something the game never signals, with our threads all idle. The
open question is what our presence changes about the render or job path such that a
holocall on a brand-new character never completes. Repeat-sampling proved the state is
static; the next step is identifying which object thread 41144 is spinning on.
