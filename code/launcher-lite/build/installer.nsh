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
; THE FOOTPRINT RULE (crew decree, 2026-08-21): uninstall leaves NOTHING. This macro
; is the manifest's mirror on the installer side - keep it in sync with
; launcherFootprint() in main.js. All three historical data-folder names are swept
; because Electron's runtime name, the build's product name, and the package name
; have all been used, and old installs left data under each. This also catches
; whatever Electron recreates (caches) between the launcher's own purge and process
; exit - which is why the launcher purging first is not enough on its own.
!macro customUnInstall
  ${ifNot} ${isUpdated}
    RMDir /r "$APPDATA\Night City Online"
    RMDir /r "$APPDATA\Night City Online Launcher"
    RMDir /r "$APPDATA\nightcity-launcher"
    RMDir /r "$LOCALAPPDATA\Night City Online-updater"
    RMDir /r "$LOCALAPPDATA\Night City Online Launcher-updater"
    RMDir /r "$LOCALAPPDATA\nightcity-launcher-updater"
    Delete "$DESKTOP\Night City Online Launcher.lnk"
    Delete "$APPDATA\Microsoft\Windows\Start Menu\Programs\Night City Online Launcher.lnk"

    ; The nxm:// handler registration (proven left behind on 2026-08-22: a dead
    ; handler pointing at the uninstalled exe, so "Mod Manager Download" on Nexus
    ; silently does nothing forever after). Deleted ONLY when the command is exactly
    ; this install's exe - Vortex and Mod Organizer write the same key, and theirs
    ; must survive our uninstall. Electron writes the value as `"<exe>" "%1"`,
    ; verified against a live install.
    ReadRegStr $0 HKCU "Software\Classes\nxm\shell\open\command" ""
    StrCmp $0 '"$INSTDIR\${APP_EXECUTABLE_FILENAME}" "%1"' 0 +2
    DeleteRegKey HKCU "Software\Classes\nxm"
  ${endIf}
!macroend
