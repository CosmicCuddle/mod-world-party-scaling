#!/usr/bin/env python3
"""Mutation check: each mutant breaks one owner-approved rule in a temporary copy of the module
source; the regression suite must FAIL for every mutant. Proves the tests can detect regressions.

    tests/mutation_check.py [--cxx g++]
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src" / "WorldPartyScaling.cpp"

# (id, rule, [(old, new), ...])
MUTANTS = [
    ("M01", "R1 elite no longer excluded", [("info->rank == CREATURE_ELITE_ELITE\n", "info->rank == 999u\n")]),
    ("M02", "R1 rare-elite no longer excluded", [("|| info->rank == CREATURE_ELITE_RAREELITE\n", "|| info->rank == 998u\n")]),
    ("M03", "R1 world boss no longer excluded", [("|| info->rank == CREATURE_ELITE_WORLDBOSS))", "|| info->rank == 997u))")]),
    ("M04", "R1 normal rares wrongly excluded", [("|| info->rank == CREATURE_ELITE_WORLDBOSS))", "|| info->rank == CREATURE_ELITE_WORLDBOSS || info->rank == CREATURE_ELITE_RARE))")]),
    ("M05", "R2 alive filter added (dead nearby stops counting)", [("if (member->IsWithinDistInMap(creature, rewardDistance))", "if (member->IsAlive() && member->IsWithinDistInMap(creature, rewardDistance))")]),
    ("M06", "R2 core corpse-based reward distance used instead of live position", [("if (member->IsWithinDistInMap(creature, rewardDistance))", "if (member->IsAtGroupRewardDistance(creature))")]),
    ("M07", "R2 distance ignored (whole group counts)", [("if (member->IsWithinDistInMap(creature, rewardDistance))", "if (rewardDistance > -1.0f)")]),
    ("M08", "R2 offline references counted", [("        if (!member)\n            continue;\n\n        // Count the member's", "        if (!member)\n        {\n            ++nearbyMembers;\n            continue;\n        }\n\n        // Count the member's")]),
    ("M09", "R2 tier cap lowered to 5", [("return std::clamp<uint32>(nearbyMembers, 1, MaxGroupSize);", "return std::clamp<uint32>(nearbyMembers, 1, 5u);")]),
    ("M10", "R3 tier recalculated on every damage event", [("    if (FindState(key, out))\n        return true;\n\n    if (!TryStartScaling", "    if (FindState(key, out))\n        RestoreAndErase(creature);\n\n    if (!TryStartScaling")]),
    ("M11", "R3 re-engage replaces the frozen state", [("    (void)TryStartScaling(creature, ResolvePlayer(victim));", "    RestoreAndErase(creature);\n    (void)TryStartScaling(creature, ResolvePlayer(victim));")]),
    ("M12", "R3 combat exit no longer restores", [("void WorldPartyScalingMgr::OnCreatureExitCombat(Creature* creature)\n{\n    RestoreAndErase(creature);", "void WorldPartyScalingMgr::OnCreatureExitCombat(Creature* creature)\n{\n    (void)creature;")]),
    ("M13", "instance gate removed", [("if (!map || !map->IsWorldMap() || map->IsDungeon() || map->IsBattlegroundOrArena())", "if (!map)")]),
    ("M14", "XP: core party bonus not divided out", [("double const scaled = double(amount) * double(desired) / coreRate;", "double const scaled = double(amount) * double(desired) + 0.0 * coreRate;")]),
    ("M15", "XP: excluded/unscaled creature XP modified", [("    if (!FindState(GetScalingKey(creature), state))\n        return;\n\n    float const desired", "    if (!FindState(GetScalingKey(creature), state))\n        state.groupSize = 6;\n\n    float const desired")]),
    ("M16", "health percent not preserved on scale", [("creature->SetHealth(HealthFromPercentage(creature->GetMaxHealth(), healthPct));\n}\n\nvoid RestoreScaling", "creature->SetHealth(creature->GetMaxHealth()); (void)healthPct;\n}\n\nvoid RestoreScaling")]),
    ("M17", "disable reload no longer restores", [("    if (g_WorldPartyScalingConfig.Enabled())\n        return;\n\n    std::vector", "    return;\n\n    std::vector")]),
    ("M18", "periodic heal-mixed spells scaled", [("return !spellInfo->HasAura(SPELL_AURA_PERIODIC_HEAL)\n        && ", "return ")]),
]


def build_and_run(cxx, src_dir, out):
    tests = ROOT / "tests"
    cmd = [cxx, "-std=gnu++20", "-O0", "-pthread", f"-I{tests/'fakecore'}", f"-I{tests}", f"-I{src_dir}",
           f'-DWPS_SOURCE_DIR="{ROOT}"', str(src_dir / "WorldPartyScaling.cpp"), str(tests / "WorldPartyScalingTests.cpp"),
           "-o", str(out)]
    b = subprocess.run(cmd, capture_output=True, text=True)
    if b.returncode != 0:
        return "build-failed", b.stderr[-2000:]
    r = subprocess.run([str(out)], capture_output=True, text=True, timeout=600)
    last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else ""
    return ("passed" if r.returncode == 0 else "failed"), last


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    args = ap.parse_args()
    original = SRC.read_text()
    killed = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        shutil.copy(ROOT / "src" / "WorldPartyScaling.h", tmp / "WorldPartyScaling.h")
        (tmp / "WorldPartyScaling.cpp").write_text(original)
        status, last = build_and_run(args.cxx, tmp, tmp / "baseline")
        print(f"baseline: {status} ({last})")
        if status != "passed":
            return 2
        for mid, rule, edits in MUTANTS:
            text = original
            for old, new in edits:
                if old not in text:
                    print(f"{mid}: STALE mutant (pattern not found) - {rule}")
                    return 2
                text = text.replace(old, new, 1)
            (tmp / "WorldPartyScaling.cpp").write_text(text)
            status, last = build_and_run(args.cxx, tmp, tmp / mid)
            ok = status == "failed"
            killed += ok
            print(f"{mid}: {'KILLED ' if ok else 'SURVIVED'} [{status}] {rule} :: {last}")
    print(f"\n{killed}/{len(MUTANTS)} mutants killed")
    return 0 if killed == len(MUTANTS) else 1


if __name__ == "__main__":
    sys.exit(main())
