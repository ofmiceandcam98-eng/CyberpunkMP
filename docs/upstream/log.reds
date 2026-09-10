native func Log(const text: script_ref<String>) -> Void
native func LogWarning(const text: script_ref<String>) -> Void
native func LogError(const text: script_ref<String>) -> Void

// output goes to CET window
native func LogChannel(channel: CName, const text: script_ref<String>)
native func LogChannelWarning(channel: CName, const text: script_ref<String>) -> Void
native func LogChannelError(channel: CName, const text: script_ref<String>) -> Void

native func FTLog(const value: script_ref<String>) -> Void
native func FTLogWarning(const value: script_ref<String>) -> Void
native func FTLogError(const value: script_ref<String>) -> Void

native func Trace() -> Void
native func TraceToString() -> String
// MOVED OUT OF THE REPO ROOT, 2026-09-04. Kept, not deleted: it is upstream's file.
//
// Unreferenced by anything in this fork, and not shipped - the redscript that ships lives
// in code/assets/redscript. These are CET-era native declarations ("output goes to CET
// window"), and CET is listed known_incompatible with this mod (it caused a GPU hard-lock;
// the mod ships its own ImGui backend for exactly that reason). Our script logging goes
// through NetworkWorldSystem.ScriptLog instead, which reaches the log that ships to the
// server.
