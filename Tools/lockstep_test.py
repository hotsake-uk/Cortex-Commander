#!/usr/bin/env python3
"""Plays an online (lockstep) match between two copies of the game on this machine, over loopback, and checks they stayed in sync.

Both copies run headless, with made-up input for each player (CCCP_LOCKSTEP_TEST_INPUT), for the number of sim updates given. They compare
their simulation's hash every 60 updates while they play; afterwards this compares their whole determinism traces, which have to match line
for line. Run from the repository root, like determinism_test.py, whose settings handling this shares.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from determinism_test import TEST_ENVIRONMENT  # noqa: E402


# Settings the one who joins starts with: the first few change what happens in the match (and so are the host's for it), the last only how it looks.
JOINER_SETTINGS = {"LegStrength": "0.5", "CarriedWeightMultiplier": "2", "JetpackFuelMultiplier": "0.25", "UnitToughness": "3", "ScreenShakeStrength": "1.5"}


def set_settings(text, values):
    """Sets settings in a settings file's text, adding any it doesn't have."""
    lines = text.splitlines()
    for name, value in values.items():
        for number, line in enumerate(lines):
            if line.startswith("\t") and line[1:].split(" =")[0] == name:
                lines[number] = f"\t{name} = {value}"
                break
        else:
            lines.insert(1, f"\t{name} = {value}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="the game executable")
    parser.add_argument("--base-settings", default="Userdata/Settings.ini", help="the settings both copies start from (default: %(default)s)")
    parser.add_argument("--activity", nargs=3, default=["GAScripted", "Arena", "Ketanot Hills"], metavar=("TYPE", "ACTIVITY", "SCENE"), help="the match to play (default: Arena on Ketanot Hills)")
    parser.add_argument("--updates", type=int, default=3000, help="how many sim updates to play (default: %(default)s)")
    parser.add_argument("--seed", type=int, default=0, help="the match's seed (default: %(default)s)")
    parser.add_argument("--port", type=int, default=47100, help="the port the host waits on (default: %(default)s)")
    parser.add_argument("--work-dir", type=Path, default=Path("Instances/LockstepTest"), help="where the settings, logs and traces go (default: %(default)s)")
    parser.add_argument("--windowed", action="store_true", help="play with the game windows, in real time, instead of headless")
    parser.add_argument("--timeout", type=int, default=1800, help="how many seconds the match can take (default: %(default)s)")
    options = parser.parse_args()

    options.work_dir.mkdir(parents=True, exist_ok=True)
    exe = str(Path(options.exe).resolve())
    # The host says what to play, with which seed; the one who joins only says where the host is.
    copies = {"Host": ["-activity", *options.activity, "-seed", str(options.seed), "-lockstep-host", str(options.port)], "Join": ["-lockstep-join", f"127.0.0.1:{options.port}"]}
    processes = {}
    base = Path(options.base_settings)
    base_text = base.read_text() if base.exists() else "SettingsMan\n"
    for name, lockstep_args in copies.items():
        settings = options.work_dir / f"{name}.Settings.ini"
        # The base settings as they are: an online match sets up what the sim needs to run the same on both machines by itself. The one who
        # joins starts with some settings of their own that change the match, which the host's replace, and some that only change the looks,
        # which they keep.
        text = base_text if name == "Host" else set_settings(base_text, JOINER_SETTINGS)
        settings.write_text(text)
        args = [exe, "-sim-updates", str(options.updates), *lockstep_args]
        if not options.windowed:
            args.append("-headless")
        environment = {**os.environ, **TEST_ENVIRONMENT, "CCCP_SETTINGSPATH": str(settings), "CCCP_INSTANCE": f"Lockstep{name}", "CCCP_LOCKSTEP_TEST_INPUT": "1", "CCCP_CONSOLE_LOG": str((options.work_dir / f"{name}.console.txt").resolve())}
        processes[name] = subprocess.Popen(args, stderr=subprocess.PIPE, text=True, env=environment)

    failed = False
    for name, process in processes.items():
        try:
            _, stderr = process.communicate(timeout=options.timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            print(f"{name}: still going after {options.timeout} seconds; stopped.")
            failed = True
            continue
        if process.returncode != 0:
            noise = ("libpng warning", "Enable Debug output")
            report = "\n".join(line for line in stderr.splitlines() if line.strip() and not line.startswith(noise))
            print(f"{name}: exited with code {process.returncode}.\n{report}")
            failed = True

    for name in copies:
        log = options.work_dir / f"{name}.console.txt"
        lines = log.read_text(errors="replace").splitlines() if log.exists() else []
        for line in lines:
            if line.startswith("ONLINE:") and ("out of sync" in line or "left" in line or "can't play" in line or "couldn't" in line or "has to say" in line):
                print(f"{name}: {line}")
                failed = True

    # The one who joined took on the host's settings for the match only: their settings file still has their own.
    join_settings = (options.work_dir / "Join.Settings.ini").read_text().splitlines()
    for name, value in JOINER_SETTINGS.items():
        line = next((line for line in join_settings if line.startswith("\t") and line[1:].split(" =")[0] == name), "")
        if not line or float(line.split("=")[1]) != float(value):
            print(f"Join: the settings file has {line.strip() or 'no ' + name} after the match, not its own {name} = {value}.")
            failed = True

    traces = [Path("Instances") / "LockstepHost" / "LockstepPlayer1.trace", Path("Instances") / "LockstepJoin" / "LockstepPlayer2.trace"]
    if all(trace.exists() for trace in traces):
        host_lines = traces[0].read_text().splitlines()
        join_lines = traces[1].read_text().splitlines()
        for number, (host_line, join_line) in enumerate(zip(host_lines, join_lines), 1):
            if host_line != join_line:
                print(f"The traces part at line {number}:\n  host: {host_line}\n  join: {join_line}")
                failed = True
                break
        else:
            if len(host_lines) != len(join_lines):
                print(f"The traces match as far as they go, but the host's has {len(host_lines)} lines and the joiner's {len(join_lines)}.")
                failed = True
            else:
                print(f"The traces match: {len(host_lines)} checkpoints, through sim update {host_lines[-1].split()[0] if host_lines else 0}.")
    else:
        print("A trace is missing: " + ", ".join(str(trace) for trace in traces if not trace.exists()))
        failed = True

    print("FAILED" if failed else "PASSED: the two copies stayed in sync.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
