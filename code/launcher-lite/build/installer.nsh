; Uninstalling the launcher removes what the launcher wrote to this PC: its data
; folder under %APPDATA% - saved Discord sign-in, settings, the Nexus key - and the
; updater's cache. An uninstall that leaves a signed-in token on a machine the person
; is walking away from is not an uninstall.
;
; The ${isUpdated} guard is load-bearing: the auto-updater runs this same uninstaller
; on EVERY update (old version out, new version in). Without the guard, updating would
; silently sign everyone out and wipe their settings each release. Only a real,
; user-initiated uninstall clears the data.
;
; Deliberately NOT touched: the game folder and the mod. Those belong to Cyberpunk,
; not the launcher - the Settings screen's "Remove mod" handles them explicitly first
; if someone wants everything gone.
!macro customUnInstall
  ${ifNot} ${isUpdated}
    RMDir /r "$APPDATA\nightcity-launcher"
    RMDir /r "$LOCALAPPDATA\nightcity-launcher-updater"
  ${endIf}
!macroend
