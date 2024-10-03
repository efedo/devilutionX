#include "monster_beastiary.h"
#include "monster_manager.h"

// eftodo: cut out uneccessary headers

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

_beastiary Beastiary;

namespace {

/** Maps from monster action to monster animation letter. */
constexpr char Animletter[7] = "nwahds";

size_t GetNumAnims(const MonsterData &monsterData)
{
	return monsterData.hasSpecial ? 6 : 5;
}

size_t GetNumAnimsWithGraphics(const MonsterData &monsterData)
{
	// Monster graphics can be missing for certain actions,
	// e.g. Golem has no standing graphics.
	const size_t numAnims = GetNumAnims(monsterData);
	size_t result = 0;
	for (size_t i = 0; i < numAnims; ++i) {
		if (monsterData.hasAnim(i))
			++result;
	}
	return result;
}

MonsterSpritesData LoadMonsterSpritesData(const MonsterData &monsterData)
{
	const size_t numAnims = GetNumAnims(monsterData);

	MonsterSpritesData result;
	result.data = MultiFileLoader<MonsterSpritesData::MaxAnims> {}(
	    numAnims,
	    FileNameWithCharAffixGenerator({ "monsters\\", monsterData.spritePath() }, DEVILUTIONX_CL2_EXT, Animletter),
	    result.offsets.data(),
	    [&monsterData](size_t index) { return monsterData.hasAnim(index); });

#ifndef UNPACKED_MPQS
	// Convert CL2 to CLX:
	std::vector<std::vector<uint8_t>> clxData;
	size_t accumulatedSize = 0;
	for (size_t i = 0, j = 0; i < numAnims; ++i) {
		if (!monsterData.hasAnim(i))
			continue;
		const uint32_t begin = result.offsets[j];
		const uint32_t end = result.offsets[j + 1];
		clxData.emplace_back();
		Cl2ToClx(reinterpret_cast<uint8_t *>(&result.data[begin]), end - begin,
		    PointerOrValue<uint16_t> { monsterData.width }, clxData.back());
		result.offsets[j] = static_cast<uint32_t>(accumulatedSize);
		accumulatedSize += clxData.back().size();
		++j;
	}
	result.offsets[clxData.size()] = static_cast<uint32_t>(accumulatedSize);
	result.data = nullptr;
	result.data = std::unique_ptr<std::byte[]>(new std::byte[accumulatedSize]);
	for (size_t i = 0; i < clxData.size(); ++i) {
		memcpy(&result.data[result.offsets[i]], clxData[i].data(), clxData[i].size());
	}
#endif

	return result;
}

void InitMonsterTRN(CMonster &monst)
{
	char path[64];
	*BufCopy(path, "monsters\\", monst.data().trnFile, ".trn") = '\0';
	std::array<uint8_t, 256> colorTranslations;
	LoadFileInMem(path, colorTranslations);
	std::replace(colorTranslations.begin(), colorTranslations.end(), 255, 0);

	const size_t numAnims = GetNumAnims(monst.data());
	for (size_t i = 0; i < numAnims; i++) {
		if (i == 1 && IsAnyOf(monst.type, MT_COUNSLR, MT_MAGISTR, MT_CABALIST, MT_ADVOCATE)) {
			continue;
		}

		AnimStruct &anim = monst.anims[i];
		if (anim.sprites->isSheet()) {
			ClxApplyTrans(ClxSpriteSheet { anim.sprites->sheet() }, colorTranslations.data());
		} else {
			ClxApplyTrans(ClxSpriteList { anim.sprites->list() }, colorTranslations.data());
		}
	}
}

bool IsMonsterAvailable(const MonsterData &monsterData)
{
	if (monsterData.availability == MonsterAvailability::Never)
		return false;

	if (gbIsSpawn && monsterData.availability == MonsterAvailability::Retail)
		return false;

	return currlevel >= monsterData.minDunLvl && currlevel <= monsterData.maxDunLvl;
}


} // anonymous namespace

size_t _beastiary::AddMonsterType(_monster_id type, placeflag placeflag)
{
	const size_t typeIndex = GetMonsterTypeIndex(type);
	CMonster &monsterType = LevelMonsterTypes[typeIndex];

	if (typeIndex == LevelMonsterTypeCount) {
		LevelMonsterTypeCount++;
		monsterType.type = type;
		const MonsterData &monsterData = MonstersData[type];
		monstimgtot += monsterData.image;

		const size_t numAnims = GetNumAnims(monsterData);
		for (size_t i = 0; i < numAnims; ++i) {
			AnimStruct &anim = monsterType.anims[i];
			anim.frames = monsterData.frames[i];
			if (monsterData.hasAnim(i)) {
				anim.rate = monsterData.rate[i];
				anim.width = monsterData.width;
			}
		}

		InitMonsterSND(monsterType);
	}

	monsterType.placeFlags |= placeflag;
	return typeIndex;
}

void _beastiary::GetLevelMTypes()
{
	AddMonsterType(MT_GOLEM, PLACE_SPECIAL);
	if (currlevel == 16) {
		AddMonsterType(MT_ADVOCATE, PLACE_SCATTER);
		AddMonsterType(MT_RBLACK, PLACE_SCATTER);
		AddMonsterType(MT_DIABLO, PLACE_SPECIAL);
		return;
	}

	if (currlevel == 18)
		AddMonsterType(MT_HORKSPWN, PLACE_SCATTER);
	if (currlevel == 19) {
		AddMonsterType(MT_HORKSPWN, PLACE_SCATTER);
		AddMonsterType(MT_HORKDMN, PLACE_UNIQUE);
	}
	if (currlevel == 20)
		AddMonsterType(MT_DEFILER, PLACE_UNIQUE);
	if (currlevel == 24) {
		AddMonsterType(MT_ARCHLICH, PLACE_SCATTER);
		AddMonsterType(MT_NAKRUL, PLACE_SPECIAL);
	}

	if (!setlevel) {
		if (Quests[Q_BUTCHER].IsAvailable())
			AddMonsterType(MT_CLEAVER, PLACE_SPECIAL);
		if (Quests[Q_GARBUD].IsAvailable())
			AddMonsterType(UniqueMonsterType::Garbud, PLACE_UNIQUE);
		if (Quests[Q_ZHAR].IsAvailable())
			AddMonsterType(UniqueMonsterType::Zhar, PLACE_UNIQUE);
		if (Quests[Q_LTBANNER].IsAvailable())
			AddMonsterType(UniqueMonsterType::SnotSpill, PLACE_UNIQUE);
		if (Quests[Q_VEIL].IsAvailable())
			AddMonsterType(UniqueMonsterType::Lachdan, PLACE_UNIQUE);
		if (Quests[Q_WARLORD].IsAvailable())
			AddMonsterType(UniqueMonsterType::WarlordOfBlood, PLACE_UNIQUE);

		if (UseMultiplayerQuests() && currlevel == Quests[Q_SKELKING]._qlevel) {

			AddMonsterType(MT_SKING, PLACE_UNIQUE);

			int skeletonTypeCount = 0;
			_monster_id skeltypes[NUM_MTYPES];
			for (_monster_id skeletonType : SkeletonTypes) {
				if (!IsMonsterAvailable(MonstersData[skeletonType]))
					continue;

				skeltypes[skeletonTypeCount++] = skeletonType;
			}
			AddMonsterType(skeltypes[GenerateRnd(skeletonTypeCount)], PLACE_SCATTER);
		}

		_monster_id typelist[MaxMonsters];

		int nt = 0;
		for (int i = MT_NZOMBIE; i < NUM_MTYPES; i++) {
			if (!IsMonsterAvailable(MonstersData[i]))
				continue;

			typelist[nt++] = (_monster_id)i;
		}

		while (nt > 0 && LevelMonsterTypeCount < MaxLvlMTypes && monstimgtot < 4000) {
			for (int i = 0; i < nt;) {
				if (MonstersData[typelist[i]].image > 4000 - monstimgtot) {
					typelist[i] = typelist[--nt];
					continue;
				}

				i++;
			}

			if (nt != 0) {
				int i = GenerateRnd(nt);
				AddMonsterType(typelist[i], PLACE_SCATTER);
				typelist[i] = typelist[--nt];
			}
		}
	} else {
		if (setlvlnum == SL_SKELKING) {
			AddMonsterType(MT_SKING, PLACE_UNIQUE);
		}
	}
}

void _beastiary::InitMonsterSND(CMonster &monsterType)
{
	if (!gbSndInited)
		return;

	const char *prefixes[] {
		"a", // Attack
		"h", // Hit
		"d", // Death
		"s", // Special
	};

	const MonsterData &data = MonstersData[monsterType.type];
	std::string_view soundSuffix = data.soundPath();

	for (int i = 0; i < 4; i++) {
		std::string_view prefix = prefixes[i];
		if (prefix == "s" && !data.hasSpecialSound)
			continue;

		for (int j = 0; j < 2; j++) {
			char path[64];
			*BufCopy(path, "monsters\\", soundSuffix, prefix, j + 1, ".wav") = '\0';
			monsterType.sounds[i][j] = sound_file_load(path);
		}
	}
}

void _beastiary::InitMonsterGFX(CMonster &monsterType, MonsterSpritesData &&spritesData)
{
	if (HeadlessMode)
		return;

	const _monster_id mtype = monsterType.type;
	const MonsterData &monsterData = MonstersData[mtype];
	if (spritesData.data == nullptr)
		spritesData = LoadMonsterSpritesData(monsterData);
	monsterType.animData = std::move(spritesData.data);

	const size_t numAnims = GetNumAnims(monsterData);
	for (size_t i = 0, j = 0; i < numAnims; ++i) {
		if (!monsterData.hasAnim(i)) {
			monsterType.anims[i].sprites = std::nullopt;
			continue;
		}
		const uint32_t begin = spritesData.offsets[j];
		const uint32_t end = spritesData.offsets[j + 1];
		auto spritesData = reinterpret_cast<uint8_t *>(&monsterType.animData[begin]);
		const uint16_t numLists = GetNumListsFromClxListOrSheetBuffer(spritesData, end - begin);
		monsterType.anims[i].sprites = ClxSpriteListOrSheet { spritesData, numLists };
		++j;
	}

	if (!monsterData.trnFile.empty()) {
		InitMonsterTRN(monsterType);
	}

	if (IsAnyOf(mtype, MT_NMAGMA, MT_YMAGMA, MT_BMAGMA, MT_WMAGMA))
		GetMissileSpriteData(MissileGraphicID::MagmaBall).LoadGFX();
	if (IsAnyOf(mtype, MT_STORM, MT_RSTORM, MT_STORML, MT_MAEL))
		GetMissileSpriteData(MissileGraphicID::ThinLightning).LoadGFX();
	if (mtype == MT_SNOWWICH) {
		GetMissileSpriteData(MissileGraphicID::BloodStarBlue).LoadGFX();
		GetMissileSpriteData(MissileGraphicID::BloodStarBlueExplosion).LoadGFX();
	}
	if (mtype == MT_HLSPWN) {
		GetMissileSpriteData(MissileGraphicID::BloodStarRed).LoadGFX();
		GetMissileSpriteData(MissileGraphicID::BloodStarRedExplosion).LoadGFX();
	}
	if (mtype == MT_SOLBRNR) {
		GetMissileSpriteData(MissileGraphicID::BloodStarYellow).LoadGFX();
		GetMissileSpriteData(MissileGraphicID::BloodStarYellowExplosion).LoadGFX();
	}
	if (IsAnyOf(mtype, MT_NACID, MT_RACID, MT_BACID, MT_XACID, MT_SPIDLORD)) {
		GetMissileSpriteData(MissileGraphicID::Acid).LoadGFX();
		GetMissileSpriteData(MissileGraphicID::AcidSplat).LoadGFX();
		GetMissileSpriteData(MissileGraphicID::AcidPuddle).LoadGFX();
	}
	if (mtype == MT_LICH) {
		GetMissileSpriteData(MissileGraphicID::OrangeFlare).LoadGFX();
		GetMissileSpriteData(MissileGraphicID::OrangeFlareExplosion).LoadGFX();
	}
	if (mtype == MT_ARCHLICH) {
		GetMissileSpriteData(MissileGraphicID::YellowFlare).LoadGFX();
		GetMissileSpriteData(MissileGraphicID::YellowFlareExplosion).LoadGFX();
	}
	if (IsAnyOf(mtype, MT_PSYCHORB, MT_BONEDEMN))
		GetMissileSpriteData(MissileGraphicID::BlueFlare2).LoadGFX();
	if (mtype == MT_NECRMORB) {
		GetMissileSpriteData(MissileGraphicID::RedFlare).LoadGFX();
		GetMissileSpriteData(MissileGraphicID::RedFlareExplosion).LoadGFX();
	}
	if (mtype == MT_PSYCHORB)
		GetMissileSpriteData(MissileGraphicID::BlueFlareExplosion).LoadGFX();
	if (mtype == MT_BONEDEMN)
		GetMissileSpriteData(MissileGraphicID::BlueFlareExplosion2).LoadGFX();
	if (mtype == MT_DIABLO)
		GetMissileSpriteData(MissileGraphicID::DiabloApocalypseBoom).LoadGFX();
}

void _beastiary::InitAllMonsterGFX()
{
	if (HeadlessMode)
		return;

	using LevelMonsterTypeIndices = StaticVector<size_t, 8>;
	std::vector<LevelMonsterTypeIndices> monstersBySprite(GetNumMonsterSprites());
	for (size_t i = 0; i < LevelMonsterTypeCount; ++i) {
		monstersBySprite[static_cast<size_t>(LevelMonsterTypes[i].data().spriteId)].emplace_back(i);
	}
	size_t totalUniqueBytes = 0;
	size_t totalBytes = 0;
	for (const LevelMonsterTypeIndices &monsterTypes : monstersBySprite) {
		if (monsterTypes.empty())
			continue;
		CMonster &firstMonster = LevelMonsterTypes[monsterTypes[0]];
		if (firstMonster.animData != nullptr)
			continue;
		MonsterSpritesData spritesData = LoadMonsterSpritesData(firstMonster.data());
		const size_t spritesDataSize = spritesData.offsets[GetNumAnimsWithGraphics(firstMonster.data())];
		for (size_t i = 1; i < monsterTypes.size(); ++i) {
			MonsterSpritesData spritesDataCopy { std::unique_ptr<std::byte[]> { new std::byte[spritesDataSize] }, spritesData.offsets };
			memcpy(spritesDataCopy.data.get(), spritesData.data.get(), spritesDataSize);
			InitMonsterGFX(LevelMonsterTypes[monsterTypes[i]], std::move(spritesDataCopy));
		}
		LogVerbose("Loaded monster graphics: {:15s} {:>4d} KiB   x{:d}", firstMonster.data().spritePath(), spritesDataSize / 1024, monsterTypes.size());
		totalUniqueBytes += spritesDataSize;
		totalBytes += spritesDataSize * monsterTypes.size();
		InitMonsterGFX(firstMonster, std::move(spritesData));
	}
	LogVerbose(" Total monster graphics:                 {:>4d} KiB {:>4d} KiB", totalUniqueBytes / 1024, totalBytes / 1024);

	if (totalUniqueBytes > 0) {
		// we loaded new sprites, check if we need to update existing monsters
		// eftodo: move to separate monster manager function
		for (size_t i = 0; i < MonsterManager.ActiveMonsterCount; i++) {
			Monster &monster = Monsters[ActiveMonsters[i]];
			if (!monster.animInfo.sprites)
				monster.syncAnim();
		}
	}
}

void _beastiary::FreeMonsters()
{
	for (CMonster &monsterType : LevelMonsterTypes) {
		monsterType.animData = nullptr;
		monsterType.corpseId = 0;
		for (AnimStruct &animData : monsterType.anims) {
			animData.sprites = std::nullopt;
		}

		for (auto &variants : monsterType.sounds) {
			for (auto &sound : variants) {
				sound = nullptr;
			}
		}
	}
}

size_t _beastiary::GetMonsterTypeIndex(_monster_id type)
{
	for (size_t i = 0; i < LevelMonsterTypeCount; i++) {
		if (Beastiary.LevelMonsterTypes[i].type == type)
			return i;
	}
	return LevelMonsterTypeCount;
}

}
