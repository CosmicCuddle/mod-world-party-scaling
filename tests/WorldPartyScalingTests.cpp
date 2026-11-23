/*
 * Regression tests for mod-world-party-scaling.
 *
 * The UNCHANGED module source (src/WorldPartyScaling.cpp) is linked into this binary and driven only
 * through the script hooks it registers, in the order core calls them. Core is replaced by the
 * test-only stand-ins in tests/fakecore (a mock, not a Worldserver).
 *
 * Owner-approved custom rules under test (Coordination #43, #45):
 *   R1  open-world CREATURE_ELITE_ELITE / RAREELITE / WORLDBOSS are never scaled; normal and rare are.
 *   R2  the tier counts online group members whose CURRENT position is within Group.XPDistance of the
 *       creature, regardless of alive/dead state (nearby dead counts; a released ghost far away does not).
 *   R3  the tier and coefficients are frozen for the whole engagement and recalculated only on a new
 *       engagement after a full combat exit, evade, death or removal.
 *   Playerbots are ordinary Player objects: counted exactly like humans, with no Playerbots API.
 */

#include "TestHarness.h"

#include "ScriptMgr.h"
#include "WorldPartyScaling.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <thread>

using fakecore::Hooks;

namespace
{
// Server rates from the owner's conf/dist/env.ac as recorded in docs/COMPATIBILITY.md.
constexpr float NormalHpRate = 1.20f;
constexpr float EliteHpRate = 1.50f;
constexpr float NormalMeleeRate = 1.35f;
constexpr float EliteMeleeRate = 1.40f;
constexpr float SpellRate = 1.30f;

constexpr std::array<float, 6> HealthTier = { 1.00f, 1.70f, 2.50f, 3.30f, 4.20f, 5.20f };
constexpr std::array<float, 6> DamageTier = { 1.00f, 1.35f, 1.50f, 1.65f, 1.80f, 1.95f };
constexpr std::array<float, 6> XpTier = { 1.00f, 1.10f, 1.20f, 1.30f, 1.40f, 1.50f };

constexpr uint32 RawHealth = 1000;
constexpr float XpDistance = 74.0f;

void ApplyServerRates()
{
    auto& w = fakecore::TheWorld();
    w.Reset();
    w.SetRate(RATE_CREATURE_NORMAL_HP, NormalHpRate);
    w.SetRate(RATE_CREATURE_ELITE_ELITE_HP, EliteHpRate);
    w.SetRate(RATE_CREATURE_ELITE_RAREELITE_HP, EliteHpRate);
    w.SetRate(RATE_CREATURE_ELITE_WORLDBOSS_HP, EliteHpRate);
    w.SetRate(RATE_CREATURE_ELITE_RARE_HP, EliteHpRate);
    w.SetRate(RATE_CREATURE_NORMAL_DAMAGE, NormalMeleeRate);
    w.SetRate(RATE_CREATURE_ELITE_ELITE_DAMAGE, EliteMeleeRate);
    w.SetRate(RATE_CREATURE_ELITE_RAREELITE_DAMAGE, EliteMeleeRate);
    w.SetRate(RATE_CREATURE_ELITE_WORLDBOSS_DAMAGE, EliteMeleeRate);
    w.SetRate(RATE_CREATURE_ELITE_RARE_DAMAGE, EliteMeleeRate);
    for (Rates r : { RATE_CREATURE_NORMAL_SPELLDAMAGE, RATE_CREATURE_ELITE_ELITE_SPELLDAMAGE,
             RATE_CREATURE_ELITE_RAREELITE_SPELLDAMAGE, RATE_CREATURE_ELITE_WORLDBOSS_SPELLDAMAGE,
             RATE_CREATURE_ELITE_RARE_SPELLDAMAGE })
        w.SetRate(r, SpellRate);
    w.SetFloat(CONFIG_GROUP_XP_DISTANCE, XpDistance);
}

float HpRateFor(uint32 rank)
{
    return rank == CREATURE_ELITE_NORMAL ? NormalHpRate : EliteHpRate;
}

float MeleeRateFor(uint32 rank)
{
    return rank == CREATURE_ELITE_NORMAL ? NormalMeleeRate : EliteMeleeRate;
}

void ReloadConfig(bool reload)
{
    Hooks::OnBeforeConfigLoad(reload);
    Hooks::OnAfterConfigLoad(reload);
}

// Emulates a clean worldserver start with the given extra config.
void StartServer(std::map<std::string, std::string> const& options = {})
{
    // Drop any state left by a previous test (disable-reload restores and clears active creatures).
    fakecore::Config().Clear();
    fakecore::Config().Set("WorldPartyScaling.Enable", "0");
    ReloadConfig(true);

    fakecore::Config().Clear();
    for (auto const& [k, v] : options)
        fakecore::Config().Set(k, v);

    ApplyServerRates();
    g_WorldPartyScalingConfig.ResetForTest();
    ReloadConfig(false);
}

uint32 ExpectedScaledHealth(uint32 rank, uint32 tier)
{
    uint32 const create = static_cast<uint32>(double(RawHealth) * HpRateFor(rank));
    double const reference = std::llround(double(create) / HpRateFor(rank));
    return static_cast<uint32>(std::llround(reference * HealthTier[tier - 1]));
}

uint32 ExpectedMelee(uint32 damage, uint32 rank, uint32 tier)
{
    return static_cast<uint32>(std::llround(double(damage) * (double(DamageTier[tier - 1]) / double(MeleeRateFor(rank)))));
}

// A small world: the open-world map, a second continent, and instance-style maps.
struct Scenario
{
    Map eastern{ 0, 0, Map::Kind::World };
    Map kalimdor{ 1, 0, Map::Kind::World };
    Map dungeon{ 36, 7, Map::Kind::Dungeon };
    Map raid{ 533, 9, Map::Kind::Raid };
    Map battleground{ 489, 11, Map::Kind::Battleground };
    Map arena{ 559, 12, Map::Kind::Arena };

    std::array<CreatureTemplate, 6> templates{ {
        { 100, CREATURE_ELITE_NORMAL },
        { 101, CREATURE_ELITE_ELITE },
        { 102, CREATURE_ELITE_RAREELITE },
        { 103, CREATURE_ELITE_WORLDBOSS },
        { 104, CREATURE_ELITE_RARE },
        { 105, CREATURE_UNKNOWN },
    } };

    std::vector<std::unique_ptr<Creature>> creatures;
    std::vector<std::unique_ptr<Player>> players;
    std::vector<std::unique_ptr<Group>> groups;

    explicit Scenario(std::map<std::string, std::string> const& options = {})
    {
        fakecore::TheMapMgr().maps = { &eastern, &kalimdor, &dungeon, &raid, &battleground, &arena };
        StartServer(options);
    }

    ~Scenario()
    {
        // Leave no active module state behind for the next test.
        fakecore::Config().Set("WorldPartyScaling.Enable", "0");
        ReloadConfig(true);
        fakecore::TheMapMgr().maps.clear();
    }

    CreatureTemplate const* Template(uint32 rank)
    {
        for (auto const& t : templates)
            if (t.rank == rank)
                return &t;
        return &templates[0];
    }

    Creature* SpawnCreature(uint32 rank = CREATURE_ELITE_NORMAL, Map* map = nullptr, Position p = { 0, 0, 0 })
    {
        creatures.push_back(std::make_unique<Creature>(Template(rank)));
        Creature* c = creatures.back().get();
        c->SpawnWithHealth(map ? map : &eastern, p, RawHealth, rank == CREATURE_UNKNOWN ? EliteHpRate : HpRateFor(rank));
        c->zoneId = 12;
        return c;
    }

    Player* SpawnPlayer(Position p = { 0, 0, 0 }, Map* map = nullptr, bool bot = false)
    {
        players.push_back(std::make_unique<Player>());
        Player* pl = players.back().get();
        pl->PlaceInMap(map ? map : &eastern, p);
        pl->isPlayerbot = bot;
        return pl;
    }

    Group* MakeGroup(std::vector<Player*> const& members)
    {
        groups.push_back(std::make_unique<Group>());
        Group* g = groups.back().get();
        for (Player* m : members)
            g->AddMember(m);
        return g;
    }

    // n players (including the returned leader) at the creature, grouped.
    std::vector<Player*> NearbyParty(uint32 n, bool bots = false)
    {
        std::vector<Player*> members;
        for (uint32 i = 0; i < n; ++i)
            members.push_back(SpawnPlayer({ float(i), 0, 0 }, nullptr, bots && i > 0));
        if (n > 1)
            MakeGroup(members);
        return members;
    }
};

// Core call-order helpers -----------------------------------------------------------------------

void Engage(Creature* c, Unit* victim)
{
    c->inCombat = true;
    Hooks::OnUnitEnterCombat(c, victim); // Creature::AtEngage
}

uint32 Melee(Creature* c, Unit* target, uint32 damage)
{
    Hooks::ModifyMeleeDamage(target, c, damage); // Unit::CalculateMeleeDamage
    return damage;
}

int32 SpellHit(Creature* c, Unit* target, int32 damage, SpellInfo const* spell)
{
    Hooks::ModifySpellDamageTaken(target, c, damage, spell); // Unit::CalculateSpellDamageTaken
    return damage;
}

uint32 Tick(Creature* c, Unit* target, uint32 damage, SpellInfo const* spell)
{
    Hooks::ModifyPeriodicDamageAurasTick(target, c, damage, spell); // AuraEffect::HandlePeriodic*
    return damage;
}

void ExitCombat(Creature* c)
{
    c->inCombat = false;
    Hooks::OnUnitExitCombat(c); // Unit::AtExitCombat
}

void EvadeAndReset(Creature* c)
{
    c->inEvade = true;
    ExitCombat(c);
    c->SetHealth(c->GetMaxHealth()); // evade home regenerates
    c->inEvade = false;
}

// Mirrors Unit::Kill + KillRewarder (same-level party, all members not gray): rewards first,
// then setDeathState(JustDied) -> CombatStop -> AtExitCombat, then OnUnitDeath.
std::map<Player*, uint32> Kill(Player* killer, Creature* victim, uint32 baseXp)
{
    std::map<Player*, uint32> rewarded;
    Group* group = killer->GetGroup();

    float groupRate = 1.0f;
    uint32 aliveSumLevel = killer->GetLevel();
    std::vector<Player*> recipients{ killer };
    if (group)
    {
        recipients.clear();
        uint32 count = 0;
        aliveSumLevel = 0;
        for (GroupReference* r = group->GetFirstMember(); r; r = r->next())
            if (Player* m = r->GetSource())
                if (m == killer || m->IsAtGroupRewardDistance(victim))
                {
                    recipients.push_back(m);
                    if (m->IsAlive())
                    {
                        ++count;
                        aliveSumLevel += m->GetLevel();
                    }
                }
        bool const isRaid = victim->GetMap()->IsRaid() && group->isRaidGroup();
        groupRate = Acore::XP::xp_in_group_rate(count, isRaid);
    }

    for (Player* p : recipients)
    {
        if (group && !p->IsAlive())
            continue; // KillRewarder::_RewardXP gives dead group members 0 and skips the hook
        float const rate = group ? groupRate * float(p->GetLevel()) / float(aliveSumLevel) : 1.0f;
        uint32 xp = group ? uint32(baseXp * rate) : baseXp;
        Hooks::OnPlayerGiveXP(p, xp, victim, XPSOURCE_KILL);
        rewarded[p] = xp;
    }

    victim->SetHealth(0);
    victim->alive = false;
    ExitCombat(victim);
    Hooks::OnUnitDeath(victim, killer);
    return rewarded;
}

uint32 CoreGroupXp(uint32 baseXp, uint32 aliveCount, uint32 levelShareCount)
{
    float const rate = Acore::XP::xp_in_group_rate(aliveCount, false) / float(levelShareCount);
    return uint32(baseXp * rate);
}

SpellInfo MakeSpell(SpellEffects effect, AuraType aura = SPELL_AURA_NONE, uint32 attrEx3 = 0)
{
    SpellInfo s;
    s.Id = 1;
    s.AttributesEx3 = attrEx3;
    s.Effects[0].Effect = effect;
    s.Effects[0].ApplyAuraName = aura;
    return s;
}

SpellInfo MakeDot(AuraType second = SPELL_AURA_NONE)
{
    SpellInfo s = MakeSpell(SPELL_EFFECT_APPLY_AURA, SPELL_AURA_PERIODIC_DAMAGE);
    if (second != SPELL_AURA_NONE)
    {
        s.Effects[1].Effect = SPELL_EFFECT_APPLY_AURA;
        s.Effects[1].ApplyAuraName = second;
    }
    return s;
}

uint32 NativeHealth(uint32 rank)
{
    return static_cast<uint32>(double(RawHealth) * HpRateFor(rank));
}

std::string ReadSource(char const* relative)
{
    std::ifstream in(std::string(WPS_SOURCE_DIR) + "/" + relative);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
} // namespace

// ================================================================================================
// R1 — elite exclusion
// ================================================================================================

TEST("elite-exclusion", NormalAndRareScaleAtEveryTier)
{
    for (uint32 rank : { uint32(CREATURE_ELITE_NORMAL), uint32(CREATURE_ELITE_RARE) })
        for (uint32 tier = 1; tier <= 6; ++tier)
        {
            Scenario s;
            Creature* c = s.SpawnCreature(rank);
            auto party = s.NearbyParty(tier);
            Engage(c, party[0]);
            CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(rank, tier));
            CHECK_EQ(c->GetHealth(), c->GetMaxHealth());
            CHECK_EQ(Melee(c, party[0], 140), ExpectedMelee(140, rank, tier));
        }
}

TEST("elite-exclusion", EliteRareEliteWorldBossNeverScale)
{
    for (uint32 rank : { uint32(CREATURE_ELITE_ELITE), uint32(CREATURE_ELITE_RAREELITE), uint32(CREATURE_ELITE_WORLDBOSS) })
        for (uint32 tier : { 1u, 3u, 6u })
        {
            Scenario s;
            Creature* c = s.SpawnCreature(rank);
            auto party = s.NearbyParty(tier, /*bots*/ tier == 6);
            Engage(c, party[0]);
            CHECK_EQ(c->GetMaxHealth(), NativeHealth(rank));
            CHECK_EQ(Melee(c, party[0], 140), 140u);
            SpellInfo bolt = MakeSpell(SPELL_EFFECT_SCHOOL_DAMAGE);
            CHECK_EQ(SpellHit(c, party[0], 130, &bolt), 130);
            SpellInfo dot = MakeDot();
            CHECK_EQ(Tick(c, party[0], 50, &dot), 50u);
            // Group churn mid-fight cannot create state for an excluded elite either.
            s.SpawnPlayer();
            CHECK_EQ(Melee(c, party[0], 140), 140u);
            auto xp = Kill(party[0], c, 300);
            uint32 const expected = tier == 1 ? 300u : CoreGroupXp(300, tier, tier);
            CHECK_EQ(xp[party[0]], expected);
            CHECK_EQ(c->GetHealth(), 0u);
        }
}

TEST("elite-exclusion", ExcludedElitePulledByPetStaysNative)
{
    Scenario s;
    Creature* c = s.SpawnCreature(CREATURE_ELITE_ELITE);
    auto party = s.NearbyParty(4);
    s.creatures.push_back(std::make_unique<Creature>(s.Template(CREATURE_ELITE_NORMAL)));
    Creature* pet = s.creatures.back().get();
    pet->SpawnWithHealth(&s.eastern, { 1, 1, 0 }, 100, 1.0f);
    pet->ownerPlayer = party[0];
    pet->isPet = true;
    Engage(c, pet);
    CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_ELITE));
    CHECK_EQ(Melee(c, pet, 99), 99u);
}

TEST("elite-exclusion", InstanceAndPvpMapsUntouchedForEveryRank)
{
    for (uint32 rank : { uint32(CREATURE_ELITE_NORMAL), uint32(CREATURE_ELITE_ELITE), uint32(CREATURE_ELITE_RARE),
             uint32(CREATURE_ELITE_WORLDBOSS) })
    {
        Scenario s;
        for (Map* map : { &s.dungeon, &s.raid, &s.battleground, &s.arena })
        {
            Creature* c = s.SpawnCreature(rank, map);
            std::vector<Player*> members;
            for (int i = 0; i < 5; ++i)
                members.push_back(s.SpawnPlayer({ float(i), 0, 0 }, map));
            s.MakeGroup(members);
            Engage(c, members[0]);
            CHECK_EQ(c->GetMaxHealth(), NativeHealth(rank));
            CHECK_EQ(Melee(c, members[0], 200), 200u);
            auto xp = Kill(members[0], c, 300);
            CHECK_EQ(xp[members[0]], CoreGroupXp(300, 5, 5));
        }
    }
}

TEST("elite-exclusion", UnknownRankIsNotExcluded)
{
    // Ranks outside the excluded three are not excluded; the rate switch default treats them as elite rates.
    Scenario s;
    Creature* c = s.SpawnCreature(CREATURE_UNKNOWN);
    auto party = s.NearbyParty(2);
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), uint32(std::llround(double(RawHealth) * HealthTier[1])));
}

// ================================================================================================
// R2 — nearby party-member counting
// ================================================================================================

TEST("nearby-count", SoloUngroupedIsTierOne)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    Player* p = s.SpawnPlayer({ 5, 0, 0 });
    s.SpawnPlayer({ 1, 0, 0 }); // nearby stranger, not grouped
    Engage(c, p);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 1));
}

TEST("nearby-count", RaidOfEightNearbyClampsToSix)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto members = s.NearbyParty(8);
    s.groups.back()->raid = true;
    Engage(c, members[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 6));
}

TEST("nearby-count", DistantOtherMapOtherPhaseAndOfflineMembersDoNotCount)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    Player* leader = s.SpawnPlayer({ 2, 0, 0 });
    Player* near = s.SpawnPlayer({ 0, 10, 0 });
    Player* far = s.SpawnPlayer({ 200, 0, 0 });
    Player* otherMap = s.SpawnPlayer({ 0, 0, 0 }, &s.kalimdor);
    Player* otherPhase = s.SpawnPlayer({ 1, 1, 0 });
    otherPhase->phaseMask = 2;
    Player* offline = s.SpawnPlayer({ 1, 0, 0 });
    Group* g = s.MakeGroup({ leader, near, far, otherMap, otherPhase, offline });
    g->SetOffline(offline);
    Engage(c, leader);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));
}

TEST("nearby-count", DistanceBoundaryFollowsCoreStrictLessThan)
{
    // Core _IsWithinDist uses distsq < maxdist^2 (radii added). Object sizes are zero here.
    for (auto const& [x, expectedTier] : std::vector<std::pair<float, uint32>>{ { 73.9f, 2 }, { 74.0f, 1 }, { 74.5f, 1 } })
    {
        Scenario s;
        Creature* c = s.SpawnCreature();
        Player* leader = s.SpawnPlayer({ 0, 0, 0 });
        Player* edge = s.SpawnPlayer({ x, 0, 0 });
        s.MakeGroup({ leader, edge });
        Engage(c, leader);
        CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, expectedTier));
    }
}

TEST("nearby-count", UsesLiveGroupXpDistanceConfig)
{
    Scenario s;
    fakecore::TheWorld().SetFloat(CONFIG_GROUP_XP_DISTANCE, 150.0f);
    Creature* c = s.SpawnCreature();
    Player* leader = s.SpawnPlayer({ 0, 0, 0 });
    Player* mid = s.SpawnPlayer({ 120, 0, 0 });
    s.MakeGroup({ leader, mid });
    Engage(c, leader);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));
}

TEST("nearby-count", PetEngagementResolvesToOwnersGroup)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(3);
    s.creatures.push_back(std::make_unique<Creature>(s.Template(CREATURE_ELITE_NORMAL)));
    Creature* pet = s.creatures.back().get();
    pet->SpawnWithHealth(&s.eastern, { 3, 3, 0 }, 100, 1.0f);
    pet->ownerPlayer = party[1];
    pet->isPet = true;
    pet->faction = party[1]->faction;
    Engage(c, pet);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 3));
}

TEST("nearby-count", DistantEngagingPlayerIsNotForceCounted)
{
    // Maintenance #45 nuance: only current positions count; the engaging player is not added if far away.
    Scenario s;
    Creature* c = s.SpawnCreature();
    Player* hunter = s.SpawnPlayer({ 90, 0, 0 });
    Player* a = s.SpawnPlayer({ 1, 0, 0 });
    Player* b = s.SpawnPlayer({ 2, 0, 0 });
    s.MakeGroup({ hunter, a, b });
    Engage(c, hunter);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));

    Scenario s2;
    Creature* c2 = s2.SpawnCreature();
    Player* lone = s2.SpawnPlayer({ 90, 0, 0 });
    Player* farMate = s2.SpawnPlayer({ 300, 0, 0 });
    s2.MakeGroup({ lone, farMate });
    Engage(c2, lone);
    CHECK_EQ(c2->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 1)); // minimum tier 1
}

TEST("nearby-count", OnlyTheEngagingPlayersGroupCounts)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto mine = s.NearbyParty(2);
    auto theirs = s.NearbyParty(5);
    Engage(c, mine[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));
    (void)theirs;
}

TEST("nearby-count", NpcVictimDoesNotStartThenFirstPlayerHitDoes)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    s.creatures.push_back(std::make_unique<Creature>(s.Template(CREATURE_ELITE_NORMAL)));
    Creature* escort = s.creatures.back().get();
    escort->SpawnWithHealth(&s.eastern, { 1, 0, 0 }, 500, 1.0f);
    escort->faction = 35;
    Engage(c, escort);
    CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));
    CHECK_EQ(Melee(c, escort, 135), 135u);

    auto party = s.NearbyParty(3);
    c->SetHealth(c->GetMaxHealth() / 2);
    CHECK_EQ(Melee(c, party[0], 135), ExpectedMelee(135, CREATURE_ELITE_NORMAL, 3));
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 3));
    CHECK_EQ(c->GetHealth(), c->GetMaxHealth() / 2); // health percentage preserved on mid-fight start
}

// ================================================================================================
// R2 — dead players and distant ghosts
// ================================================================================================

TEST("dead-and-ghosts", NearbyDeadUnreleasedMembersCount)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(4);
    party[1]->alive = false;
    party[2]->alive = false;
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 4));
}

TEST("dead-and-ghosts", ReleasedGhostAtDistantGraveyardDoesNotCountOnNewPull)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(3);
    Player* ghost = party[2];
    ghost->alive = false;
    ghost->corpse.PlaceInMap(&s.eastern, ghost->pos); // corpse stays at the fight
    ghost->PlaceInMap(&s.eastern, { 900, 900, 0 });   // spirit at the graveyard

    // Core reward distance would still use the corpse position for this ghost; WPS uses the live position.
    CHECK(ghost->IsAtGroupRewardDistance(c));
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));
}

TEST("dead-and-ghosts", GhostOnAnotherContinentDoesNotCount)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(2);
    party[1]->alive = false;
    party[1]->PlaceInMap(&s.kalimdor, { 0, 0, 0 });
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 1));
}

TEST("dead-and-ghosts", GhostThatRunsBackNearbyCountsAgain)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(2);
    party[1]->alive = false;
    party[1]->PlaceInMap(&s.eastern, { 10, 0, 0 }); // ghost walked back, not yet resurrected
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));
}

TEST("dead-and-ghosts", DeadMembersReceiveNoKillXpAndXpUsesFrozenTier)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(3);
    party[2]->alive = false; // dead nearby: counts for the tier (3)
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 3));
    auto xp = Kill(party[0], c, 600);
    CHECK(xp.find(party[2]) == xp.end());
    // Core: two alive at reward distance -> rate 1.0 split by level share; WPS replaces 1.0 with tier-3 XP 1.2.
    uint32 const core = CoreGroupXp(600, 2, 2);
    CHECK_NEAR(double(xp[party[0]]), double(core) * XpTier[2] / 1.0, 1.0);
}

// ================================================================================================
// Playerbots — ordinary Player objects, no special handling
// ================================================================================================

TEST("playerbots", HumanPlusFiveNearbyBotsIsTierSix)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(6, /*bots*/ true);
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 6));
}

TEST("playerbots", DistantAndOfflineBotsDoNotCount)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    Player* master = s.SpawnPlayer({ 0, 0, 0 });
    Player* nearBot = s.SpawnPlayer({ 3, 0, 0 }, nullptr, true);
    Player* farBot = s.SpawnPlayer({ 400, 0, 0 }, nullptr, true);
    Player* offlineAltbot = s.SpawnPlayer({ 1, 0, 0 }, nullptr, true);
    Group* g = s.MakeGroup({ master, nearBot, farBot, offlineAltbot });
    g->SetOffline(offlineAltbot);
    Engage(c, master);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));
}

TEST("playerbots", BotPullUsesBotsGroupIncludingMaster)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(4, true);
    Engage(c, party[3]); // a bot pulls
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 4));
}

TEST("playerbots", UngroupedRandomBotsNearbyDoNotCount)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(2);
    for (int i = 0; i < 6; ++i)
        s.SpawnPlayer({ float(i), 2, 0 }, nullptr, true);
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));
}

TEST("playerbots", RandomBotSoloPullIsTierOne)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    Player* rnd = s.SpawnPlayer({ 0, 0, 0 }, nullptr, true);
    Engage(c, rnd);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 1));
}

TEST("playerbots", BotsAndElitesStillExcluded)
{
    Scenario s;
    Creature* c = s.SpawnCreature(CREATURE_ELITE_RAREELITE);
    auto party = s.NearbyParty(5, true);
    Engage(c, party[2]);
    CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_RAREELITE));
}

TEST("playerbots", ModuleSourceHasNoPlayerbotsDependency)
{
    for (char const* file : { "src/WorldPartyScaling.cpp", "src/WorldPartyScaling.h" })
    {
        std::string const src = ReadSource(file);
        CHECK(!src.empty());
        CHECK(src.find("Playerbot") == std::string::npos);
        CHECK(src.find("PlayerbotAI") == std::string::npos);
        CHECK(src.find("RandomPlayerbotMgr") == std::string::npos);
    }
}

// ================================================================================================
// R3 — frozen for the whole encounter
// ================================================================================================

TEST("frozen-combat", TierFrozenThroughMovementDeathReleaseAndGroupChurn)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(3);
    Engage(c, party[0]);
    uint32 const frozenMax = ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 3);
    uint32 const frozenMelee = ExpectedMelee(200, CREATURE_ELITE_NORMAL, 3);
    SpellInfo bolt = MakeSpell(SPELL_EFFECT_SCHOOL_DAMAGE);
    SpellInfo dot = MakeDot();

    auto expectFrozen = [&](char const*) {
        CHECK_EQ(c->GetMaxHealth(), frozenMax);
        CHECK_EQ(Melee(c, party[0], 200), frozenMelee);
        CHECK_EQ(SpellHit(c, party[0], 130, &bolt), int32(std::llround(130.0 * DamageTier[2] / SpellRate)));
        CHECK_EQ(Tick(c, party[0], 130, &dot), uint32(std::llround(130.0 * DamageTier[2] / SpellRate)));
    };

    expectFrozen("start");
    party[1]->PlaceInMap(&s.eastern, { 500, 0, 0 });
    expectFrozen("member ran away");
    party[2]->alive = false;
    expectFrozen("member died");
    party[2]->PlaceInMap(&s.eastern, { 900, 900, 0 });
    expectFrozen("member released");
    s.groups.back()->RemoveMember(party[1]);
    expectFrozen("member left group");
    for (int i = 0; i < 3; ++i)
        s.groups.back()->AddMember(s.SpawnPlayer({ float(i), 1, 0 }, nullptr, true));
    expectFrozen("reinforcements joined nearby");
    Engage(c, party[0]); // core may re-run engage/threat on a new victim
    expectFrozen("re-engage event during the fight");
    s.groups.back()->SetOffline(party[0]);
    expectFrozen("leader logged out");
}

TEST("frozen-combat", TierFrozenUpwardAndDownward)
{
    // Solo pull stays tier 1 when five party members arrive mid-fight.
    Scenario s;
    Creature* c = s.SpawnCreature();
    Player* leader = s.SpawnPlayer({ 0, 0, 0 });
    std::vector<Player*> late;
    for (int i = 0; i < 5; ++i)
        late.push_back(s.SpawnPlayer({ 500.0f + i, 0, 0 }));
    std::vector<Player*> all{ leader };
    all.insert(all.end(), late.begin(), late.end());
    s.MakeGroup(all);
    Engage(c, leader);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 1));
    for (Player* p : late)
        p->PlaceInMap(&s.eastern, { 1, 1, 0 });
    CHECK_EQ(Melee(c, leader, 135), ExpectedMelee(135, CREATURE_ELITE_NORMAL, 1));
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 1));
}

TEST("frozen-combat", CombatExitRestoresAndNewPullRecalculates)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(5);
    Engage(c, party[0]);
    c->SetHealth(c->GetMaxHealth() * 3 / 5); // 60%
    ExitCombat(c);
    CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));
    CHECK_EQ(c->GetHealth(), NativeHealth(CREATURE_ELITE_NORMAL) * 3 / 5);
    CHECK_EQ(Melee(c, party[0], 135), 135u); // out of combat: no state, nothing starts

    for (int i = 2; i < 5; ++i)
        party[i]->PlaceInMap(&s.eastern, { 400, 0, 0 });
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));
    CHECK_EQ(c->GetHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2) * 3 / 5);
}

TEST("frozen-combat", EvadeRestoresAndRePullRecalculates)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(4);
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 4));
    EvadeAndReset(c);
    CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));
    CHECK_EQ(c->GetHealth(), NativeHealth(CREATURE_ELITE_NORMAL));
    party[3]->PlaceInMap(&s.eastern, { 999, 0, 0 });
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 3));
}

TEST("frozen-combat", EvadingCreatureCannotStartScaling)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(3);
    c->inEvade = true;
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));
    CHECK_EQ(Melee(c, party[0], 135), 135u);
}

TEST("frozen-combat", DeathRestoresAtZeroAndKillXpUsesFrozenTier)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(4);
    Engage(c, party[0]);
    // Two more members arrive and join before the kill: the XP tier remains the frozen tier 4.
    for (int i = 0; i < 2; ++i)
        s.groups.back()->AddMember(s.SpawnPlayer({ 2, float(i), 0 }));
    auto xp = Kill(party[0], c, 1000);
    uint32 const core = CoreGroupXp(1000, 6, 6);
    double const coreRate = Acore::XP::xp_in_group_rate(6, false);
    CHECK_NEAR(double(xp[party[0]]), double(core) * XpTier[3] / coreRate, 1.0);
    CHECK_EQ(c->GetHealth(), 0u);
    CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));

    // Respawn and pull again with the six now present: new engagement recalculates to tier 6.
    c->alive = true;
    c->SetHealth(c->GetMaxHealth());
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 6));
}

TEST("frozen-combat", RemovalFromWorldDropsStateWithoutRestart)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(3);
    Engage(c, party[0]);
    c->RemoveFromMap();
    Hooks::OnCreatureRemoveWorld(c);
    CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));
    CHECK_EQ(Melee(c, party[0], 135), 135u); // not in world: cannot start again
}

TEST("frozen-combat", ConfigReloadKeepsActiveFightFrozenNextPullUsesNewValues)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(2);
    Engage(c, party[0]);
    fakecore::Config().Set("WorldPartyScaling.Group2.Health", "2.00");
    fakecore::Config().Set("WorldPartyScaling.Group2.Damage", "2.70");
    ReloadConfig(true);
    CHECK_EQ(c->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));
    CHECK_EQ(Melee(c, party[0], 135), ExpectedMelee(135, CREATURE_ELITE_NORMAL, 2));
    ExitCombat(c);
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), 2000u);
    CHECK_EQ(Melee(c, party[0], 135), 270u);
}

TEST("frozen-combat", DisableReloadRestoresActiveCreaturesImmediately)
{
    Scenario s;
    Creature* a = s.SpawnCreature();
    Creature* b = s.SpawnCreature(CREATURE_ELITE_RARE, nullptr, { 5, 5, 0 });
    auto party = s.NearbyParty(3);
    Engage(a, party[0]);
    Engage(b, party[1]);
    a->SetHealth(a->GetMaxHealth() / 4);
    fakecore::Config().Set("WorldPartyScaling.Enable", "0");
    ReloadConfig(true);
    CHECK_EQ(a->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));
    CHECK_EQ(a->GetHealth(), NativeHealth(CREATURE_ELITE_NORMAL) / 4);
    CHECK_EQ(b->GetMaxHealth(), NativeHealth(CREATURE_ELITE_RARE));
    CHECK_EQ(Melee(a, party[0], 135), 135u);
    auto xp = Kill(party[0], a, 500);
    CHECK_EQ(xp[party[0]], CoreGroupXp(500, 3, 3));
}

TEST("frozen-combat", IndependentCreaturesKeepIndependentTiers)
{
    Scenario s;
    Creature* a = s.SpawnCreature();
    Creature* b = s.SpawnCreature(CREATURE_ELITE_NORMAL, nullptr, { 0, 0, 0 });
    auto party = s.NearbyParty(2);
    Engage(a, party[0]);
    s.groups.back()->AddMember(s.SpawnPlayer({ 1, 1, 0 }));
    s.groups.back()->AddMember(s.SpawnPlayer({ 1, 2, 0 }));
    Engage(b, party[0]);
    CHECK_EQ(a->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));
    CHECK_EQ(b->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 4));
}

TEST("frozen-combat", CreaturesOnDifferentMapsAreIndependent)
{
    // ScalingKey includes map and instance; ending one fight must not touch a creature on another map.
    Scenario s;
    Creature* a = s.SpawnCreature(CREATURE_ELITE_NORMAL, &s.eastern);
    Creature* b = s.SpawnCreature(CREATURE_ELITE_NORMAL, &s.kalimdor);
    auto partyA = s.NearbyParty(2);
    Player* soloB = s.SpawnPlayer({ 0, 0, 0 }, &s.kalimdor);
    Engage(a, partyA[0]);
    Engage(b, soloB);
    ExitCombat(a);
    CHECK_EQ(b->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 1));
    CHECK_EQ(a->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));
}

// ================================================================================================
// Damage, health and XP arithmetic
// ================================================================================================

TEST("arithmetic", SpellCategoriesUseTheirOwnRates)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(3);
    Engage(c, party[0]);
    double const coef = DamageTier[2];
    SpellInfo school = MakeSpell(SPELL_EFFECT_SCHOOL_DAMAGE);
    SpellInfo weapon = MakeSpell(SPELL_EFFECT_WEAPON_DAMAGE);
    SpellInfo weaponPct = MakeSpell(SPELL_EFFECT_WEAPON_PERCENT_DAMAGE);
    SpellInfo normalized = MakeSpell(SPELL_EFFECT_NORMALIZED_WEAPON_DMG);
    SpellInfo noschool = MakeSpell(SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL);
    SpellInfo ignore = MakeSpell(SPELL_EFFECT_SCHOOL_DAMAGE, SPELL_AURA_NONE, SPELL_ATTR3_IGNORE_CASTER_MODIFIERS);
    SpellInfo weaponIgnore = MakeSpell(SPELL_EFFECT_WEAPON_DAMAGE, SPELL_AURA_NONE, SPELL_ATTR3_IGNORE_CASTER_MODIFIERS);
    CHECK_EQ(SpellHit(c, party[0], 1000, &school), int32(std::llround(1000 * coef / SpellRate)));
    for (SpellInfo const* w : { &weapon, &weaponPct, &normalized, &noschool, &weaponIgnore })
        CHECK_EQ(SpellHit(c, party[0], 1000, w), int32(std::llround(1000 * coef / NormalMeleeRate)));
    CHECK_EQ(SpellHit(c, party[0], 1000, &ignore), int32(std::llround(1000 * coef)));
    CHECK_EQ(SpellHit(c, party[0], 0, &school), 0);
    CHECK_EQ(SpellHit(c, party[0], -50, &school), -50);
    CHECK_EQ(SpellHit(c, party[0], 1000, nullptr), 1000);
}

TEST("arithmetic", PeriodicOnlyScalesUnambiguousDamage)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(2);
    Engage(c, party[0]);
    double const coef = DamageTier[1];
    SpellInfo dot = MakeDot();
    SpellInfo dotIgnore = MakeDot();
    dotIgnore.AttributesEx3 = SPELL_ATTR3_IGNORE_CASTER_MODIFIERS;
    CHECK_EQ(Tick(c, party[0], 1000, &dot), uint32(std::llround(1000 * coef / SpellRate)));
    CHECK_EQ(Tick(c, party[0], 1000, &dotIgnore), uint32(std::llround(1000 * coef)));
    for (AuraType mixed : { SPELL_AURA_PERIODIC_HEAL, SPELL_AURA_OBS_MOD_HEALTH, SPELL_AURA_PERIODIC_LEECH,
             SPELL_AURA_PERIODIC_DAMAGE_PERCENT, SPELL_AURA_PERIODIC_HEALTH_FUNNEL })
    {
        SpellInfo m = MakeDot(mixed);
        CHECK_EQ(Tick(c, party[0], 1000, &m), 1000u);
    }
    // Core also calls this hook for periodic heals (SpellAuraEffects HandlePeriodicHealAurasTick).
    SpellInfo hot = MakeSpell(SPELL_EFFECT_APPLY_AURA, SPELL_AURA_PERIODIC_HEAL);
    CHECK_EQ(Tick(c, c, 1000, &hot), 1000u);
    CHECK_EQ(Tick(c, party[0], 1000, nullptr), 1000u);
    CHECK_EQ(Tick(c, party[0], 0, &dot), 0u);
}

TEST("arithmetic", PlayerOutgoingDamageIsNeverTouched)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(4);
    Engage(c, party[0]);
    uint32 melee = 500;
    Hooks::ModifyMeleeDamage(c, party[0], melee);
    CHECK_EQ(melee, 500u);
    int32 spell = 500;
    SpellInfo bolt = MakeSpell(SPELL_EFFECT_SCHOOL_DAMAGE);
    Hooks::ModifySpellDamageTaken(c, party[0], spell, &bolt);
    CHECK_EQ(spell, 500);
}

TEST("arithmetic", DamageClampsAtUpperBound)
{
    Scenario s({ { "WorldPartyScaling.Group1.Damage", "1000000" } });
    Creature* c = s.SpawnCreature();
    Player* p = s.SpawnPlayer();
    Engage(c, p);
    uint32 const maxDamage = uint32(std::numeric_limits<int32>::max() / 2);
    CHECK_EQ(Melee(c, p, 4000000), maxDamage);
    SpellInfo bolt = MakeSpell(SPELL_EFFECT_SCHOOL_DAMAGE);
    CHECK_EQ(SpellHit(c, p, 4000000, &bolt), int32(maxDamage));
    CHECK_EQ(Melee(c, p, 1), 740741u); // 1 * 1e6 / 1.35
}

TEST("arithmetic", ZeroDamageCoefficientFloorsAtOne)
{
    Scenario s({ { "WorldPartyScaling.Group1.Damage", "0" } });
    Creature* c = s.SpawnCreature();
    Player* p = s.SpawnPlayer();
    Engage(c, p);
    CHECK_EQ(Melee(c, p, 500), 1u);
}

TEST("arithmetic", HealthPercentAndAuraFlatSurviveScaleAndRestore)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    c->SetStatFlatModifier(UNIT_MOD_HEALTH, TOTAL_VALUE, 200.0f); // e.g. a stamina buff
    c->UpdateMaxHealth();
    c->SetHealth(c->GetMaxHealth() / 2);
    auto party = s.NearbyParty(3);
    Engage(c, party[0]);
    uint32 const scaledMax = ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 3) + 200;
    CHECK_EQ(c->GetMaxHealth(), scaledMax);
    CHECK_EQ(c->GetHealth(), scaledMax / 2);
    ExitCombat(c);
    CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL) + 200);
    CHECK_EQ(c->GetHealth(), (NativeHealth(CREATURE_ELITE_NORMAL) + 200) / 2);
}

TEST("arithmetic", ZeroCreateHealthFallsBackToMaxHealth)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    c->SetCreateHealth(0);
    auto party = s.NearbyParty(2);
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), uint32(std::llround(std::llround(NativeHealth(CREATURE_ELITE_NORMAL) / double(NormalHpRate)) * double(HealthTier[1]))));
}

TEST("arithmetic", XpSoloGroupAndNonKillSources)
{
    Scenario s;
    Creature* solo = s.SpawnCreature();
    Player* p = s.SpawnPlayer();
    Engage(solo, p);
    CHECK_EQ(Kill(p, solo, 777)[p], 777u);

    for (uint32 n = 2; n <= 6; ++n)
    {
        Scenario g;
        Creature* c = g.SpawnCreature();
        auto party = g.NearbyParty(n);
        Engage(c, party[0]);
        auto xp = Kill(party[0], c, 1200);
        double const coreRate = Acore::XP::xp_in_group_rate(n, false);
        double const expected = double(CoreGroupXp(1200, n, n)) * XpTier[n - 1] / coreRate;
        for (Player* m : party)
            CHECK_NEAR(double(xp[m]), expected, 1.0);
    }

    Scenario q;
    Creature* c = q.SpawnCreature();
    auto party = q.NearbyParty(3);
    Engage(c, party[0]);
    for (uint8 src : { uint8(XPSOURCE_QUEST), uint8(XPSOURCE_QUEST_DF), uint8(XPSOURCE_EXPLORE), uint8(XPSOURCE_BATTLEGROUND) })
    {
        uint32 amount = 400;
        Hooks::OnPlayerGiveXP(party[0], amount, c, src);
        CHECK_EQ(amount, 400u);
    }
    uint32 none = 400;
    Hooks::OnPlayerGiveXP(party[0], none, nullptr, XPSOURCE_KILL);
    CHECK_EQ(none, 400u);
}

TEST("arithmetic", XpTierZeroGivesZero)
{
    Scenario s({ { "WorldPartyScaling.Group1.XP", "0" } });
    Creature* c = s.SpawnCreature();
    Player* p = s.SpawnPlayer();
    Engage(c, p);
    CHECK_EQ(Kill(p, c, 500)[p], 0u);
}

TEST("arithmetic", UnscaledCreatureKillXpIsCore)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(4);
    // Never engaged by a player (e.g. killed while scaling disabled for its entry).
    fakecore::Config().Set("WorldPartyScaling.ExcludedCreatureEntries", "100");
    ReloadConfig(true);
    Engage(c, party[0]);
    CHECK_EQ(Kill(party[0], c, 900)[party[0]], CoreGroupXp(900, 4, 4));
}

// ================================================================================================
// Eligibility and configuration
// ================================================================================================

TEST("eligibility", NonScalableCreatureKindsAreSkipped)
{
    using Setter = std::function<void(Creature*)>;
    std::vector<std::pair<char const*, Setter>> cases = {
        { "pet", [](Creature* c) { c->isPet = true; } },
        { "guardian", [](Creature* c) { c->isGuardian = true; } },
        { "totem", [](Creature* c) { c->isTotem = true; } },
        { "summon", [](Creature* c) { c->isSummon = true; } },
        { "controlled", [](Creature* c) { c->controlledByPlayer = true; } },
        { "charmed", [](Creature* c) { c->charmed = true; } },
        { "guard", [](Creature* c) { c->isGuard = true; } },
        { "civilian", [](Creature* c) { c->isCivilian = true; } },
        { "critter", [](Creature* c) { c->isCritter = true; } },
        { "non-attackable", [](Creature* c) { c->unitFlags |= UNIT_FLAG_NON_ATTACKABLE; } },
        { "not-selectable", [](Creature* c) { c->unitFlags |= UNIT_FLAG_NOT_SELECTABLE; } },
        { "dead", [](Creature* c) { c->alive = false; } },
        { "not-in-world", [](Creature* c) { c->inWorld = false; } },
        { "friendly", [](Creature* c) { c->faction = 1; } },
    };
    for (auto const& [name, set] : cases)
    {
        Scenario s;
        Creature* c = s.SpawnCreature();
        auto party = s.NearbyParty(3);
        set(c);
        Engage(c, party[0]);
        CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));
        CHECK_EQ(Melee(c, party[0], 135), 135u);
        (void)name;
    }
}

TEST("eligibility", EntryMapZoneExclusionListsParseLeniently)
{
    Scenario s({ { "WorldPartyScaling.ExcludedCreatureEntries", " 12, abc, 100 ,, 5x, 99" },
        { "WorldPartyScaling.ExcludedMaps", "1" }, { "WorldPartyScaling.ExcludedZones", "\t44 " } });
    auto party = s.NearbyParty(3);

    Creature* byEntry = s.SpawnCreature(CREATURE_ELITE_NORMAL); // entry 100
    Engage(byEntry, party[0]);
    CHECK_EQ(byEntry->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));

    Creature* rare = s.SpawnCreature(CREATURE_ELITE_RARE); // entry 104 not listed
    Engage(rare, party[0]);
    CHECK_EQ(rare->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_RARE, 3));

    Creature* onMap1 = s.SpawnCreature(CREATURE_ELITE_RARE, &s.kalimdor);
    Player* k = s.SpawnPlayer({ 0, 0, 0 }, &s.kalimdor);
    Engage(onMap1, k);
    CHECK_EQ(onMap1->GetMaxHealth(), NativeHealth(CREATURE_ELITE_RARE));

    Creature* inZone = s.SpawnCreature(CREATURE_ELITE_RARE);
    inZone->zoneId = 44;
    Engage(inZone, party[0]);
    CHECK_EQ(inZone->GetMaxHealth(), NativeHealth(CREATURE_ELITE_RARE));
}

TEST("eligibility", InvalidTierValuesFallBackToDefaults)
{
    Scenario s({ { "WorldPartyScaling.Group2.Health", "-1" }, { "WorldPartyScaling.Group2.Damage", "-3" },
        { "WorldPartyScaling.Group3.Health", "0" }, { "WorldPartyScaling.Group4.Health", "nan" },
        { "WorldPartyScaling.Group5.Health", "inf" } });
    CHECK_NEAR(g_WorldPartyScalingConfig.HealthMultiplier(2), 1.70, 1e-6);
    CHECK_NEAR(g_WorldPartyScalingConfig.DamageMultiplier(2), 1.35, 1e-6);
    CHECK_NEAR(g_WorldPartyScalingConfig.HealthMultiplier(3), 2.50, 1e-6);
    CHECK_NEAR(g_WorldPartyScalingConfig.HealthMultiplier(4), 3.30, 1e-6);
    CHECK_NEAR(g_WorldPartyScalingConfig.HealthMultiplier(5), 4.20, 1e-6);
    CHECK_NEAR(g_WorldPartyScalingConfig.HealthMultiplier(0), 1.00, 1e-6);  // clamped
    CHECK_NEAR(g_WorldPartyScalingConfig.HealthMultiplier(99), 5.20, 1e-6); // clamped
    CHECK_NEAR(g_WorldPartyScalingConfig.XpMultiplier(6), 1.50, 1e-6);
}

TEST("eligibility", DisabledAtStartupDoesNothing)
{
    Scenario s({ { "WorldPartyScaling.Enable", "0" } });
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(6);
    Engage(c, party[0]);
    CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));
    CHECK_EQ(Melee(c, party[0], 135), 135u);
    CHECK_EQ(Kill(party[0], c, 600)[party[0]], CoreGroupXp(600, 6, 6));
}

TEST("eligibility", RegistersOnlyTheHooksItImplements)
{
    auto const& reg = fakecore::Scripts();
    CHECK_EQ(reg.world.size(), std::size_t(1));
    CHECK_EQ(reg.allCreature.size(), std::size_t(1));
    CHECK_EQ(reg.unit.size(), std::size_t(1));
    CHECK_EQ(reg.player.size(), std::size_t(1));
    CHECK_EQ(reg.unit[0]->hooks.size(), std::size_t(6));
    CHECK(fakecore::Enabled(reg.player[0]->hooks, PLAYERHOOK_ON_GIVE_EXP));
    CHECK(fakecore::Enabled(reg.world[0]->hooks, WORLDHOOK_ON_BEFORE_CONFIG_LOAD));
}

TEST("eligibility", DebugLoggingFormatsWithoutThrowing)
{
    Scenario s({ { "WorldPartyScaling.EnableDebug", "1" } });
    std::size_t const before = fakecore::Logs().lines.size();
    Creature* c = s.SpawnCreature();
    auto party = s.NearbyParty(2);
    Engage(c, party[0]);
    ExitCombat(c);
    CHECK(fakecore::Logs().lines.size() >= before + 2);
    CHECK(fakecore::Logs().lines.back().find("restored entry 100") != std::string::npos);
}

// ================================================================================================
// Failure injection
// ================================================================================================

TEST("failure-injection", NullArgumentsAreIgnored)
{
    Scenario s;
    Creature* c = s.SpawnCreature();
    Player* p = s.SpawnPlayer();
    uint32 u = 100;
    int32 i = 100;
    SpellInfo bolt = MakeSpell(SPELL_EFFECT_SCHOOL_DAMAGE);
    Hooks::OnUnitEnterCombat(nullptr, p);
    Hooks::OnUnitEnterCombat(c, nullptr);
    Hooks::OnUnitExitCombat(nullptr);
    Hooks::OnUnitDeath(nullptr, nullptr);
    Hooks::OnCreatureRemoveWorld(nullptr);
    Hooks::ModifyMeleeDamage(nullptr, nullptr, u);
    Hooks::ModifyMeleeDamage(p, nullptr, u);
    Hooks::ModifySpellDamageTaken(nullptr, nullptr, i, &bolt);
    Hooks::ModifyPeriodicDamageAurasTick(nullptr, nullptr, u, nullptr);
    Hooks::OnPlayerGiveXP(nullptr, u, c, XPSOURCE_KILL);
    CHECK_EQ(u, 100u);
    CHECK_EQ(i, 100);
    CHECK_EQ(c->GetMaxHealth(), NativeHealth(CREATURE_ELITE_NORMAL));
    // Engage with a null victim then a player hit: the hit starts scaling.
    c->inCombat = true;
    CHECK_EQ(Melee(c, p, 135), ExpectedMelee(135, CREATURE_ELITE_NORMAL, 1));
}

TEST("failure-injection", CreatureWithoutTemplateOrMap)
{
    Scenario s;
    Player* p = s.SpawnPlayer();
    s.creatures.push_back(std::make_unique<Creature>(nullptr));
    Creature* noTemplate = s.creatures.back().get();
    noTemplate->SpawnWithHealth(&s.eastern, { 0, 0, 0 }, 1000, 1.0f);
    Engage(noTemplate, p); // no rank: not excluded; rates fall back to 1.0
    CHECK_EQ(noTemplate->GetMaxHealth(), 1000u);
    ExitCombat(noTemplate);

    s.creatures.push_back(std::make_unique<Creature>(s.Template(CREATURE_ELITE_NORMAL)));
    Creature* noMap = s.creatures.back().get();
    noMap->SpawnWithHealth(nullptr, { 0, 0, 0 }, 1000, 1.0f);
    noMap->inWorld = true; // inconsistent object: must still be rejected by the map gate
    Engage(noMap, p);
    CHECK_EQ(noMap->GetMaxHealth(), 1000u);
}

TEST("failure-injection", DisableReloadAfterCreatureOrMapVanished)
{
    Scenario s;
    auto party = s.NearbyParty(2);
    Creature* gone = s.SpawnCreature();
    Creature* mapGone = s.SpawnCreature(CREATURE_ELITE_NORMAL, &s.kalimdor);
    Player* k = s.SpawnPlayer({ 0, 0, 0 }, &s.kalimdor);
    Engage(gone, party[0]);
    Engage(mapGone, k);
    gone->RemoveFromMap(); // removed without the remove-world hook firing
    fakecore::TheMapMgr().maps = { &s.eastern };
    fakecore::Config().Set("WorldPartyScaling.Enable", "0");
    ReloadConfig(true); // must skip both entries without touching freed/foreign objects
    CHECK_EQ(gone->GetMaxHealth(), ExpectedScaledHealth(CREATURE_ELITE_NORMAL, 2));
    fakecore::TheMapMgr().maps = { &s.eastern, &s.kalimdor };
}

TEST("failure-injection", ConcurrentMapsHammerTheSharedState)
{
    std::vector<std::unique_ptr<Map>> maps; // declared first: must outlive the creatures in the scenario
    Scenario s;
    constexpr int Threads = 8;
    constexpr int Rounds = 300;
    std::vector<Creature*> creatures;
    std::vector<std::vector<Player*>> parties;
    for (int t = 0; t < Threads; ++t)
    {
        maps.push_back(std::make_unique<Map>(1000 + t, 0, Map::Kind::World));
        fakecore::TheMapMgr().maps.push_back(maps.back().get());
        creatures.push_back(s.SpawnCreature(CREATURE_ELITE_NORMAL, maps.back().get()));
        std::vector<Player*> party;
        for (int i = 0; i <= t % 6; ++i)
            party.push_back(s.SpawnPlayer({ float(i), 0, 0 }, maps.back().get()));
        if (party.size() > 1)
            s.MakeGroup(party);
        parties.push_back(party);
    }

    std::vector<int> mismatches(Threads, 0);
    std::vector<std::thread> workers;
    for (int t = 0; t < Threads; ++t)
        workers.emplace_back([&, t] {
            Creature* c = creatures[t];
            Player* p = parties[t][0];
            uint32 const tier = uint32(parties[t].size());
            for (int r = 0; r < Rounds; ++r)
            {
                Engage(c, p);
                if (Melee(c, p, 270) != ExpectedMelee(270, CREATURE_ELITE_NORMAL, tier))
                    ++mismatches[t];
                if (c->GetMaxHealth() != ExpectedScaledHealth(CREATURE_ELITE_NORMAL, tier))
                    ++mismatches[t];
                ExitCombat(c);
                if (c->GetMaxHealth() != NativeHealth(CREATURE_ELITE_NORMAL))
                    ++mismatches[t];
            }
        });
    for (auto& w : workers)
        w.join();
    for (int t = 0; t < Threads; ++t)
        CHECK_EQ(mismatches[t], 0);
    fakecore::TheMapMgr().maps.resize(6);
}

int main(int argc, char** argv)
{
    AddWorldPartyScalingScripts();

    char const* filter = argc > 1 ? argv[1] : nullptr;
    unsigned ran = 0;
    unsigned failedTests = 0;
    for (auto const& test : wpstest::Registry())
    {
        if (filter && !std::strstr(test.group, filter) && !std::strstr(test.name, filter))
            continue;

        unsigned const before = wpstest::Count().failures;
        try
        {
            test.body();
        }
        catch (std::exception const& e)
        {
            ++wpstest::Count().failures;
            std::fprintf(stderr, "    EXCEPTION %s\n", e.what());
        }
        bool const ok = wpstest::Count().failures == before;
        failedTests += ok ? 0 : 1;
        ++ran;
        std::printf("[%s] %s / %s\n", ok ? "PASS" : "FAIL", test.group, test.name);
    }

    std::printf("\n%u tests, %u checks, %u failed checks, %u failed tests\n", ran, wpstest::Count().checks,
        wpstest::Count().failures, failedTests);
    return failedTests == 0 && ran > 0 ? 0 : 1;
}
