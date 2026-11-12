# CosmicCuddle — World Party Scaling Custom Modifications Register

Owner: CosmicCuddle  
Repository: `CosmicCuddle/mod-world-party-scaling`  
Current recorded main: `36ad573c0ee22e24abf3f48b6e40c4bc360ec8e8`

This register protects Naxxramas-specific World Party Scaling behavior from silent loss during upstream updates. A documented source merge, successful compile, gameplay test and live deployment are separate states and must not be conflated.

## WPS-NAXX-ELITE-EXCLUSION-20261112 — native difficulty for open-world elite ranks

- **Intended behavior:** World Party Scaling must not scale open-world creatures whose template rank is Elite, Rare Elite or World Boss. Those creatures retain their native server health/damage and kill-XP behavior. Normal creatures and normal Rare creatures remain eligible when all other WPS conditions pass.
- **Owner decision/source:** CosmicCuddle; merged PR #1, merge commit `016999b24c5698b83fefb7f1919061cfd156f9c4`, source head `410be49366bc199d72506bc5f26b556d6d897dd4`.
- **Owning component / affected runtime:** `src/WorldPartyScaling.cpp`, inside the existing eligibility path. The rank rejection occurs before active scaling state is created, so excluded creatures do not receive WPS health, melee/spell/periodic damage or kill-XP coefficients.
- **Config / SQL / DBC / client dependencies:** no new config key, SQL, DB schema, DBC or client patch. Existing WPS configuration still controls the module generally.
- **Compatibility:** WPS PR #2 CI-only coexistence gate used Core18 + PB15 + IP164 + Naxx292 + Services and public `azerothcore/mod-autobalance@1d707cb61a0962ab01e4123681e2d3b66cc558a2`; workflow run `43220368124` was SUCCESS. That is source/native coexistence evidence only, not proof of CosmicCuddle's installed AutoBalance revision.
- **Required tests:** solo and grouped normal creatures still scale; normal Rare creatures still scale; Elite, Rare Elite and World Boss creatures never gain WPS active state or WPS XP/damage/health changes; dungeons/raids remain outside WPS and continue using their intended instance/AutoBalance behavior; test humans and Playerbots.
- **Known risks:** a future upstream refactor could move scaling-state creation ahead of eligibility, add new rank semantics, or alter XP paths. Any upstream sync must re-run source guards and real Worldserver compilation, then gameplay-test representative ranks.
- **Status:** gameplay source merged to owner fork main. No claim here that it is deployed or gameplay-accepted on the owner server.
- **Rollback:** restore/revert the merged customization to the prior reviewed WPS source only after owner approval; no DB migration is involved. If deployed, preserve the prior module binary/config for rollback.

## WPS-NAXX-NEARBY-PARTY-TIER-20261112 — nearby members choose frozen engagement tier

- **Intended behavior:** when an eligible open-world creature first enters a valid WPS engagement, choose the tier from online group members whose **current Player position** is within Core `CONFIG_GROUP_XP_DISTANCE` of that creature. Alive/dead state does not affect the count. Offline/logged-out or distant members do not count. The result is clamped to tiers 1–6.
- **Frozen-fight rule:** capture the selected tier before active state insertion and retain it for the entire fight. A member dying, releasing, moving away, joining/leaving the group, or reinforcements arriving later must not alter an already active creature's WPS tier. A released ghost only affects a **new** engagement if its current position is nearby.
- **Owner decision/source:** CosmicCuddle; merged PR #3, merge commit `36ad573c0ee22e24abf3f48b6e40c4bc360ec8e8`, reviewed source head `c0d2cc043f0987eca376083a460319c18c804c26`.
- **Owning component / affected paths:** runtime `src/WorldPartyScaling.cpp`; documentation/config-description updates in `README.md`, `CHANGELOG.md`, `conf/mod_world_party_scaling.conf.dist`, `docs/COMPATIBILITY.md`, and `docs/DEPLOYMENT.md`.
- **Config / SQL / DBC / client dependencies:** no new runtime toggle. This is standard behavior of CosmicCuddle's fork. No SQL, DB schema, DBC or client patch.
- **Dependencies:** Core group membership/reference API, `World::getFloatConfig(CONFIG_GROUP_XP_DISTANCE)`, `Player::IsWithinDistInMap`, and the existing WPS first-engagement/frozen-state lifecycle. Six-player support may raise the runtime party cap, but WPS intentionally clamps at six.
- **Interaction with elite exclusion:** elite/rank eligibility is checked before tier selection/state creation. The nearby-tier feature must never re-enable WPS for Elite, Rare Elite or World Boss creatures. Normal Rare remains eligible.
- **AutoBalance interaction:** intended deployment model keeps WPS for eligible open-world creatures and AutoBalance for its configured instance scope. Services #69/#70 use the current public AutoBalance head `1d707cb61a0962ab01e4123681e2d3b66cc558a2` for combined-source coexistence. CosmicCuddle's exact installed AutoBalance source/commit remains unverified; public-green builds are not installed-stack proof.
- **Required tests:** ungrouped player = tier 1; nearby online members count; distant/same-group members do not; nearby dead member counts; after release a distant ghost does not count for a new engagement; cross-map members do not count; six nearby members = tier 6; more than six nearby still tier 6; movement/death/release/group changes after pull do not alter active tier; next fresh engagement recalculates.
- **Known risks:** distance/config semantics can change upstream; large groups create a small membership walk at engagement; first valid engager/owner resolution must remain correct; future WPS changes must not recalculate tier mid-fight. Verify kill-XP still uses the stored engagement tier.
- **Status:** source and documentation merged to owner fork main. Broader current native coexistence gates are Services #69 (Core21/IP168/WPS/public AutoBalance) and #70 (Core23/IP168/WPS/public AutoBalance); their terminal outcomes must be checked separately. No gameplay/deployment claim.
- **Rollback:** revert/restore the nearby-tier merge while preserving the separate elite-exclusion decision, or restore the last approved WPS binary/config. No data migration is required.

## Upstream-update preservation rule

Before updating this WPS fork:

1. identify the exact upstream base and current owner `main`;
2. compare upstream changes to `src/WorldPartyScaling.cpp` and all config/docs above;
3. explicitly preserve both custom behaviors unless CosmicCuddle decides otherwise;
4. run source contracts and a real combined Worldserver compile with the current Core/IP/PB/Naxx/Services and the actual installed AutoBalance source when known;
5. run controlled gameplay checks for eligibility, nearby counts, frozen tier, health/damage/XP restoration and instance coexistence;
6. prepare module/config/binary rollback before production.

Never resolve an upstream conflict by accepting upstream `WorldPartyScaling.cpp` wholesale if doing so removes either owner customization.
