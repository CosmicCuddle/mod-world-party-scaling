/*
 * Test-only ScriptMgr stand-in. Scripts register exactly like core script objects and the dispatcher
 * only delivers a hook to a script that enabled it in its constructor hook list, as core ScriptMgr
 * does (WorldScript/UnitScript/PlayerScript). AllCreatureScript has no hook list in core.
 */

#pragma once

#include "FakeCore.h"

#include <algorithm>
#include <vector>

class WorldScript
{
public:
    WorldScript(char const* name, std::vector<uint16> enabledHooks = std::vector<uint16>());
    virtual ~WorldScript() = default;
    virtual void OnBeforeConfigLoad(bool /*reload*/) { }
    virtual void OnAfterConfigLoad(bool /*reload*/) { }

    std::string name;
    std::vector<uint16> hooks;
};

class AllCreatureScript
{
public:
    AllCreatureScript(char const* name);
    virtual ~AllCreatureScript() = default;
    virtual void OnCreatureRemoveWorld(Creature* /*creature*/) { }

    std::string name;
};

class UnitScript
{
public:
    UnitScript(char const* name, bool addToScripts = true, std::vector<uint16> enabledHooks = std::vector<uint16>());
    virtual ~UnitScript() = default;
    virtual void OnUnitEnterCombat(Unit* /*unit*/, Unit* /*victim*/) { }
    virtual void OnUnitExitCombat(Unit* /*unit*/) { }
    virtual void OnUnitDeath(Unit* /*unit*/, Unit* /*killer*/) { }
    virtual void ModifyMeleeDamage(Unit* /*target*/, Unit* /*attacker*/, uint32& /*damage*/) { }
    virtual void ModifySpellDamageTaken(Unit* /*target*/, Unit* /*attacker*/, int32& /*damage*/, SpellInfo const* /*spellInfo*/) { }
    virtual void ModifyPeriodicDamageAurasTick(Unit* /*target*/, Unit* /*attacker*/, uint32& /*damage*/, SpellInfo const* /*spellInfo*/) { }

    std::string name;
    std::vector<uint16> hooks;
};

class PlayerScript
{
public:
    PlayerScript(char const* name, std::vector<uint16> enabledHooks = std::vector<uint16>());
    virtual ~PlayerScript() = default;
    virtual void OnPlayerGiveXP(Player* /*player*/, uint32& /*amount*/, Unit* /*victim*/, uint8 /*xpSource*/) { }

    std::string name;
    std::vector<uint16> hooks;
};

namespace fakecore
{
struct ScriptRegistry
{
    std::vector<WorldScript*> world;
    std::vector<AllCreatureScript*> allCreature;
    std::vector<UnitScript*> unit;
    std::vector<PlayerScript*> player;

    // Core ScriptMgr owns and deletes registered script objects at unload.
    ~ScriptRegistry();
};

inline ScriptRegistry& Scripts()
{
    static ScriptRegistry registry;
    return registry;
}

inline bool Enabled(std::vector<uint16> const& hooks, uint16 hook)
{
    return std::find(hooks.begin(), hooks.end(), hook) != hooks.end();
}

// Dispatchers, named after the core sScriptMgr call they stand in for.
struct Hooks
{
    static void OnBeforeConfigLoad(bool reload)
    {
        for (WorldScript* s : Scripts().world)
            if (Enabled(s->hooks, WORLDHOOK_ON_BEFORE_CONFIG_LOAD))
                s->OnBeforeConfigLoad(reload);
    }

    static void OnAfterConfigLoad(bool reload)
    {
        for (WorldScript* s : Scripts().world)
            if (Enabled(s->hooks, WORLDHOOK_ON_AFTER_CONFIG_LOAD))
                s->OnAfterConfigLoad(reload);
    }

    static void OnCreatureRemoveWorld(Creature* creature)
    {
        for (AllCreatureScript* s : Scripts().allCreature)
            s->OnCreatureRemoveWorld(creature);
    }

    static void OnUnitEnterCombat(Unit* unit, Unit* victim)
    {
        for (UnitScript* s : Scripts().unit)
            if (Enabled(s->hooks, UNITHOOK_ON_UNIT_ENTER_COMBAT))
                s->OnUnitEnterCombat(unit, victim);
    }

    static void OnUnitExitCombat(Unit* unit)
    {
        for (UnitScript* s : Scripts().unit)
            if (Enabled(s->hooks, UNITHOOK_ON_UNIT_EXIT_COMBAT))
                s->OnUnitExitCombat(unit);
    }

    static void OnUnitDeath(Unit* unit, Unit* killer)
    {
        for (UnitScript* s : Scripts().unit)
            if (Enabled(s->hooks, UNITHOOK_ON_UNIT_DEATH))
                s->OnUnitDeath(unit, killer);
    }

    static void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage)
    {
        for (UnitScript* s : Scripts().unit)
            if (Enabled(s->hooks, UNITHOOK_MODIFY_MELEE_DAMAGE))
                s->ModifyMeleeDamage(target, attacker, damage);
    }

    static void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo)
    {
        for (UnitScript* s : Scripts().unit)
            if (Enabled(s->hooks, UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN))
                s->ModifySpellDamageTaken(target, attacker, damage, spellInfo);
    }

    static void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo)
    {
        for (UnitScript* s : Scripts().unit)
            if (Enabled(s->hooks, UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK))
                s->ModifyPeriodicDamageAurasTick(target, attacker, damage, spellInfo);
    }

    static void OnPlayerGiveXP(Player* player, uint32& amount, Unit* victim, uint8 source)
    {
        for (PlayerScript* s : Scripts().player)
            if (Enabled(s->hooks, PLAYERHOOK_ON_GIVE_EXP))
                s->OnPlayerGiveXP(player, amount, victim, source);
    }
};
} // namespace fakecore

inline fakecore::ScriptRegistry::~ScriptRegistry()
{
    for (auto* s : world) delete s;
    for (auto* s : allCreature) delete s;
    for (auto* s : unit) delete s;
    for (auto* s : player) delete s;
}

inline WorldScript::WorldScript(char const* n, std::vector<uint16> enabledHooks) : name(n), hooks(std::move(enabledHooks))
{
    fakecore::Scripts().world.push_back(this);
}

inline AllCreatureScript::AllCreatureScript(char const* n) : name(n)
{
    fakecore::Scripts().allCreature.push_back(this);
}

inline UnitScript::UnitScript(char const* n, bool addToScripts, std::vector<uint16> enabledHooks)
    : name(n), hooks(std::move(enabledHooks))
{
    if (addToScripts)
        fakecore::Scripts().unit.push_back(this);
}

inline PlayerScript::PlayerScript(char const* n, std::vector<uint16> enabledHooks) : name(n), hooks(std::move(enabledHooks))
{
    fakecore::Scripts().player.push_back(this);
}
