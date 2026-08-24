"""Deterministic pass/fail gate for the movement validation lab.

Run from the repository root:
    python tools/netlab/validate.py
    python tools/netlab/validate.py --json

This is an offline test of the algorithms in replay.py. It does not run in the game.
"""
import argparse
import json
import sys

from netmodel import PROFILES
from replay import run
from strategies import ALL
import synth
from authority import AuthorityModel


PROFILES_TO_CHECK = ("lan", "rimtek", "wifi_burst")
KINDS_TO_CHECK = ("player", "vehicle")


def measure(profile_name, kind):
    truth = synth.make_truth(kind)
    wire = synth.sample_wire(truth, 30)
    records = synth.receive(wire, PROFILES[profile_name])
    board = run(records, kind, 30, truth=truth, strategies=[S(kind, 30) for S in ALL])
    return {row["name"]: row for row in board}


def check(results):
    failures = []

    authority = AuthorityModel("driver-a")
    if not authority.accepts_movement("driver-a", 0):
        failures.append("authority rejected the initial simulator")
    if authority.transfer("driver-a").epoch != 0:
        failures.append("authority bumped epoch for a same-owner announcement")
    if authority.accepts_movement("driver-b", 0):
        failures.append("authority accepted movement from a non-owner")
    authority.transfer("driver-b")
    if authority.state.epoch != 1:
        failures.append("authority did not bump epoch on ownership transfer")
    if authority.accepts_movement("driver-a", 1):
        failures.append("authority accepted movement from the previous owner")
    if not authority.accepts_movement("driver-b", 1):
        failures.append("authority rejected movement from the new owner")
    authority.transfer(None)
    if authority.accepts_movement("driver-b", 2):
        failures.append("parked authority accepted movement")

    for profile_name in PROFILES_TO_CHECK:
        row = results[profile_name]["player"]["baseline"]
        if row["pops"] != 0:
            failures.append(f"{profile_name}/player baseline pops={row['pops']} (expected 0)")
        if row["err_p95"] > 1.0:
            failures.append(f"{profile_name}/player baseline err_p95={row['err_p95']:.3f}m (limit 1.0m)")

        recovery = results[profile_name]["player"]["player_recovery"]
        if recovery["pops"] != 0:
            failures.append(f"{profile_name}/player player_recovery pops={recovery['pops']} (expected 0)")
        if recovery["err_p95"] > 1.0:
            failures.append(f"{profile_name}/player player_recovery err_p95={recovery['err_p95']:.3f}m (limit 1.0m)")

    for profile_name in PROFILES_TO_CHECK:
        row = results[profile_name]["vehicle"]["vehicle_dr"]
        if row["pops"] != 0:
            failures.append(f"{profile_name}/vehicle vehicle_dr pops={row['pops']} (expected 0)")
        if row["starve%"] > 2.0:
            failures.append(f"{profile_name}/vehicle vehicle_dr starve={row['starve%']:.3f}% (limit 2.0%)")

    baseline = results["rimtek"]["vehicle"]["baseline"]
    candidate = results["rimtek"]["vehicle"]["vehicle_dr"]
    if candidate["pops"] >= baseline["pops"]:
        failures.append("rimtek/vehicle vehicle_dr did not reduce teleport pops")
    if candidate["starve%"] >= baseline["starve%"]:
        failures.append("rimtek/vehicle vehicle_dr did not reduce starvation")
    if candidate["err_p95"] >= baseline["err_p95"]:
        failures.append("rimtek/vehicle vehicle_dr did not reduce p95 error")

    return failures


def main():
    parser = argparse.ArgumentParser(description="Run the deterministic netcode validation gate")
    parser.add_argument("--json", action="store_true", help="emit machine-readable results")
    args = parser.parse_args()

    results = {
        profile_name: {kind: measure(profile_name, kind) for kind in KINDS_TO_CHECK}
        for profile_name in PROFILES_TO_CHECK
    }
    failures = check(results)
    output = {"ok": not failures, "failures": failures, "results": results}

    if args.json:
        print(json.dumps(output, sort_keys=True))
    else:
        for profile_name in PROFILES_TO_CHECK:
            for kind in KINDS_TO_CHECK:
                row = results[profile_name][kind]
                print(
                    f"{profile_name:12} {kind:8} "
                    f"player={row.get('baseline', {}).get('err_p95', 0):6.3f}m "
                    f"vehicle_dr pops={row.get('vehicle_dr', {}).get('pops', 0):4} "
                    f"starve={row.get('vehicle_dr', {}).get('starve%', 0):6.3f}%"
                )
        if failures:
            print("\nFAIL")
            for failure in failures:
                print(f"- {failure}")
        else:
            print("\nPASS")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
