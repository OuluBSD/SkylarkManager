#!/usr/bin/env python3
"""AgentLab runner: seeds a composed ManagerSkylark, replays a scenario through skylarkctl, asserts.

A scenario is data (scenarios/<id>.json): the sites it uses, the seed, the accounts to provision,
the agents, and an ordered list of steps. Every site interaction goes through the SiteCli binary
(`--session` per agent), so the runner never calls a site route or reads a site store. The other
processes it starts are the ManagerSkylark binary: once to provision accounts, once as the composed
server, and once to approve a TaskFoundry grant (the operator's CLI step).

Usage:
  agentlab.py --binary <ManagerSkylark> --site-cli <SiteCli> [--scenario scenarios/x.json]
              [--port-base 18240] [--announce FILE]
Exit 0 when every assertion passes, 1 when any fails, 2 on usage errors.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SITE_ORDER = ["privatehub", "science", "taskfoundry"]
STOCK_SECTIONS = ["github"]  # served by the main port, no composed port of their own


def password_for(user):
    """Deterministic seed password per account; the same value provisions and signs in."""
    return "pw-" + user


def run_cli(site_cli, server, sites, jar, args):
    """One skylarkctl call. Returns (rc, combined output)."""
    cmd = [site_cli, "--server", server]
    for section, url in sites.items():
        cmd += ["--site", "%s=%s" % (section, url)]
    if jar:
        cmd += ["--session", jar]
    cmd += args
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    return p.returncode, p.stdout + p.stderr


def wait_up(site_cli, server, sites, timeout=20.0):
    """Polls the command manifest through SiteCli until the server answers."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if run_cli(site_cli, server, sites, None, ["manifest"])[0] == 0:
            return True
        time.sleep(0.25)
    return False


def seed(scenario, tmp):
    """Writes the fixture repo and corpus files the scenario starts from."""
    workspace = os.path.join(tmp, "ws")
    corpus = os.path.join(tmp, "corpus")
    repo = os.path.join(workspace, "acme", "widget")
    os.makedirs(repo)
    os.makedirs(corpus)
    subprocess.run(["git", "-C", repo, "init", "-q"], check=True)
    subprocess.run(["git", "-C", repo, "-c", "user.name=agentlab", "-c", "user.email=agentlab@example.invalid",
                    "commit", "-q", "--allow-empty", "-m", "seed"], check=True)
    for paper in scenario.get("seed", {}).get("papers", []):
        with open(os.path.join(corpus, paper["id"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(paper, f)
    return workspace, corpus


def provision(binary, site_dir, workspace, scenario):
    """Creates the scenario's accounts in the seeded data root, through the ManagerSkylark CLI."""
    for site, accounts in scenario.get("provision", {}).items():
        for acct in accounts:
            env = dict(os.environ)
            user = acct["user"]
            if site == "privatehub":
                env["MANAGERSKYLARK_PRIVATEHUB_PASSWORD"] = password_for(user)
                flags = ["--set-privatehub-user", user, "--privatehub-workspace", workspace]
                if acct.get("role"):
                    flags += ["--privatehub-role", acct["role"]]
            elif site == "science":
                env["MANAGERSKYLARK_SCIENCE_PASSWORD"] = password_for(user)
                flags = ["--set-science-user", user]
            elif site == "taskfoundry":
                env["MANAGERSKYLARK_TASKFOUNDRY_PASSWORD"] = password_for(user)
                flags = ["--set-taskfoundry-user", user, "--company-repos", "acme/widget"]
            elif site == "github":
                env["MANAGERSKYLARK_SITE_PASSWORD"] = password_for(user)
                flags = ["--set-site-password", user]
            else:
                raise ValueError("no provisioning for site '%s'" % site)
            p = subprocess.run([binary, "--site-dir", site_dir] + flags, env=env,
                               capture_output=True, text=True, timeout=60)
            if p.returncode != 0:
                raise RuntimeError("provision %s/%s failed: %s" % (site, user, p.stdout + p.stderr))


def server_args(binary, site_dir, ports, workspace, corpus, company_root):
    """Command line for the composed server; each site gets its own port."""
    args = [binary, "--site-dir", site_dir, "--port", str(ports["main"]),
            "--github-backend", "emulated", "--github-store", os.path.join(os.path.dirname(site_dir), "github")]
    if "privatehub" in ports:
        args += ["--privatehub-port", str(ports["privatehub"]), "--privatehub-workspace", workspace]
    if "science" in ports:
        args += ["--science-port", str(ports["science"]), "--science-corpus", corpus]
    if "taskfoundry" in ports:
        args += ["--taskfoundry-port", str(ports["taskfoundry"]), "--company-root", company_root,
                 "--company-repos", "acme/widget"]
    return args


def substitute(word, captured):
    """Replaces {name} with a value captured by an earlier step."""
    for name, value in captured.items():
        word = word.replace("{%s}" % name, value)
    return word


def check(results, label, ok, detail=""):
    results.append((label, ok))
    line = ("ok   " if ok else "FAIL ") + label
    if not ok and detail:
        line += "\n     " + detail.replace("\n", "\n     ")[:1200]
    print(line)


def run_scenario(path, binary, site_cli, port_base, announce):
    with open(path, encoding="utf-8") as f:
        scenario = json.load(f)
    sites_used = scenario["sites"]
    ports = {"main": port_base}
    for i, name in enumerate(SITE_ORDER):
        if name in sites_used:
            ports[name] = port_base + 1 + i
    main_url = "http://127.0.0.1:%d" % ports["main"]
    sites = {name: "http://127.0.0.1:%d" % ports[name] for name in sites_used if name in ports}
    for name in sites_used:
        if name in STOCK_SECTIONS:
            sites[name] = main_url
    results = []

    with tempfile.TemporaryDirectory(prefix="agentlab-") as tmp:
        site_dir = os.path.join(tmp, "site")
        company_root = os.path.join(tmp, "company")
        jars = {a: os.path.join(tmp, "jar-%s.txt" % a) for a in scenario["agents"]}
        workspace, corpus = seed(scenario, tmp)
        provision(binary, site_dir, workspace, scenario)

        server = subprocess.Popen(server_args(binary, site_dir, ports, workspace, corpus, company_root),
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            if not wait_up(site_cli, main_url, sites):
                print("FAIL server did not come up")
                return 1
            captured = {}
            for step in scenario["steps"]:
                sid = step.get("id", "step")
                agent = step.get("as")
                if "login" in step:
                    user = scenario["agents"][agent]["user"]
                    os.environ["AGENTLAB_PW"] = password_for(user)
                    rc, out = run_cli(site_cli, main_url, sites, jars[agent],
                                      ["--password-env", "AGENTLAB_PW", "login", step["login"], user])
                    check(results, "%s: %s signs in" % (sid, agent), rc == 0, out)
                    continue
                if "approve_grant" in step:
                    # Operator step: the ManagerSkylark CLI approves a requested grant by id.
                    gid = substitute(step["approve_grant"], captured)
                    p = subprocess.run([binary, "--site-dir", site_dir, "--approve-taskfoundry-grant", gid],
                                       capture_output=True, text=True, timeout=60)
                    check(results, "%s: operator approves grant %s" % (sid, gid), p.returncode == 0,
                          p.stdout + p.stderr)
                    continue
                args = [substitute(a, captured) for a in step["run"]]
                rc, out = run_cli(site_cli, main_url, sites, jars[agent], args)
                want = step.get("expect", {})
                ok = rc == want.get("rc", 0)
                for needle in want.get("contains", []):
                    ok = ok and substitute(needle, captured) in out
                for needle in want.get("not_contains", []):
                    ok = ok and substitute(needle, captured) not in out
                if "capture" in step:
                    cap = step["capture"]
                    m = re.search(cap["pattern"], out)
                    ok = ok and m is not None
                    if m:
                        captured[cap["name"]] = m.group(1)
                check(results, "%s: %s" % (sid, step.get("describe", "")), ok,
                      "rc %d, output: %s" % (rc, out))
                if ok and "announce" in step:
                    line = "ANNOUNCE %s: %s" % (agent, step["announce"])
                    print(line)
                    if announce:
                        with open(announce, "a", encoding="utf-8") as f:
                            f.write(line + "\n")
        finally:
            server.terminate()
            server.wait(timeout=10)

    passed = sum(1 for _, ok in results if ok)
    print("scenario %s: %s (%d/%d)" % (scenario["id"], "PASS" if passed == len(results) else "FAIL",
                                       passed, len(results)))
    return 0 if passed == len(results) else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True, help="ManagerSkylark binary")
    ap.add_argument("--site-cli", required=True, help="SiteCli binary (skylarkctl)")
    ap.add_argument("--scenario", action="append", help="scenario JSON; repeatable; default all")
    ap.add_argument("--port-base", type=int, default=18240)
    ap.add_argument("--announce", help="append ANNOUNCE lines to this file for the oversight client")
    args = ap.parse_args()

    scenarios = args.scenario or sorted(glob.glob(os.path.join(HERE, "scenarios", "*.json")))
    if not scenarios:
        print("error: no scenarios", file=sys.stderr)
        return 2
    rc = 0
    base = args.port_base
    for path in scenarios:
        # Each scenario gets its own block of ports, so one run cannot hold another's ports.
        rc |= run_scenario(path, args.binary, args.site_cli, base, args.announce)
        base += 10
    return rc


if __name__ == "__main__":
    sys.exit(main())
