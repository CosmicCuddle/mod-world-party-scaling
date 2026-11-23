# Owner-controlled gameplay acceptance matrix

Run this only on an owner-approved **disposable** test realm, never on the live server. Before testing, set `WorldPartyScaling.EnableDebug = 1` and `Logger.module = 5`. Each pull then logs `scaling entry … for nearby group size N (tier N)` and `restored entry …`, so the selected tier can be read directly. Restore both settings afterwards.

Default tiers are health 1.00/1.70/2.50/3.30/4.20/5.20 and damage 1.00/1.35/1.50/1.65/1.80/1.95. `Group.XPDistance` defaults to 74 yd.

| # | Setup | Action | Expected |
|---|---|---|---|
| 1 | Solo human, not grouped | Pull a normal open-world mob | tier 1. Its HP is the reference HP, not the server rate HP. |
| 2 | Human + 1 nearby human | Pull a normal mob | tier 2 |
| 3 | Human + 5 nearby existing altbots | Pull a normal mob | tier 6 |
| 4 | Party of 4, one member 100+ yd away | Pull | tier 3 |
| 5 | Party of 3, one dead **unreleased** member at the pull | Pull | tier 3 (dead nearby counts) |
| 6 | As 5, but the dead member has released to a distant graveyard | Pull a **new** mob | tier 2 (distant ghost does not count) |
| 7 | Party of 3 in combat | Move a member out of range, let one die and release, invite a new member | The log shows no new `scaling entry` line for that mob, and its max HP is unchanged until combat ends. |
| 8 | Solo pull, then 3 party members arrive mid-fight | Keep fighting | stays tier 1 |
| 9 | After 7 or 8 | Let the mob evade or leave combat, then pull again | A `restored entry` line appears, then a new tier from the current positions. |
| 10 | Party of 6 | Pull an open-world **elite**, a **rare elite** and a **world boss** | No `scaling entry` line. Native HP, damage and XP. |
| 11 | Party of 6 | Pull a normal **rare** (silver, non-elite) | Scaled (tier 6) |
| 12 | Party of 5 in a dungeon or raid | Pull | No WPS log lines. AutoBalance, if installed, behaves as before. |
| 13 | Ungrouped random bots near the mob | A human pulls solo | tier 1 (strangers do not count) |
| 14 | Distant or logged-out altbot in the group | Pull | Not counted |
| 15 | Solo kill of a normal mob versus a tier-3 kill | Compare XP | Tier 3 XP is the core group XP × 1.20 / 1.166, so the tier coefficient replaces the core party bonus. Elite kills keep core XP. |

Report the realm, build SHA and the module, Core, Playerbots, Individual Progression and AutoBalance SHAs with the results. The owner's installed AutoBalance SHA is still unrecorded (Coordination #43).
