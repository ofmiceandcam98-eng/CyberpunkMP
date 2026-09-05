# Verify.ps1 - the audit and the unit tests, in one command.
#
# WHY THIS EXISTS
#
# Cam, 2026-09-03: "make sure you do what we just did any time we build something new...
# we have to be SURE they work so thats less times i have to come back to you about an
# issue." And: "run this command before we ship anything from now on."
#
# An audit of one day's work found two bugs that compiled perfectly and that no build would
# ever have caught:
#
#   - /call had TWO dispatches. An older deprecation stub matched first and returned, so the
#     new player-to-player call command was dead code - "/call 555-014-372" answered "use
#     your phone" and rang nobody.
#   - CreateCharacterRequest was declared, took a oneof slot, and was never sent or handled.
#
# Both had been reported as working. Every check below maps to a failure that has actually
# happened on this project, not to a category of bug in the abstract.
#
# EVERY FAILURE SAYS WHAT, WHERE AND HOW TO FIX IT
#
# Cam, 2026-09-03: "make sure the verifier also tells us what the problem is that way you and
# zeldfeps claude have an easier time figuring it out." Two assistants work on this codebase
# from separate sessions, and a bare "FAIL" costs whichever one picks it up a fresh
# investigation of something the check already knew. So a failure carries the consequence,
# the exact location, and the concrete next action.
#
#   .\tools\Verify.ps1            # everything
#   .\tools\Verify.ps1 -SkipTests # static checks only, fast, no compiler needed
#
[CmdletBinding()]
param(
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot "Environment.ps1")
Set-Location $Repo

$problems = @()

# A failure is not a verdict, it is a work order: what broke, what it costs, where it is,
# and what to do about it.
function Fail {
    param(
        [Parameter(Mandatory)][string]$Summary,   # the one-line claim
        [Parameter(Mandatory)][string]$What,      # the CONSEQUENCE, not a restatement
        [string]$Where,                           # file:line, as precise as possible
        [Parameter(Mandatory)][string]$Fix        # the concrete next action
    )

    $script:problems += [pscustomobject]@{ Summary = $Summary; What = $What; Where = $Where; Fix = $Fix }

    Write-Host "  FAIL  $Summary" -ForegroundColor Red
    Write-Host "        what   $What" -ForegroundColor DarkYellow
    if ($Where) { Write-Host "        where  $Where" -ForegroundColor DarkYellow }
    Write-Host "        fix    $Fix" -ForegroundColor DarkYellow
}

function Pass($m) { Write-Host "  ok    $m" -ForegroundColor DarkGray }
function Head($m) { Write-Host "`n$m" -ForegroundColor Cyan }

# ---------------------------------------------------------------------------
Head "BOM"
$bom = @()
foreach ($root in @("code\assets\redscript", "distrib\launcher\mod\assets\redscript", "code\server\native", "code\client")) {
    if (-not (Test-Path $root)) { continue }
    Get-ChildItem $root -Recurse -Include *.reds,*.h,*.cpp -ErrorAction SilentlyContinue | ForEach-Object {
        $b = [System.IO.File]::ReadAllBytes($_.FullName)
        if ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF) { $bom += $_.FullName }
    }
}
if ($bom) {
    foreach ($f in $bom) {
        $rel = $f.Replace("$Repo\", "")
        $isReds = $f -like "*.reds"
        Fail -Summary "byte-order mark in $rel" `
             -What $(if ($isReds) { "redscript refuses the whole file with 'syntax error at 1:1', which names nothing - and ONE bad .reds aborts ALL compilation, so the game starts with no scripts at all while looking like the mod simply did nothing" }
                     else { "harmless to MSVC and GCC, but it means this file was written by PowerShell 5.1's -Encoding UTF8 and nobody noticed" }) `
             -Where $rel `
             -Fix "strip it: `$t=[IO.File]::ReadAllText('$rel'); if(`$t[0] -eq [char]0xFEFF){`$t=`$t.Substring(1)}; [IO.File]::WriteAllText('$rel',`$t,(New-Object Text.UTF8Encoding `$false))   -- and stop using Set-Content -Encoding UTF8; it writes a BOM on PS 5.1"
    }
} else { Pass "no BOM in any source file" }

# ---------------------------------------------------------------------------
# The Linux build, checked on a machine with no Linux compiler on it.
#
# This closes the gap this script used to admit to at the bottom: "there is no GCC on this
# machine, so server portability is only ever proven by a deploy". The server runs GCC in a
# container on the NAS and that is the build that serves players; MSVC supplies far more of
# the standard library transitively, so code that compiles perfectly here can fail there.
#
# It is not theoretical. On 2026-09-02 four files missing <cstdio> and one missing <map>
# took production down for three hours without anybody noticing, because the deploy script
# keeps the container on its PREVIOUS image when a build fails - so the old protocol went on
# answering, every updated client was refused at connect, and the only visible symptom was a
# menu button that appeared to do nothing.
#
# A lint, not a compiler: a clean run is not a promise that GCC is happy. It catches the one
# class of error that has actually cost us a day, on the machine where the mistake is made.
Head "portability (GCC headers)"
$includeOut = & (Join-Path $PSScriptRoot "CheckIncludes.ps1") 2>&1
$includeOk = $LASTEXITCODE -eq 0
Set-Location $Repo   # CheckIncludes.ps1 sets its own location

if ($includeOk) {
    # Its own summary line has already gone to the console - CheckIncludes.ps1 reports with
    # Write-Host, which bypasses the pipeline, so $includeOut cannot carry it here.
    Pass "every std:: symbol has the header that declares it"
}
else {
    # No counts here on purpose. CheckIncludes.ps1 reports with Write-Host, which bypasses
    # the pipeline, so $includeOut is empty and any number derived from it would be 0 - a
    # summary reading "0 symbols used without their header" next to a failure is worse than
    # no number at all, because it makes the gate look broken and teaches people to skip it.
    # The child has already printed the exact file/symbol list immediately above.
    Fail -Summary "std:: symbols used without the header that declares them" `
         -What "these compile on MSVC and FAIL under GCC, which is the build that actually serves players. A failed container build does not roll back - the deploy keeps the PREVIOUS image running, so the old protocol goes on answering and every updated client is refused at connect. The last time this happened it cost three hours and the only symptom was a menu button appearing to do nothing" `
         -Where "listed directly above this block, one line per file with the exact symbol" `
         -Fix "add each #include named above. Adding one cannot change behaviour - it can only fix portability - so this is always safe. Re-run .\tools\CheckIncludes.ps1 to confirm"
}

# ---------------------------------------------------------------------------
Head "redscript natives vs C++ RTTI"
$reds = "code\assets\redscript\World\NetworkWorldSystem.reds"
$hdr  = "code\client\App\World\NetworkWorldSystem.h"
$declared = (Select-String -Path $reds -Pattern 'public native func (\w+)' -AllMatches).Matches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
$exposed  = (Select-String -Path $hdr  -Pattern 'RTTI_METHOD\((\w+)\)'     -AllMatches).Matches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique

foreach ($n in ($declared | Where-Object { $exposed -notcontains $_ })) {
    $line = (Select-String -Path $reds -Pattern "native func $n\b" | Select-Object -First 1).LineNumber
    Fail -Summary "native '$n' has no C++ behind it" `
         -What "this fails when the mod LOADS, not when it compiles - and it takes EVERY script in the mod down with it. The game starts with no menu entry, no chat and no HUD, looking exactly like the mod did nothing" `
         -Where "declared at $reds`:$line, missing from $hdr" `
         -Fix "either add RTTI_METHOD($n) to the RTTI block in $hdr and implement it, or delete the declaration in the .reds if it was speculative"
}
# The reverse is not an error - C++ may expose more than script uses - so it is not checked.
if (-not ($declared | Where-Object { $exposed -notcontains $_ })) { Pass "$($declared.Count) natives all resolve" }

# ---------------------------------------------------------------------------
Head "chat command dispatch"
$chat = "code\server\native\Systems\ChatSystem.cpp"
$top = @{}
$dupeFound = $false
Select-String -Path $chat -Pattern 'if \(command == "(/[a-z]+)"' | ForEach-Object {
    # Compare by INDENTATION. A nested branch inside a compound `if` is not a duplicate, and
    # reporting it as one makes the whole check untrustworthy.
    $indent = $_.Line.Length - $_.Line.TrimStart().Length
    if ($indent -le 4) {
        $c = $_.Matches[0].Groups[1].Value
        if ($top.ContainsKey($c)) {
            $dupeFound = $true
            Fail -Summary "'$c' is dispatched twice" `
                 -What "the FIRST block wins and returns, so the second is dead code. It compiles, it looks reachable, and the only symptom is the command doing the wrong thing - this is exactly how /call ended up answering 'use your phone' instead of ringing anybody" `
                 -Where "$chat`:$($top[$c]) (wins) and $chat`:$($_.LineNumber) (unreachable)" `
                 -Fix "merge them into one block that handles both meanings, or rename one of the commands. Decide which behaviour is wanted FIRST - the live one is whichever is at line $($top[$c])"
        }
        else { $top[$c] = $_.LineNumber }
    }
}
if (-not $dupeFound) { Pass "$($top.Count) commands, each dispatching once" }

# ---------------------------------------------------------------------------
# Every command the server ADVERTISES has to be a command the server ANSWERS.
#
# Cam, 2026-09-04: "/kill doesnt actually down or flatline the player" - and chasing that
# turned up something worse sitting behind it. The bleedout path had been telling players
# "Use /respawn when you are ready" since the medical system shipped, and /respawn was never
# written. Anybody who actually bled out was told to type a command that did nothing and had
# no route back into the world.
#
# It survived because the two gaps hid each other: /kill was the only way to go down, /kill
# never downed anyone, so nothing ever reached the message. That is the shape of this bug
# class - the broken half is only reachable through another broken half - and it is why this
# is a check rather than a fix. The compiler cannot see inside a string literal, and neither
# can a code review that is looking at the command being changed.
Head "advertised commands"

$known = @{}
foreach ($k in $top.Keys) { $known[$k] = $top[$k] }

# EVERY literal compared against `command`, not just the first on its line. Plenty of
# commands are aliases sharing one block - /character || /char, /answer || /decline ||
# /hangup, /block || /unblock, /stabilize || /stabilise, /assess || /revive - and some of
# those chains wrap onto a continuation line. Taking only the first literal per line
# reported six live commands as missing.
Select-String -Path $chat -Pattern 'command == "(/[a-z]+)"' -AllMatches | ForEach-Object {
    foreach ($m in $_.Matches) { $known[$m.Groups[1].Value] = $_.LineNumber }
}

# And the chat CHANNELS - /yell, /whisper, /advert - are rows in a table (search this file
# for ChatChannel::) rather than branches, because they differ only by range, prefix and
# permission. A check that knew only about if-blocks reported /advert as missing, which is
# exactly the kind of false positive that teaches people to ignore the gate.
Select-String -Path $chat -Pattern '\{\s*"(/[a-z]+)"' | ForEach-Object {
    $known[$_.Matches[0].Groups[1].Value] = $_.LineNumber
}

# A few diagnostics match the WHOLE line rather than the parsed command, because they take a
# fixed sub-verb (`/dummy servertick`). Same dispatch, different variable.
Select-String -Path $chat -Pattern 'line == "(/[a-z]+)' | ForEach-Object {
    $known[$_.Matches[0].Groups[1].Value] = $_.LineNumber
}

$advertised = @{}
Select-String -Path $chat -Pattern 'Tell\(' | ForEach-Object {
    foreach ($m in [regex]::Matches($_.Line, '(?<![\w/])(/[a-z]{2,})')) {
        $name = $m.Groups[1].Value
        if (-not $advertised.ContainsKey($name)) { $advertised[$name] = $_.LineNumber }
    }
}
$ghosts = $advertised.Keys | Where-Object { -not $known.ContainsKey($_) } | Sort-Object
foreach ($g in $ghosts) {
    Fail -Summary "$g is advertised to players but never dispatched" `
         -What "a player who is told to type $g types it and nothing happens - the server does not recognise it, so they get no reply at all. When the text offering it is the ONLY way out of a state (a respawn prompt, a trade confirmation), that player is stuck with no way forward and no error to report" `
         -Where "$chat`:$($advertised[$g]) offers it; no dispatch block for $g exists" `
         -Fix "either implement $g beside the other commands in this file, or change the message to name a command that does exist. Check which states can reach that message before deciding - if it is the only exit from one, it has to be implemented"
}
if ($ghosts.Count -eq 0) { Pass "$($advertised.Count) advertised command(s), all dispatched" }

# ---------------------------------------------------------------------------
Head "protocol"
foreach ($p in @("client","server")) {
    $path = "code\protocol\$p.proto"
    $lines = Get-Content $path
    $start = ($lines | Select-String -Pattern 'oneof content' | Select-Object -First 1).LineNumber
    $nums = @()
    for ($i = $start; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '=\s*(\d+);') { $nums += [int]$Matches[1] }
        if ($lines[$i] -match '^\s*\}') { break }
    }
    $d = $nums | Group-Object | Where-Object Count -gt 1
    if ($d) {
        Fail -Summary "$p.proto reuses oneof field number(s) $($d.Name -join ',')" `
             -What "two messages share a wire slot, so the receiver decodes one as the other - fields land in the wrong place and the corruption is silent, not an error" `
             -Where "$path, in the 'oneof content' block near line $start" `
             -Fix "give the newer message an unused number. NEVER reuse a retired one - leave a gap and a comment saying what used to be there"
    }
    else { Pass "$p.proto oneof: $($nums.Count) entries, no duplicate numbers" }
}

$requests = (Select-String -Path "code\protocol\client.proto" -Pattern '^message (\w+Request) \{' -AllMatches).Matches | ForEach-Object { $_.Groups[1].Value }
$handlers = (Get-ChildItem "code\server\native" -Recurse -Include *.cpp | Select-String -Pattern 'RegisterHandler<&\w+::Handle(\w+)>' -AllMatches).Matches | ForEach-Object { $_.Groups[1].Value }
# Handlers are not always named after the whole message - GameServer::HandleAuthentication
# takes an AuthenticationRequest - so the stem counts too.
$unhandled = $requests | Where-Object { $stem = $_ -replace 'Request$',''; ($handlers -notcontains $_) -and ($handlers -notcontains $stem) }
foreach ($u in $unhandled) {
    $line = (Select-String -Path "code\protocol\client.proto" -Pattern "^message $u \{" | Select-Object -First 1).LineNumber
    Fail -Summary "$u is declared but nothing handles it" `
         -What "a client sending this gets SILENCE - no error on either side. Whoever wires a client to it next loses a debugging session to a message the server was never listening for" `
         -Where "code\protocol\client.proto`:$line" `
         -Fix "either add GServer->RegisterHandler<&YourSystem::Handle$u>(this) and write the handler, or delete the message. If deleting, leave its oneof number retired with a comment rather than reusing it"
}
if (-not $unhandled) { Pass "$($requests.Count) client requests, all handled" }

# ---------------------------------------------------------------------------
# Only movement and voice may be unreliable.
#
# This guards a PROOF, not a style rule. The 2026-09-04 session/epoch audit concluded there
# is no stale-session mutation path, and the argument has three legs:
#
#   1. an old CONNECTION cannot reach a new session - GameNetworkingSockets is
#      connection-oriented and the connection->player mapping is erased on disconnect;
#   2. an old CHARACTER cannot mutate a new one - a character switch is refused while the
#      player still has a puppet in the world;
#   3. within one connection, nothing can arrive out of order - because every message that
#      mutates persistent state travels RELIABLE and ORDERED.
#
# Leg 3 is the one a future commit can break silently. Marking some new mutation unreliable
# for latency reasons would reopen the whole class - an older packet overtaking a newer one
# and rewriting state - and nothing else in the build would notice.
#
# Movement is unreliable by design (retransmitting a stale position is worse than losing it)
# and has its own ordering check. Voice is unreliable and mutates nothing persistent.
Head "unreliable messages"
# Both directions of the same two systems, and nothing else.
#
#   MoveEntityRequest / NotifyEntityMove - position. Unreliable on purpose: a retransmitted
#       stale position is worse than a lost one, since a newer sample is always moments
#       behind. Both ends have an explicit ordering check - the server compares `tick`
#       (Level::HandleMoveEntityRequest), the client drops anything <= the last sequence it
#       applied (InterpolationSystem.cpp:721).
#   VoiceFrameRequest / NotifyVoiceFrame - audio frames. Mutate no persistent state at all;
#       ordering is the client jitter buffer's problem and it carries its own sequence.
$allowedUnreliable = @("MoveEntityRequest", "VoiceFrameRequest", "NotifyEntityMove", "NotifyVoiceFrame")
$unreliableFound = @()

foreach ($p in @("client", "server")) {
    $protoPath = "code\protocol\$p.proto"
    $protoLines = Get-Content $protoPath
    $currentMessage = ""

    for ($i = 0; $i -lt $protoLines.Count; $i++) {
        if ($protoLines[$i] -match '^\s*message\s+(\w+)') { $currentMessage = $Matches[1] }
        if ($protoLines[$i] -match '^\s*bool\s+unreliable\s*=') {
            $unreliableFound += [pscustomobject]@{ Proto = $p; Name = $currentMessage; Line = $i + 1 }
        }
    }
}

$unexpected = @($unreliableFound | Where-Object { $allowedUnreliable -notcontains $_.Name })

if ($unexpected.Count -eq 0) {
    Pass "$($unreliableFound.Count) unreliable message(s), all expected"
}
else {
    foreach ($u in $unexpected) {
        Fail -Summary "$($u.Name) is marked unreliable" `
             -What "an unreliable message can be REORDERED, so an older one can overtake a newer and rewrite state it should not. The session audit's conclusion that no stale-packet path exists depends on every state-mutating message being reliable and ordered - this reopens that class, and nothing else in the build would notice" `
             -Where "code\protocol\$($u.Proto).proto`:$($u.Line)" `
             -Fix "if this message does NOT mutate persistent state, add it to `$allowedUnreliable in this script with a note saying why. If it DOES, either make it reliable or give it an explicit ordering check like MoveEntityRequest's tick comparison in Level::HandleMoveEntityRequest"
    }
}

# ---------------------------------------------------------------------------
Head "server wiring"
$gsPath = "code\server\native\GameServer.cpp"
$gs = Get-Content $gsPath -Raw
$wiringOk = $true
foreach ($s in @('m_players.Load','m_bans.Load','m_vehicles.Load','m_worldFacts.Load','m_messages.Load','m_calls.Load')) {
    if ($gs -notmatch [regex]::Escape($s)) {
        $wiringOk = $false
        Fail -Summary "$s is never called" `
             -What "that store starts EMPTY every boot and silently holds nothing. Reads return defaults, writes go to a file nobody loaded, and it looks like the feature simply does not work rather than like a missing line" `
             -Where "$gsPath, in the boot block beside the other .Load calls" `
             -Fix "add $s(serverPath / `"config`" / `"<name>.json`") next to the others"
    }
}
foreach ($t in @('SavePlayerPositions','EnforceJail','ExpireCalls')) {
    if ($gs -notmatch "$t\(now\)") {
        $wiringOk = $false
        Fail -Summary "$t is not driven from OnUpdate" `
             -What "nothing ever calls it, so whatever it maintains never happens - timers never expire, state never settles, and there is no error to notice" `
             -Where "$gsPath, in GameServer::OnUpdate" `
             -Fix "add $t(now); alongside the other periodic calls in OnUpdate"
    }
}
foreach ($t in @('TickCalls','TickTrades','TickMedical')) {
    if ($gs -notmatch "$t\(\)") {
        $wiringOk = $false
        Fail -Summary "$t is not driven from OnUpdate" `
             -What "nothing ever calls it. Calls never ring out, trades never expire, nobody ever bleeds out - all of which look like the feature being broken rather than unwired" `
             -Where "$gsPath, in GameServer::OnUpdate" `
             -Fix "add pChat->$t(); inside the existing 'if (auto* pChat = GetWorld()->get_mut<ChatSystem>())' block"
    }
}
if ($wiringOk) { Pass "stores load and ticks are wired" }

# ---------------------------------------------------------------------------
if (-not $SkipTests) {
    Head "unit tests"

    $vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars)) {
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vswhere) {
            $vs = & $vswhere -latest -property installationPath
            if ($vs) { $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat" }
        }
    }

    $out = Join-Path ([System.IO.Path]::GetTempPath()) "nco-tests"
    New-Item -ItemType Directory -Force $out | Out-Null
    $src = Join-Path $Repo "code/server/native"

    <#
        TWO TOOLCHAINS, because two kinds of machine run this now.

        MSVC on the Windows workstations, and GCC in a Linux container - a Claude Code on
        the web session checks this repo out on Ubuntu, where there is no MSVC and never
        will be. Before this branch existed the suite printed "tests SKIPPED" there, so a
        cloud session got the static checks and nothing else: 491 assertions quietly not
        run, on exactly the side CONTRIBUTING already calls the project's thinnest
        coverage.

        It is cheap only because of how the tests are written: every file in tools\tests is
        one self-contained translation unit, so "compile it" is a single command either way
        and only the flags and the header locations differ. Keep them that way.
    #>
    $exeExt = if ($IsWindows -eq $false) { "" } else { ".exe" }

    $gpp = $null
    $toolchain = $null
    if (Test-Path $vcvars) { $toolchain = "msvc" }
    else {
        $gpp = (Get-Command g++ -ErrorAction SilentlyContinue).Source
        if ($gpp) { $toolchain = "gcc" }
    }

    if (-not $toolchain) {
        Write-Host "  !!    no C++ compiler found (no MSVC, no g++) - tests SKIPPED, static checks above still ran" -ForegroundColor Yellow
    }
    else {
        $gccLibs = @()

        if ($toolchain -eq "msvc") {
            $json = (Get-ChildItem "$env:USERPROFILE\AppData\Local\.xmake\packages\n\nlohmann_json" -Recurse -Filter "json.hpp" -ErrorAction SilentlyContinue |
                     Select-Object -First 1).Directory.Parent.FullName

            <#
                glm and spdlog, so a test can compile the REAL PlayerStore.

                Without these, PlayerStore.h cannot be included by a test at all - it uses
                glm::vec3 and spdlog - which is why trade_test restates its algorithm instead of
                testing it. That was an acceptable compromise for arithmetic; it is NOT
                acceptable for the economy migration, where the thing that needs proving is the
                TRANSACTION (persist, then swap) rather than the arithmetic, and a miniature
                model of a store proves nothing about the real one.

                Both are header-only packages already on disk for the normal build, so this adds
                no dependency - it only lets the test harness see what the server already sees.
                Missing packages degrade to no include path and the affected test fails loudly
                rather than being silently skipped.
            #>
            $packageInclude = {
                param($letter, $name)
                (Get-ChildItem "$env:USERPROFILE\AppData\Local\.xmake\packages\$letter\$name" -Recurse -Directory -Filter "include" -ErrorAction SilentlyContinue |
                 Select-Object -First 1).FullName
            }

            $glm = & $packageInclude "g" "glm"
            $spdlog = & $packageInclude "s" "spdlog"

            $compileFix = "cl /std:c++17 /EHsc /I code\server\native tools\tests\<test>.cpp"
        }
        else {
            <#
                The three headers the tests need - nlohmann_json, glm and spdlog - are the
                distribution's own packages here (nlohmann-json3-dev, libglm-dev,
                libspdlog-dev). They sit on the default include path, so there is no xmake
                package directory to go hunting through and nothing to add.

                -lfmt, and only when libfmt is actually installed: Debian and Ubuntu build
                libspdlog against the system fmt (SPDLOG_FMT_EXTERNAL), so the two tests
                that include spdlog reference fmt symbols that are NOT header-only and die
                at link time under a wall of undefined references. Passing -lfmt blind
                instead would turn a missing package into "cannot find -lfmt", which reads
                as a broken test rather than a machine that needs one apt install.
            #>
            $ld = ""
            try { $ld = (& ldconfig -p 2>$null) -join "`n" } catch { }
            if ($ld -match "libfmt\.so") { $gccLibs += "-lfmt" }

            $compileFix = "g++ -std=c++17 -I code/server/native tools/tests/<test>.cpp -o /tmp/t $($gccLibs -join ' ')   -- headers come from: apt install nlohmann-json3-dev libglm-dev libspdlog-dev"
        }

        foreach ($t in (Get-ChildItem (Join-Path $PSScriptRoot "tests") -Filter *.cpp)) {
            $exe = Join-Path $out "$($t.BaseName)$exeExt"

            if ($toolchain -eq "msvc") {
                $inc = ""
                if ($json)   { $inc += " /I `"$json`"" }
                if ($glm)    { $inc += " /I `"$glm`"" }
                if ($spdlog) { $inc += " /I `"$spdlog`"" }
                # NOTE the doubled backslash before the closing quote. cmd treats \" as an
                # escaped quote, so /Fo:"$out\" swallows the rest of the line and cl reports
                # "D8003: missing source filename" - which reads as the test being at fault.
                # /utf-8 because spdlog's bundled fmt static_asserts on it - "Unicode support
                # requires compiling with /utf-8". The real build already sets it; without it
                # here, any test that includes spdlog fails to compile for a reason that has
                # nothing to do with the test.
                $r = cmd /c "`"$vcvars`" >nul 2>&1 && cl /nologo /std:c++17 /utf-8 /EHsc $inc /I `"$src`" /Fe:`"$exe`" /Fo:`"$out\\`" `"$($t.FullName)`" 2>&1"
            }
            else {
                # Libraries after the source, or the linker has already discarded them by
                # the time it sees the references.
                $r = & $gpp -std=c++17 "-I$src" $t.FullName -o $exe @gccLibs 2>&1
            }

            if ($LASTEXITCODE -ne 0) {
                $err = ($r | Where-Object { $_ -match 'error' } | Select-Object -First 3) -join ' | '
                Fail -Summary "$($t.BaseName) does not compile" `
                     -What "the test cannot run, so whatever rule it protects is currently unguarded - this usually means the header it tests changed shape under it" `
                     -Where "tools\tests\$($t.Name)  --  $err" `
                     -Fix "compile it by hand to see the whole error: $compileFix"
                continue
            }

            $r = & $exe
            $ok = ($r | Select-String -Pattern "^ok").Count
            $failed = $r | Select-String -Pattern "^FAIL"

            if ($failed -or $LASTEXITCODE -ne 0) {
                Fail -Summary "$($t.BaseName): $($failed.Count) check(s) failing" `
                     -What "a rule this project relies on is no longer true. Each line below names the exact claim that broke" `
                     -Where (($failed | ForEach-Object { $_.Line.Trim() }) -join ' ;; ') `
                     -Fix "run it directly for the full output: $exe   -- then either the code regressed, or the rule changed deliberately and the test needs updating to match"
            }
            else { Pass "$($t.BaseName): $ok checks" }
        }
    }
}

# ---------------------------------------------------------------------------
Write-Host ""
if ($problems) {
    Write-Host "$($problems.Count) problem(s):" -ForegroundColor Red
    foreach ($p in $problems) { Write-Host "  - $($p.Summary)" -ForegroundColor Red }
    Write-Host ""
    Write-Host "Each is printed above with what it costs, where it is, and what to do." -ForegroundColor DarkGray
    exit 1
}

Write-Host "VERIFIED - nothing above needs a human." -ForegroundColor Green
Write-Host "Still not covered: anything needing the game running or two players." -ForegroundColor DarkGray
Write-Host "The Linux build is now PARTLY covered - the portability check catches std::" -ForegroundColor DarkGray
Write-Host "symbols used without their header, which is the one class of GCC-only failure" -ForegroundColor DarkGray
Write-Host "that has actually cost us a day. It is a lint, not a compiler: a clean run is" -ForegroundColor DarkGray
Write-Host "not a promise that GCC is happy, and a real deploy is still the only proof." -ForegroundColor DarkGray
exit 0
