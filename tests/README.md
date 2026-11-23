# World Party Scaling regression tests

These tests guard the owner-approved Naxxramas custom rules that are already merged on `main`.
They do not change gameplay.

| Rule | Source | What is locked in |
|---|---|---|
| R1 elite exclusion | Coordination #43, PR #1 | Open-world `CREATURE_ELITE_ELITE`, `RAREELITE` and `WORLDBOSS` are never scaled. Normal and rare creatures are. |
| R2 nearby counting | Coordination #45, PR #3 | The tier counts online group members whose **current** position is within `Group.XPDistance` of the creature. Alive or dead makes no difference: a nearby dead member counts, a released ghost far away does not. The tier is clamped to 1–6. |
| R3 frozen tier | Coordination #45, PR #3 | The tier and coefficients are fixed for the whole engagement. They are recalculated only on a new pull after a full combat exit, evade, death or removal. |
| Playerbots | Coordination #45 | Bots are ordinary `Player` objects. They count exactly like humans, and the module uses no Playerbots API. |

## What runs

| Script | What it proves | What it does not prove |
|---|---|---|
| `tests/run_tests.sh` | The **unchanged** `src/WorldPartyScaling.cpp`, linked with `tests/fakecore` (a mock core) and driven through its registered hooks in core call order. Builds with GCC and Clang, each plain and with ASan+UBSan. | Worldserver, database, client or real gameplay behaviour. |
| `tests/mutation_check.py` | Each of 18 mutants breaks one rule in a temporary copy of the source, and the suite must fail for every one. This shows the tests can catch regressions. | Same as above. |
| `tests/check_core_compile.sh <core>` | Both module translation units compile to objects against **real** AzerothCore headers (GCC/Clang, plain and with sanitizers). The API, enums and hook signatures match that core. | No link, no server start. |

```bash
tests/run_tests.sh                       # four configurations
python3 tests/mutation_check.py          # 18/18 mutants must be killed
tests/check_core_compile.sh ../azerothcore-wotlk /path/with/boost-and-mysql-headers
```

No GitHub Actions workflow is added, to save the owner's Actions quota. Run these locally.

## Fidelity notes for the mock

- `WorldObject::IsWithinDistInMap` copies core `Object.cpp`: same map, same phase, a strict `<` and object radii added.
- `Player::IsAtGroupRewardDistance` copies core `Player.cpp`, so it uses the corpse position for a dead player. One test shows WPS deliberately uses the live position instead.
- `Kill()` follows the `Unit::Kill` order: KillRewarder XP (`OnPlayerGiveXP`) first, then `setDeathState` → `CombatStop` → `OnUnitExitCombat`, then `OnUnitDeath`. The XP group rate copies `KillRewarder` and `Acore::XP::xp_in_group_rate`, for same-level, non-grey parties only.
- A hook is delivered only when the script enabled it in its constructor hook list, as in core.
- The creature's health is modelled as `(BASE_VALUE * basePct + TOTAL_VALUE) * totalPct`. At spawn, both the create health and the `BASE_VALUE` flat carry the native rank rate (`Creature::SelectLevel`).
