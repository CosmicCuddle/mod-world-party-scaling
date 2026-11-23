/*
 * Test-only stand-in for the AzerothCore surfaces used by src/WorldPartyScaling.cpp.
 *
 * The real module source is compiled unchanged against these headers. Every type, function and enum
 * name mirrors the real core declaration the module calls (tests/check_core_compile.sh proves the
 * module compiles against the real core headers), and behaviour mirrors the cited core functions:
 *
 *   WorldObject::IsWithinDistInMap / _IsWithinDist / IsInMap   Entities/Object/Object.cpp
 *   Player::IsAtGroupRewardDistance                            Entities/Player/Player.cpp
 *   Acore::XP::xp_in_group_rate                                Miscellaneous/Formulas.h
 *   KillRewarder group rate and OnPlayerGiveXP call site       Entities/Player/KillRewarder.cpp
 *   Unit::Kill ordering (rewards -> setDeathState/CombatStop -> OnUnitDeath)  Entities/Unit/Unit.cpp
 *   ConfigValueCache                                           common/Configuration/ConfigValueCache.h
 *
 * It is a MOCK. It does not prove Worldserver, database, client or gameplay behaviour.
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

class Map;
class Unit;
class Player;
class Creature;
class Group;
class SpellInfo;

namespace fakecore
{
// ------------------------------------------------------------------------------------------------ log
struct LogState
{
    std::mutex mutex;
    std::vector<std::string> lines;
};

inline LogState& Logs()
{
    static LogState state;
    return state;
}

inline void Log(std::string_view level, std::string_view filter, std::string message)
{
    std::lock_guard<std::mutex> lock(Logs().mutex);
    Logs().lines.push_back(std::string(level) + " [" + std::string(filter) + "] " + message);
}
} // namespace fakecore

#define LOG_DEBUG(filter, ...) ::fakecore::Log("DEBUG", filter, std::format(__VA_ARGS__))
#define LOG_INFO(filter, ...) ::fakecore::Log("INFO", filter, std::format(__VA_ARGS__))
#define LOG_ERROR(filter, ...) ::fakecore::Log("ERROR", filter, std::format(__VA_ARGS__))

#define ASSERT(cond, ...) do { if (!(cond)) throw std::logic_error("fakecore ASSERT failed: " #cond); } while (0)

// ------------------------------------------------------------------------------------------------ guid
class ObjectGuid
{
public:
    ObjectGuid() = default;
    explicit ObjectGuid(uint64 raw) : _raw(raw) { }

    [[nodiscard]] uint64 GetRawValue() const { return _raw; }
    [[nodiscard]] std::string ToString() const { return std::format("Guid({})", _raw); }
    bool operator==(ObjectGuid const& other) const = default;

private:
    uint64 _raw = 0;
};

template<>
struct std::hash<ObjectGuid>
{
    std::size_t operator()(ObjectGuid const& guid) const noexcept { return std::hash<uint64>()(guid.GetRawValue()); }
};

// ------------------------------------------------------------------------------------------------ enums (names match core)
enum CreatureEliteType : uint32
{
    CREATURE_ELITE_NORMAL = 0,
    CREATURE_ELITE_ELITE = 1,
    CREATURE_ELITE_RAREELITE = 2,
    CREATURE_ELITE_WORLDBOSS = 3,
    CREATURE_ELITE_RARE = 4,
    CREATURE_UNKNOWN = 5
};

enum Rates
{
    RATE_CREATURE_NORMAL_HP,
    RATE_CREATURE_ELITE_ELITE_HP,
    RATE_CREATURE_ELITE_RAREELITE_HP,
    RATE_CREATURE_ELITE_WORLDBOSS_HP,
    RATE_CREATURE_ELITE_RARE_HP,
    RATE_CREATURE_NORMAL_DAMAGE,
    RATE_CREATURE_ELITE_ELITE_DAMAGE,
    RATE_CREATURE_ELITE_RAREELITE_DAMAGE,
    RATE_CREATURE_ELITE_WORLDBOSS_DAMAGE,
    RATE_CREATURE_ELITE_RARE_DAMAGE,
    RATE_CREATURE_NORMAL_SPELLDAMAGE,
    RATE_CREATURE_ELITE_ELITE_SPELLDAMAGE,
    RATE_CREATURE_ELITE_RAREELITE_SPELLDAMAGE,
    RATE_CREATURE_ELITE_WORLDBOSS_SPELLDAMAGE,
    RATE_CREATURE_ELITE_RARE_SPELLDAMAGE,
    MAX_RATES
};

enum WorldFloatConfigs
{
    CONFIG_GROUP_XP_DISTANCE,
    FLOAT_CONFIG_VALUE_COUNT
};

enum UnitMods
{
    UNIT_MOD_HEALTH = 0,
    UNIT_MOD_END
};

enum UnitModifierFlatType
{
    BASE_VALUE = 0,
    TOTAL_VALUE = 1
};

enum UnitFlags : uint32
{
    UNIT_FLAG_NON_ATTACKABLE = 0x00000002,
    UNIT_FLAG_NOT_SELECTABLE = 0x02000000
};

enum SpellEffects : uint32
{
    SPELL_EFFECT_NONE = 0,
    SPELL_EFFECT_SCHOOL_DAMAGE = 2,
    SPELL_EFFECT_APPLY_AURA = 6,
    SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL = 17,
    SPELL_EFFECT_WEAPON_PERCENT_DAMAGE = 31,
    SPELL_EFFECT_WEAPON_DAMAGE = 58,
    SPELL_EFFECT_NORMALIZED_WEAPON_DMG = 121
};

enum SpellAttr3 : uint32
{
    SPELL_ATTR3_IGNORE_CASTER_MODIFIERS = 0x20000000
};

enum AuraType : uint32
{
    SPELL_AURA_NONE = 0,
    SPELL_AURA_PERIODIC_DAMAGE = 3,
    SPELL_AURA_PERIODIC_HEAL = 8,
    SPELL_AURA_OBS_MOD_HEALTH = 20,
    SPELL_AURA_PERIODIC_LEECH = 53,
    SPELL_AURA_PERIODIC_HEALTH_FUNNEL = 62,
    SPELL_AURA_PERIODIC_DAMAGE_PERCENT = 89
};

enum PlayerXPSource : uint8
{
    XPSOURCE_KILL = 0,
    XPSOURCE_QUEST = 1,
    XPSOURCE_QUEST_DF = 2,
    XPSOURCE_EXPLORE = 3,
    XPSOURCE_BATTLEGROUND = 4
};

enum WorldHook : uint16
{
    WORLDHOOK_ON_BEFORE_CONFIG_LOAD,
    WORLDHOOK_ON_AFTER_CONFIG_LOAD
};

enum UnitHook : uint16
{
    UNITHOOK_ON_UNIT_ENTER_COMBAT,
    UNITHOOK_ON_UNIT_EXIT_COMBAT,
    UNITHOOK_ON_UNIT_DEATH,
    UNITHOOK_MODIFY_MELEE_DAMAGE,
    UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
    UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK
};

enum PlayerHook : uint16
{
    PLAYERHOOK_ON_GIVE_EXP
};

// ------------------------------------------------------------------------------------------------ config
namespace fakecore
{
class ConfigMgr
{
public:
    void Set(std::string const& name, std::string value) { _values[name] = std::move(value); }
    void Clear() { _values.clear(); }

    template<class T>
    T GetOption(std::string const& name, T const& def) const
    {
        auto const itr = _values.find(name);
        if (itr == _values.end())
            return def;

        std::string const& raw = itr->second;
        if constexpr (std::is_same_v<T, bool>)
            return raw == "1" || raw == "true" || raw == "yes";
        else if constexpr (std::is_same_v<T, float>)
        {
            try { return std::stof(raw); } catch (...) { return def; }
        }
        else if constexpr (std::is_same_v<T, uint32>)
        {
            try { return static_cast<uint32>(std::stoul(raw)); } catch (...) { return def; }
        }
        else
            return raw;
    }

private:
    std::unordered_map<std::string, std::string> _values;
};

inline ConfigMgr& Config()
{
    static ConfigMgr mgr;
    return mgr;
}
} // namespace fakecore

#define sConfigMgr (&::fakecore::Config())

// Behavioural mirror of core ConfigValueCache (validation failure falls back to the default).
template<typename ConfigEnum>
class ConfigValueCache
{
public:
    enum class Reloadable : bool { No = false, Yes = true };

    explicit ConfigValueCache(ConfigEnum const configCount) { _configs.resize(static_cast<uint32>(configCount)); }
    virtual ~ConfigValueCache() = default;

    void Initialize(bool reload)
    {
        _reloading = reload;
        BuildConfigCache();
        _reloading = false;
        for (auto const& variant : _configs)
            ASSERT(variant.index() != 0, "config not loaded");
    }

    template<class T>
    void SetConfigValue(ConfigEnum const config, std::string const& configName, T const& defaultValue,
        Reloadable reloadable = Reloadable::Yes, std::function<bool(T const& value)>&& checker = {},
        std::string const& validationErrorText = "")
    {
        uint32 const index = static_cast<uint32>(config);
        ASSERT(index < _configs.size());
        T const configValue = sConfigMgr->GetOption<T>(configName, defaultValue);

        if (_reloading)
        {
            if (reloadable == Reloadable::No)
                return;
        }
        else
            ASSERT(_configs[index].index() == 0, "Config overwriting an existing value");

        if (checker && !checker(configValue))
        {
            LOG_ERROR("server.loading", "Server Config (Name: {}) failed validation check '{}'.", configName,
                validationErrorText);
            _configs[index] = defaultValue;
        }
        else
            _configs[index] = configValue;
    }

    template<class T>
    T GetConfigValue(ConfigEnum const config) const
    {
        T const* value = std::get_if<T>(&_configs.at(static_cast<uint32>(config)));
        ASSERT(value, "Wrong config variant type");
        return *value;
    }

    std::string_view GetConfigValue(ConfigEnum const config) const
    {
        std::string const* value = std::get_if<std::string>(&_configs.at(static_cast<uint32>(config)));
        ASSERT(value, "Wrong config variant type");
        return std::string_view(*value);
    }

    // Test-only: emulate a fresh worldserver process.
    void ResetForTest() { for (auto& v : _configs) v = std::monostate{}; }

protected:
    virtual void BuildConfigCache() = 0;

private:
    std::vector<std::variant<std::monostate, float, bool, uint32, std::string>> _configs;
    bool _reloading = false;
};

// ------------------------------------------------------------------------------------------------ world
namespace fakecore
{
class World
{
public:
    World() { Reset(); }

    void Reset()
    {
        _floats.fill(0.0f);
        _floats[CONFIG_GROUP_XP_DISTANCE] = 74.0f; // core default Group.XPDistance
        _rates.fill(1.0f);
    }

    [[nodiscard]] float getFloatConfig(WorldFloatConfigs index) const { return _floats[index]; }
    [[nodiscard]] float getRate(Rates rate) const { return _rates[rate]; }
    void SetFloat(WorldFloatConfigs index, float value) { _floats[index] = value; }
    void SetRate(Rates rate, float value) { _rates[rate] = value; }

private:
    std::array<float, FLOAT_CONFIG_VALUE_COUNT> _floats{};
    std::array<float, MAX_RATES> _rates{};
};

inline World& TheWorld()
{
    static World world;
    return world;
}
} // namespace fakecore

#define sWorld (&::fakecore::TheWorld())

namespace Acore
{
namespace XP
{
// Copy of core Formulas.h xp_in_group_rate.
inline float xp_in_group_rate(uint32 count, bool isRaid)
{
    if (isRaid)
        return 1.0f;

    switch (count)
    {
        case 0:
        case 1:
        case 2:
            return 1.0f;
        case 3:
            return 1.166f;
        case 4:
            return 1.3f;
        case 5:
        default:
            return 1.4f;
    }
}
} // namespace XP

inline std::vector<std::string_view> Tokenize(std::string_view str, char sep, bool keepEmpty)
{
    std::vector<std::string_view> tokens;
    std::size_t start = 0;
    for (std::size_t end = str.find(sep); end != std::string_view::npos; end = str.find(sep, start))
    {
        if (keepEmpty || (start < end))
            tokens.push_back(str.substr(start, end - start));
        start = end + 1;
    }
    if (keepEmpty || (start < str.length()))
        tokens.push_back(str.substr(start));
    return tokens;
}
} // namespace Acore

// ------------------------------------------------------------------------------------------------ spells
struct SpellEffectInfo
{
    SpellEffects Effect = SPELL_EFFECT_NONE;
    AuraType ApplyAuraName = SPELL_AURA_NONE;

    [[nodiscard]] bool IsAura(AuraType aura) const { return Effect == SPELL_EFFECT_APPLY_AURA && ApplyAuraName == aura; }
};

class SpellInfo
{
public:
    uint32 Id = 0;
    uint32 AttributesEx3 = 0;
    std::array<SpellEffectInfo, 3> Effects{};

    [[nodiscard]] std::array<SpellEffectInfo, 3> const& GetEffects() const { return Effects; }
    [[nodiscard]] bool HasAttribute(SpellAttr3 attribute) const { return (AttributesEx3 & attribute) != 0; }
    [[nodiscard]] bool HasAura(AuraType aura) const
    {
        return std::any_of(Effects.begin(), Effects.end(), [aura](SpellEffectInfo const& e) { return e.IsAura(aura); });
    }
};

// ------------------------------------------------------------------------------------------------ maps
class Creature;

class Map
{
public:
    enum class Kind { World, Dungeon, Raid, Battleground, Arena };

    Map(uint32 id, uint32 instanceId, Kind kind) : _id(id), _instanceId(instanceId), _kind(kind) { }

    [[nodiscard]] uint32 GetId() const { return _id; }
    [[nodiscard]] uint32 GetInstanceId() const { return _instanceId; }
    // Core: MapEntry::IsWorldMap() is map_type == MAP_COMMON; dungeons/raids/BGs/arenas are not.
    [[nodiscard]] bool IsWorldMap() const { return _kind == Kind::World; }
    [[nodiscard]] bool IsDungeon() const { return _kind == Kind::Dungeon || _kind == Kind::Raid; }
    [[nodiscard]] bool IsRaid() const { return _kind == Kind::Raid; }
    [[nodiscard]] bool IsBattlegroundOrArena() const { return _kind == Kind::Battleground || _kind == Kind::Arena; }

    Creature* GetCreature(ObjectGuid const& guid)
    {
        auto const itr = _creatures.find(guid);
        return itr == _creatures.end() ? nullptr : itr->second;
    }

    void Register(ObjectGuid guid, Creature* creature) { _creatures[guid] = creature; }
    void Unregister(ObjectGuid guid) { _creatures.erase(guid); }

private:
    uint32 _id;
    uint32 _instanceId;
    Kind _kind;
    std::unordered_map<ObjectGuid, Creature*> _creatures;
};

namespace fakecore
{
class MapMgr
{
public:
    Map* FindMap(uint32 mapId, uint32 instanceId) const
    {
        for (Map* map : maps)
            if (map->GetId() == mapId && map->GetInstanceId() == instanceId)
                return map;
        return nullptr;
    }

    std::vector<Map*> maps;
};

inline MapMgr& TheMapMgr()
{
    static MapMgr mgr;
    return mgr;
}

inline uint64 NextGuid()
{
    static std::atomic<uint64> counter{ 1 };
    return counter++;
}
} // namespace fakecore

#define sMapMgr (&::fakecore::TheMapMgr())

// ------------------------------------------------------------------------------------------------ objects
struct Position
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

class WorldObject
{
public:
    virtual ~WorldObject() = default;

    [[nodiscard]] ObjectGuid GetGUID() const { return _guid; }
    [[nodiscard]] Map* GetMap() const { return _map; }
    [[nodiscard]] Map* FindMap() const { return _map; }
    [[nodiscard]] uint32 GetMapId() const { return _map ? _map->GetId() : 0; }
    [[nodiscard]] uint32 GetInstanceId() const { return _map ? _map->GetInstanceId() : 0; }
    [[nodiscard]] uint32 GetZoneId() const { return zoneId; }
    [[nodiscard]] bool IsInWorld() const { return inWorld; }
    [[nodiscard]] float GetObjectSize() const { return objectSize; }

    // Core Object.cpp: IsInWorld() && obj->IsInWorld() && same map.
    [[nodiscard]] bool IsInMap(WorldObject const* obj) const
    {
        return obj && IsInWorld() && obj->IsInWorld() && FindMap() == obj->FindMap();
    }

    [[nodiscard]] bool InSamePhase(WorldObject const* obj) const { return (phaseMask & obj->phaseMask) != 0; }

    [[nodiscard]] float GetDistance(WorldObject const* obj) const
    {
        float const dx = pos.x - obj->pos.x;
        float const dy = pos.y - obj->pos.y;
        float const dz = pos.z - obj->pos.z;
        float const d = std::sqrt(dx * dx + dy * dy + dz * dz) - GetObjectSize() - obj->GetObjectSize();
        return d > 0.0f ? d : 0.0f;
    }

    // Core Object.cpp: obj && IsInMap(obj) && InSamePhase(obj) && _IsWithinDist (strict '<', radii added).
    [[nodiscard]] bool IsWithinDistInMap(WorldObject const* obj, float dist2compare, bool is3D = true,
        bool incOwnRadius = true, bool incTargetRadius = true) const
    {
        if (!obj || !IsInMap(obj) || !InSamePhase(obj))
            return false;

        float maxdist = dist2compare;
        if (incOwnRadius)
            maxdist += GetObjectSize();
        if (incTargetRadius)
            maxdist += obj->GetObjectSize();

        float const dx = pos.x - obj->pos.x;
        float const dy = pos.y - obj->pos.y;
        float distsq = dx * dx + dy * dy;
        if (is3D)
        {
            float const dz = pos.z - obj->pos.z;
            distsq += dz * dz;
        }
        return distsq < maxdist * maxdist;
    }

    // Test wiring.
    void PlaceInMap(Map* map, Position p)
    {
        _map = map;
        pos = p;
        inWorld = map != nullptr;
    }

    Position pos;
    uint32 zoneId = 0;
    uint32 phaseMask = 1;
    float objectSize = 0.0f;
    bool inWorld = false;

protected:
    WorldObject() : _guid(fakecore::NextGuid()) { }

    ObjectGuid _guid;
    Map* _map = nullptr;
};

class Unit : public WorldObject
{
public:
    [[nodiscard]] virtual Creature* ToCreature() { return nullptr; }
    [[nodiscard]] virtual Player* ToPlayer() { return nullptr; }

    [[nodiscard]] bool IsAlive() const { return alive; }
    [[nodiscard]] bool IsInCombat() const { return inCombat; }
    [[nodiscard]] bool HasUnitFlag(UnitFlags flag) const { return (unitFlags & flag) != 0; }
    [[nodiscard]] bool IsControlledByPlayer() const { return controlledByPlayer; }
    [[nodiscard]] bool IsCharmed() const { return charmed; }
    [[nodiscard]] bool IsFriendlyTo(Unit const* unit) const { return unit && unit->faction == faction; }

    [[nodiscard]] Player* GetCharmerOrOwnerPlayerOrPlayerItself() const;

    // Health model mirrors Unit::GetTotalAuraModValue/UpdateMaxHealth: (base*basePct + total)*totalPct.
    [[nodiscard]] uint32 GetHealth() const { return _health; }
    [[nodiscard]] uint32 GetMaxHealth() const { return _maxHealth; }
    [[nodiscard]] uint32 GetCreateHealth() const { return _createHealth; }
    void SetHealth(uint32 value) { _health = std::min(value, _maxHealth); }
    void SetMaxHealth(uint32 value) { _maxHealth = value; _health = std::min(_health, _maxHealth); }
    void SetCreateHealth(uint32 value) { _createHealth = value; }

    [[nodiscard]] float GetFlatModifierValue(UnitMods mod, UnitModifierFlatType type) const
    {
        (void)mod;
        return type == BASE_VALUE ? _baseFlat : _totalFlat;
    }

    void SetStatFlatModifier(UnitMods mod, UnitModifierFlatType type, float value)
    {
        (void)mod;
        (type == BASE_VALUE ? _baseFlat : _totalFlat) = value;
    }

    void UpdateMaxHealth()
    {
        double const value = (double(_baseFlat) * basePct + double(_totalFlat)) * totalPct;
        _maxHealth = value <= 0.0 ? 1u : static_cast<uint32>(value);
        _health = std::min(_health, _maxHealth);
    }

    bool alive = true;
    bool inCombat = false;
    bool controlledByPlayer = false;
    bool charmed = false;
    uint32 unitFlags = 0;
    uint32 faction = 0;
    float basePct = 1.0f;
    float totalPct = 1.0f;
    Player* ownerPlayer = nullptr; // charmer/owner player, if any

protected:
    uint32 _health = 1;
    uint32 _maxHealth = 1;
    uint32 _createHealth = 0;
    float _baseFlat = 0.0f;
    float _totalFlat = 0.0f;
};

class Corpse : public WorldObject
{
};

class Player final : public Unit
{
public:
    Player() { faction = 1; }

    [[nodiscard]] Player* ToPlayer() override { return this; }
    [[nodiscard]] Group* GetGroup() const { return group; }
    [[nodiscard]] uint8 GetLevel() const { return level; }
    [[nodiscard]] WorldObject const* GetCorpse() const { return corpse.inWorld ? &corpse : nullptr; }

    // Copy of core Player::IsAtGroupRewardDistance (corpse position for dead, unreleased players).
    [[nodiscard]] bool IsAtGroupRewardDistance(WorldObject const* pRewardSource) const
    {
        WorldObject const* player = GetCorpse();
        if (!player || IsAlive())
            player = this;

        if (!pRewardSource || !player->IsInMap(pRewardSource))
            return false;

        if (pRewardSource->GetMap()->IsDungeon())
            return true;

        return pRewardSource->GetDistance(player) <= sWorld->getFloatConfig(CONFIG_GROUP_XP_DISTANCE);
    }

    Group* group = nullptr;
    uint8 level = 60;
    Corpse corpse;
    bool isPlayerbot = false; // test metadata only; the module must not depend on it
};

inline Player* Unit::GetCharmerOrOwnerPlayerOrPlayerItself() const
{
    if (ownerPlayer)
        return ownerPlayer;
    return const_cast<Unit*>(this)->ToPlayer();
}

struct CreatureTemplate
{
    uint32 Entry = 0;
    uint32 rank = CREATURE_ELITE_NORMAL;
};

class Creature final : public Unit
{
public:
    explicit Creature(CreatureTemplate const* tmpl) : _template(tmpl) { faction = 14; }
    ~Creature() override { if (_map) _map->Unregister(_guid); }

    [[nodiscard]] Creature* ToCreature() override { return this; }
    [[nodiscard]] CreatureTemplate const* GetCreatureTemplate() const { return _template; }
    [[nodiscard]] uint32 GetEntry() const { return _template ? _template->Entry : 0; }
    [[nodiscard]] Player* GetLootRecipient() const { return lootRecipient; }

    [[nodiscard]] bool IsPet() const { return isPet; }
    [[nodiscard]] bool IsGuardian() const { return isGuardian; }
    [[nodiscard]] bool IsTotem() const { return isTotem; }
    [[nodiscard]] bool IsSummon() const { return isSummon; }
    [[nodiscard]] bool IsGuard() const { return isGuard; }
    [[nodiscard]] bool IsCivilian() const { return isCivilian; }
    [[nodiscard]] bool IsCritter() const { return isCritter; }
    [[nodiscard]] bool IsInEvadeMode() const { return inEvade; }

    // Core Creature::SelectLevel: create health and BASE_VALUE flat both carry the native rank rate.
    void SpawnWithHealth(Map* map, Position p, uint32 rawHealth, float nativeRate)
    {
        PlaceInMap(map, p);
        if (map)
            map->Register(_guid, this);
        uint32 const rated = static_cast<uint32>(double(rawHealth) * nativeRate);
        SetCreateHealth(rated);
        SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(rated));
        UpdateMaxHealth();
        SetHealth(GetMaxHealth());
    }

    void RemoveFromMap()
    {
        if (_map)
            _map->Unregister(_guid);
        inWorld = false;
    }

    Player* lootRecipient = nullptr;
    bool isPet = false;
    bool isGuardian = false;
    bool isTotem = false;
    bool isSummon = false;
    bool isGuard = false;
    bool isCivilian = false;
    bool isCritter = false;
    bool inEvade = false;

private:
    CreatureTemplate const* _template;
};

// ------------------------------------------------------------------------------------------------ groups
class GroupReference
{
public:
    GroupReference(Player* source) : _source(source) { }
    [[nodiscard]] Player* GetSource() const { return _source; }
    [[nodiscard]] GroupReference* next() const { return _next; }

    Player* _source;
    GroupReference* _next = nullptr;
};

class Group
{
public:
    [[nodiscard]] GroupReference* GetFirstMember() const { return _refs.empty() ? nullptr : _refs.front().get(); }
    [[nodiscard]] bool isRaidGroup() const { return raid; }

    void AddMember(Player* player)
    {
        _refs.push_back(std::make_unique<GroupReference>(player));
        Relink();
        player->group = this;
    }

    void RemoveMember(Player* player)
    {
        _refs.erase(std::remove_if(_refs.begin(), _refs.end(),
            [player](auto const& r) { return r->_source == player; }), _refs.end());
        Relink();
        if (player->group == this)
            player->group = nullptr;
    }

    // Offline member: the reference stays but has no live source (the module must skip it).
    void SetOffline(Player* player)
    {
        for (auto& r : _refs)
            if (r->_source == player)
                r->_source = nullptr;
    }

    bool raid = false;

private:
    void Relink()
    {
        for (std::size_t i = 0; i < _refs.size(); ++i)
            _refs[i]->_next = i + 1 < _refs.size() ? _refs[i + 1].get() : nullptr;
    }

    std::vector<std::unique_ptr<GroupReference>> _refs;
};
