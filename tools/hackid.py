# TweakDBID <-> name, offline - the fast half of the quickhack live-dump loop.
#
# The server logs every refused hack as `[QUICKHACK] refused <decimal id> from <who>`;
# this turns that number back into a record name (and the reverse) using the same
# encoding the server's QuickhackComponent.h documents: zlib CRC32 of the name in the
# low 32 bits, name length in bits 32-39. Sanity anchor from the C++ static_assert:
# "QuickHack.BaseOverheatHack" -> crc 0xC9259006, length 26.
#
#   python tools/hackid.py QuickHack.BaseOverheatHack        # name -> id
#   python tools/hackid.py 111708748038                     # id -> name (candidate scan)
#   python tools/hackid.py 111708748038 --names dump.txt    # scan a full name dump too
import argparse
import itertools
import sys
import zlib


def tweak_id(name: str) -> int:
    return (zlib.crc32(name.encode("utf-8")) & 0xFFFFFFFF) | ((len(name) & 0xFF) << 32)


# The table the server ships (Level.cpp QuickhackRules) plus the leveled-variant name
# shapes seen across TweakDB dumps. Candidates cost nothing to test - only a CRC match
# with the right length can hit, so a wrong guess here cannot produce a wrong answer.
BASE_NAMES = [
    "QuickHack.BaseOverheatHack", "QuickHack.BrainMeltBaseHack", "QuickHack.OverloadBaseHack",
    "QuickHack.BaseContagionHack", "QuickHack.SystemCollapseHackBase", "QuickHack.SuicideHackBase",
    "QuickHack.BaseBlindHack", "QuickHack.BaseWeaponMalfunctionHack",
    "QuickHack.BaseLocomotionMalfunctionHack", "QuickHack.BaseCyberwareMalfunctionHack",
    "QuickHack.BaseMemoryWipeHack", "QuickHack.MadnessHackBase",
    "QuickHack.BasePingHack", "QuickHack.BaseWhistleHack",
    "QuickHack.BaseCommsCallInHack", "QuickHack.BaseCommsNoiseHack",
]

STEMS = ["Overheat", "BrainMelt", "Overload", "Contagion", "SystemCollapse", "Suicide",
         "Blind", "WeaponMalfunction", "LocomotionMalfunction", "CyberwareMalfunction",
         "MemoryWipe", "Madness", "Ping", "Whistle", "CommsCallIn", "CommsNoise",
         "Cripple", "Reboot", "ShortCircuit", "SynapseBurnout", "CommsCall"]

PATTERNS = ["QuickHack.{s}Hack", "QuickHack.Base{s}Hack", "QuickHack.{s}BaseHack",
            "QuickHack.{s}Hack{n}", "QuickHack.{s}HackLvl{n}", "QuickHack.{s}Hack{n}Hack",
            "QuickHack.{s}LvL{n}Hack", "QuickHack.{s}Hack_inline{n}"]


def candidates(extra_file=None):
    seen = set(BASE_NAMES)
    yield from BASE_NAMES
    for stem, pat, n in itertools.product(STEMS, PATTERNS, range(0, 6)):
        name = pat.format(s=stem, n=n)
        if name not in seen:
            seen.add(name)
            yield name
    if extra_file:
        with open(extra_file, encoding="utf-8", errors="replace") as f:
            for line in f:
                name = line.strip()
                if name and name not in seen:
                    seen.add(name)
                    yield name


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("query", help="a record name, or a decimal/0x id from the server log")
    ap.add_argument("--names", help="optional newline-separated name dump to scan as well")
    args = ap.parse_args()

    q = args.query
    if q.lstrip("-").isdigit() or q.lower().startswith("0x"):
        target = int(q, 0)
        length = (target >> 32) & 0xFF
        print(f"id {target} (crc 0x{target & 0xFFFFFFFF:08X}, name length {length})")
        for name in candidates(args.names):
            if len(name) == length and tweak_id(name) == target:
                print(f"MATCH: {name}")
                return
        print("no candidate matched - feed a fuller name dump with --names, or add the "
              "name shape to PATTERNS; only an exact CRC+length hit can ever match.")
        sys.exit(1)
    else:
        print(f"{q} -> {tweak_id(q)} (0x{tweak_id(q):X})")


if __name__ == "__main__":
    # Anchor from the server's static_assert - if this ever fails, the encodings drifted.
    assert tweak_id("QuickHack.BaseOverheatHack") & 0xFFFFFFFF == 0xC9259006
    main()
