#!/usr/bin/env python3
"""AgentLab oversight client: watches a scenario channel and checks the milestones.

Each expected message is {"sender": "<agent id>", "body": "<exact text>"} and must contain
the "@scenario-oversight" mention. Messages may arrive in any order; each expected entry
matches at most one channel message. Exit 0 when every expected message arrived, 1 when any
is missing at the deadline, 2 on usage or read errors.

Usage:
  oversight.py --expected expected.json [--timeout 120] [--replay lines.txt]
    --replay reads channel lines from a file (the same format `agentipc wait` prints) instead of
    the live channel, for offline checks.
Live mode runs: agentipc vs --direct ... wait <channel> (see AGENTIPC_CMD below).
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

MENTION = "@scenario-oversight"
LINE = re.compile(r"^#\S+ \[[^\]]*\] (\S+): (.*)$")
AGENTIPC = [os.path.expanduser("~/.venvs/agentipc/bin/python"), "/home/sblo/bin/agentipc"]


def parse_lines(text):
    """Channel lines -> (sender, body) pairs. Other lines are ignored."""
    out = []
    for line in text.splitlines():
        m = LINE.match(line.strip())
        if m:
            out.append((m.group(1), m.group(2)))
    return out


def match(expected, received):
    """Each expected entry takes the first unused received message with the same sender and
    exact body that carries the mention. Returns (matched, missing) lists of expected entries."""
    used = [False] * len(received)
    matched, missing = [], []
    for want in expected:
        hit = False
        for i, (sender, body) in enumerate(received):
            if used[i] or sender != want["sender"] or body != want["body"] or MENTION not in body:
                continue
            used[i] = True
            hit = True
            break
        (matched if hit else missing).append(want)
    return matched, missing


def read_live(channel, s2_pw_env, wait_secs):
    """One `wait` call on the channel; returns the parsed lines it printed."""
    env = dict(os.environ)
    cmd = AGENTIPC + ["vs", "--direct", "127.0.0.1:7440", "--server", "skylark",
                      "--user", "s2", "--password-env", s2_pw_env, "wait", channel]
    try:
        p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=wait_secs)
        return p.stdout
    except subprocess.TimeoutExpired as e:
        return e.stdout.decode() if isinstance(e.stdout, bytes) else (e.stdout or "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--expected", required=True)
    ap.add_argument("--channel", default="SKYLARK-MANAGER")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--replay")
    ap.add_argument("--password-env", default="S2_PW")
    args = ap.parse_args()

    try:
        with open(args.expected, encoding="utf-8") as f:
            expected = json.load(f)
        for e in expected:
            if not isinstance(e, dict) or not {"sender", "body"} <= set(e) or MENTION not in e["body"]:
                raise ValueError("each expected entry needs sender and a body with " + MENTION)
    except (OSError, ValueError) as e:
        print("error: bad expected list: %s" % e, file=sys.stderr)
        return 2

    received = []
    if args.replay:
        try:
            with open(args.replay, encoding="utf-8") as f:
                received = parse_lines(f.read())
        except OSError as e:
            print("error: %s" % e, file=sys.stderr)
            return 2
    else:
        deadline = time.time() + args.timeout
        while time.time() < deadline and match(expected, received)[1]:
            received += parse_lines(read_live(args.channel, args.password_env,
                                              max(1, int(deadline - time.time()))))

    matched, missing = match(expected, received)
    for e in matched:
        print("ok      %s: %s" % (e["sender"], e["body"]))
    for e in missing:
        print("MISSING %s: %s" % (e["sender"], e["body"]))
    print("oversight: %d/%d expected messages matched" % (len(matched), len(expected)))
    return 0 if not missing else 1


if __name__ == "__main__":
    sys.exit(main())
