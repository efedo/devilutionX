/**
 * @file monster.cpp
 *
 * Implementation of monster functionality, AI, actions, spawning, loading, etc.
 */
#include "monster.h"
#include "monster_beastiary.h"
#include "monster_manager.h"

#include <climits>
#include <cmath>
#include <cstdint>

#include <algorithm>
#include <array>
#include <numeric>
#include <string_view>

#include <fmt/core.h>
#include <fmt/format.h>

#include "control.h"
#include "crawl.hpp"
#include "cursor.h"
#include "dead.h"
#include "engine/load_cl2.hpp"
#include "engine/load_file.hpp"
#include "engine/points_in_rectangle_range.hpp"
#include "engine/random.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/sound_position.hpp"
#include "engine/world_tile.hpp"
#include "init.h"
#include "levels/crypt.h"
#include "levels/drlg_l4.h"
#include "levels/themes.h"
#include "levels/trigs.h"
#include "lighting.h"
#include "minitext.h"
#include "missiles.h"
#include "movie.h"
#include "options.h"
#include "qol/floatingnumbers.h"
#include "spelldat.h"
#include "storm/storm_net.hpp"
#include "towners.h"
#include "utils/attributes.h"
#include "utils/cl2_to_clx.hpp"
#include "utils/file_name_generator.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/static_vector.hpp"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

#ifdef _DEBUG
#include "debug.h"
#endif

namespace devilution {

//CMonster LevelMonsterTypes[MaxLvlMTypes];
//size_t LevelMonsterTypeCount;
//Monster Monsters[MaxMonsters];
//unsigned ActiveMonsters[MaxMonsters];
//size_t MonsterManager.ActiveMonsterCount;
/** Tracks the total number of monsters killed per monster_id. */
int MonsterKillCounts[NUM_MTYPES];
bool sgbSaveSoundOn;

const std::array<_monster_id, 12> SkeletonTypes {
	MT_WSKELAX,
	MT_TSKELAX,
	MT_RSKELAX,
	MT_XSKELAX,
	MT_WSKELBW,
	MT_TSKELBW,
	MT_RSKELBW,
	MT_XSKELBW,
	MT_WSKELSD,
	MT_TSKELSD,
	MT_RSKELSD,
	MT_XSKELSD,
};


namespace {

constexpr int NightmareToHitBonus = 85;
constexpr int HellToHitBonus = 120;

constexpr int NightmareAcBonus = 50;
constexpr int HellAcBonus = 80;

void NewMonsterAnim(Monster &monster, MonsterGraphic graphic, Direction md, AnimationDistributionFlags flags = AnimationDistributionFlags::None, int8_t numSkippedFrames = 0, int8_t distributeFramesBeforeFrame = 0)
{
	const auto &animData = monster.type().getAnimData(graphic);
	monster.animInfo.setNewAnimation(animData.spritesForDirection(md), animData.frames, animData.rate, flags, numSkippedFrames, distributeFramesBeforeFrame);
	monster.flags &= ~(MFLAG_LOCK_ANIMATION | MFLAG_ALLOW_SPECIAL);
	monster.direction = md;
}

// eftodo
void StartMonsterGotHit(Monster &monster)
{
	if (monster.type().type != MT_GOLEM) {
		auto animationFlags = gGameLogicStep < GameLogicStep::ProcessMonsters ? AnimationDistributionFlags::ProcessAnimationPending : AnimationDistributionFlags::None;
		int8_t numSkippedFrames = (gbIsHellfire && monster.type().type == MT_DIABLO) ? 4 : 0;
		NewMonsterAnim(monster, MonsterGraphic::GotHit, monster.direction, animationFlags, numSkippedFrames);
		monster.mode = MonsterMode::HitRecovery;
	}
	monster.position.tile = monster.position.old;
	monster.position.future = monster.position.old;
	monster.clearSquares();
	monster.occupyTile(monster.position.tile, false);
}

DVL_ALWAYS_INLINE bool IsRanged(Monster &monster)
{
	return IsAnyOf(monster.ai, MonsterAIID::SkeletonRanged, MonsterAIID::GoatRanged, MonsterAIID::Succubus, MonsterAIID::LazarusSuccubus);
}

void UpdateEnemy(Monster &monster)
{
	WorldTilePosition target;
	int menemy = -1;
	int bestDist = -1;
	bool bestsameroom = false;
	const WorldTilePosition position = monster.position.tile;
	const bool isPlayerMinion = monster.isPlayerMinion();
	if (!isPlayerMinion) {
		for (size_t pnum = 0; pnum < Players.size(); pnum++) {
			const Player &player = Players[pnum];
			if (!player.plractive || !player.isOnActiveLevel() || player._pLvlChanging
			    || (((player._pHitPoints >> 6) == 0) && gbIsMultiplayer))
				continue;
			const bool sameroom = (dTransVal[position.x][position.y] == dTransVal[player.position.tile.x][player.position.tile.y]);
			const int dist = position.WalkingDistance(player.position.tile);
			if ((sameroom && !bestsameroom)
			    || ((sameroom || !bestsameroom) && dist < bestDist)
			    || (menemy == -1)) {
				monster.flags &= ~MFLAG_TARGETS_MONSTER;
				menemy = static_cast<int>(pnum);
				target = player.position.future;
				bestDist = dist;
				bestsameroom = sameroom;
			}
		}
	}
	for (size_t i = 0; i < MonsterManager.ActiveMonsterCount; i++) {
		const unsigned monsterId = MonsterManager.ActiveMonsters[i];
		Monster &otherMonster = MonsterManager.Monsters[monsterId];
		if (&otherMonster == &monster)
			continue;
		if ((otherMonster.hitPoints >> 6) <= 0)
			continue;
		if (otherMonster.position.tile == GolemHoldingCell)
			continue;
		if (otherMonster.talkMsg != TEXT_NONE && otherMonster.isTalker())
			continue;
		if (isPlayerMinion && otherMonster.isPlayerMinion()) // prevent golems from fighting each other
			continue;

		const int dist = otherMonster.position.tile.WalkingDistance(position);
		if (((monster.flags & MFLAG_GOLEM) == 0
		        && (monster.flags & MFLAG_BERSERK) == 0
		        && dist >= 2
		        && !IsRanged(monster))
		    || ((monster.flags & MFLAG_GOLEM) == 0
		        && (monster.flags & MFLAG_BERSERK) == 0
		        && (otherMonster.flags & MFLAG_GOLEM) == 0)) {
			continue;
		}
		const bool sameroom = dTransVal[position.x][position.y] == dTransVal[otherMonster.position.tile.x][otherMonster.position.tile.y];
		if ((sameroom && !bestsameroom)
		    || ((sameroom || !bestsameroom) && dist < bestDist)
		    || (menemy == -1)) {
			monster.flags |= MFLAG_TARGETS_MONSTER;
			menemy = static_cast<int>(monsterId);
			target = otherMonster.position.future;
			bestDist = dist;
			bestsameroom = sameroom;
		}
	}
	if (menemy != -1) {
		monster.flags &= ~MFLAG_NO_ENEMY;
		monster.enemy = menemy;
		monster.enemyPosition = target;
	} else {
		monster.flags |= MFLAG_NO_ENEMY;
	}
}

/**
 * @brief Make the AI wait a bit before thinking again
 * @param monster The monster that will wait
 * @param len
 */
void AiDelay(Monster &monster, int len)
{
	if (len <= 0) {
		return;
	}

	if (monster.ai == MonsterAIID::Lazarus) {
		return;
	}

	monster.var2 = len;
	monster.mode = MonsterMode::Delay;
}

/**
 * @brief Get the direction from the monster to its current enemy
 */

// eftodo
Direction GetMonsterDirection(Monster &monster)
{
	return GetDirection(monster.position.tile, monster.enemyPosition);
}

// eftodo
void StartSpecialStand(Monster &monster, Direction md)
{
	NewMonsterAnim(monster, MonsterGraphic::Special, md);
	monster.mode = MonsterMode::SpecialStand;
	monster.position.future = monster.position.tile;
	monster.position.old = monster.position.tile;
}

// eftodo
void WalkInDirection(Monster &monster, Direction endDir)
{
	Point dir = { 0, 0 };
	dir += endDir;

	const auto fx = static_cast<WorldTileCoord>(monster.position.tile.x + dir.x);
	const auto fy = static_cast<WorldTileCoord>(monster.position.tile.y + dir.y);

	MonsterMode mode;
	switch (endDir) {
	case Direction::NorthWest:
	case Direction::North:
	case Direction::NorthEast:
		mode = MonsterMode::MoveNorthwards;
		break;
	case Direction::West:
	case Direction::East:
		mode = MonsterMode::MoveSideways;
		break;
	case Direction::SouthWest:
	case Direction::South:
	case Direction::SouthEast:
		mode = MonsterMode::MoveSouthwards;
		break;
	case Direction::NoDirection:
		DVL_UNREACHABLE();
		break;
	}
	monster.mode = mode;
	monster.position.old = monster.position.tile;
	monster.position.future = { fx, fy };
	monster.occupyTile(monster.position.future, true);
	monster.var1 = dir.x;
	monster.var2 = dir.y;
	monster.var3 = static_cast<int8_t>(endDir);
	NewMonsterAnim(monster, MonsterGraphic::Walk, endDir, AnimationDistributionFlags::ProcessAnimationPending, -1);
}

void StartAttack(Monster &monster)
{
	Direction md = GetMonsterDirection(monster);
	NewMonsterAnim(monster, MonsterGraphic::Attack, md, AnimationDistributionFlags::ProcessAnimationPending);
	monster.mode = MonsterMode::MeleeAttack;
	monster.position.future = monster.position.tile;
	monster.position.old = monster.position.tile;
}

void StartRangedAttack(Monster &monster, MissileID missileType, int dam)
{
	Direction md = GetMonsterDirection(monster);
	NewMonsterAnim(monster, MonsterGraphic::Attack, md, AnimationDistributionFlags::ProcessAnimationPending);
	monster.mode = MonsterMode::RangedAttack;
	monster.var1 = static_cast<int8_t>(missileType);
	monster.var2 = dam;
	monster.position.future = monster.position.tile;
	monster.position.old = monster.position.tile;
}

void StartRangedSpecialAttack(Monster &monster, MissileID missileType, int dam)
{
	Direction md = GetMonsterDirection(monster);
	int8_t distributeFramesBeforeFrame = 0;
	if (monster.ai == MonsterAIID::Mega)
		distributeFramesBeforeFrame = monster.data().animFrameNumSpecial;
	NewMonsterAnim(monster, MonsterGraphic::Special, md, AnimationDistributionFlags::ProcessAnimationPending, 0, distributeFramesBeforeFrame);
	monster.mode = MonsterMode::SpecialRangedAttack;
	monster.var1 = static_cast<int8_t>(missileType);
	monster.var2 = 0;
	monster.var3 = dam;
	monster.position.future = monster.position.tile;
	monster.position.old = monster.position.tile;
}

void StartSpecialAttack(Monster &monster)
{
	Direction md = GetMonsterDirection(monster);
	NewMonsterAnim(monster, MonsterGraphic::Special, md);
	monster.mode = MonsterMode::SpecialMeleeAttack;
	monster.position.future = monster.position.tile;
	monster.position.old = monster.position.tile;
}

void StartEating(Monster &monster)
{
	NewMonsterAnim(monster, MonsterGraphic::Special, monster.direction);
	monster.mode = MonsterMode::SpecialMeleeAttack;
	monster.position.future = monster.position.tile;
	monster.position.old = monster.position.tile;
}

// eftodo
void DiabloDeath(Monster &diablo, bool sendmsg)
{
	PlaySFX(SfxID::DiabloDeath);
	auto &quest = Quests[Q_DIABLO];
	quest._qactive = QUEST_DONE;
	if (sendmsg)
		NetSendCmdQuest(true, quest);
	sgbSaveSoundOn = gbSoundOn;
	gbProcessPlayers = false;
	for (size_t i = 0; i < MonsterManager.ActiveMonsterCount; i++) {
		int monsterId = MonsterManager.ActiveMonsters[i];
		Monster &monster = MonsterManager.Monsters[monsterId];
		if (monster.type().type == MT_DIABLO || diablo.activeForTicks == 0)
			continue;

		NewMonsterAnim(monster, MonsterGraphic::Death, monster.direction);
		monster.mode = MonsterMode::Death;
		monster.var1 = 0;
		monster.position.tile = monster.position.old;
		monster.position.future = monster.position.tile;
		monster.clearSquares();
		monster.occupyTile(monster.position.tile, false);
	}
	AddLight(diablo.position.tile, 8);
	DoVision(diablo.position.tile, 8, MAP_EXP_NONE, true);
	int dist = diablo.position.tile.WalkingDistance(ViewPosition);
	if (dist > 20)
		dist = 20;
	diablo.var3 = ViewPosition.x << 16;
	diablo.position.temp.x = ViewPosition.y << 16;
	diablo.position.temp.y = (int)((diablo.var3 - (diablo.position.tile.x << 16)) / (double)dist);
	if (!gbIsMultiplayer) {
		Player &myPlayer = *MyPlayer;
		myPlayer.pDiabloKillLevel = std::max(myPlayer.pDiabloKillLevel, static_cast<uint8_t>(sgGameInitInfo.nDifficulty + 1));
	}
}

// eftodo
void SpawnLoot(Monster &monster, bool sendmsg)
{
	if (monster.type().type == MT_HORKSPWN) {
		return;
	}

	if (Quests[Q_GARBUD].IsAvailable() && monster.uniqueType == UniqueMonsterType::Garbud) {
		CreateTypeItem(monster.position.tile + Displacement { 1, 1 }, true, ItemType::Mace, IMISC_NONE, sendmsg, false);
	} else if (monster.uniqueType == UniqueMonsterType::Defiler) {
		if (effect_is_playing(SfxID::Defiler8))
			stream_stop();
		SpawnMapOfDoom(monster.position.tile, sendmsg);
		Quests[Q_DEFILER]._qactive = QUEST_DONE;
		NetSendCmdQuest(true, Quests[Q_DEFILER]);
	} else if (monster.uniqueType == UniqueMonsterType::HorkDemon) {
		if (sgGameInitInfo.bTheoQuest != 0) {
			SpawnTheodore(monster.position.tile, sendmsg);
		} else {
			CreateAmulet(monster.position.tile, 13, sendmsg, false);
		}
	} else if (monster.type().type == MT_NAKRUL) {
		SfxID nSFX = IsUberRoomOpened ? SfxID::NaKrul4 : SfxID::NaKrul5;
		if (sgGameInitInfo.bCowQuest != 0)
			nSFX = SfxID::NaKrul6;
		if (effect_is_playing(nSFX))
			stream_stop();
		UberDiabloMonsterIndex = -2;
		CreateMagicWeapon(monster.position.tile, ItemType::Sword, ICURS_GREAT_SWORD, sendmsg, false);
		CreateMagicWeapon(monster.position.tile, ItemType::Staff, ICURS_WAR_STAFF, sendmsg, false);
		CreateMagicWeapon(monster.position.tile, ItemType::Bow, ICURS_LONG_WAR_BOW, sendmsg, false);
		CreateSpellBook(monster.position.tile, SpellID::Apocalypse, sendmsg, false);
	} else if (!monster.isPlayerMinion()) {
		SpawnItem(monster, monster.position.tile, sendmsg);
	}
}

std::optional<Point> GetTeleportTile(const Monster &monster)
{
	int mx = monster.enemyPosition.x;
	int my = monster.enemyPosition.y;
	int rx = PickRandomlyAmong({ -1, 1 });
	int ry = PickRandomlyAmong({ -1, 1 });

	for (int j = -1; j <= 1; j++) {
		for (int k = -1; k < 1; k++) {
			if (j == 0 && k == 0)
				continue;
			int x = mx + rx * j;
			int y = my + ry * k;
			if (!InDungeonBounds({ x, y }) || x == monster.position.tile.x || y == monster.position.tile.y)
				continue;
			if (monster.isTileAvailable({ x, y }))
				return Point { x, y };
		}
	}
	return {};
}

// eftodo
void Teleport(Monster &monster)
{
	if (monster.mode == MonsterMode::Petrified)
		return;

	std::optional<Point> position = GetTeleportTile(monster);
	if (!position)
		return;

	monster.clearSquares();
	dMonster[monster.position.tile.x][monster.position.tile.y] = 0;
	monster.occupyTile(*position, false);
	monster.position.old = *position;
	monster.direction = GetMonsterDirection(monster);
	ChangeLightXY(monster.lightId, *position);
}

// eftodo
bool IsHardHit(Monster &target, unsigned dam)
{
	switch (target.type().type) {
	case MT_SNEAK:
	case MT_STALKER:
	case MT_UNSEEN:
	case MT_ILLWEAV:
		return true;
	default:
		return (dam >> 6) >= target.level(sgGameInitInfo.nDifficulty) + 3;
	}
}

void MonsterHitMonster(Monster &attacker, Monster &target, int dam)
{
	if (IsHardHit(target, dam)) {
		target.direction = Opposite(attacker.direction);
	}

	target.startHit(dam);
}

void StartDeathFromMonster(Monster &attacker, Monster &target)
{
	Direction md = GetDirection(target.position.tile, attacker.position.tile);
	target.death(md, true);

	if (gbIsHellfire)
		attacker.startStand();
}

void StartFadein(Monster &monster, Direction md, bool backwards)
{
	NewMonsterAnim(monster, MonsterGraphic::Special, md);
	monster.mode = MonsterMode::FadeIn;
	monster.position.future = monster.position.tile;
	monster.position.old = monster.position.tile;
	monster.flags &= ~MFLAG_HIDDEN;
	if (backwards) {
		monster.flags |= MFLAG_LOCK_ANIMATION;
		monster.animInfo.currentFrame = monster.animInfo.numberOfFrames - 1;
	}
}

void StartFadeout(Monster &monster, Direction md, bool backwards)
{
	NewMonsterAnim(monster, MonsterGraphic::Special, md);
	monster.mode = MonsterMode::FadeOut;
	monster.position.future = monster.position.tile;
	monster.position.old = monster.position.tile;
	if (backwards) {
		monster.flags |= MFLAG_LOCK_ANIMATION;
		monster.animInfo.currentFrame = monster.animInfo.numberOfFrames - 1;
	}
}

/**
 * @brief Starts the monster healing procedure.
 *
 * The monster will be healed between 1.47% and 25% of its max HP. The healing amount is stored in _mVar1.
 *
 * This is only used by Gargoyles.
 *
 * @param monster The monster that will be healed.
 */
void StartHeal(Monster &monster)
{
	monster.changeAnimationData(MonsterGraphic::Special);
	monster.animInfo.currentFrame = monster.type().getAnimData(MonsterGraphic::Special).frames - 1;
	monster.flags |= MFLAG_LOCK_ANIMATION;
	monster.mode = MonsterMode::Heal;
	monster.var1 = monster.maxHitPoints / (16 * (GenerateRnd(5) + 4));
}

// eftodo
void SyncLightPosition(Monster &monster)
{
	if (monster.lightId == NO_LIGHT)
		return;

	const WorldTileDisplacement offset = monster.isWalking() ? monster.position.CalculateWalkingOffset(monster.direction, monster.animInfo) : WorldTileDisplacement {};
	ChangeLightOffset(monster.lightId, offset.screenToLight());
}

void MonsterIdle(Monster &monster)
{
	if (monster.type().type == MT_GOLEM)
		monster.changeAnimationData(MonsterGraphic::Walk);
	else
		monster.changeAnimationData(MonsterGraphic::Stand);

	if (monster.animInfo.isLastFrame())
		UpdateEnemy(monster);

	if (monster.var2 < std::numeric_limits<int16_t>::max())
		monster.var2++;
}


void MonsterAttackMonster(Monster &attacker, Monster &target, int hper, int mind, int maxd)
{
	if (!target.isPossibleToHit())
		return;

	int hit = GenerateRnd(100);
	if (target.mode == MonsterMode::Petrified)
		hit = 0;
	if (target.tryLiftGargoyle())
		return;
	if (hit >= hper)
		return;

	int dam = RandomIntBetween(mind, maxd) << 6;
	target.applyDamage(DamageType::Physical, dam);

	if (attacker.isPlayerMinion()) {
		size_t playerId = attacker.getId();
		const Player &player = Players[playerId];
		target.tag(player);
	}

	if (target.hitPoints >> 6 <= 0) {
		StartDeathFromMonster(attacker, target);
	} else {
		MonsterHitMonster(attacker, target, dam);
	}

	if (target.activeForTicks == 0) {
		target.activeForTicks = UINT8_MAX;
		target.position.last = attacker.position.tile;
	}
}

int CheckReflect(Monster &monster, Player &player, int dam)
{
	player.wReflections--;
	if (player.wReflections <= 0)
		NetSendCmdParam1(true, CMD_SETREFLECT, 0);
	// reflects 20-30% damage
	int mdam = dam * RandomIntBetween(20, 30, true) / 100;
	monster.applyDamage(DamageType::Physical, mdam);
	if (monster.hitPoints >> 6 <= 0)
		monster.startKill(player);
	else
		monster.startHit(player, mdam);

	return mdam;
}

int GetMinHit()
{
	switch (currlevel) {
	case 16:
		return 30;
	case 15:
		return 25;
	case 14:
		return 20;
	default:
		return 15;
	}
}

void MonsterAttackPlayer(Monster &monster, Player &player, int hit, int minDam, int maxDam)
{
	if (player._pHitPoints >> 6 <= 0 || player._pInvincible || HasAnyOf(player._pSpellFlags, SpellFlag::Etherealize))
		return;
	if (monster.position.tile.WalkingDistance(player.position.tile) >= 2)
		return;

	int hper = GenerateRnd(100);
#ifdef _DEBUG
	if (DebugGodMode)
		hper = 1000;
#endif
	int ac = player.GetArmor();
	if (HasAnyOf(player.pDamAcFlags, ItemSpecialEffectHf::ACAgainstDemons) && monster.data().monsterClass == MonsterClass::Demon)
		ac += 40;
	if (HasAnyOf(player.pDamAcFlags, ItemSpecialEffectHf::ACAgainstUndead) && monster.data().monsterClass == MonsterClass::Undead)
		ac += 20;
	hit += 2 * (monster.level(sgGameInitInfo.nDifficulty) - player.getCharacterLevel())
	    + 30
	    - ac;
	int minhit = GetMinHit();
	hit = std::max(hit, minhit);
	int blkper = 100;
	if ((player._pmode == PM_STAND || player._pmode == PM_ATTACK) && player._pBlockFlag) {
		blkper = GenerateRnd(100);
	}
	int blk = player.GetBlockChance() - (monster.level(sgGameInitInfo.nDifficulty) * 2);
	blk = std::clamp(blk, 0, 100);
	if (hper >= hit)
		return;
	if (blkper < blk) {
		Direction dir = GetDirection(player.position.tile, monster.position.tile);
		StartPlrBlock(player, dir);
		if (&player == MyPlayer && player.wReflections > 0) {
			int dam = GenerateRnd(((maxDam - minDam) << 6) + 1) + (minDam << 6);
			dam = std::max(dam + (player._pIGetHit << 6), 64);
			CheckReflect(monster, player, dam);
		}
		return;
	}
	if (monster.type().type == MT_YZOMBIE && &player == MyPlayer) {
		if (player._pMaxHP > 64) {
			if (player._pMaxHPBase > 64) {
				player._pMaxHP -= 64;
				if (player._pHitPoints > player._pMaxHP) {
					player._pHitPoints = player._pMaxHP;
				}
				player._pMaxHPBase -= 64;
				if (player._pHPBase > player._pMaxHPBase) {
					player._pHPBase = player._pMaxHPBase;
				}
			}
		}
	}
	// New method fixes a bug which caused the maximum possible damage value to be 63/64ths too low.
	int dam = RandomIntBetween(minDam << 6, maxDam << 6);
	dam = std::max(dam + (player._pIGetHit << 6), 64);
	if (&player == MyPlayer) {
		if (player.wReflections > 0) {
			int reflectedDamage = CheckReflect(monster, player, dam);
			dam = std::max(dam - reflectedDamage, 0);
		}
		ApplyPlrDamage(DamageType::Physical, player, 0, 0, dam);
	}

	// Reflect can also kill a monster, so make sure the monster is still alive
	if (HasAnyOf(player._pIFlags, ItemSpecialEffect::Thorns) && monster.mode != MonsterMode::Death) {
		int mdam = (GenerateRnd(3) + 1) << 6;
		monster.applyDamage(DamageType::Physical, mdam);
		if (monster.hitPoints >> 6 <= 0)
			monster.startKill(player);
		else
			monster.startHit(player, mdam);
	}

	if ((monster.flags & MFLAG_NOLIFESTEAL) == 0 && monster.type().type == MT_SKING && gbIsMultiplayer)
		monster.hitPoints += dam;
	if (player._pHitPoints >> 6 <= 0) {
		if (gbIsHellfire)
			monster.startStand();
		return;
	}
	StartPlrHit(player, dam, false);
	if ((monster.flags & MFLAG_KNOCKBACK) != 0) {
		if (player._pmode != PM_GOTHIT)
			StartPlrHit(player, 0, true);

		Point newPosition = player.position.tile + monster.direction;
		if (PosOkPlayer(player, newPosition)) {
			player.position.tile = newPosition;
			FixPlayerLocation(player, player._pdir);
			FixPlrWalkTags(player);
			player.occupyTile(newPosition, false);
			SetPlayerOld(player);
		}
	}
}

void MonsterAttackEnemy(Monster &monster, int hit, int minDam, int maxDam)
{
	if ((monster.flags & MFLAG_TARGETS_MONSTER) != 0)
		MonsterAttackMonster(monster, MonsterManager.Monsters[monster.enemy], hit, minDam, maxDam);
	else
		MonsterAttackPlayer(monster, Players[monster.enemy], hit, minDam, maxDam);
}

bool MonsterAttack(Monster &monster)
{
	if (monster.animInfo.currentFrame == monster.data().animFrameNum - 1) {
		MonsterAttackEnemy(monster, monster.toHit, monster.minDamage, monster.maxDamage);
		if (monster.ai != MonsterAIID::Snake)
			monster.playEffect(MonsterSound::Attack);
	}
	if (IsAnyOf(monster.type().type, MT_NMAGMA, MT_YMAGMA, MT_BMAGMA, MT_WMAGMA) && monster.animInfo.currentFrame == 8) {
		MonsterAttackEnemy(monster, monster.toHit + 10, monster.minDamage - 2, monster.maxDamage - 2);

		monster.playEffect(MonsterSound::Attack);
	}
	if (IsAnyOf(monster.type().type, MT_STORM, MT_RSTORM, MT_STORML, MT_MAEL) && monster.animInfo.currentFrame == 12) {
		MonsterAttackEnemy(monster, monster.toHit - 20, monster.minDamage + 4, monster.maxDamage + 4);

		monster.playEffect(MonsterSound::Attack);
	}
	if (monster.ai == MonsterAIID::Snake && monster.animInfo.currentFrame == 0)
		monster.playEffect(MonsterSound::Attack);
	if (monster.animInfo.isLastFrame()) {
		monster.startStand();
		return true;
	}

	return false;
}

bool MonsterRangedAttack(Monster &monster)
{
	if (monster.animInfo.currentFrame == monster.data().animFrameNum - 1) {
		const auto &missileType = static_cast<MissileID>(monster.var1);
		if (missileType != MissileID::Null) {
			int multimissiles = 1;
			if (missileType == MissileID::ChargedBolt)
				multimissiles = 3;
			for (int mi = 0; mi < multimissiles; mi++) {
				AddMissile(
				    monster.position.tile,
				    monster.enemyPosition,
				    monster.direction,
				    missileType,
				    TARGET_PLAYERS,
				    monster,
				    monster.var2,
				    0);
			}
		}
		monster.playEffect(MonsterSound::Attack);
	}

	if (monster.animInfo.isLastFrame()) {
		monster.startStand();
		return true;
	}

	return false;
}

bool MonsterRangedSpecialAttack(Monster &monster)
{
	if (monster.animInfo.currentFrame == monster.data().animFrameNumSpecial - 1 && monster.animInfo.tickCounterOfCurrentFrame == 0 && (monster.ai != MonsterAIID::Mega || monster.var2 == 0)) {
		if (AddMissile(
		        monster.position.tile,
		        monster.enemyPosition,
		        monster.direction,
		        static_cast<MissileID>(monster.var1),
		        TARGET_PLAYERS,
		        monster,
		        monster.var3,
		        0)
		    != nullptr) {
			monster.playEffect(MonsterSound::Special);
		}
	}

	if (monster.ai == MonsterAIID::Mega && monster.animInfo.currentFrame == monster.data().animFrameNumSpecial - 1) {
		if (monster.var2++ == 0) {
			monster.flags |= MFLAG_ALLOW_SPECIAL;
		} else if (monster.var2 == 15) {
			monster.flags &= ~MFLAG_ALLOW_SPECIAL;
		}
	}

	if (monster.animInfo.isLastFrame()) {
		monster.startStand();
		return true;
	}

	return false;
}

bool MonsterSpecialAttack(Monster &monster)
{
	if (monster.animInfo.currentFrame == monster.data().animFrameNumSpecial - 1) {
		MonsterAttackEnemy(monster, monster.toHitSpecial(sgGameInitInfo.nDifficulty), monster.minDamageSpecial, monster.maxDamageSpecial);
	}

	if (monster.animInfo.isLastFrame()) {
		monster.startStand();
		return true;
	}

	return false;
}

bool MonsterFadein(Monster &monster)
{
	if (((monster.flags & MFLAG_LOCK_ANIMATION) == 0 || monster.animInfo.currentFrame != 0)
	    && ((monster.flags & MFLAG_LOCK_ANIMATION) != 0 || monster.animInfo.currentFrame != monster.animInfo.numberOfFrames - 1)) {
		return false;
	}

	monster.startStand();
	monster.flags &= ~MFLAG_LOCK_ANIMATION;

	return true;
}

bool MonsterFadeout(Monster &monster)
{
	if (((monster.flags & MFLAG_LOCK_ANIMATION) == 0 || monster.animInfo.currentFrame != 0)
	    && ((monster.flags & MFLAG_LOCK_ANIMATION) != 0 || monster.animInfo.currentFrame != monster.animInfo.numberOfFrames - 1)) {
		return false;
	}

	monster.flags &= ~MFLAG_LOCK_ANIMATION;
	monster.flags |= MFLAG_HIDDEN;

	monster.startStand();

	return true;
}

/**
 * @brief Applies the healing effect on the monster.
 *
 * This is triggered by StartHeal()
 *
 * @param monster The monster that will be healed.
 * @return
 */
void MonsterHeal(Monster &monster)
{
	if (monster.animInfo.currentFrame == 0) {
		monster.flags &= ~MFLAG_LOCK_ANIMATION;
		monster.flags |= MFLAG_ALLOW_SPECIAL;
		if (monster.var1 + monster.hitPoints < monster.maxHitPoints) {
			monster.hitPoints = monster.var1 + monster.hitPoints;
		} else {
			monster.hitPoints = monster.maxHitPoints;
			monster.flags &= ~MFLAG_ALLOW_SPECIAL;
			monster.mode = MonsterMode::SpecialMeleeAttack;
		}
	}
}

void MonsterTalk(Monster &monster)
{
	monster.startStand();
	monster.goal = MonsterGoal::Talking;
	if (effect_is_playing(Speeches[monster.talkMsg].sfxnr))
		return;
	InitQTextMsg(monster.talkMsg);
	if (monster.uniqueType == UniqueMonsterType::SnotSpill) {
		if (monster.talkMsg == TEXT_BANNER10 && (monster.flags & MFLAG_QUEST_COMPLETE) == 0) {
			ObjChangeMap(SetPiece.position.x, SetPiece.position.y, SetPiece.position.x + (SetPiece.size.width / 2) + 2, SetPiece.position.y + (SetPiece.size.height / 2) - 2);
			auto tren = TransVal;
			TransVal = 9;
			DRLG_MRectTrans({ SetPiece.position, WorldTileSize(SetPiece.size.width / 2 + 4, SetPiece.size.height / 2) });
			TransVal = tren;
			Quests[Q_LTBANNER]._qvar1 = 2;
			if (Quests[Q_LTBANNER]._qactive == QUEST_INIT)
				Quests[Q_LTBANNER]._qactive = QUEST_ACTIVE;
			monster.flags |= MFLAG_QUEST_COMPLETE;
			NetSendCmdQuest(true, Quests[Q_LTBANNER]);
		}
		if (Quests[Q_LTBANNER]._qvar1 < 2) {
			app_fatal(StrCat("SS Talk = ", monster.talkMsg, ", Flags = ", monster.flags));
		}
	}
	if (monster.uniqueType == UniqueMonsterType::Lachdan) {
		if (monster.talkMsg == TEXT_VEIL9) {
			Quests[Q_VEIL]._qactive = QUEST_ACTIVE;
			Quests[Q_VEIL]._qlog = true;
			NetSendCmdQuest(true, Quests[Q_VEIL]);
		}
	}
	if (monster.uniqueType == UniqueMonsterType::WarlordOfBlood) {
		Quests[Q_WARLORD]._qvar1 = QS_WARLORD_TALKING;
		NetSendCmdQuest(true, Quests[Q_WARLORD]);
	}
	if (monster.uniqueType == UniqueMonsterType::Lazarus && UseMultiplayerQuests()) {
		Quests[Q_BETRAYER]._qvar1 = 6;
		monster.goal = MonsterGoal::Normal;
		monster.activeForTicks = UINT8_MAX;
		monster.talkMsg = TEXT_NONE;
	}
}

bool MonsterGotHit(Monster &monster)
{
	if (monster.animInfo.isLastFrame()) {
		monster.startStand();

		return true;
	}

	return false;
}

// eftodo
void ReleaseMinions(const Monster &leader)
{
	for (size_t i = 0; i < MonsterManager.ActiveMonsterCount; i++) {
		Monster &minion = MonsterManager.Monsters[MonsterManager.ActiveMonsters[i]];
		if (minion.leaderRelation == LeaderRelation::Leashed && minion.getLeader() == &leader) {
			minion.setLeader(nullptr);
		}
	}
}

// eftodo
void ShrinkLeaderPacksize(const Monster &monster)
{
	if (monster.leaderRelation == LeaderRelation::Leashed) {
		monster.getLeader()->packSize--;
	}
}

// eftodo
bool MonsterSpecialStand(Monster &monster)
{
	if (monster.animInfo.currentFrame == monster.data().animFrameNumSpecial - 1)
		monster.playEffect(MonsterSound::Special);

	if (monster.animInfo.isLastFrame()) {
		monster.startStand();
		return true;
	}

	return false;
}

bool MonsterDelay(Monster &monster)
{
	monster.changeAnimationData(MonsterGraphic::Stand, GetMonsterDirection(monster));
	if (monster.ai == MonsterAIID::Lazarus) {
		if (monster.var2 > 8 || monster.var2 < 0)
			monster.var2 = 8;
	}

	if (monster.var2-- == 0) {
		int oFrame = monster.animInfo.currentFrame;
		monster.startStand();
		monster.animInfo.currentFrame = oFrame;
		return true;
	}

	return false;
}

void MonsterPetrified(Monster &monster)
{
	if (monster.hitPoints <= 0) {
		dMonster[monster.position.tile.x][monster.position.tile.y] = 0;
		monster.isInvalid = true;
	}
}

std::optional<size_t> GetRandomSkeletonTypeIndex()
{
	int32_t typeCount = 0;
	size_t skeletonIndexes[SkeletonTypes.size()];
	for (size_t i = 0; i < Beastiary.LevelMonsterTypeCount; i++) {
		if (IsSkel(Beastiary.LevelMonsterTypes[i].type)) {
			skeletonIndexes[typeCount++] = i;
		}
	}

	if (typeCount == 0) {
		return {};
	}

	const size_t typeIndex = skeletonIndexes[GenerateRnd(typeCount)];
	return typeIndex;
}

Monster *AddSkeleton(Point position, Direction dir, bool inMap)
{
	auto typeIndex = GetRandomSkeletonTypeIndex();
	if (!typeIndex)
		return nullptr;

	return MonsterManager.AddMonster(position, dir, *typeIndex, inMap);
}

bool IsLineNotSolid(Point startPoint, Point endPoint)
{
	return LineClear(IsTileNotSolid, startPoint, endPoint);
}

void FollowTheLeader(Monster &monster)
{
	if (monster.leaderRelation != LeaderRelation::Leashed)
		return;

	Monster *leader = monster.getLeader();
	if (leader == nullptr)
		return;

	if (monster.activeForTicks >= leader->activeForTicks)
		return;

	monster.position.last = leader->position.tile;
	monster.activeForTicks = leader->activeForTicks - 1;
}

void GroupUnity(Monster &monster)
{
	if (monster.leaderRelation == LeaderRelation::None)
		return;

	// No unique monster would be a minion of someone else!
	assert(!monster.isUnique());

	// Someone with a leaderRelation should have a leader, if we end up trying to access a nullptr then the relation was already broken...

	auto &leader = *monster.getLeader();
	if (IsLineNotSolid(monster.position.tile, leader.position.future)) {
		if (monster.leaderRelation == LeaderRelation::Separated
		    && monster.position.tile.WalkingDistance(leader.position.future) < 4) {
			// Reunite the separated monster with the pack
			leader.packSize++;
			monster.leaderRelation = LeaderRelation::Leashed;
		}
	} else if (monster.leaderRelation == LeaderRelation::Leashed) {
		leader.packSize--;
		monster.leaderRelation = LeaderRelation::Separated;
	}

	if (monster.leaderRelation == LeaderRelation::Leashed) {
		if (monster.activeForTicks > leader.activeForTicks) {
			leader.position.last = monster.position.tile;
			leader.activeForTicks = monster.activeForTicks - 1;
		}
		if (leader.ai == MonsterAIID::Gargoyle && (leader.flags & MFLAG_ALLOW_SPECIAL) != 0) {
			leader.flags &= ~MFLAG_ALLOW_SPECIAL;
			leader.mode = MonsterMode::SpecialMeleeAttack;
		}
	}
}

// eftodo
bool RandomWalk2(Monster &monster, Direction md)
{
	Direction mdtemp = md;
	bool ok = monster.dirOK(md); // Can we continue in the same direction

	// Randomly go left or right
	if (FlipCoin()) {
		ok = ok || (mdtemp = Right(md), monster.dirOK(Right(md))) || (mdtemp = Left(md), monster.dirOK(Left(md)));
	} else {
		ok = ok || (mdtemp = Left(md), monster.dirOK(Left(md))) || (mdtemp = Right(md), monster.dirOK(Right(md)));
	}

	if (ok)
		monster.walk(mdtemp);

	return ok;
}

/**
 * @brief Check if a tile is affected by a spell we are vulnerable to
 */

// eftodo
bool IsTileSafe(const Monster &monster, Point position)
{
	if (!InDungeonBounds(position))
		return false;

	const bool fearsFire = (monster.resistance & IMMUNE_FIRE) == 0 || monster.type().type == MT_DIABLO;
	const bool fearsLightning = (monster.resistance & IMMUNE_LIGHTNING) == 0 || monster.type().type == MT_DIABLO;

	return !(fearsFire && HasAnyOf(dFlags[position.x][position.y], DungeonFlag::MissileFireWall))
	    && !(fearsLightning && HasAnyOf(dFlags[position.x][position.y], DungeonFlag::MissileLightningWall));
}

/**
 * @brief Check that the given tile is not currently blocked
 */
bool IsTileAvailable(Point position)
{
	if (dPlayer[position.x][position.y] != 0 || dMonster[position.x][position.y] != 0)
		return false;

	if (!IsTileWalkable(position))
		return false;

	return true;
}

/**
 * @brief If a monster can access the given tile (possibly by opening a door)
 */
bool IsTileAccessible(const Monster &monster, Point position)
{
	if (dPlayer[position.x][position.y] != 0 || dMonster[position.x][position.y] != 0)
		return false;

	if (!IsTileWalkable(position, (monster.flags & MFLAG_CAN_OPEN_DOOR) != 0))
		return false;

	return IsTileSafe(monster, position);
}

bool AiPlanWalk(Monster &monster)
{
	int8_t path[MaxPathLength];

	/** Maps from walking path step to facing direction. */
	const Direction plr2monst[9] = { Direction::South, Direction::NorthEast, Direction::NorthWest, Direction::SouthEast, Direction::SouthWest, Direction::North, Direction::East, Direction::South, Direction::West };

	if (FindPath([&monster](Point position) { return IsTileAccessible(monster, position); }, monster.position.tile, monster.enemyPosition, path) == 0) {
		return false;
	}

	monster.randomWalk(plr2monst[path[0]]);
	return true;
}

Direction Turn(Direction direction, bool turnLeft)
{
	return turnLeft ? Left(direction) : Right(direction);
}

bool RoundWalk(Monster &monster, Direction direction, int8_t *dir)
{
	Direction turn45deg = Turn(direction, *dir != 0);
	Direction turn90deg = Turn(turn45deg, *dir != 0);

	// Turn 90 degrees
	if (monster.walk(turn90deg)) {
		return true;
	}

	// Only do a small turn
	if (monster.walk(turn45deg)) {
		return true;
	}

	// Continue straight
	if (monster.walk(direction)) {
		return true;
	}

	// Try 90 degrees in the opposite than desired direction
	*dir = (*dir == 0) ? 1 : 0;
	return monster.randomWalk(Opposite(turn90deg));
}

bool AiPlanPath(Monster &monster)
{
	if (monster.type().type != MT_GOLEM) {
		if (monster.activeForTicks == 0)
			return false;
		if (monster.mode != MonsterMode::Stand)
			return false;
		if (IsNoneOf(monster.goal, MonsterGoal::Normal, MonsterGoal::Move, MonsterGoal::Attack))
			return false;
		if (monster.position.tile == GolemHoldingCell)
			return false;
	}

	bool clear = LineClear(
	    [&monster](Point position) { return monster.isTileAvailable(position); },
	    monster.position.tile,
	    monster.enemyPosition);
	if (!clear || (monster.pathCount >= 5 && monster.pathCount < 8)) {
		if ((monster.flags & MFLAG_CAN_OPEN_DOOR) != 0)
			MonstCheckDoors(monster);
		monster.pathCount++;
		if (monster.pathCount < 5)
			return false;
		if (AiPlanWalk(monster))
			return true;
	}

	if (monster.type().type != MT_GOLEM)
		monster.pathCount = 0;

	return false;
}

void AiAvoidance(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	Direction md = GetDirection(monster.position.tile, monster.position.last);
	if (monster.activeForTicks < UINT8_MAX)
		MonstCheckDoors(monster);
	int v = GenerateRnd(100);
	unsigned distanceToEnemy = monster.distanceToEnemy();
	if (distanceToEnemy >= 2 && monster.activeForTicks == UINT8_MAX && dTransVal[monster.position.tile.x][monster.position.tile.y] == dTransVal[monster.enemyPosition.x][monster.enemyPosition.y]) {
		if (monster.goal == MonsterGoal::Move || (distanceToEnemy >= 4 && FlipCoin(4))) {
			if (monster.goal != MonsterGoal::Move) {
				monster.goalVar1 = 0;
				monster.goalVar2 = GenerateRnd(2);
			}
			monster.goal = MonsterGoal::Move;
			if ((monster.goalVar1++ >= static_cast<int>(2 * distanceToEnemy) && monster.dirOK(md)) || dTransVal[monster.position.tile.x][monster.position.tile.y] != dTransVal[monster.enemyPosition.x][monster.enemyPosition.y]) {
				monster.goal = MonsterGoal::Normal;
			} else if (!RoundWalk(monster, md, &monster.goalVar2)) {
				AiDelay(monster, GenerateRnd(10) + 10);
			}
		}
	} else {
		monster.goal = MonsterGoal::Normal;
	}
	if (monster.goal == MonsterGoal::Normal) {
		if (distanceToEnemy >= 2) {
			if ((monster.var2 > 20 && v < 2 * monster.intelligence + 28)
			    || (IsMonsterModeMove(static_cast<MonsterMode>(monster.var1))
			        && monster.var2 == 0
			        && v < 2 * monster.intelligence + 78)) {
				monster.randomWalk(md);
			}
		} else if (v < 2 * monster.intelligence + 23) {
			monster.direction = md;
			if (IsAnyOf(monster.ai, MonsterAIID::GoatMelee, MonsterAIID::Gharbad) && monster.hitPoints < (monster.maxHitPoints / 2) && !FlipCoin())
				StartSpecialAttack(monster);
			else
				StartAttack(monster);
		}
	}

	monster.checkStandAnimationIsLoaded(md);
}

MissileID GetMissileType(MonsterAIID ai)
{
	switch (ai) {
	case MonsterAIID::GoatMelee:
		return MissileID::Arrow;
	case MonsterAIID::Succubus:
	case MonsterAIID::LazarusSuccubus:
		return MissileID::BloodStar;
	case MonsterAIID::Acid:
	case MonsterAIID::AcidUnique:
		return MissileID::Acid;
	case MonsterAIID::FireBat:
		return MissileID::Firebolt;
	case MonsterAIID::Torchant:
		return MissileID::Fireball;
	case MonsterAIID::Lich:
		return MissileID::OrangeFlare;
	case MonsterAIID::ArchLich:
		return MissileID::YellowFlare;
	case MonsterAIID::Psychorb:
		return MissileID::BlueFlare;
	case MonsterAIID::Necromorb:
		return MissileID::RedFlare;
	case MonsterAIID::Magma:
		return MissileID::MagmaBall;
	case MonsterAIID::Storm:
		return MissileID::ThinLightningControl;
	case MonsterAIID::Diablo:
		return MissileID::DiabloApocalypse;
	case MonsterAIID::BoneDemon:
		return MissileID::BlueFlare2;
	default:
		return MissileID::Arrow;
	}
}

void AiRanged(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand) {
		return;
	}

	if (monster.activeForTicks == UINT8_MAX || (monster.flags & MFLAG_TARGETS_MONSTER) != 0) {
		Direction md = GetMonsterDirection(monster);
		if (monster.activeForTicks < UINT8_MAX)
			MonstCheckDoors(monster);
		monster.direction = md;
		if (static_cast<MonsterMode>(monster.var1) == MonsterMode::RangedAttack) {
			AiDelay(monster, GenerateRnd(20));
		} else if (monster.distanceToEnemy() < 4) {
			if (GenerateRnd(100) < 10 * (monster.intelligence + 7))
				monster.randomWalk(Opposite(md));
		}
		if (monster.mode == MonsterMode::Stand) {
			if (LineClearMissile(monster.position.tile, monster.enemyPosition)) {
				MissileID missileType = GetMissileType(monster.ai);
				if (monster.ai == MonsterAIID::AcidUnique)
					StartRangedSpecialAttack(monster, missileType, 0);
				else
					StartRangedAttack(monster, missileType, 0);
			} else {
				monster.checkStandAnimationIsLoaded(md);
			}
		}
		return;
	}

	if (monster.activeForTicks != 0) {
		Direction md = GetDirection(monster.position.tile, monster.position.last);
		monster.randomWalk(md);
	}
}

void AiRangedAvoidance(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	Direction md = GetDirection(monster.position.tile, monster.position.last);
	if (IsAnyOf(monster.ai, MonsterAIID::Magma, MonsterAIID::Storm, MonsterAIID::BoneDemon) && monster.activeForTicks < UINT8_MAX)
		MonstCheckDoors(monster);
	int lessmissiles = (monster.ai == MonsterAIID::Acid) ? 1 : 0;
	int dam = (monster.ai == MonsterAIID::Diablo) ? 40 : 0;
	MissileID missileType = GetMissileType(monster.ai);
	int v = GenerateRnd(10000);
	unsigned distanceToEnemy = monster.distanceToEnemy();
	if (distanceToEnemy >= 2 && monster.activeForTicks == UINT8_MAX && dTransVal[monster.position.tile.x][monster.position.tile.y] == dTransVal[monster.enemyPosition.x][monster.enemyPosition.y]) {
		if (monster.goal == MonsterGoal::Move || (distanceToEnemy >= 3 && FlipCoin(4 << lessmissiles))) {
			if (monster.goal != MonsterGoal::Move) {
				monster.goalVar1 = 0;
				monster.goalVar2 = GenerateRnd(2);
			}
			monster.goal = MonsterGoal::Move;
			if (monster.goalVar1++ >= static_cast<int>(2 * distanceToEnemy) && monster.dirOK(md)) {
				monster.goal = MonsterGoal::Normal;
			} else if (v < (500 * (monster.intelligence + 1) >> lessmissiles)
			    && (LineClearMissile(monster.position.tile, monster.enemyPosition))) {
				StartRangedSpecialAttack(monster, missileType, dam);
			} else {
				RoundWalk(monster, md, &monster.goalVar2);
			}
		}
	} else {
		monster.goal = MonsterGoal::Normal;
	}
	if (monster.goal == MonsterGoal::Normal) {
		if (((distanceToEnemy >= 3 && v < ((500 * (monster.intelligence + 2)) >> lessmissiles))
		        || v < ((500 * (monster.intelligence + 1)) >> lessmissiles))
		    && LineClearMissile(monster.position.tile, monster.enemyPosition)) {
			StartRangedSpecialAttack(monster, missileType, dam);
		} else if (distanceToEnemy >= 2) {
			v = GenerateRnd(100);
			if (v < 1000 * (monster.intelligence + 5)
			    || (IsMonsterModeMove(static_cast<MonsterMode>(monster.var1))
			        && monster.var2 == 0
			        && v < 1000 * (monster.intelligence + 8))) {
				monster.randomWalk(md);
			}
		} else if (v < 1000 * (monster.intelligence + 6)) {
			monster.direction = md;
			StartAttack(monster);
		}
	}
	if (monster.mode == MonsterMode::Stand) {
		AiDelay(monster, GenerateRnd(10) + 5);
	}
}

void ZombieAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand) {
		return;
	}

	if (!IsTileVisible(monster.position.tile)) {
		return;
	}

	if (GenerateRnd(100) < 2 * monster.intelligence + 10) {
		int dist = monster.enemyPosition.WalkingDistance(monster.position.tile);
		if (dist >= 2) {
			if (dist >= 2 * monster.intelligence + 4) {
				Direction md = monster.direction;
				if (GenerateRnd(100) < 2 * monster.intelligence + 20) {
					md = static_cast<Direction>(GenerateRnd(8));
				}
				monster.walk(md);
			} else {
				monster.randomWalk(GetMonsterDirection(monster));
			}
		} else {
			StartAttack(monster);
		}
	}

	monster.checkStandAnimationIsLoaded(monster.direction);
}

void OverlordAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	Direction md = GetMonsterDirection(monster);
	monster.direction = md;
	int v = GenerateRnd(100);
	if (monster.distanceToEnemy() >= 2) {
		if ((monster.var2 > 20 && v < 4 * monster.intelligence + 20)
		    || (IsMonsterModeMove(static_cast<MonsterMode>(monster.var1))
		        && monster.var2 == 0
		        && v < 4 * monster.intelligence + 70)) {
			monster.randomWalk(md);
		}
	} else if (v < 4 * monster.intelligence + 15) {
		StartAttack(monster);
	} else if (v < 4 * monster.intelligence + 20) {
		StartSpecialAttack(monster);
	}

	monster.checkStandAnimationIsLoaded(md);
}

void SkeletonAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	Direction md = GetDirection(monster.position.tile, monster.position.last);
	monster.direction = md;
	if (monster.distanceToEnemy() >= 2) {
		if (static_cast<MonsterMode>(monster.var1) == MonsterMode::Delay || (GenerateRnd(100) >= 35 - 4 * monster.intelligence)) {
			monster.randomWalk(md);
		} else {
			AiDelay(monster, 15 - 2 * monster.intelligence + GenerateRnd(10));
		}
	} else {
		if (static_cast<MonsterMode>(monster.var1) == MonsterMode::Delay || (GenerateRnd(100) < 2 * monster.intelligence + 20)) {
			StartAttack(monster);
		} else {
			AiDelay(monster, 2 * (5 - monster.intelligence) + GenerateRnd(10));
		}
	}

	monster.checkStandAnimationIsLoaded(md);
}

void SkeletonBowAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	Direction md = GetMonsterDirection(monster);
	monster.direction = md;
	int v = GenerateRnd(100);

	bool walking = false;

	if (monster.distanceToEnemy() < 4) {
		if ((monster.var2 > 20 && v < 2 * monster.intelligence + 13)
		    || (IsMonsterModeMove(static_cast<MonsterMode>(monster.var1))
		        && monster.var2 == 0
		        && v < 2 * monster.intelligence + 63)) {
			walking = monster.walk(Opposite(md));
		}
	}

	if (!walking) {
		if (GenerateRnd(100) < 2 * monster.intelligence + 3) {
			if (LineClearMissile(monster.position.tile, monster.enemyPosition))
				StartRangedAttack(monster, MissileID::Arrow, 4);
		}
	}

	monster.checkStandAnimationIsLoaded(md);
}

std::optional<Point> ScavengerFindCorpse(const Monster &scavenger)
{
	bool reverseSearch = FlipCoin();
	int first = reverseSearch ? 4 : -4;
	int last = reverseSearch ? -4 : 4;
	int increment = reverseSearch ? -1 : 1;

	for (int y = first; y <= last; y += increment) {
		for (int x = first; x <= last; x += increment) {
			Point position = scavenger.position.tile + Displacement { x, y };
			if (!InDungeonBounds(position))
				continue;
			if (dCorpse[position.x][position.y] == 0)
				continue;
			if (!IsLineNotSolid(scavenger.position.tile, position))
				continue;
			return position;
		}
	}
	return {};
}

void ScavengerAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand)
		return;
	if (monster.hitPoints < (monster.maxHitPoints / 2) && monster.goal != MonsterGoal::Healing) {
		if (monster.leaderRelation != LeaderRelation::None) {
			ShrinkLeaderPacksize(monster);
			monster.leaderRelation = LeaderRelation::None;
		}
		monster.goal = MonsterGoal::Healing;
		monster.goalVar3 = 10;
	}
	if (monster.goal == MonsterGoal::Healing && monster.goalVar3 != 0) {
		monster.goalVar3--;
		if (dCorpse[monster.position.tile.x][monster.position.tile.y] != 0) {
			StartEating(monster);
			if (gbIsHellfire) {
				int mMaxHP = monster.maxHitPoints;
				monster.hitPoints += mMaxHP / 8;
				if (monster.hitPoints > monster.maxHitPoints)
					monster.hitPoints = monster.maxHitPoints;
				if (monster.goalVar3 <= 0 || monster.hitPoints == monster.maxHitPoints)
					dCorpse[monster.position.tile.x][monster.position.tile.y] = 0;
			} else {
				monster.hitPoints += 64;
			}
			int targetHealth = monster.maxHitPoints;
			if (!gbIsHellfire)
				targetHealth = (monster.maxHitPoints / 2) + (monster.maxHitPoints / 4);
			if (monster.hitPoints >= targetHealth) {
				monster.goal = MonsterGoal::Normal;
				monster.goalVar1 = 0;
				monster.goalVar2 = 0;
			}
		} else {
			if (monster.goalVar1 == 0) {
				std::optional<Point> position = ScavengerFindCorpse(monster);
				if (position) {
					monster.goalVar1 = position->x + 1;
					monster.goalVar2 = position->y + 1;
				}
			}
			if (monster.goalVar1 != 0) {
				int x = monster.goalVar1 - 1;
				int y = monster.goalVar2 - 1;
				monster.direction = GetDirection(monster.position.tile, { x, y });
				monster.randomWalk(monster.direction);
			}
		}
	}

	if (monster.mode == MonsterMode::Stand)
		SkeletonAi(monster);
}

void RhinoAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	Direction md = GetDirection(monster.position.tile, monster.position.last);
	if (monster.activeForTicks < UINT8_MAX)
		MonstCheckDoors(monster);
	int v = GenerateRnd(100);
	unsigned distanceToEnemy = monster.distanceToEnemy();
	if (distanceToEnemy >= 2) {
		if (monster.goal == MonsterGoal::Move || (distanceToEnemy >= 5 && !FlipCoin(4))) {
			if (monster.goal != MonsterGoal::Move) {
				monster.goalVar1 = 0;
				monster.goalVar2 = GenerateRnd(2);
			}
			monster.goal = MonsterGoal::Move;
			if (monster.goalVar1++ >= static_cast<int>(2 * distanceToEnemy) || dTransVal[monster.position.tile.x][monster.position.tile.y] != dTransVal[monster.enemyPosition.x][monster.enemyPosition.y]) {
				monster.goal = MonsterGoal::Normal;
			} else if (!RoundWalk(monster, md, &monster.goalVar2)) {
				AiDelay(monster, GenerateRnd(10) + 10);
			}
		}
	} else {
		monster.goal = MonsterGoal::Normal;
	}
	if (monster.goal == MonsterGoal::Normal) {
		if (distanceToEnemy >= 5
		    && v < 2 * monster.intelligence + 43
		    && LineClear([&monster](Point position) { return monster.isTileAvailable(position); }, monster.position.tile, monster.enemyPosition)) {
			if (AddMissile(monster.position.tile, monster.enemyPosition, md, MissileID::Rhino, TARGET_PLAYERS, monster, 0, 0) != nullptr) {
				if (monster.data().hasSpecialSound)
					monster.playEffect(MonsterSound::Special);
				monster.occupyTile(monster.position.tile, true);
				monster.mode = MonsterMode::Charge;
			}
		} else {
			if (distanceToEnemy >= 2) {
				v = GenerateRnd(100);
				if (v >= 2 * monster.intelligence + 33
				    && (IsNoneOf(static_cast<MonsterMode>(monster.var1), MonsterMode::MoveNorthwards, MonsterMode::MoveSouthwards, MonsterMode::MoveSideways)
				        || monster.var2 != 0
				        || v >= 2 * monster.intelligence + 83)) {
					AiDelay(monster, GenerateRnd(10) + 10);
				} else {
					monster.randomWalk(md);
				}
			} else if (v < 2 * monster.intelligence + 28) {
				monster.direction = md;
				StartAttack(monster);
			}
		}
	}

	monster.checkStandAnimationIsLoaded(monster.direction);
}

void FallenAi(Monster &monster)
{
	if (monster.goal == MonsterGoal::Attack) {
		if (monster.goalVar1 != 0)
			monster.goalVar1--;
		else
			monster.goal = MonsterGoal::Normal;
	}
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	if (monster.goal == MonsterGoal::Retreat) {
		if (monster.goalVar1-- == 0) {
			monster.goal = MonsterGoal::Normal;
			monster.startStand(Opposite(static_cast<Direction>(monster.goalVar2)));
		}
	}

	if (monster.animInfo.isLastFrame()) {
		if (!FlipCoin(4)) {
			return;
		}
		StartSpecialStand(monster, monster.direction);
		if (monster.maxHitPoints - (2 * monster.intelligence + 2) >= monster.hitPoints)
			monster.hitPoints += 2 * monster.intelligence + 2;
		else
			monster.hitPoints = monster.maxHitPoints;
		int rad = 2 * monster.intelligence + 4;
		for (int y = -rad; y <= rad; y++) {
			for (int x = -rad; x <= rad; x++) {
				int xpos = monster.position.tile.x + x;
				int ypos = monster.position.tile.y + y;
				if (InDungeonBounds({ xpos, ypos })) {
					int m = dMonster[xpos][ypos];
					if (m <= 0)
						continue;

					Monster &otherMonster = MonsterManager.Monsters[m - 1];
					if (otherMonster.ai != MonsterAIID::Fallen)
						continue;

					otherMonster.goal = MonsterGoal::Attack;
					otherMonster.goalVar1 = 30 * monster.intelligence + 105;
				}
			}
		}
	} else if (monster.goal == MonsterGoal::Retreat) {
		monster.direction = static_cast<Direction>(monster.goalVar2);
		monster.randomWalk(monster.direction);
	} else if (monster.goal == MonsterGoal::Attack) {
		if (monster.distanceToEnemy() < 2)
			StartAttack(monster);
		else
			monster.randomWalk(GetMonsterDirection(monster));
	} else
		SkeletonAi(monster);
}

void LeoricAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	Direction md = GetDirection(monster.position.tile, monster.position.last);
	if (monster.activeForTicks < UINT8_MAX)
		MonstCheckDoors(monster);
	int v = GenerateRnd(100);
	unsigned distanceToEnemy = monster.distanceToEnemy();
	if (distanceToEnemy >= 2 && monster.activeForTicks == UINT8_MAX && dTransVal[monster.position.tile.x][monster.position.tile.y] == dTransVal[monster.enemyPosition.x][monster.enemyPosition.y]) {
		if (monster.goal == MonsterGoal::Move || (distanceToEnemy >= 3 && FlipCoin(4))) {
			if (monster.goal != MonsterGoal::Move) {
				monster.goalVar1 = 0;
				monster.goalVar2 = GenerateRnd(2);
			}
			monster.goal = MonsterGoal::Move;
			if ((monster.goalVar1++ >= static_cast<int>(2 * distanceToEnemy) && monster.dirOK(md)) || dTransVal[monster.position.tile.x][monster.position.tile.y] != dTransVal[monster.enemyPosition.x][monster.enemyPosition.y]) {
				monster.goal = MonsterGoal::Normal;
			} else if (!RoundWalk(monster, md, &monster.goalVar2)) {
				AiDelay(monster, GenerateRnd(10) + 10);
			}
		}
	} else {
		monster.goal = MonsterGoal::Normal;
	}
	if (monster.goal == MonsterGoal::Normal) {
		if (!UseMultiplayerQuests()
		    && ((distanceToEnemy >= 3 && v < 4 * monster.intelligence + 35) || v < 6)
		    && LineClearMissile(monster.position.tile, monster.enemyPosition)) {
			Point newPosition = monster.position.tile + md;
			if (monster.isTileAvailable(newPosition) && MonsterManager.ActiveMonsterCount < MaxMonsters) {
				auto typeIndex = GetRandomSkeletonTypeIndex();
				if (typeIndex) {
					SpawnMonster(newPosition, md, *typeIndex, true);
				}
				StartSpecialStand(monster, md);
			}
		} else {
			if (distanceToEnemy >= 2) {
				v = GenerateRnd(100);
				if (v >= monster.intelligence + 25
				    && (IsNoneOf(static_cast<MonsterMode>(monster.var1), MonsterMode::MoveNorthwards, MonsterMode::MoveSouthwards, MonsterMode::MoveSideways) || monster.var2 != 0 || (v >= monster.intelligence + 75))) {
					AiDelay(monster, GenerateRnd(10) + 10);
				} else {
					monster.randomWalk(md);
				}
			} else if (v < monster.intelligence + 20) {
				monster.direction = md;
				StartAttack(monster);
			}
		}
	}

	monster.checkStandAnimationIsLoaded(md);
}

void BatAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	Direction md = GetDirection(monster.position.tile, monster.position.last);
	monster.direction = md;
	int v = GenerateRnd(100);
	if (monster.goal == MonsterGoal::Retreat) {
		if (monster.goalVar1 == 0) {
			monster.randomWalk(Opposite(md));
			monster.goalVar1++;
		} else {
			monster.randomWalk(PickRandomlyAmong({ Right(md), Left(md) }));
			monster.goal = MonsterGoal::Normal;
		}
		return;
	}

	unsigned distanceToEnemy = monster.distanceToEnemy();
	if (monster.type().type == MT_GLOOM
	    && distanceToEnemy >= 5
	    && v < 4 * monster.intelligence + 33
	    && LineClear([&monster](Point position) { return monster.isTileAvailable(position); }, monster.position.tile, monster.enemyPosition)) {
		if (AddMissile(monster.position.tile, monster.enemyPosition, md, MissileID::Rhino, TARGET_PLAYERS, monster, 0, 0) != nullptr) {
			monster.occupyTile(monster.position.tile, true);
			monster.mode = MonsterMode::Charge;
		}
	} else if (distanceToEnemy >= 2) {
		if ((monster.var2 > 20 && v < monster.intelligence + 13)
		    || (IsMonsterModeMove(static_cast<MonsterMode>(monster.var1))
		        && monster.var2 == 0
		        && v < monster.intelligence + 63)) {
			monster.randomWalk(md);
		}
	} else if (v < 4 * monster.intelligence + 8) {
		StartAttack(monster);
		monster.goal = MonsterGoal::Retreat;
		monster.goalVar1 = 0;
		if (monster.type().type == MT_FAMILIAR) {
			AddMissile(monster.enemyPosition, monster.enemyPosition + Direction::SouthEast, Direction::South, MissileID::Lightning, TARGET_PLAYERS, monster, GenerateRnd(10) + 1, 0);
		}
	}

	monster.checkStandAnimationIsLoaded(md);
}

void GargoyleAi(Monster &monster)
{
	Direction md = GetMonsterDirection(monster);
	unsigned distanceToEnemy = monster.distanceToEnemy();
	if (monster.activeForTicks != 0 && (monster.flags & MFLAG_ALLOW_SPECIAL) != 0) {
		UpdateEnemy(monster);
		if (distanceToEnemy < monster.intelligence + 2u) {
			monster.flags &= ~MFLAG_ALLOW_SPECIAL;
		}
		return;
	}

	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	if (monster.hitPoints < (monster.maxHitPoints / 2))
		monster.goal = MonsterGoal::Retreat;
	if (monster.goal == MonsterGoal::Retreat) {
		if (distanceToEnemy >= monster.intelligence + 2u) {
			monster.goal = MonsterGoal::Normal;
			StartHeal(monster);
		} else if (!monster.randomWalk(Opposite(md))) {
			monster.goal = MonsterGoal::Normal;
		}
	}
	AiAvoidance(monster);
}

void ButcherAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	Direction md = GetDirection(monster.position.tile, monster.position.last);
	monster.direction = md;

	if (monster.distanceToEnemy() >= 2)
		monster.randomWalk(md);
	else
		StartAttack(monster);

	monster.checkStandAnimationIsLoaded(md);
}

void SneakAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	unsigned dist = 5 - monster.intelligence;
	unsigned distanceToEnemy = monster.distanceToEnemy();
	if (static_cast<MonsterMode>(monster.var1) == MonsterMode::HitRecovery) {
		monster.goal = MonsterGoal::Retreat;
		monster.goalVar1 = 0;
	} else if (distanceToEnemy >= dist + 3 || monster.goalVar1 > 8) {
		monster.goal = MonsterGoal::Normal;
		monster.goalVar1 = 0;
	}
	Direction md = GetMonsterDirection(monster);
	if (monster.goal == MonsterGoal::Retreat && (monster.flags & MFLAG_NO_ENEMY) == 0) {
		if ((monster.flags & MFLAG_TARGETS_MONSTER) != 0)
			md = GetDirection(monster.position.tile, MonsterManager.Monsters[monster.enemy].position.tile);
		else
			md = GetDirection(monster.position.tile, Players[monster.enemy].position.last);
		md = Opposite(md);
		if (monster.type().type == MT_UNSEEN) {
			md = PickRandomlyAmong({ Right(md), Left(md) });
		}
	}
	monster.direction = md;
	int v = GenerateRnd(100);
	if (distanceToEnemy < dist && (monster.flags & MFLAG_HIDDEN) != 0) {
		StartFadein(monster, md, false);
	} else {
		if ((distanceToEnemy >= dist + 1) && (monster.flags & MFLAG_HIDDEN) == 0) {
			StartFadeout(monster, md, true);
		} else {
			if (monster.goal == MonsterGoal::Retreat
			    || (distanceToEnemy >= 2
			        && ((monster.var2 > 20 && v < 4 * monster.intelligence + 14)
			            || (IsMonsterModeMove(static_cast<MonsterMode>(monster.var1))
			                && monster.var2 == 0 && v < 4 * monster.intelligence + 64)))) {
				monster.goalVar1++;
				monster.randomWalk(md);
			}
		}
	}
	if (monster.mode == MonsterMode::Stand) {
		if (distanceToEnemy >= 2 || v >= 4 * monster.intelligence + 10)
			monster.changeAnimationData(MonsterGraphic::Stand);
		else
			StartAttack(monster);
	}
}

void GharbadAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand) {
		return;
	}

	Direction md = GetMonsterDirection(monster);

	if (monster.talkMsg >= TEXT_GARBUD1
	    && monster.talkMsg <= TEXT_GARBUD3
	    && !IsTileVisible(monster.position.tile)
	    && monster.goal == MonsterGoal::Talking) {
		monster.goal = MonsterGoal::Inquiring;
		switch (monster.talkMsg) {
		case TEXT_GARBUD1:
			monster.talkMsg = TEXT_GARBUD2;
			Quests[Q_GARBUD]._qvar1 = QS_GHARBAD_FIRST_ITEM_READY;
			NetSendCmdQuest(true, Quests[Q_GARBUD]);
			break;
		case TEXT_GARBUD2:
			monster.talkMsg = TEXT_GARBUD3;
			Quests[Q_GARBUD]._qvar1 = QS_GHARBAD_SECOND_ITEM_NEARLY_DONE;
			NetSendCmdQuest(true, Quests[Q_GARBUD]);
			break;
		case TEXT_GARBUD3:
			monster.talkMsg = TEXT_GARBUD4;
			Quests[Q_GARBUD]._qvar1 = QS_GHARBAD_SECOND_ITEM_READY;
			NetSendCmdQuest(true, Quests[Q_GARBUD]);
			break;
		default:
			break;
		}
	}

	if (IsTileVisible(monster.position.tile)) {
		if (monster.talkMsg == TEXT_GARBUD4) {
			if (!effect_is_playing(SfxID::Gharbad4) && monster.goal == MonsterGoal::Talking) {
				monster.goal = MonsterGoal::Normal;
				monster.activeForTicks = UINT8_MAX;
				monster.talkMsg = TEXT_NONE;
				Quests[Q_GARBUD]._qvar1 = QS_GHARBAD_ATTACKING;
				NetSendCmdQuest(true, Quests[Q_GARBUD]);
			}
		}
	}

	if (IsAnyOf(monster.goal, MonsterGoal::Normal, MonsterGoal::Move))
		AiAvoidance(monster);

	monster.checkStandAnimationIsLoaded(md);
}

void SnotSpilAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand) {
		return;
	}

	Direction md = GetMonsterDirection(monster);

	if (monster.talkMsg == TEXT_BANNER10 && !IsTileVisible(monster.position.tile) && monster.goal == MonsterGoal::Talking) {
		monster.talkMsg = TEXT_BANNER11;
		monster.goal = MonsterGoal::Inquiring;
	}

	if (monster.talkMsg == TEXT_BANNER11 && Quests[Q_LTBANNER]._qvar1 == 3) {
		monster.talkMsg = TEXT_NONE;
		monster.goal = MonsterGoal::Normal;
	}

	if (IsTileVisible(monster.position.tile)) {
		if (monster.talkMsg == TEXT_BANNER12) {
			if (!effect_is_playing(SfxID::Snotspill3) && monster.goal == MonsterGoal::Talking) {
				ObjChangeMap(SetPiece.position.x, SetPiece.position.y, SetPiece.position.x + SetPiece.size.width + 1, SetPiece.position.y + SetPiece.size.height + 1);
				Quests[Q_LTBANNER]._qvar1 = 3;
				NetSendCmdQuest(true, Quests[Q_LTBANNER]);
				RedoPlayerVision();
				monster.activeForTicks = UINT8_MAX;
				monster.talkMsg = TEXT_NONE;
				monster.goal = MonsterGoal::Normal;
			}
		}
		if (Quests[Q_LTBANNER]._qvar1 == 3) {
			if (IsAnyOf(monster.goal, MonsterGoal::Normal, MonsterGoal::Attack))
				FallenAi(monster);
		}
	}

	monster.checkStandAnimationIsLoaded(md);
}

void SnakeAi(Monster &monster)
{
	int8_t pattern[6] = { 1, 1, 0, -1, -1, 0 };
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0)
		return;
	Direction md = GetDirection(monster.position.tile, monster.position.last);
	monster.direction = md;
	unsigned distanceToEnemy = monster.distanceToEnemy();
	if (distanceToEnemy >= 2) {
		if (distanceToEnemy < 3 && LineClear([&monster](Point position) { return monster.isTileAvailable(position); }, monster.position.tile, monster.enemyPosition) && static_cast<MonsterMode>(monster.var1) != MonsterMode::Charge) {
			if (AddMissile(monster.position.tile, monster.enemyPosition, md, MissileID::Rhino, TARGET_PLAYERS, monster, 0, 0) != nullptr) {
				monster.playEffect(MonsterSound::Attack);
				monster.occupyTile(monster.position.tile, true);
				monster.mode = MonsterMode::Charge;
			}
		} else if (static_cast<MonsterMode>(monster.var1) == MonsterMode::Delay || GenerateRnd(100) >= 35 - 2 * monster.intelligence) {
			if (pattern[monster.goalVar1] == -1)
				md = Left(md);
			else if (pattern[monster.goalVar1] == 1)
				md = Right(md);

			monster.goalVar1++;
			if (monster.goalVar1 > 5)
				monster.goalVar1 = 0;

			Direction targetDirection = static_cast<Direction>(monster.goalVar2);
			if (md != targetDirection) {
				int drift = static_cast<int>(md) - monster.goalVar2;
				if (drift < 0)
					drift += 8;

				if (drift < 4)
					md = Right(targetDirection);
				else if (drift > 4)
					md = Left(targetDirection);
				monster.goalVar2 = static_cast<int>(md);
			}

			if (!monster.walk(md))
				RandomWalk2(monster, monster.direction);
		} else {
			AiDelay(monster, 15 - monster.intelligence + GenerateRnd(10));
		}
	} else {
		if (IsAnyOf(static_cast<MonsterMode>(monster.var1), MonsterMode::Delay, MonsterMode::Charge)
		    || (GenerateRnd(100) < monster.intelligence + 20)) {
			StartAttack(monster);
		} else
			AiDelay(monster, 10 - monster.intelligence + GenerateRnd(10));
	}

	monster.checkStandAnimationIsLoaded(monster.direction);
}

void CounselorAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}
	Direction md = GetDirection(monster.position.tile, monster.position.last);
	if (monster.activeForTicks < UINT8_MAX)
		MonstCheckDoors(monster);
	int v = GenerateRnd(100);
	unsigned distanceToEnemy = monster.distanceToEnemy();
	if (monster.goal == MonsterGoal::Retreat) {
		if (monster.goalVar1++ <= 3)
			monster.randomWalk(Opposite(md));
		else {
			monster.goal = MonsterGoal::Normal;
			StartFadein(monster, md, true);
		}
	} else if (monster.goal == MonsterGoal::Move) {
		if (distanceToEnemy >= 2 && monster.activeForTicks == UINT8_MAX && dTransVal[monster.position.tile.x][monster.position.tile.y] == dTransVal[monster.enemyPosition.x][monster.enemyPosition.y]) {
			if (monster.goalVar1++ < static_cast<int>(2 * distanceToEnemy) || !monster.dirOK(md)) {
				RoundWalk(monster, md, &monster.goalVar2);
			} else {
				monster.goal = MonsterGoal::Normal;
				StartFadein(monster, md, true);
			}
		} else {
			monster.goal = MonsterGoal::Normal;
			StartFadein(monster, md, true);
		}
	} else if (monster.goal == MonsterGoal::Normal) {
		if (distanceToEnemy >= 2) {
			if (v < 5 * (monster.intelligence + 10) && LineClearMissile(monster.position.tile, monster.enemyPosition)) {
				constexpr MissileID MissileTypes[4] = { MissileID::Firebolt, MissileID::ChargedBolt, MissileID::LightningControl, MissileID::Fireball };
				StartRangedAttack(monster, MissileTypes[monster.intelligence], RandomIntBetween(monster.minDamage, monster.maxDamage));
			} else if (GenerateRnd(100) < 30) {
				monster.goal = MonsterGoal::Move;
				monster.goalVar1 = 0;
				StartFadeout(monster, md, false);
			} else
				AiDelay(monster, GenerateRnd(10) + 2 * (5 - monster.intelligence));
		} else {
			monster.direction = md;
			if (monster.hitPoints < (monster.maxHitPoints / 2)) {
				monster.goal = MonsterGoal::Retreat;
				monster.goalVar1 = 0;
				StartFadeout(monster, md, false);
			} else if (static_cast<MonsterMode>(monster.var1) == MonsterMode::Delay
			    || GenerateRnd(100) < 2 * monster.intelligence + 20) {
				StartRangedAttack(monster, MissileID::Null, 0);
				AddMissile(monster.position.tile, { 0, 0 }, monster.direction, MissileID::FlashBottom, TARGET_PLAYERS, monster, 4, 0);
				AddMissile(monster.position.tile, { 0, 0 }, monster.direction, MissileID::FlashTop, TARGET_PLAYERS, monster, 4, 0);
			} else
				AiDelay(monster, GenerateRnd(10) + 2 * (5 - monster.intelligence));
		}
	}
	if (monster.mode == MonsterMode::Stand) {
		AiDelay(monster, GenerateRnd(10) + 5);
	}
}

void ZharAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand) {
		return;
	}

	Direction md = GetMonsterDirection(monster);
	if (monster.talkMsg == TEXT_ZHAR1 && !IsTileVisible(monster.position.tile) && monster.goal == MonsterGoal::Talking) {
		monster.talkMsg = TEXT_ZHAR2;
		monster.goal = MonsterGoal::Inquiring;
		Quests[Q_ZHAR]._qvar1 = QS_ZHAR_ANGRY;
		NetSendCmdQuest(true, Quests[Q_ZHAR]);
	}

	if (IsTileVisible(monster.position.tile)) {
		if (monster.talkMsg == TEXT_ZHAR2) {
			if (!effect_is_playing(SfxID::Zhar2) && monster.goal == MonsterGoal::Talking) {
				monster.activeForTicks = UINT8_MAX;
				monster.talkMsg = TEXT_NONE;
				monster.goal = MonsterGoal::Normal;
				Quests[Q_ZHAR]._qvar1 = QS_ZHAR_ATTACKING;
				NetSendCmdQuest(true, Quests[Q_ZHAR]);
			}
		}
	}

	if (IsAnyOf(monster.goal, MonsterGoal::Normal, MonsterGoal::Retreat, MonsterGoal::Move))
		CounselorAi(monster);

	monster.checkStandAnimationIsLoaded(md);
}

void MegaAi(Monster &monster)
{
	unsigned distanceToEnemy = monster.distanceToEnemy();
	if (distanceToEnemy >= 5) {
		SkeletonAi(monster);
		return;
	}

	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	Direction md = GetDirection(monster.position.tile, monster.position.last);
	if (monster.activeForTicks < UINT8_MAX)
		MonstCheckDoors(monster);
	int v = GenerateRnd(100);
	if (distanceToEnemy >= 2 && monster.activeForTicks == UINT8_MAX && dTransVal[monster.position.tile.x][monster.position.tile.y] == dTransVal[monster.enemyPosition.x][monster.enemyPosition.y]) {
		if (monster.goal == MonsterGoal::Move || distanceToEnemy >= 3) {
			if (monster.goal != MonsterGoal::Move) {
				monster.goalVar1 = 0;
				monster.goalVar2 = GenerateRnd(2);
			}
			monster.goal = MonsterGoal::Move;
			monster.goalVar3 = 4;
			if (monster.goalVar1++ < static_cast<int>(2 * distanceToEnemy) || !monster.dirOK(md)) {
				if (v < 5 * (monster.intelligence + 16))
					RoundWalk(monster, md, &monster.goalVar2);
			} else
				monster.goal = MonsterGoal::Normal;
		}
	} else {
		monster.goal = MonsterGoal::Normal;
	}
	if (monster.goal == MonsterGoal::Normal) {
		if (((distanceToEnemy >= 3 && v < 5 * (monster.intelligence + 2)) || v < 5 * (monster.intelligence + 1) || monster.goalVar3 == 4) && LineClearMissile(monster.position.tile, monster.enemyPosition)) {
			StartRangedSpecialAttack(monster, MissileID::InfernoControl, 0);
		} else if (distanceToEnemy >= 2) {
			v = GenerateRnd(100);
			if (v < 2 * (5 * monster.intelligence + 25)
			    || (IsMonsterModeMove(static_cast<MonsterMode>(monster.var1))
			        && monster.var2 == 0
			        && v < 2 * (5 * monster.intelligence + 40))) {
				monster.randomWalk(md);
			}
		} else {
			if (GenerateRnd(100) < 10 * (monster.intelligence + 4)) {
				monster.direction = md;
				if (FlipCoin())
					StartRangedSpecialAttack(monster, MissileID::InfernoControl, 0);
				else
					StartAttack(monster);
			}
		}
		monster.goalVar3 = 1;
	}
	if (monster.mode == MonsterMode::Stand) {
		AiDelay(monster, GenerateRnd(10) + 5);
	}
}

void LazarusAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand) {
		return;
	}

	Direction md = GetMonsterDirection(monster);
	if (IsTileVisible(monster.position.tile)) {
		if (!UseMultiplayerQuests()) {
			Player &myPlayer = *MyPlayer;
			if (monster.talkMsg == TEXT_VILE13 && monster.goal == MonsterGoal::Inquiring && myPlayer.position.tile == Point { 35, 46 }) {
				if (!gbIsMultiplayer) {
					// Playing ingame movies is currently not supported in multiplayer
					PlayInGameMovie("gendata\\fprst3.smk");
				}
				monster.mode = MonsterMode::Talk;
				Quests[Q_BETRAYER]._qvar1 = 5;
				NetSendCmdQuest(true, Quests[Q_BETRAYER]);
			}

			if (monster.talkMsg == TEXT_VILE13 && !effect_is_playing(SfxID::LazarusGreeting) && monster.goal == MonsterGoal::Talking) {
				ObjChangeMap(1, 18, 20, 24);
				RedoPlayerVision();
				Quests[Q_BETRAYER]._qvar1 = 6;
				monster.goal = MonsterGoal::Normal;
				monster.activeForTicks = UINT8_MAX;
				monster.talkMsg = TEXT_NONE;
				NetSendCmdQuest(true, Quests[Q_BETRAYER]);
			}
		}

		if (UseMultiplayerQuests() && monster.talkMsg == TEXT_VILE13 && monster.goal == MonsterGoal::Inquiring && Quests[Q_BETRAYER]._qvar1 <= 3) {
			monster.mode = MonsterMode::Talk;
		}
	}

	if (IsAnyOf(monster.goal, MonsterGoal::Normal, MonsterGoal::Retreat, MonsterGoal::Move)) {
		if (!UseMultiplayerQuests() && Quests[Q_BETRAYER]._qvar1 == 4 && monster.talkMsg == TEXT_NONE) { // Fix save games affected by teleport bug
			ObjChangeMapResync(1, 18, 20, 24);
			RedoPlayerVision();
			Quests[Q_BETRAYER]._qvar1 = 6;
		}
		monster.talkMsg = TEXT_NONE;
		CounselorAi(monster);
	}

	monster.checkStandAnimationIsLoaded(md);
}

void LazarusMinionAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand)
		return;

	Direction md = GetMonsterDirection(monster);

	if (IsTileVisible(monster.position.tile)) {
		if (!UseMultiplayerQuests()) {
			if (Quests[Q_BETRAYER]._qvar1 <= 5) {
				monster.goal = MonsterGoal::Inquiring;
			} else {
				monster.goal = MonsterGoal::Normal;
				monster.talkMsg = TEXT_NONE;
			}
		} else
			monster.goal = MonsterGoal::Normal;
	}
	if (monster.goal == MonsterGoal::Normal)
		AiRanged(monster);

	monster.checkStandAnimationIsLoaded(md);
}

void LachdananAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand) {
		return;
	}

	Direction md = GetMonsterDirection(monster);

	if (monster.talkMsg == TEXT_VEIL9 && !IsTileVisible(monster.position.tile) && monster.goal == MonsterGoal::Talking) {
		monster.talkMsg = TEXT_VEIL10;
		monster.goal = MonsterGoal::Inquiring;
		Quests[Q_VEIL]._qvar2 = QS_VEIL_EARLY_RETURN;
		NetSendCmdQuest(true, Quests[Q_VEIL]);
	}

	if (IsTileVisible(monster.position.tile)) {
		if (monster.talkMsg == TEXT_VEIL11) {
			if (!effect_is_playing(SfxID::Lachdanan3) && monster.goal == MonsterGoal::Talking) {
				monster.talkMsg = TEXT_NONE;
				Quests[Q_VEIL]._qactive = QUEST_DONE;
				NetSendCmdQuest(true, Quests[Q_VEIL]);
				monster.death(monster.direction, true);
				delta_kill_monster(monster, monster.position.tile, *MyPlayer);
				NetSendCmdLocParam1(false, CMD_MONSTDEATH, monster.position.tile, static_cast<uint16_t>(monster.getId()));
			}
		}
	}

	monster.checkStandAnimationIsLoaded(md);
}

void WarlordAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand) {
		return;
	}

	Direction md = GetMonsterDirection(monster);
	if (IsTileVisible(monster.position.tile)) {
		if (monster.talkMsg == TEXT_WARLRD9 && monster.goal == MonsterGoal::Inquiring)
			monster.mode = MonsterMode::Talk;
		if (monster.talkMsg == TEXT_WARLRD9 && !effect_is_playing(SfxID::Warlord) && monster.goal == MonsterGoal::Talking) {
			monster.activeForTicks = UINT8_MAX;
			monster.talkMsg = TEXT_NONE;
			monster.goal = MonsterGoal::Normal;
			Quests[Q_WARLORD]._qvar1 = QS_WARLORD_ATTACKING;
			NetSendCmdQuest(true, Quests[Q_WARLORD]);
		}
	}

	if (monster.goal == MonsterGoal::Normal)
		SkeletonAi(monster);

	monster.checkStandAnimationIsLoaded(md);
}

void HorkDemonAi(Monster &monster)
{
	if (monster.mode != MonsterMode::Stand || monster.activeForTicks == 0) {
		return;
	}

	Direction md = GetDirection(monster.position.tile, monster.position.last);

	if (monster.activeForTicks < 255) {
		MonstCheckDoors(monster);
	}

	int v = GenerateRnd(100);

	unsigned distanceToEnemy = monster.distanceToEnemy();
	if (distanceToEnemy < 2) {
		monster.goal = MonsterGoal::Normal;
	} else if (monster.goal == MonsterGoal::Move || (distanceToEnemy >= 5 && !FlipCoin(4))) {
		if (monster.goal != MonsterGoal::Move) {
			monster.goalVar1 = 0;
			monster.goalVar2 = GenerateRnd(2);
		}
		monster.goal = MonsterGoal::Move;
		if (monster.goalVar1++ >= static_cast<int>(2 * distanceToEnemy) || dTransVal[monster.position.tile.x][monster.position.tile.y] != dTransVal[monster.enemyPosition.x][monster.enemyPosition.y]) {
			monster.goal = MonsterGoal::Normal;
		} else if (!RoundWalk(monster, md, &monster.goalVar2)) {
			AiDelay(monster, GenerateRnd(10) + 10);
		}
	}

	if (monster.goal == MonsterGoal::Normal) {
		if ((distanceToEnemy >= 3) && v < 2 * monster.intelligence + 43) {
			Point position = monster.position.tile + monster.direction;
			if (monster.isTileAvailable(position) && MonsterManager.ActiveMonsterCount < MaxMonsters) {
				StartRangedSpecialAttack(monster, MissileID::HorkSpawn, 0);
			}
		} else if (distanceToEnemy < 2) {
			if (v < 2 * monster.intelligence + 28) {
				monster.direction = md;
				StartAttack(monster);
			}
		} else {
			v = GenerateRnd(100);
			if (v < 2 * monster.intelligence + 33
			    || (IsMonsterModeMove(static_cast<MonsterMode>(monster.var1))
			        && monster.var2 == 0
			        && v < 2 * monster.intelligence + 83)) {
				monster.randomWalk(md);
			} else {
				AiDelay(monster, GenerateRnd(10) + 10);
			}
		}
	}

	monster.checkStandAnimationIsLoaded(monster.direction);
}

std::string_view GetMonsterTypeText(const MonsterData &monsterData)
{
	switch (monsterData.monsterClass) {
	case MonsterClass::Animal:
		return _("Animal");
	case MonsterClass::Demon:
		return _("Demon");
	case MonsterClass::Undead:
		return _("Undead");
	}

	app_fatal(StrCat("Unknown monsterClass ", static_cast<int>(monsterData.monsterClass)));
}

void ActivateSpawn(Monster &monster, Point position, Direction dir)
{
	monster.occupyTile(position, false);
	monster.position.tile = position;
	monster.position.future = position;
	monster.position.old = position;
	StartSpecialStand(monster, dir);
}

/** Maps from monster AI ID to monster AI function. */
void (*AiProc[])(Monster &monster) = {
	/*MonsterAIID::Zombie         */ &ZombieAi,
	/*MonsterAIID::Fat            */ &OverlordAi,
	/*MonsterAIID::SkeletonMelee  */ &SkeletonAi,
	/*MonsterAIID::SkeletonRanged */ &SkeletonBowAi,
	/*MonsterAIID::Scavenger      */ &ScavengerAi,
	/*MonsterAIID::Rhino          */ &RhinoAi,
	/*MonsterAIID::GoatMelee      */ &AiAvoidance,
	/*MonsterAIID::GoatRanged     */ &AiRanged,
	/*MonsterAIID::Fallen         */ &FallenAi,
	/*MonsterAIID::Magma          */ &AiRangedAvoidance,
	/*MonsterAIID::SkeletonKing   */ &LeoricAi,
	/*MonsterAIID::Bat            */ &BatAi,
	/*MonsterAIID::Gargoyle       */ &GargoyleAi,
	/*MonsterAIID::Butcher        */ &ButcherAi,
	/*MonsterAIID::Succubus       */ &AiRanged,
	/*MonsterAIID::Sneak          */ &SneakAi,
	/*MonsterAIID::Storm          */ &AiRangedAvoidance,
	/*MonsterAIID::FireMan        */ nullptr,
	/*MonsterAIID::Gharbad        */ &GharbadAi,
	/*MonsterAIID::Acid           */ &AiRangedAvoidance,
	/*MonsterAIID::AcidUnique     */ &AiRanged,
	/*MonsterAIID::Golem          */ &GolumAi,
	/*MonsterAIID::Zhar           */ &ZharAi,
	/*MonsterAIID::Snotspill      */ &SnotSpilAi,
	/*MonsterAIID::Snake          */ &SnakeAi,
	/*MonsterAIID::Counselor      */ &CounselorAi,
	/*MonsterAIID::Mega           */ &MegaAi,
	/*MonsterAIID::Diablo         */ &AiRangedAvoidance,
	/*MonsterAIID::Lazarus        */ &LazarusAi,
	/*MonsterAIID::LazarusSuccubus*/ &LazarusMinionAi,
	/*MonsterAIID::Lachdanan      */ &LachdananAi,
	/*MonsterAIID::Warlord        */ &WarlordAi,
	/*MonsterAIID::FireBat        */ &AiRanged,
	/*MonsterAIID::Torchant       */ &AiRanged,
	/*MonsterAIID::HorkDemon      */ &HorkDemonAi,
	/*MonsterAIID::Lich           */ &AiRanged,
	/*MonsterAIID::ArchLich       */ &AiRanged,
	/*MonsterAIID::Psychorb       */ &AiRanged,
	/*MonsterAIID::Necromorb      */ &AiRanged,
	/*MonsterAIID::BoneDemon      */ &AiRangedAvoidance
};

// eftodo
bool IsRelativeMoveOK(const Monster &monster, Point position, Direction mdir)
{
	Point futurePosition = position + mdir;
	if (!InDungeonBounds(futurePosition) || !monster.isTileAvailable(futurePosition))
		return false;
	if (mdir == Direction::East) {
		if (IsTileSolid(position + Direction::SouthEast))
			return false;
	} else if (mdir == Direction::West) {
		if (IsTileSolid(position + Direction::SouthWest))
			return false;
	} else if (mdir == Direction::North) {
		if (IsTileSolid(position + Direction::NorthEast) || IsTileSolid(position + Direction::NorthWest))
			return false;
	} else if (mdir == Direction::South)
		if (IsTileSolid(position + Direction::SouthWest) || IsTileSolid(position + Direction::SouthEast))
			return false;
	return true;
}

bool UpdateModeStance(Monster &monster)
{
	switch (monster.mode) {
	case MonsterMode::Stand:
		MonsterIdle(monster);
		return false;
	case MonsterMode::MoveNorthwards:
	case MonsterMode::MoveSouthwards:
	case MonsterMode::MoveSideways:
		return monster.walk();
	case MonsterMode::MeleeAttack:
		return MonsterAttack(monster);
	case MonsterMode::HitRecovery:
		return MonsterGotHit(monster);
	case MonsterMode::Death:
		monster.death();
		return false;
	case MonsterMode::SpecialMeleeAttack:
		return MonsterSpecialAttack(monster);
	case MonsterMode::FadeIn:
		return MonsterFadein(monster);
	case MonsterMode::FadeOut:
		return MonsterFadeout(monster);
	case MonsterMode::RangedAttack:
		return MonsterRangedAttack(monster);
	case MonsterMode::SpecialStand:
		return MonsterSpecialStand(monster);
	case MonsterMode::SpecialRangedAttack:
		return MonsterRangedSpecialAttack(monster);
	case MonsterMode::Delay:
		return MonsterDelay(monster);
	case MonsterMode::Petrified:
		MonsterPetrified(monster);
		return false;
	case MonsterMode::Heal:
		MonsterHeal(monster);
		return false;
	case MonsterMode::Talk:
		MonsterTalk(monster);
		return false;
	default:
		return false;
	}
}

void EnsureMonsterIndexIsActive(size_t monsterId)
{
	assert(monsterId < MaxMonsters);
	for (size_t index = 0; index < MaxMonsters; index++) {
		if (MonsterManager.ActiveMonsters[index] != monsterId)
			continue;
		if (index < MonsterManager.ActiveMonsterCount)
			return; // monster is already active
		const unsigned oldId = MonsterManager.ActiveMonsters[MonsterManager.ActiveMonsterCount];
		MonsterManager.ActiveMonsters[MonsterManager.ActiveMonsterCount] = static_cast<unsigned>(monsterId);
		MonsterManager.ActiveMonsters[index] = oldId;
		MonsterManager.ActiveMonsterCount += 1;
	}
}

} // namespace

/**
 * @brief Sets the current cell sprite to match the desired desiredDirection and animation sequence
 * @param graphic Animation sequence of interest
 * @param desiredDirection Desired desiredDirection the monster should be visually facing
 */
void Monster::changeAnimationData(MonsterGraphic graphic, Direction desiredDirection)
{
	const AnimStruct &animationData = type().getAnimData(graphic);

	// Passing the frames and rate properties here is only relevant when initialising a monster, but doesn't cause any harm when switching animations.
	this->animInfo.changeAnimationData(animationData.spritesForDirection(desiredDirection), animationData.frames, animationData.rate);
}

/**
 * @brief Sets the current cell sprite to match the desired animation sequence using the direction the monster is currently facing
 * @param graphic Animation sequence of interest
 */
void Monster::changeAnimationData(MonsterGraphic graphic)
{
	this->changeAnimationData(graphic, this->direction);
}

const CMonster & Monster::type() const
{
	return Beastiary.LevelMonsterTypes[levelType];
}

const MonsterData & Monster::data() const
{
	return type().data();
}

	/**
 * @brief Returns monster's name
 * Internally it returns a name stored in global array of monsters' data.
 * @return Monster's name
 */
std::string_view Monster::name() const
{
	if (uniqueType != UniqueMonsterType::None)
		return pgettext("monster", UniqueMonstersData[static_cast<int8_t>(uniqueType)].mName);

	return pgettext("monster", data().name);
}

/**
 * @brief Calculates monster's experience points.
 * Fetches base exp value from @p MonstersData array.
 * @param difficulty - difficulty on which calculation is performed
 * @return Monster's experience points, including bonuses from difficulty and monster being unique
 */
unsigned int Monster::exp(_difficulty difficulty) const
{
	unsigned int monsterExp = data().exp;

	if (difficulty == DIFF_NIGHTMARE) {
		monsterExp = 2 * (monsterExp + 1000);
	} else if (difficulty == DIFF_HELL) {
		monsterExp = 4 * (monsterExp + 1000);
	}

	if (isUnique()) {
		monsterExp *= 2;
	}

	return monsterExp;
}

/**
 * @brief Calculates monster's level.
 * Fetches base level value from @p MonstersData array or @p UniqueMonstersData.
 * @param difficulty - difficulty on which calculation is performed
 * @return Monster's level, including bonuses from difficulty and monster being unique
 */
unsigned int Monster::level(_difficulty difficulty) const
{
	unsigned int baseLevel = data().level;
	if (isUnique()) {
		baseLevel = UniqueMonstersData[static_cast<int8_t>(uniqueType)].mlevel;
		if (baseLevel != 0) {
			baseLevel *= 2;
		} else {
			baseLevel = data().level + 5;
		}
	}

	if (type().type == MT_DIABLO && !gbIsHellfire) {
		baseLevel -= 15;
	}

	if (difficulty == DIFF_NIGHTMARE) {
		baseLevel += 15;
	} else if (difficulty == DIFF_HELL) {
		baseLevel += 30;
	}

	return baseLevel;
}


void Monster::death()
{
	var1++;
	if (type().type == MT_DIABLO) {
		if (position.tile.x < ViewPosition.x) {
			ViewPosition.x--;
		} else if (position.tile.x > ViewPosition.x) {
			ViewPosition.x++;
		}

		if (position.tile.y < ViewPosition.y) {
			ViewPosition.y--;
		} else if (position.tile.y > ViewPosition.y) {
			ViewPosition.y++;
		}

		if (var1 == 140)
			PrepDoEnding();
	} else if (animInfo.isLastFrame()) {
		if (isUnique())
			AddCorpse(position.tile, corpseId, direction);
		else
			AddCorpse(position.tile, type().corpseId, direction);

		dMonster[position.tile.x][position.tile.y] = 0;
		isInvalid = true;

		updateRelations();
	}
}

bool Monster::randomWalk(Direction md)
{
	Direction mdtemp = md;

	bool ok = dirOK(md);
	if (FlipCoin())
		ok = ok || (md = Right(mdtemp), dirOK(md)) || (md = Left(mdtemp), dirOK(md));
	else
		ok = ok || (md = Left(mdtemp), dirOK(md)) || (md = Right(mdtemp), dirOK(md));
	if (FlipCoin()) {
		ok = ok
		    || (md = Left(Left(mdtemp)), dirOK(md))
		    || (md = Right(Right(mdtemp)), dirOK(md));
	} else {
		ok = ok
		    || (md = Right(Right(mdtemp)), dirOK(md))
		    || (md = Left(Left(mdtemp)), dirOK(md));
	}
	if (ok)
		walk(md);
	return ok;
}

// eftodo
void InitTRNForUniqueMonster(Monster &monster)
{
	char filestr[64];
	*BufCopy(filestr, R"(monsters\monsters\)", UniqueMonstersData[static_cast<size_t>(monster.uniqueType)].mTrnName, ".trn") = '\0';
	monster.uniqueMonsterTRN = LoadFileInMem<uint8_t>(filestr);
}

void PrepareUniqueMonst(Monster &monster, UniqueMonsterType monsterType, size_t minionType, int bosspacksize, const UniqueMonsterData &uniqueMonsterData)
{
	monster.uniqueType = monsterType;
	monster.maxHitPoints = uniqueMonsterData.mmaxhp << 6;

	if (!gbIsMultiplayer)
		monster.maxHitPoints = std::max(monster.maxHitPoints / 2, 64);

	monster.hitPoints = monster.maxHitPoints;
	monster.ai = uniqueMonsterData.mAi;
	monster.intelligence = uniqueMonsterData.mint;
	monster.minDamage = uniqueMonsterData.mMinDamage;
	monster.maxDamage = uniqueMonsterData.mMaxDamage;
	monster.minDamageSpecial = uniqueMonsterData.mMinDamage;
	monster.maxDamageSpecial = uniqueMonsterData.mMaxDamage;
	monster.resistance = uniqueMonsterData.mMagicRes;
	monster.talkMsg = uniqueMonsterData.mtalkmsg;
	if (monsterType == UniqueMonsterType::HorkDemon)
		monster.lightId = NO_LIGHT;
	else
		monster.lightId = AddLight(monster.position.tile, 3);

	if (UseMultiplayerQuests()) {
		if (monster.ai == MonsterAIID::LazarusSuccubus)
			monster.talkMsg = TEXT_NONE;
		if (monster.ai == MonsterAIID::Lazarus && Quests[Q_BETRAYER]._qvar1 > 3) {
			monster.goal = MonsterGoal::Normal;
		} else if (monster.talkMsg != TEXT_NONE) {
			monster.goal = MonsterGoal::Inquiring;
		}
	} else if (monster.talkMsg != TEXT_NONE) {
		monster.goal = MonsterGoal::Inquiring;
	}

	if (sgGameInitInfo.nDifficulty == DIFF_NIGHTMARE) {
		monster.maxHitPoints = 3 * monster.maxHitPoints;
		if (gbIsHellfire)
			monster.maxHitPoints += (gbIsMultiplayer ? 100 : 50) << 6;
		else
			monster.maxHitPoints += 100 << 6;
		monster.hitPoints = monster.maxHitPoints;
		monster.minDamage = 2 * (monster.minDamage + 2);
		monster.maxDamage = 2 * (monster.maxDamage + 2);
		monster.minDamageSpecial = 2 * (monster.minDamageSpecial + 2);
		monster.maxDamageSpecial = 2 * (monster.maxDamageSpecial + 2);
	} else if (sgGameInitInfo.nDifficulty == DIFF_HELL) {
		monster.maxHitPoints = 4 * monster.maxHitPoints;
		if (gbIsHellfire)
			monster.maxHitPoints += (gbIsMultiplayer ? 200 : 100) << 6;
		else
			monster.maxHitPoints += 200 << 6;
		monster.hitPoints = monster.maxHitPoints;
		monster.minDamage = 4 * monster.minDamage + 6;
		monster.maxDamage = 4 * monster.maxDamage + 6;
		monster.minDamageSpecial = 4 * monster.minDamageSpecial + 6;
		monster.maxDamageSpecial = 4 * monster.maxDamageSpecial + 6;
	}

	InitTRNForUniqueMonster(monster);
	monster.uniqTrans = MonsterManager.uniquetrans++;

	if (uniqueMonsterData.customToHit != 0) {
		monster.toHit = uniqueMonsterData.customToHit;

		if (sgGameInitInfo.nDifficulty == DIFF_NIGHTMARE) {
			monster.toHit += NightmareToHitBonus;
		} else if (sgGameInitInfo.nDifficulty == DIFF_HELL) {
			monster.toHit += HellToHitBonus;
		}
	}
	if (uniqueMonsterData.customArmorClass != 0) {
		monster.armorClass = uniqueMonsterData.customArmorClass;

		if (sgGameInitInfo.nDifficulty == DIFF_NIGHTMARE) {
			monster.armorClass += NightmareAcBonus;
		} else if (sgGameInitInfo.nDifficulty == DIFF_HELL) {
			monster.armorClass += HellAcBonus;
		}
	}

	if (uniqueMonsterData.monsterPack != UniqueMonsterPack::None) {
		MonsterManager.PlaceGroup(minionType, bosspacksize, &monster, uniqueMonsterData.monsterPack == UniqueMonsterPack::Leashed);
	}

	if (monster.ai != MonsterAIID::Gargoyle) {
		monster.changeAnimationData(MonsterGraphic::Stand);
		monster.animInfo.currentFrame = GenerateRnd(monster.animInfo.numberOfFrames - 1);
		monster.flags &= ~MFLAG_ALLOW_SPECIAL;
		monster.mode = MonsterMode::Stand;
	}
}

void WeakenNaKrul()
{
	if (currlevel != 24 || static_cast<size_t>(UberDiabloMonsterIndex) >= MonsterManager.ActiveMonsterCount)
		return;

	Monster &monster = MonsterManager.Monsters[UberDiabloMonsterIndex];
	monster.playEffect(MonsterSound::Death);
	monster.armorClass -= 50;
	int hp = monster.maxHitPoints / 2;
	monster.resistance = 0;
	monster.hitPoints = hp;
	monster.maxHitPoints = hp;
}

void InitGolems()
{
	if (!setlevel) {
		for (int i = 0; i < MAX_PLRS; i++)
			MonsterManager.AddMonster(GolemHoldingCell, Direction::South, 0, false);
	}
}

void InitMonsters()
{
	if (!gbIsSpawn && !setlevel && currlevel == 16)
		MonsterManager.LoadDiabMonsts();

	int nt = numtrigs;
	if (currlevel == 15)
		nt = 1;
	for (int i = 0; i < nt; i++) {
		for (int s = -2; s < 2; s++) {
			for (int t = -2; t < 2; t++)
				DoVision(trigs[i].position + Displacement { s, t }, 15, MAP_EXP_NONE, false);
		}
	}
	if (!gbIsSpawn)
		MonsterManager.PlaceQuestMonsters();
	if (!setlevel) {
		if (!gbIsSpawn)
			MonsterManager.PlaceUniqueMonsters();
		size_t na = 0;
		for (int s = 16; s < 96; s++) {
			for (int t = 16; t < 96; t++) {
				if (!IsTileSolid({ s, t }))
					na++;
			}
		}
		size_t numplacemonsters = na / 30;
		if (gbIsMultiplayer)
			numplacemonsters += numplacemonsters / 2;
		if (MonsterManager.ActiveMonsterCount + numplacemonsters > MaxMonsters - 10)
			numplacemonsters = MaxMonsters - 10 - MonsterManager.ActiveMonsterCount;
		MonsterManager.totalmonsters = MonsterManager.ActiveMonsterCount + numplacemonsters;
		int numscattypes = 0;
		size_t scattertypes[NUM_MTYPES];
		for (size_t i = 0; i < Beastiary.LevelMonsterTypeCount; i++) {
			if ((Beastiary.LevelMonsterTypes[i].placeFlags & PLACE_SCATTER) != 0) {
				scattertypes[numscattypes] = i;
				numscattypes++;
			}
		}
		while (MonsterManager.ActiveMonsterCount < MonsterManager.totalmonsters) {
			const size_t typeIndex = scattertypes[GenerateRnd(numscattypes)];
			if (currlevel == 1 || FlipCoin())
				na = 1;
			else if (currlevel == 2 || leveltype == DTYPE_CRYPT)
				na = GenerateRnd(2) + 2;
			else
				na = GenerateRnd(3) + 3;
			MonsterManager.PlaceGroup(typeIndex, na);
		}
	}
	for (int i = 0; i < nt; i++) {
		for (int s = -2; s < 2; s++) {
			for (int t = -2; t < 2; t++)
				DoUnVision(trigs[i].position + Displacement { s, t }, 15);
		}
	}

	Beastiary.InitAllMonsterGFX();
}

void SpawnMonster(Point position, Direction dir, size_t typeIndex, bool startSpecialStand /*= false*/)
{
	if (MonsterManager.ActiveMonsterCount >= MaxMonsters)
		return;

	// The command is only executed for the level owner, to prevent desyncs in multiplayer.
	if (!MyPlayer->isLevelOwnedByLocalClient())
		return;

	size_t monsterIndex = MonsterManager.ActiveMonsters[MonsterManager.ActiveMonsterCount];
	MonsterManager.ActiveMonsterCount += 1;
	uint32_t seed = GetLCGEngineState();
	// Update local state immediately to increase ActiveMonsterCount instantly (this allows multiple monsters to be spawned in one game tick)
	InitializeSpawnedMonster(position, dir, typeIndex, monsterIndex, seed);
	NetSendCmdSpawnMonster(position, dir, static_cast<uint16_t>(typeIndex), static_cast<uint16_t>(monsterIndex), seed);
}

void LoadDeltaSpawnedMonster(size_t typeIndex, size_t monsterId, uint32_t seed)
{
	SetRndSeed(seed);
	EnsureMonsterIndexIsActive(monsterId);
	WorldTilePosition position = GolemHoldingCell;
	Monster &monster = MonsterManager.Monsters[monsterId];
	monster.clearSquares();
	monster.InitMonster(Direction::South, typeIndex, position);
}

void InitializeSpawnedMonster(Point position, Direction dir, size_t typeIndex, size_t monsterId, uint32_t seed)
{
	SetRndSeed(seed);
	EnsureMonsterIndexIsActive(monsterId);
	Monster &monster = MonsterManager.Monsters[monsterId];
	monster.clearSquares();

	// When we receive a network message, the position we got for the new monster may already be occupied.
	// That's why we check for the next free tile for the monster.
	auto freePosition = Crawl(0, MaxCrawlRadius, [&](Displacement displacement) -> std::optional<Point> {
		Point posToCheck = position + displacement;
		if (IsTileAvailable(posToCheck))
			return posToCheck;
		return {};
	});

	assert(freePosition);
	assert(!MyPlayer->isLevelOwnedByLocalClient() || (freePosition && position == *freePosition));
	position = freePosition.value_or(position);

	monster.occupyTile(position, false);
	monster.InitMonster(dir, typeIndex, position);

	if (IsSkel(monster.type().type))
		StartSpecialStand(monster, dir);
	else
		monster.startStand(dir);
}

void AddDoppelganger(Monster &monster)
{
	Point target = { 0, 0 };
	for (int d = 0; d < 8; d++) {
		const Point position = monster.position.tile + static_cast<Direction>(d);
		if (!IsTileAvailable(position))
			continue;
		target = position;
	}
	if (target != Point { 0, 0 }) {
		const size_t typeIndex = Beastiary.GetMonsterTypeIndex(monster.type().type);
		SpawnMonster(target, monster.direction, typeIndex);
	}
}

// eftodo rename
void Monster::InitMonster(Direction rd, size_t typeIndex, Point position)
{
	direction = rd;
	this->position.tile = position;
	this->position.future = position;
	this->position.old = position;
	levelType = static_cast<uint8_t>(typeIndex);
	mode = MonsterMode::Stand;
	animInfo = {};
	changeAnimationData(MonsterGraphic::Stand);
	animInfo.tickCounterOfCurrentFrame = GenerateRnd(animInfo.ticksPerFrame - 1);
	animInfo.currentFrame = GenerateRnd(animInfo.numberOfFrames - 1);

	int maxhp = RandomIntBetween(data().hitPointsMinimum, data().hitPointsMaximum);
	if (type().type == MT_DIABLO && !gbIsHellfire) {
		maxhp /= 2;
	}
	maxHitPoints = maxhp << 6;

	if (!gbIsMultiplayer)
		maxHitPoints = std::max(maxHitPoints / 2, 64);

	hitPoints = maxHitPoints;
	ai = data().ai;
	intelligence = data().intelligence;
	goal = MonsterGoal::Normal;
	goalVar1 = 0;
	goalVar2 = 0;
	goalVar3 = 0;
	pathCount = 0;
	isInvalid = false;
	uniqueType = UniqueMonsterType::None;
	activeForTicks = 0;
	lightId = NO_LIGHT;
	rndItemSeed = AdvanceRndSeed();
	aiSeed = AdvanceRndSeed();
	whoHit = 0;
	toHit = data().toHit;
	minDamage = data().minDamage;
	maxDamage = data().maxDamage;
	minDamageSpecial = data().minDamageSpecial;
	maxDamageSpecial = data().maxDamageSpecial;
	armorClass = data().armorClass;
	resistance = data().resistance;
	leader = Monster::NoLeader;
	leaderRelation = LeaderRelation::None;
	flags = data().abilityFlags;
	talkMsg = TEXT_NONE;

	if (ai == MonsterAIID::Gargoyle) {
		changeAnimationData(MonsterGraphic::Special);
		animInfo.currentFrame = 0;
		flags |= MFLAG_ALLOW_SPECIAL;
		mode = MonsterMode::SpecialMeleeAttack;
	}

	if (sgGameInitInfo.nDifficulty == DIFF_NIGHTMARE) {
		maxHitPoints = 3 * maxHitPoints;
		if (gbIsHellfire)
			maxHitPoints += (gbIsMultiplayer ? 100 : 50) << 6;
		else
			maxHitPoints += 100 << 6;
		hitPoints = maxHitPoints;
		toHit += NightmareToHitBonus;
		minDamage = 2 * (minDamage + 2);
		maxDamage = 2 * (maxDamage + 2);
		minDamageSpecial = 2 * (minDamageSpecial + 2);
		maxDamageSpecial = 2 * (maxDamageSpecial + 2);
		armorClass += NightmareAcBonus;
	} else if (sgGameInitInfo.nDifficulty == DIFF_HELL) {
		maxHitPoints = 4 * maxHitPoints;
		if (gbIsHellfire)
			maxHitPoints += (gbIsMultiplayer ? 200 : 100) << 6;
		else
			maxHitPoints += 200 << 6;
		hitPoints = maxHitPoints;
		toHit += HellToHitBonus;
		minDamage = 4 * minDamage + 6;
		maxDamage = 4 * maxDamage + 6;
		minDamageSpecial = 4 * minDamageSpecial + 6;
		maxDamageSpecial = 4 * maxDamageSpecial + 6;
		armorClass += HellAcBonus;
		resistance = data().resistanceHell;
	}
}


void Monster::applyDamage(DamageType damageType, int damage)
{
	AddFloatingNumber(damageType, *this, damage);

	hitPoints -= damage;

	if (hitPoints >> 6 <= 0) {
		delta_kill_monster(*this, position.tile, *MyPlayer);
		NetSendCmdLocParam1(false, CMD_MONSTDEATH, position.tile, static_cast<uint16_t>(getId()));
		return;
	}

	delta_monster_hp(*this, *MyPlayer);
	NetSendCmdMonDmg(false, static_cast<uint16_t>(getId()), damage);
}

bool Monster::isTalker() const
{
	return IsAnyOf(ai, MonsterAIID::Lazarus, MonsterAIID::Warlord, MonsterAIID::Gharbad, MonsterAIID::Zhar, MonsterAIID::Snotspill, MonsterAIID::Lachdanan, MonsterAIID::LazarusSuccubus);
}

void Monster::startStand(Direction md)
{
	if (md == Direction::NoDirection) md = direction;

	ClearMVars();
	if (type().type == MT_GOLEM)
		NewMonsterAnim(*this, MonsterGraphic::Walk, md);
	else
		NewMonsterAnim(*this, MonsterGraphic::Stand, md);
	var1 = static_cast<int>(mode);
	var2 = 0;
	mode = MonsterMode::Stand;
	position.future = position.tile;
	position.old = position.tile;
	UpdateEnemy(*this);
}

void Monster::clearSquares() const
{
	for (Point searchTile : PointsInRectangle(Rectangle { position.old, 1 })) {
		if (FindMonsterAtPosition(searchTile) == this)
			dMonster[searchTile.x][searchTile.y] = 0;
	}
}

void Monster::getKnockback(WorldTilePosition attackerStartPos)
{
	Direction dir = GetDirection(attackerStartPos, position.tile);
	if (!IsRelativeMoveOK(*this, position.old, dir)) {
		return;
	}

	clearSquares();
	position.old += dir;
	StartMonsterGotHit(*this);
	ChangeLightXY(lightId, position.tile);
}

void Monster::startHit(int dam)
{
	playEffect(MonsterSound::Hit);

	if (IsHardHit(*this, dam)) {
		if (type().type == MT_BLINK) {
			Teleport(*this);
		} else if (IsAnyOf(type().type, MT_NSCAV, MT_BSCAV, MT_WSCAV, MT_YSCAV, MT_GRAVEDIG)) {
			goal = MonsterGoal::Normal;
			goalVar1 = 0;
			goalVar2 = 0;
		}
		if (mode != MonsterMode::Petrified) {
			StartMonsterGotHit(*this);
		}
	}
}

void Monster::startHit(const Player &player, int dam)
{
	tag(player);
	if (IsHardHit(*this, dam)) {
		enemy = player.getId();
		enemyPosition = player.position.future;
		flags &= ~MFLAG_TARGETS_MONSTER;
		if (mode != MonsterMode::Petrified) {
			direction = GetMonsterDirection(*this);
		}
	}

	startHit(dam);
}

void Monster::death(Direction md, bool sendmsg)
{
	if (!isPlayerMinion())
		AddPlrMonstExper(level(sgGameInitInfo.nDifficulty), exp(sgGameInitInfo.nDifficulty), whoHit);

	MonsterKillCounts[type().type]++;
	hitPoints = 0;
	flags &= ~MFLAG_HIDDEN;
	SetRndSeed(rndItemSeed);

	SpawnLoot(*this, sendmsg);

	if (type().type == MT_DIABLO)
		DiabloDeath(*this, true);
	else
		playEffect(MonsterSound::Death);

	if (mode != MonsterMode::Petrified) {
		if (type().type == MT_GOLEM)
			md = Direction::South;
		NewMonsterAnim(*this, MonsterGraphic::Death, md, gGameLogicStep < GameLogicStep::ProcessMonsters ? AnimationDistributionFlags::ProcessAnimationPending : AnimationDistributionFlags::None);
		mode = MonsterMode::Death;
	} else if (isUnique()) {
		AddUnLight(lightId);
	}
	goal = MonsterGoal::None;
	var1 = 0;
	position.tile = position.old;
	position.future = position.old;
	clearSquares();
	occupyTile(position.tile, false);
	CheckQuestKill(*this, sendmsg);
	M_FallenFear(position.tile);
	if (IsAnyOf(type().type, MT_NACID, MT_RACID, MT_BACID, MT_XACID, MT_SPIDLORD))
		AddMissile(position.tile, { 0, 0 }, Direction::South, MissileID::AcidPuddle, TARGET_PLAYERS, *this, intelligence + 1, 0);
}

void Monster::startDeath(const Player &player, bool sendmsg)
{
	this->tag(player);
	Direction md = GetDirection(this->position.tile, player.position.tile);
	death(md, sendmsg);
}

void KillMyGolem()
{
	Monster &golem = MonsterManager.Monsters[MyPlayerId];
	delta_kill_monster(golem, golem.position.tile, *MyPlayer);
	NetSendCmdLoc(MyPlayerId, false, CMD_KILLGOLEM, golem.position.tile);
	golem.startKill(*MyPlayer);
}

void Monster::startKill(const Player &player)
{
	startDeath(player, true);
}

void Monster::syncStartKill(Point position, const Player &player)
{
	if (hitPoints == 0 || mode == MonsterMode::Death) {
		return;
	}

	if (dMonster[position.x][position.y] == 0) {
		clearSquares();
		this->position.tile = position;
		this->position.old = position;
	}

	startDeath(player, false);
}

void Monster::updateRelations() const
{
	if (hasLeashedMinions())
		ReleaseMinions(*this);

	ShrinkLeaderPacksize(*this);
}

void DoEnding()
{
	if (gbIsMultiplayer) {
		SNetLeaveGame(LEAVE_ENDING);
	}

	music_stop();

	if (gbIsMultiplayer) {
		SDL_Delay(1000);
	}

	if (gbIsSpawn)
		return;

	switch (MyPlayer->_pClass) {
	case HeroClass::Sorcerer:
	case HeroClass::Monk:
		play_movie("gendata\\diabvic1.smk", false);
		break;
	case HeroClass::Warrior:
	case HeroClass::Barbarian:
		play_movie("gendata\\diabvic2.smk", false);
		break;
	default:
		play_movie("gendata\\diabvic3.smk", false);
		break;
	}
	play_movie("gendata\\diabend.smk", false);

	bool bMusicOn = gbMusicOn;
	gbMusicOn = true;

	int musicVolume = sound_get_or_set_music_volume(1);
	sound_get_or_set_music_volume(0);

	music_start(TMUSIC_CATACOMBS);
	loop_movie = true;
	play_movie("gendata\\loopdend.smk", true);
	loop_movie = false;
	music_stop();

	sound_get_or_set_music_volume(musicVolume);
	gbMusicOn = bMusicOn;
}

/**
 * @brief Continue movement towards new tile
 */
bool Monster::walk()
{
	// Check if we reached new tile
	const bool isAnimationEnd = animInfo.isLastFrame();
	if (isAnimationEnd) {
		dMonster[position.tile.x][position.tile.y] = 0;
		position.tile.x += var1;
		position.tile.y += var2;
		// dMonster is set here for backwards compatibility; without it, the monster would be invisible if loaded from a vanilla save.
		occupyTile(position.tile, false);
		ChangeLightXY(lightId, position.tile);
		startStand();
	} else { // We didn't reach new tile so update monster's "sub-tile" position
		if (animInfo.tickCounterOfCurrentFrame == 0) {
			if (animInfo.currentFrame == 0 && type().type == MT_FLESTHNG)
				playEffect(MonsterSound::Special);
		}
	}

	SyncLightPosition(*this);

	return isAnimationEnd;
}

bool Monster::walk(Direction md)
{
	if (!dirOK(md)) {
		return false;
	}

	if (md == Direction::NoDirection)
		return true;

	WalkInDirection(*this, md);
	return true;
}


void PrepDoEnding()
{
	gbSoundOn = sgbSaveSoundOn;
	gbRunGame = false;
	MyPlayerIsDead = false;
	cineflag = true;

	Player &myPlayer = *MyPlayer;

	myPlayer.pDiabloKillLevel = std::max(myPlayer.pDiabloKillLevel, static_cast<uint8_t>(sgGameInitInfo.nDifficulty + 1));

	for (Player &player : Players) {
		player._pmode = PM_QUIT;
		player._pInvincible = true;
		if (gbIsMultiplayer) {
			if (player._pHitPoints >> 6 == 0)
				player._pHitPoints = 64;
			if (player._pMana >> 6 == 0)
				player._pMana = 64;
		}
	}
}

bool Walk(Monster &monster, Direction md)
{
	if (!monster.dirOK(md)) {
		return false;
	}

	if (md == Direction::NoDirection)
		return true;

	WalkInDirection(monster, md);
	return true;
}

void GolumAi(Monster &golem)
{
	if (golem.position.tile.x == 1 && golem.position.tile.y == 0) {
		return;
	}

	if (IsAnyOf(golem.mode, MonsterMode::Death, MonsterMode::SpecialStand) || golem.isWalking()) {
		return;
	}

	if ((golem.flags & MFLAG_TARGETS_MONSTER) == 0)
		UpdateEnemy(golem);

	if (golem.mode == MonsterMode::MeleeAttack) {
		return;
	}

	if ((golem.flags & MFLAG_NO_ENEMY) == 0) {
		Monster &enemy = MonsterManager.Monsters[golem.enemy];
		int mex = golem.position.tile.x - enemy.position.future.x;
		int mey = golem.position.tile.y - enemy.position.future.y;
		golem.direction = GetDirection(golem.position.tile, enemy.position.tile);
		if (std::abs(mex) < 2 && std::abs(mey) < 2) {
			golem.enemyPosition = enemy.position.tile;
			if (enemy.activeForTicks == 0) {
				enemy.activeForTicks = UINT8_MAX;
				enemy.position.last = golem.position.tile;
				for (int j = 0; j < 5; j++) {
					for (int k = 0; k < 5; k++) {
						int mx = golem.position.tile.x + k - 2;
						int my = golem.position.tile.y + j - 2;
						if (!InDungeonBounds({ mx, my }))
							continue;
						int enemyId = dMonster[mx][my];
						if (enemyId > 0)
							MonsterManager.Monsters[enemyId - 1].activeForTicks = UINT8_MAX;
					}
				}
			}
			StartAttack(golem);
			return;
		}
		if (AiPlanPath(golem))
			return;
	}

	golem.pathCount++;
	if (golem.pathCount > 8)
		golem.pathCount = 5;

	if (golem.randomWalk(Players[golem.getId()]._pdir))
		return;

	Direction md = Left(golem.direction);
	for (int j = 0; j < 8; j++) {
		md = Right(md);
		if (Walk(golem, md)) {
			break;
		}
	}
}

void DeleteMonsterList()
{
	for (int i = 0; i < MAX_PLRS; i++) {
		Monster &golem = MonsterManager.Monsters[i];
		if (!golem.isInvalid)
			continue;

		golem.position.tile = GolemHoldingCell;
		golem.position.future = { 0, 0 };
		golem.position.old = { 0, 0 };
		golem.isInvalid = false;
	}

	for (size_t i = MAX_PLRS; i < MonsterManager.ActiveMonsterCount;) {
		if (MonsterManager.Monsters[MonsterManager.ActiveMonsters[i]].isInvalid) {
			if (pcursmonst == static_cast<int>(MonsterManager.ActiveMonsters[i])) // Unselect monster if player highlighted it
				pcursmonst = -1;
			MonsterManager.DeleteMonster(i);
		} else {
			i++;
		}
	}
}

void ProcessMonsters()
{
	DeleteMonsterList();

	assert(MonsterManager.ActiveMonsterCount <= MaxMonsters);
	for (size_t i = 0; i < MonsterManager.ActiveMonsterCount; i++) {
		Monster &monster = MonsterManager.Monsters[MonsterManager.ActiveMonsters[i]];
		FollowTheLeader(monster);
		if (gbIsMultiplayer) {
			SetRndSeed(monster.aiSeed);
			monster.aiSeed = AdvanceRndSeed();
		}
		if (monster.hitPoints < monster.maxHitPoints && monster.hitPoints >> 6 > 0) {
			if (monster.level(sgGameInitInfo.nDifficulty) > 1) {
				monster.hitPoints += monster.level(sgGameInitInfo.nDifficulty) / 2;
			} else {
				monster.hitPoints += monster.level(sgGameInitInfo.nDifficulty);
			}
			monster.hitPoints = std::min(monster.hitPoints, monster.maxHitPoints); // prevent going over max HP with part of a single regen tick
		}

		if (IsTileVisible(monster.position.tile) && monster.activeForTicks == 0) {
			if (monster.type().type == MT_CLEAVER) {
				PlaySFX(SfxID::ButcherGreeting);
			}
			if (monster.type().type == MT_NAKRUL) {
				if (sgGameInitInfo.bCowQuest != 0) {
					PlaySFX(SfxID::NaKrul6);
				} else {
					if (IsUberRoomOpened)
						PlaySFX(SfxID::NaKrul4);
					else
						PlaySFX(SfxID::NaKrul5);
				}
			}
			if (monster.type().type == MT_DEFILER)
				PlaySFX(SfxID::Defiler8);
			UpdateEnemy(monster);
		}

		if ((monster.flags & MFLAG_TARGETS_MONSTER) != 0) {
			assert(monster.enemy >= 0 && monster.enemy < MaxMonsters);
			// BUGFIX: enemy target may be dead at time of access, thus reading garbage data from `Monsters[monster.enemy].position.future`.
			monster.position.last = MonsterManager.Monsters[monster.enemy].position.future;
			monster.enemyPosition = monster.position.last;
		} else {
			assert(monster.enemy >= 0 && monster.enemy < MAX_PLRS);
			Player &player = Players[monster.enemy];
			monster.enemyPosition = player.position.future;
			if (IsTileVisible(monster.position.tile)) {
				monster.activeForTicks = UINT8_MAX;
				monster.position.last = player.position.future;
			} else if (monster.activeForTicks != 0 && monster.type().type != MT_DIABLO) {
				monster.activeForTicks--;
			}
		}
		while (true) {
			if ((monster.flags & MFLAG_SEARCH) == 0 || !AiPlanPath(monster)) {
				AiProc[static_cast<int8_t>(monster.ai)](monster);
			}

			if (!UpdateModeStance(monster))
				break;

			GroupUnity(monster);
		}
		if (monster.mode != MonsterMode::Petrified && (monster.flags & MFLAG_ALLOW_SPECIAL) == 0) {
			monster.animInfo.processAnimation((monster.flags & MFLAG_LOCK_ANIMATION) != 0);
		}
	}

	DeleteMonsterList();
}


bool Monster::dirOK(Direction mdir) const
{
	Point position = this->position.tile;
	Point futurePosition = position + mdir;
	if (!IsRelativeMoveOK(*this, position, mdir))
		return false;
	if (leaderRelation == LeaderRelation::Leashed) {
		return futurePosition.WalkingDistance(getLeader()->position.future) < 4;
	}
	if (!hasLeashedMinions())
		return true;
	int mcount = 0;
	for (int x = futurePosition.x - 3; x <= futurePosition.x + 3; x++) {
		for (int y = futurePosition.y - 3; y <= futurePosition.y + 3; y++) {
			if (!InDungeonBounds({ x, y }))
				continue;
			Monster *minion = FindMonsterAtPosition({ x, y }, true);
			if (minion == nullptr)
				continue;

			if (minion->leaderRelation == LeaderRelation::Leashed && minion->getLeader() == this) {
				mcount++;
			}
		}
	}
	return mcount == packSize;
}

bool PosOkMissile(Point position)
{
	return !TileHasAny(position, TileProperties::BlockMissile);
}

bool LineClearMissile(Point startPoint, Point endPoint)
{
	return LineClear(PosOkMissile, startPoint, endPoint);
}

bool LineClear(tl::function_ref<bool(Point)> clear, Point startPoint, Point endPoint)
{
	Point position = startPoint;

	int dx = endPoint.x - position.x;
	int dy = endPoint.y - position.y;
	if (std::abs(dx) > std::abs(dy)) {
		if (dx < 0) {
			std::swap(position, endPoint);
			dx = -dx;
			dy = -dy;
		}
		int d;
		int yincD;
		int dincD;
		int dincH;
		if (dy > 0) {
			d = 2 * dy - dx;
			dincD = 2 * dy;
			dincH = 2 * (dy - dx);
			yincD = 1;
		} else {
			d = 2 * dy + dx;
			dincD = 2 * dy;
			dincH = 2 * (dx + dy);
			yincD = -1;
		}
		bool done = false;
		while (!done && position != endPoint) {
			if ((d <= 0) ^ (yincD < 0)) {
				d += dincD;
			} else {
				d += dincH;
				position.y += yincD;
			}
			position.x++;
			done = position != startPoint && !clear(position);
		}
	} else {
		if (dy < 0) {
			std::swap(position, endPoint);
			dy = -dy;
			dx = -dx;
		}
		int d;
		int xincD;
		int dincD;
		int dincH;
		if (dx > 0) {
			d = 2 * dx - dy;
			dincD = 2 * dx;
			dincH = 2 * (dx - dy);
			xincD = 1;
		} else {
			d = 2 * dx + dy;
			dincD = 2 * dx;
			dincH = 2 * (dy + dx);
			xincD = -1;
		}
		bool done = false;
		while (!done && position != endPoint) {
			if ((d <= 0) ^ (xincD < 0)) {
				d += dincD;
			} else {
				d += dincH;
				position.x += xincD;
			}
			position.y++;
			done = position != startPoint && !clear(position);
		}
	}
	return position == endPoint;
}

void Monster::syncAnim()
{
#ifdef _DEBUG
	// fix for saves with debug monsters having type originally not on the level
	CMonster &monsterType = Beastiary.LevelMonsterTypes[levelType];
	if (monsterType.corpseId == 0) {
		Beastiary.InitMonsterGFX(monsterType);
		monsterType.corpseId = 1;
	}
#endif
	if (isUnique()) {
		InitTRNForUniqueMonster(*this);
	}
	MonsterGraphic graphic = MonsterGraphic::Stand;

	switch (getVisualMonsterMode()) {
	case MonsterMode::Stand:
	case MonsterMode::Delay:
	case MonsterMode::Talk:
		break;
	case MonsterMode::MoveNorthwards:
	case MonsterMode::MoveSouthwards:
	case MonsterMode::MoveSideways:
		graphic = MonsterGraphic::Walk;
		break;
	case MonsterMode::MeleeAttack:
	case MonsterMode::RangedAttack:
		graphic = MonsterGraphic::Attack;
		break;
	case MonsterMode::HitRecovery:
		graphic = MonsterGraphic::GotHit;
		break;
	case MonsterMode::Death:
		graphic = MonsterGraphic::Death;
		break;
	case MonsterMode::SpecialMeleeAttack:
	case MonsterMode::FadeIn:
	case MonsterMode::FadeOut:
	case MonsterMode::SpecialStand:
	case MonsterMode::SpecialRangedAttack:
	case MonsterMode::Heal:
		graphic = MonsterGraphic::Special;
		break;
	case MonsterMode::Charge:
		graphic = MonsterGraphic::Attack;
		animInfo.currentFrame = 0;
		break;
	default:
		animInfo.currentFrame = 0;
		break;
	}

	changeAnimationData(graphic);
}

void M_FallenFear(Point position)
{
	const Rectangle fearArea = Rectangle { position, 4 };
	for (const Point tile : PointsInRectangle(fearArea)) {
		if (!InDungeonBounds(tile))
			continue;
		int m = dMonster[tile.x][tile.y];
		if (m == 0)
			continue;
		Monster &monster = MonsterManager.Monsters[std::abs(m) - 1];
		if (monster.ai != MonsterAIID::Fallen || monster.hitPoints >> 6 <= 0)
			continue;

		int runDistance = std::max((8 - monster.data().level), 2);
		monster.goal = MonsterGoal::Retreat;
		monster.goalVar1 = runDistance;
		monster.goalVar2 = static_cast<int>(GetDirection(position, monster.position.tile));
	}
}

void PrintMonstHistory(int mt)
{
	if (*sgOptions.Gameplay.showMonsterType) {
		AddInfoBoxString(fmt::format(fmt::runtime(_("Type: {:s}  Kills: {:d}")), GetMonsterTypeText(MonstersData[mt]), MonsterKillCounts[mt]));
	} else {
		AddInfoBoxString(fmt::format(fmt::runtime(_("Total kills: {:d}")), MonsterKillCounts[mt]));
	}

	if (MonsterKillCounts[mt] >= 30) {
		int minHP = MonstersData[mt].hitPointsMinimum;
		int maxHP = MonstersData[mt].hitPointsMaximum;
		if (!gbIsHellfire && mt == MT_DIABLO) {
			minHP /= 2;
			maxHP /= 2;
		}
		if (!gbIsMultiplayer) {
			minHP /= 2;
			maxHP /= 2;
		}
		if (minHP < 1)
			minHP = 1;
		if (maxHP < 1)
			maxHP = 1;

		int hpBonusNightmare = 100;
		int hpBonusHell = 200;
		if (gbIsHellfire) {
			hpBonusNightmare = (!gbIsMultiplayer ? 50 : 100);
			hpBonusHell = (!gbIsMultiplayer ? 100 : 200);
		}
		if (sgGameInitInfo.nDifficulty == DIFF_NIGHTMARE) {
			minHP = 3 * minHP + hpBonusNightmare;
			maxHP = 3 * maxHP + hpBonusNightmare;
		} else if (sgGameInitInfo.nDifficulty == DIFF_HELL) {
			minHP = 4 * minHP + hpBonusHell;
			maxHP = 4 * maxHP + hpBonusHell;
		}
		AddInfoBoxString(fmt::format(fmt::runtime(_("Hit Points: {:d}-{:d}")), minHP, maxHP));
	}
	if (MonsterKillCounts[mt] >= 15) {
		int res = (sgGameInitInfo.nDifficulty != DIFF_HELL) ? MonstersData[mt].resistance : MonstersData[mt].resistanceHell;
		if ((res & (RESIST_MAGIC | RESIST_FIRE | RESIST_LIGHTNING | IMMUNE_MAGIC | IMMUNE_FIRE | IMMUNE_LIGHTNING)) == 0) {
			AddInfoBoxString(_("No magic resistance"));
		} else {
			if ((res & (RESIST_MAGIC | RESIST_FIRE | RESIST_LIGHTNING)) != 0) {
				std::string resists = std::string(_("Resists:"));
				if ((res & RESIST_MAGIC) != 0)
					resists.append(_(" Magic"));
				if ((res & RESIST_FIRE) != 0)
					resists.append(_(" Fire"));
				if ((res & RESIST_LIGHTNING) != 0)
					resists.append(_(" Lightning"));
				AddInfoBoxString(resists);
			}
			if ((res & (IMMUNE_MAGIC | IMMUNE_FIRE | IMMUNE_LIGHTNING)) != 0) {
				std::string immune = std::string(_("Immune:"));
				if ((res & IMMUNE_MAGIC) != 0)
					immune.append(_(" Magic"));
				if ((res & IMMUNE_FIRE) != 0)
					immune.append(_(" Fire"));
				if ((res & IMMUNE_LIGHTNING) != 0)
					immune.append(_(" Lightning"));
				AddInfoBoxString(immune);
			}
		}
	}
}

void PrintUniqueHistory()
{
	Monster &monster = MonsterManager.Monsters[pcursmonst];
	if (*sgOptions.Gameplay.showMonsterType) {
		AddInfoBoxString(fmt::format(fmt::runtime(_("Type: {:s}")), GetMonsterTypeText(monster.data())));
	}

	int res = monster.resistance & (RESIST_MAGIC | RESIST_FIRE | RESIST_LIGHTNING | IMMUNE_MAGIC | IMMUNE_FIRE | IMMUNE_LIGHTNING);
	if (res == 0) {
		AddInfoBoxString(_("No resistances"));
		AddInfoBoxString(_("No Immunities"));
	} else {
		if ((res & (RESIST_MAGIC | RESIST_FIRE | RESIST_LIGHTNING)) != 0)
			AddInfoBoxString(_("Some Magic Resistances"));
		else
			AddInfoBoxString(_("No resistances"));
		if ((res & (IMMUNE_MAGIC | IMMUNE_FIRE | IMMUNE_LIGHTNING)) != 0) {
			AddInfoBoxString(_("Some Magic Immunities"));
		} else {
			AddInfoBoxString(_("No Immunities"));
		}
	}
}

void Monster::playEffect(MonsterSound mode)
{
	if (MyPlayer->pLvlLoad != 0) {
		return;
	}

	int sndIdx = GenerateRnd(2);
	if (!gbSndInited || !gbSoundOn || gbBufferMsgs != 0) {
		return;
	}

	TSnd *snd = type().sounds[static_cast<size_t>(mode)][sndIdx].get();
	if (snd == nullptr || snd->isPlaying()) {
		return;
	}

	int lVolume = 0;
	int lPan = 0;
	if (!CalculateSoundPosition(position.tile, &lVolume, &lPan))
		return;

	snd_play_snd(snd, lVolume, lPan);
}

void MissToMonst(Missile &missile, Point position)
{
	assert(static_cast<size_t>(missile._misource) < MaxMonsters);
	Monster &monster = MonsterManager.Monsters[missile._misource];

	Point oldPosition = missile.position.tile;
	monster.occupyTile(position, false);
	monster.direction = static_cast<Direction>(missile._mimfnum);
	monster.position.tile = position;
	monster.startStand();
	monster.startHit(0);

	if (monster.type().type == MT_GLOOM)
		return;

	if ((monster.flags & MFLAG_TARGETS_MONSTER) == 0) {
		Player *player = PlayerAtPosition(oldPosition, true);
		if (player == nullptr)
			return;

		MonsterAttackPlayer(monster, *player, 500, monster.minDamageSpecial, monster.maxDamageSpecial);

		if (IsAnyOf(monster.type().type, MT_NSNAKE, MT_RSNAKE, MT_BSNAKE, MT_GSNAKE))
			return;

		if (player->_pmode != PM_GOTHIT && player->_pmode != PM_DEATH)
			StartPlrHit(*player, 0, true);
		Point newPosition = oldPosition + GetDirection(missile.position.start, oldPosition);
		if (PosOkPlayer(*player, newPosition)) {
			player->position.tile = newPosition;
			FixPlayerLocation(*player, player->_pdir);
			FixPlrWalkTags(*player);
			player->occupyTile(newPosition, false);
			SetPlayerOld(*player);
		}
		return;
	}

	Monster *target = FindMonsterAtPosition(oldPosition, true);

	if (target == nullptr)
		return;

	MonsterAttackMonster(monster, *target, 500, monster.minDamageSpecial, monster.maxDamageSpecial);

	if (IsAnyOf(monster.type().type, MT_NSNAKE, MT_RSNAKE, MT_BSNAKE, MT_GSNAKE))
		return;

	Point newPosition = oldPosition + GetDirection(missile.position.start, oldPosition);
	if (target->isTileAvailable(newPosition)) {
		monster.occupyTile(newPosition, false);
		dMonster[oldPosition.x][oldPosition.y] = 0;
		monster.position.tile = newPosition;
		monster.position.future = newPosition;
	}
}

Monster *FindMonsterAtPosition(Point position, bool ignoreMovingMonsters)
{
	if (!InDungeonBounds(position)) {
		return nullptr;
	}

	auto monsterId = dMonster[position.x][position.y];

	if (monsterId == 0 || (ignoreMovingMonsters && monsterId < 0)) {
		// nothing at this position, return a nullptr
		return nullptr;
	}

	return &MonsterManager.Monsters[std::abs(monsterId) - 1];
}

// eftodo: move to monster manager
Monster *FindUniqueMonster(UniqueMonsterType monsterType)
{
	for (size_t i = 0; i < MonsterManager.ActiveMonsterCount; i++) {
		int monsterId = MonsterManager.ActiveMonsters[i];
		Monster &monster = MonsterManager.Monsters[monsterId];
		if (monster.uniqueType == monsterType)
			return &monster;
	}
	return nullptr;
}

bool Monster::isTileAvailable(Point position) const
{
	if (!IsTileAvailable(position))
		return false;

	return IsTileSafe(*this, position);
}

bool IsSkel(_monster_id mt)
{
	return c_find(SkeletonTypes, mt) != SkeletonTypes.end();
}

bool IsGoat(_monster_id mt)
{
	return IsAnyOf(mt,
	    MT_NGOATMC, MT_BGOATMC, MT_RGOATMC, MT_GGOATMC,
	    MT_NGOATBW, MT_BGOATBW, MT_RGOATBW, MT_GGOATBW);
}

void ActivateSkeleton(Monster &monster, Point position)
{
	if (IsTileAvailable(position)) {
		ActivateSpawn(monster, position, Direction::SouthWest);
		return;
	}

	constexpr std::array<Direction, 8> spawnDirections {
		Direction::North, Direction::NorthEast, Direction::East, Direction::NorthWest, Direction::SouthEast, Direction::West, Direction::SouthWest, Direction::South
	};
	std::bitset<8> spawnOk;

	for (size_t i = 0; i < spawnDirections.size(); i++) {
		if (IsTileAvailable(position + spawnDirections[i]))
			spawnOk.set(i);
	}
	if (spawnOk.none())
		return;

	// this is used in the following loop to find the nth set bit.
	int spawnChoice = GenerateRnd(15) % spawnOk.count();

	for (size_t i = 0; i < spawnOk.size(); i++) {
		if (!spawnOk.test(i))
			continue;

		if (spawnChoice > 0) {
			spawnChoice--;
			continue;
		}

		ActivateSpawn(monster, position + spawnDirections[i], Opposite(spawnDirections[i]));
		return;
	}
}

Monster *PreSpawnSkeleton()
{
	Monster *skeleton = AddSkeleton({ 0, 0 }, Direction::South, false);
	if (skeleton != nullptr)
		skeleton->startStand(Direction::South);

	return skeleton;
}

void Monster::talkTo(Player &player)
{
	if (&player == MyPlayer)
		mode = MonsterMode::Talk;

	if (uniqueType == UniqueMonsterType::SnotSpill
	    && Quests[Q_LTBANNER].IsAvailable() && Quests[Q_LTBANNER]._qvar1 == 2) {
		if (RemoveInventoryItemById(player, IDI_BANNER)) {
			Quests[Q_LTBANNER]._qactive = QUEST_DONE;
			talkMsg = TEXT_BANNER12;
			goal = MonsterGoal::Inquiring;
			NetSendCmdQuest(true, Quests[Q_LTBANNER]);
		}
	}
	if (uniqueType == UniqueMonsterType::Lachdan
	    && Quests[Q_VEIL].IsAvailable() && talkMsg >= TEXT_VEIL9) {
		if (RemoveInventoryItemById(player, IDI_GLDNELIX) && (flags & MFLAG_QUEST_COMPLETE) == 0) {
			talkMsg = TEXT_VEIL11;
			goal = MonsterGoal::Inquiring;
			flags |= MFLAG_QUEST_COMPLETE;
			if (MyPlayer == &player) {
				SpawnUnique(UITEM_STEELVEIL, position.tile + Direction::South);
				Quests[Q_VEIL]._qvar2 = QS_VEIL_ITEM_SPAWNED;
				NetSendCmdQuest(true, Quests[Q_VEIL]);
			}
		}
	}
	if (uniqueType == UniqueMonsterType::Zhar
	    && talkMsg == TEXT_ZHAR1
	    && (flags & MFLAG_QUEST_COMPLETE) == 0) {
		if (MyPlayer == &player) {
			Quests[Q_ZHAR]._qactive = QUEST_ACTIVE;
			Quests[Q_ZHAR]._qlog = true;
			Quests[Q_ZHAR]._qvar1 = QS_ZHAR_ITEM_SPAWNED;
			SetRndSeed(rndItemSeed);
			DiscardRandomValues(10);
			CreateTypeItem(position.tile + Displacement { 1, 1 }, false, ItemType::Misc, IMISC_BOOK, false, false, true);
			flags |= MFLAG_QUEST_COMPLETE;
			NetSendCmdQuest(true, Quests[Q_ZHAR]);
		}
	}

	if (uniqueType == UniqueMonsterType::Garbud && MyPlayer == &player) {
		if (talkMsg == TEXT_GARBUD1) {
			Quests[Q_GARBUD]._qactive = QUEST_ACTIVE;
			Quests[Q_GARBUD]._qlog = true;
			NetSendCmdQuest(true, Quests[Q_GARBUD]);
		}
		if (talkMsg == TEXT_GARBUD2 && (flags & MFLAG_QUEST_COMPLETE) == 0) {
			SetRndSeed(rndItemSeed);
			DiscardRandomValues(10);
			SpawnItem(*this, position.tile + Displacement { 1, 1 }, false, true);
			flags |= MFLAG_QUEST_COMPLETE;
			Quests[Q_GARBUD]._qvar1 = QS_GHARBAD_FIRST_ITEM_SPAWNED;
			NetSendCmdQuest(true, Quests[Q_GARBUD]);
		}
	}
}

void Monster::spawnGolem(Player &player, Point position, Missile &missile)
{
	occupyTile(position, false);
	this->position.tile = position;
	this->position.future = position;
	this->position.old = position;
	pathCount = 0;
	maxHitPoints = 2 * (320 * missile._mispllvl + player._pMaxMana / 3);
	hitPoints = maxHitPoints;
	armorClass = 25;
	toHit = 5 * (missile._mispllvl + 8) + 2 * player.getCharacterLevel();
	minDamage = 2 * (missile._mispllvl + 4);
	maxDamage = 2 * (missile._mispllvl + 8);
	flags |= MFLAG_GOLEM;
	StartSpecialStand(*this, Direction::South);
	UpdateEnemy(*this);
	if (&player == MyPlayer) {
		NetSendCmdGolem(
		    this->position.tile.x,
		    this->position.tile.y,
		    direction,
		    enemy,
		    hitPoints,
		    GetLevelForMultiplayer(player));
	}
}

bool Monster::canTalkTo() const
{
	return IsAnyOf(goal, MonsterGoal::Inquiring, MonsterGoal::Talking);
}

int encode_enemy(Monster &monster)
{
	if ((monster.flags & MFLAG_TARGETS_MONSTER) != 0)
		return monster.enemy + MAX_PLRS;

	return monster.enemy;
}

void decode_enemy(Monster &monster, int enemyId)
{
	if (enemyId < MAX_PLRS) {
		monster.flags &= ~MFLAG_TARGETS_MONSTER;
		monster.enemy = enemyId;
		monster.enemyPosition = Players[enemyId].position.future;
	} else {
		monster.flags |= MFLAG_TARGETS_MONSTER;
		enemyId -= MAX_PLRS;
		monster.enemy = enemyId;
		monster.enemyPosition = MonsterManager.Monsters[enemyId].position.future;
	}
}

/**
 * @brief Returns the network identifier for this monster
 *
 * This is currently the index into the Monsters array, but may change in the future.
 */
[[nodiscard]] size_t Monster::getId() const
{
	return std::distance<const Monster *>(&MonsterManager.Monsters[0], this);
}

Monster *Monster::getLeader() const
{
	if (leader == Monster::NoLeader)
		return nullptr;

	return &MonsterManager.Monsters[leader];
}

void Monster::setLeader(const Monster *leader)
{
	if (leader == nullptr) {
		// really we should update this->leader to NoLeader to avoid leaving a dangling reference to a dead monster
		// when passed nullptr. So that buffed minions are drawn with a distinct colour in monhealthbar we leave the
		// reference and hope that no code tries to modify the leader through this instance later.
		leaderRelation = LeaderRelation::None;
		return;
	}

	this->leader = static_cast<uint8_t>(leader->getId());
	leaderRelation = LeaderRelation::Leashed;
	ai = leader->ai;
}

[[nodiscard]] bool Monster::hasLeashedMinions() const
{
	return isUnique() && UniqueMonstersData[static_cast<size_t>(uniqueType)].monsterPack == UniqueMonsterPack::Leashed;
}

/**
 * @brief Calculates the distance in tiles between this monster and its current target
 *
 * The distance is not calculated as the euclidean distance, but rather as
 * the longest number of tiles in the coordinate system.
 *
 * @return The distance in tiles
 */
[[nodiscard]] unsigned Monster::distanceToEnemy() const
{
	int mx = position.tile.x - enemyPosition.x;
	int my = position.tile.y - enemyPosition.y;
	return std::max(std::abs(mx), std::abs(my));
}

void Monster::checkStandAnimationIsLoaded(Direction mdir)
{
	if (IsAnyOf(mode, MonsterMode::Stand, MonsterMode::Talk)) {
		direction = mdir;
		changeAnimationData(MonsterGraphic::Stand);
	}
}

void Monster::petrify()
{
	mode = MonsterMode::Petrified;
	animInfo.isPetrified = true;
}

/**
 * @brief Is the monster currently walking?
 */
bool Monster::isWalking() const
{
	switch (getVisualMonsterMode()) {
	case MonsterMode::MoveNorthwards:
	case MonsterMode::MoveSouthwards:
	case MonsterMode::MoveSideways:
		return true;
	default:
		return false;
	}
}

bool Monster::isImmune(MissileID missileType, DamageType missileElement) const
{
	if (((resistance & IMMUNE_MAGIC) != 0 && missileElement == DamageType::Magic)
	    || ((resistance & IMMUNE_FIRE) != 0 && missileElement == DamageType::Fire)
	    || ((resistance & IMMUNE_LIGHTNING) != 0 && missileElement == DamageType::Lightning)
	    || ((resistance & IMMUNE_ACID) != 0 && missileElement == DamageType::Acid))
		return true;
	if (missileType == MissileID::HolyBolt && type().type != MT_DIABLO && data().monsterClass != MonsterClass::Undead)
		return true;
	return false;
}

bool Monster::isResistant(MissileID missileType, DamageType missileElement) const
{
	if (((resistance & RESIST_MAGIC) != 0 && missileElement == DamageType::Magic)
	    || ((resistance & RESIST_FIRE) != 0 && missileElement == DamageType::Fire)
	    || ((resistance & RESIST_LIGHTNING) != 0 && missileElement == DamageType::Lightning))
		return true;
	if (gbIsHellfire && missileType == MissileID::HolyBolt && IsAnyOf(type().type, MT_DIABLO, MT_BONEDEMN))
		return true;
	return false;
}

/**
 * @brief Is this a player's golem?
 * @return 
 */

bool Monster::isPlayerMinion() const
{
	return (flags & MFLAG_GOLEM) != 0 && (flags & MFLAG_BERSERK) == 0;
}

bool Monster::isPossibleToHit() const
{
	return !(hitPoints >> 6 <= 0
	    || talkMsg != TEXT_NONE
	    || (type().type == MT_ILLWEAV && goal == MonsterGoal::Retreat)
	    || mode == MonsterMode::Charge
	    || (IsAnyOf(type().type, MT_COUNSLR, MT_MAGISTR, MT_CABALIST, MT_ADVOCATE) && goal != MonsterGoal::Normal));
}

void Monster::tag(const Player &tagger)
{
	whoHit |= 1 << tagger.getId();
}

[[nodiscard]] bool Monster::isUnique() const
{
	return uniqueType != UniqueMonsterType::None;
}

bool Monster::tryLiftGargoyle()
{
	if (ai == MonsterAIID::Gargoyle && (flags & MFLAG_ALLOW_SPECIAL) != 0) {
		flags &= ~MFLAG_ALLOW_SPECIAL;
		mode = MonsterMode::SpecialMeleeAttack;
		return true;
	}
	return false;
}

/**
 * @brief Gets the visual/shown monster mode.
 *
 * When a monster is petrified it's monster mode is changed to MonsterMode::Petrified.
 * But for graphics and rendering we show the old/real mode.
 */
MonsterMode Monster::getVisualMonsterMode() const
{
	if (mode != MonsterMode::Petrified)
		return mode;
	size_t monsterId = this->getId();
	for (auto &missile : Missiles) {
		// Search the missile that will restore the original monster mode and use the saved/original monster mode from it
		if (missile._mitype == MissileID::StoneCurse && static_cast<size_t>(missile.var2) == monsterId) {
			return static_cast<MonsterMode>(missile.var1);
		}
	}
	return MonsterMode::Petrified;
}

[[nodiscard]] Displacement Monster::getRenderingOffset(const ClxSprite sprite) const
{
	Displacement offset = { -CalculateWidth2(sprite.width()), 0 };
	if (isWalking())
		offset += GetOffsetForWalking(animInfo, direction);
	return offset;
}


/**
 * @brief Calculates monster's chance to hit with special attack.
 * Fetches base value from @p MonstersData array or @p UniqueMonstersData.
 * @param difficulty - difficulty on which calculation is performed
 * @return Monster's chance to hit with special attack, including bonuses from difficulty and monster being unique
 */
unsigned int Monster::toHitSpecial(_difficulty difficulty) const
{
	unsigned int baseToHitSpecial = data().toHitSpecial;
	if (isUnique() && UniqueMonstersData[static_cast<size_t>(uniqueType)].customToHit != 0) {
		baseToHitSpecial = UniqueMonstersData[static_cast<size_t>(uniqueType)].customToHit;
	}

	if (difficulty == DIFF_NIGHTMARE) {
		baseToHitSpecial += NightmareToHitBonus;
	} else if (difficulty == DIFF_HELL) {
		baseToHitSpecial += HellToHitBonus;
	}

	return baseToHitSpecial;
}

/**
 * @brief Sets a tile/dMonster to be occupied by the monster
 * @param position tile to update
 * @param isMoving specifies whether the monster is moving or not (true/moving results in a negative index in dMonster)
 */
void Monster::occupyTile(Point position, bool isMoving) const
{
	int16_t id = static_cast<int16_t>(this->getId() + 1);
	dMonster[position.x][position.y] = isMoving ? -id : id;
}

void Monster::ClearMVars()
{
	var1 = 0;
	var2 = 0;
	var3 = 0;
	position.temp = { 0, 0 };
}

} // namespace devilution
