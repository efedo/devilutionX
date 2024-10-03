/**
 * @file monster.h
 *
 * Interface of monster functionality, AI, actions, spawning, loading, etc.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <functional>

#include <function_ref.hpp>

#include "engine.h"
#include "engine/actor_position.hpp"
#include "engine/animationinfo.h"
#include "engine/clx_sprite.hpp"
#include "engine/point.hpp"
#include "engine/sound.h"
#include "engine/world_tile.hpp"
#include "init.h"
#include "misdat.h"
#include "monstdat.h"
#include "spelldat.h"
#include "textdat.h"
#include "utils/language.h"

namespace devilution {

struct Missile;
struct Player;
struct CMonster;

constexpr size_t MaxMonsters = 200;
constexpr size_t MaxLvlMTypes = 24;

enum monster_flag : uint16_t {
	// clang-format off
	MFLAG_HIDDEN          = 1 << 0,
	MFLAG_LOCK_ANIMATION  = 1 << 1,
	MFLAG_ALLOW_SPECIAL   = 1 << 2,
	MFLAG_TARGETS_MONSTER = 1 << 4,
	MFLAG_GOLEM           = 1 << 5,
	MFLAG_QUEST_COMPLETE  = 1 << 6,
	MFLAG_KNOCKBACK       = 1 << 7,
	MFLAG_SEARCH          = 1 << 8,
	MFLAG_CAN_OPEN_DOOR   = 1 << 9,
	MFLAG_NO_ENEMY        = 1 << 10,
	MFLAG_BERSERK         = 1 << 11,
	MFLAG_NOLIFESTEAL     = 1 << 12,
	// clang-format on
};

/** Indexes from UniqueMonstersData array for special unique monsters (usually quest related) */
enum class UniqueMonsterType : uint8_t {
	Garbud,
	SkeletonKing,
	Zhar,
	SnotSpill,
	Lazarus,
	RedVex,
	BlackJade,
	Lachdan,
	WarlordOfBlood,
	Butcher,
	HorkDemon,
	Defiler,
	NaKrul,
	None = static_cast<uint8_t>(-1),
};

enum class MonsterMode : uint8_t {
	Stand,
	MoveNorthwards, /** Movement towards N, NW, or NE */
	MoveSouthwards, /** Movement towards S, SW, or SE */
	MoveSideways, /** Movement towards W or E */
	MeleeAttack,
	HitRecovery,
	Death,
	SpecialMeleeAttack,
	FadeIn,
	FadeOut,
	RangedAttack,
	SpecialStand,
	SpecialRangedAttack,
	Delay,
	Charge,
	Petrified,
	Heal,
	Talk,
};

inline bool IsMonsterModeMove(MonsterMode mode)
{
	switch (mode) {
	case MonsterMode::MoveNorthwards:
	case MonsterMode::MoveSouthwards:
	case MonsterMode::MoveSideways:
		return true;
	default:
		return false;
	}
}

enum class MonsterGraphic : uint8_t {
	Stand,
	Walk,
	Attack,
	GotHit,
	Death,
	Special,
};

enum class MonsterGoal : uint8_t {
	None,
	Normal,
	Retreat,
	Healing,
	Move,
	Attack,
	Inquiring,
	Talking,
};

enum placeflag : uint8_t {
	// clang-format off
	PLACE_SCATTER = 1 << 0,
	PLACE_SPECIAL = 1 << 1,
	PLACE_UNIQUE  = 1 << 2,
	// clang-format on
};

/** @brief Defines the relation of the monster to a monster pack.
 *        If value is different from Individual Monster, the leader must also be set */
enum class LeaderRelation : uint8_t {
	None,
	Leashed, /** @brief Minion that sticks to the leader */
	Separated, 	/** @brief Minion that was separated from the leader and acts individually until it reaches the leader again */
};

struct AnimStruct {
	/** @brief Sprite lists for each of the 8 directions. */
	OptionalClxSpriteListOrSheet sprites;

	[[nodiscard]] OptionalClxSpriteList spritesForDirection(Direction direction) const
	{
		if (!sprites)
			return std::nullopt;
		return sprites->isSheet() ? (*sprites).sheet()[static_cast<size_t>(direction)] : (*sprites).list();
	}

	uint16_t width;
	int8_t frames;
	int8_t rate;
};

enum class MonsterSound : uint8_t {
	Attack,
	Hit,
	Death,
	Special
};

struct MonsterSpritesData {
	static constexpr size_t MaxAnims = 6;
	std::unique_ptr<std::byte[]> data;
	std::array<uint32_t, MaxAnims + 1> offsets;
};

//extern CMonster LevelMonsterTypes[MaxLvlMTypes];

struct Monster { // note: missing field _mAFNum
	std::unique_ptr<uint8_t[]> uniqueMonsterTRN;
	AnimationInfo animInfo; /** @brief Contains information for current animation */
	int maxHitPoints;
	int hitPoints;
	uint32_t flags;
	uint32_t rndItemSeed; /** Seed used to determine item drops on death */
	uint32_t aiSeed; /** Seed used to determine AI behaviour/sync sounds in multiplayer games? */
	uint16_t toHit;
	uint16_t resistance;
	_speech_id talkMsg;
	int16_t goalVar1; /** @brief Specifies monster's behaviour regarding moving and changing goals. */
	int8_t goalVar2; /** @brief Specifies turning direction for @p RoundWalk in most cases.
	                  * Used in custom way by @p FallenAi, @p SnakeAi, @p M_FallenFear and @p FallenAi. */
	int8_t goalVar3; /** @brief Controls monster's behaviour regarding special actions.
					  * Used only by @p ScavengerAi and @p MegaAi. */
	int16_t var1;
	int16_t var2;
	int8_t var3;

	ActorPosition position;
	MonsterGoal goal; /** Specifies current goal of the monster */
	WorldTilePosition enemyPosition; /** Usually corresponds to the enemy's future position */
	uint8_t levelType;
	MonsterMode mode;
	uint8_t pathCount;
	Direction direction; /** Direction faced by monster (direction enum) */
	uint8_t enemy; /** The current target of the monster. An index in to either the player or monster array based on the _meflag value. */
	bool isInvalid;
	MonsterAIID ai;
	uint8_t intelligence; 	/** @brief Specifies monster's behaviour across various actions.
							 * Generally, when monster thinks it decides what to do based on this value, among other things.
							 * Higher values should result in more aggressive behaviour (e.g. some monsters use this to calculate the @p AiDelay). */
	uint8_t activeForTicks; /** Stores information for how many ticks the monster will remain active */
	UniqueMonsterType uniqueType;
	uint8_t uniqTrans;
	int8_t corpseId;
	int8_t whoHit;
	uint8_t minDamage;
	uint8_t maxDamage;
	uint8_t minDamageSpecial;
	uint8_t maxDamageSpecial;
	uint8_t armorClass;
	uint8_t leader;
	LeaderRelation leaderRelation;
	uint8_t packSize;
	int8_t lightId;

	static constexpr uint8_t NoLeader = -1;

	void InitMonster(Direction rd, size_t typeIndex, Point position);

	void changeAnimationData(MonsterGraphic graphic, Direction desiredDirection); // Sets the current cell sprite to match the desired desired Direction and animation sequenc
	void changeAnimationData(MonsterGraphic graphic); // Sets the current cell sprite to match the desired animation sequence
	void checkStandAnimationIsLoaded(Direction dir); // Check if correct stand Animation loaded. Needed when direction changed.
	void petrify(); // Sets mode to MonsterMode::Petrified

	const CMonster &type() const;
	const MonsterData &data() const;

	std::string_view name() const; // Returns monster's name
	unsigned int exp(_difficulty difficulty) const; // Calculates monster's experience points.
	unsigned int toHitSpecial(_difficulty difficulty) const; // Calculates monster's chance to hit with special attack.
	unsigned int level(_difficulty difficulty) const; // Calculates monster's level.

	[[nodiscard]] size_t getId() const; // Returns the network identifier for this monster

	[[nodiscard]] Monster *getLeader() const;
	void setLeader(const Monster *leader);

	[[nodiscard]] bool hasLeashedMinions() const
	{
		return isUnique() && UniqueMonstersData[static_cast<size_t>(uniqueType)].monsterPack == UniqueMonsterPack::Leashed;
	}

	[[nodiscard]] unsigned distanceToEnemy() const; // Calculates the distance in tiles between this monster and its current target
	[[nodiscard]] bool isWalking() const; // Is the monster currently walking?
	[[nodiscard]] bool isImmune(MissileID mitype, DamageType missileElement) const;
	[[nodiscard]] bool isResistant(MissileID mitype, DamageType missileElement) const;
	[[nodiscard]] bool isPlayerMinion() const; // Is this a player's golem?

	bool isPossibleToHit() const;
	void tag(const Player &tagger);

	[[nodiscard]] bool isUnique() const
	{
		return uniqueType != UniqueMonsterType::None;
	}

	bool tryLiftGargoyle();

	/**
	 * @brief Gets the visual/shown monster mode.
	 *
	 * When a monster is petrified it's monster mode is changed to MonsterMode::Petrified.
	 * But for graphics and rendering we show the old/real mode.
	 */
	[[nodiscard]] MonsterMode getVisualMonsterMode() const;

	[[nodiscard]] Displacement getRenderingOffset(const ClxSprite sprite) const
	{
		Displacement offset = { -CalculateWidth2(sprite.width()), 0 };
		if (isWalking())
			offset += GetOffsetForWalking(animInfo, direction);
		return offset;
	}

	/**
	 * @brief Sets a tile/dMonster to be occupied by the monster
	 * @param position tile to update
	 * @param isMoving specifies whether the monster is moving or not (true/moving results in a negative index in dMonster)
	 */
	void occupyTile(Point position, bool isMoving) const;

	void applyDamage(DamageType damageType, int damage); // was ApplyMonsterDamage
	bool isTalker() const; // was M_Talker
	void startStand(Direction md = Direction::NoDirection); // was M_StartStand
	void clearSquares() const; // was M_ClearSquares	
	void getKnockback(WorldTilePosition attackerStartPos); // was M_GetKnockback
	void startHit(int dam); // was M_StartHit
	void startHit(const Player &player, int dam); // was M_StartHit

	bool walk(); // was MonsterWalk
	bool walk(Direction md); // was MonsterWalk

	void startDeath(const Player &player, bool sendmsg); // was StartMonsterDeath
	void death(Direction md, bool sendmsg); // was MonsterDeath
	void startKill(const Player &player); // was M_StartKill
	void syncStartKill(Point position, const Player &player); // was M_SyncStartKill

	void death();

	bool dirOK(Direction mdir) const; // was DirOK

	void syncAnim(); // was SyncMonsterAnim
	void playEffect(MonsterSound mode); // was PlayEffect

	bool isTileAvailable(Point position) const; /** @brief Check that the given tile is available to the monster */  // was IsTileAvailable
	bool canTalkTo() const; // was CanTalkToMonst
	void talkTo(Player &player); // was TalktoMonster

	void spawnGolem(Player &player,Point position, Missile &missile); // was SpawnGolem

	//private:
	bool randomWalk(Direction md);
	void updateRelations() const; // was M_UpdateRelations
	void ClearMVars()
};

// Leaving outside class for now due to Ai function pointers
void GolumAi(Monster &monster);

// tmp
int encode_enemy(Monster &monster);
void decode_enemy(Monster &monster, int enemyId);

//extern size_t LevelMonsterTypeCount;
extern Monster Monsters[MaxMonsters];  // eftodo: move to monster manager
extern unsigned ActiveMonsters[MaxMonsters];
//extern size_t ActiveMonsterCount;
extern int MonsterKillCounts[NUM_MTYPES];
extern bool sgbSaveSoundOn;

void PrepareUniqueMonst(Monster &monster, UniqueMonsterType monsterType, size_t miniontype, int bosspacksize, const UniqueMonsterData &uniqueMonsterData);

void WeakenNaKrul();
void InitGolems();
void InitMonsters();

//Monster *AddMonster(Point position, Direction dir, size_t mtype, bool inMap);
/**
 * @brief Spawns a new monsters (dynamically/not on level load).
 * The command is only executed for the level owner, to prevent desyncs in multiplayer.
 * The level owner sends a CMD_SPAWNMONSTER-message to the other players.
 */
void SpawnMonster(Point position, Direction dir, size_t typeIndex, bool startSpecialStand = false);
/**
 * @brief Loads data for a dynamically spawned monster when entering a level in multiplayer.
 */
void LoadDeltaSpawnedMonster(size_t typeIndex, size_t monsterId, uint32_t seed);
/**
 * @brief Initialize a spanwed monster (from a network message or from SpawnMonster-function).
 */
void InitializeSpawnedMonster(Point position, Direction dir, size_t typeIndex, size_t monsterId, uint32_t seed);

void AddDoppelganger(Monster &monster);
void KillMyGolem();

void DoEnding();
void PrepDoEnding();

void DeleteMonsterList();
void ProcessMonsters();


bool PosOkMissile(Point position);
bool LineClearMissile(Point startPoint, Point endPoint);
bool LineClear(tl::function_ref<bool(Point)> clear, Point startPoint, Point endPoint);

void M_FallenFear(Point position);
void PrintMonstHistory(int mt);
void PrintUniqueHistory();

void MissToMonst(Missile &missile, Point position);

Monster *FindMonsterAtPosition(Point position, bool ignoreMovingMonsters = false);
Monster *FindUniqueMonster(UniqueMonsterType monsterType);


bool IsSkel(_monster_id mt);
bool IsGoat(_monster_id mt);

/**
 * @brief Reveals a monster that was hiding in a container
 * @param monster instance returned from a previous call to PreSpawnSkeleton
 * @param position tile to try spawn the monster at, neighboring tiles will be used as a fallback
 */
void ActivateSkeleton(Monster &monster, Point position);
Monster *PreSpawnSkeleton();

} // namespace devilution
