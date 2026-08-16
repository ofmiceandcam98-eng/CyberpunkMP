# Compile-check the redscript half of the mod without shipping anything.
#
# The same gate Ship.ps1 runs, pulled out so it can be used as a fast inner loop. A
# single bad .reds file does not fail locally - redscript aborts the whole compilation
# and the game starts with NO scripts at all, which looks exactly like the mod doing
# nothing. Finding that out at ship time costs a full build.
#
# It doubles as an oracle for the game's own script API. redscript is not the dialect
# the game's .script sources are written in, and the RTTI is not documented anywhere;
# writing a call and asking scc whether it resolves is faster and more reliable than
# guessing at a method name.
#
#   .\tools\CheckScripts.ps1
#
[CmdletBinding()]
param(
    # Defaults to whatever tools\Environment.ps1 resolves. Pass it to override for one run.
    [string]$GameDir,
    [switch]$Full   # print the whole scc output, not just the errors
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot "Environment.ps1")

# A parameter beats the machine's own configuration; without one, Environment.ps1 decides.
if ($GameDir) { $script:GameDir = $GameDir }
Assert-GameDir
$GameDir = $script:GameDir

$scc = Join-Path $GameDir "engine\tools\scc.exe"
if (-not (Test-Path $scc)) { Write-Host "no scc.exe at $scc" -ForegroundColor Red; exit 2 }

$scratch = Join-Path $env:TEMP "check_scc"
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

# Compile a COPY. distrib is a junction into the game's plugin folder, so checking the
# deployed scripts would mean writing them to their live location before knowing whether
# they compile.
$staged = Join-Path $scratch "redscript"
if (Test-Path $staged) { Remove-Item $staged -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staged | Out-Null
Copy-Item (Join-Path $Repo "code\assets\redscript\*") $staged -Recurse -Force

# Every path the game compiles, or the check is meaningless: with -compilePathsFile scc
# compiles ONLY the listed paths and ignores -compile entirely, so leaving the other
# plugins out makes our files fail on `import Codeware.*` - a fake error about real code.
# zzzCyberpunkMP is a junction to distrib and would compile our mod twice.
$scriptPaths = @($staged)
$scriptPaths += Get-ChildItem (Join-Path $GameDir "red4ext\plugins") -Directory |
    Where-Object { $_.Name -ne 'zzzCyberpunkMP' -and (Test-Path (Join-Path $_.FullName 'Scripts')) } |
    ForEach-Object { Join-Path $_.FullName 'Scripts' }

# NO BOM. PowerShell 5.1's -Encoding UTF8 writes one, scc reads the first path as
# garbage, silently compiles nothing and reports success - a false pass that is worse
# than no check at all.
$pathsFile = Join-Path $scratch "paths.txt"
[System.IO.File]::WriteAllLines($pathsFile, $scriptPaths, (New-Object System.Text.UTF8Encoding($false)))

# Run it detached, and be ready to kill it.
#
# scc.exe is the same binary the game's loader runs, and on failure it pops the
# player-facing "REDScript compilation has failed" MessageBox and BLOCKS until somebody
# clicks OK. Invoking it inline therefore puts a modal error dialog on the screen of
# whoever happens to be at the machine - Cam got one for a throwaway probe file that had
# never been anywhere near his install. The errors we actually want are already on
# stdout by the time the dialog appears, so capture those and end the process.
$stdout = Join-Path $scratch "scc.out"
$stderr = Join-Path $scratch "scc.err"

$proc = Start-Process -FilePath $scc -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
    -ArgumentList @(
        # Quoted by hand. Start-Process passes ArgumentList through verbatim, so an
        # unquoted "C:\Program Files (x86)\..." arrives as several arguments and scc
        # fails on the fragment after the space.
        '-compile', ('"{0}"' -f (Join-Path $GameDir "r6\scripts")),
        ('"{0}"' -f (Join-Path $GameDir "r6\cache\final.redscripts")),
        '-compilePathsFile', ('"{0}"' -f $pathsFile),
        '-outputCacheFile', ('"{0}"' -f (Join-Path $scratch "final.redscripts"))
    )

# Kill it the moment an error shows up on stdout rather than waiting out a timeout -
# the dialog is on screen for every second we wait.
$blocked = $false
$deadline = (Get-Date).AddSeconds(120)

while ($true) {
    if ($proc.HasExited) { break }

    if ((Get-Date) -gt $deadline) { $blocked = $true; break }

    $seen = if (Test-Path $stdout) { Get-Content $stdout -Raw } else { $null }
    if ($seen -and $seen -match '\[ERROR') {
        Start-Sleep -Milliseconds 750   # let the rest of the diagnostics land
        $blocked = $true
        break
    }

    Start-Sleep -Milliseconds 250
}

if ($blocked) {
    try { $proc | Stop-Process -Force } catch {}
    Start-Sleep -Milliseconds 250
}

$out = @()
if (Test-Path $stdout) { $out += Get-Content $stdout }
if (Test-Path $stderr) { $out += Get-Content $stderr }

if ($Full) { $out | ForEach-Object { Write-Host $_ } }

# Judged on what scc SAID, not on its exit code. A Process object from Start-Process
# -PassThru reports a stale ExitCode, and a check that reports FAIL on a clean compile
# gets ignored within a day - which is the same as not having one.
$text = ($out -join "`n")
$failed = $blocked -or ($text -match '\[ERROR') -or ($text -notmatch 'Compilation complete')

if ($failed) {
    if (-not $Full) {
        $out | Where-Object { $_ -match '(?i)error' } | Select-Object -First 30 |
            ForEach-Object { Write-Host $_ -ForegroundColor Red }
    }
    Write-Host "FAIL" -ForegroundColor Red
    exit 1
}

Write-Host "OK - redscript compiles" -ForegroundColor Green
exit 0
