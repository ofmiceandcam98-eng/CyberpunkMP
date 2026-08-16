# Copy to  tools\ship.local.ps1  and edit. That file is gitignored.
#
#     copy tools\ship.local.example.ps1 tools\ship.local.ps1
#
# Everything here is optional. tools\Environment.ps1 derives what it can from the checkout
# and from PATH; this file is for the things a machine has to say for itself, and for
# overriding a derivation that guessed wrong.
#
# No secrets belong in this file. Tokens and passwords live in the environment or in the
# separately-ignored files documented in CONTRIBUTING.md.

# --- the game ---------------------------------------------------------------
# Required for anything that compiles redscript or deploys the mod. There is no reliable
# way to find this automatically - Steam libraries move, and picking the wrong install
# silently is worse than asking.
#
# Should contain bin\x64\Cyberpunk2077.exe.

# $GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077"
# $GameDir = "D:\SteamLibrary\steamapps\common\Cyberpunk 2077"


# --- xmake ------------------------------------------------------------------
# Found on PATH by default. Set this only if you have several, or it is not on PATH.

# $XMake = "C:\Users\you\scoop\shims\xmake.exe"


# --- the repo ---------------------------------------------------------------
# Derived from `git rev-parse --show-toplevel`. Set it only if you are doing something
# unusual, like driving a build in another checkout.

# $Repo = "C:\path\to\CyberpunkMP"


# --- where releases go ------------------------------------------------------
# Derived from the `fork` remote, falling back to `origin`. Set it if you publish
# somewhere other than the remote you cloned from.
#
# NOTE: publishing is not something a second checkout normally does. See the
# "Working from a second machine" section in CONTRIBUTING.md.

# $GhRepo = "youruser/CyberpunkMP"
