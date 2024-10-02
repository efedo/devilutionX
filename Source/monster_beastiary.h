#pragma once

#include "monster.h"

namespace devilution {

struct CMonster {
	std::unique_ptr<std::byte[]> animData;
	AnimStruct anims[6];
	std::unique_ptr<TSnd> sounds[4][2];

	_monster_id type;
	uint8_t placeFlags; /** placeflag enum as a flags*/
	int8_t corpseId = 0;

	const MonsterData &data() const
	{
		return MonstersData[type];
	}

	/**
	 * @brief Returns AnimStruct for specified graphic
	 */
	[[nodiscard]] const AnimStruct &getAnimData(MonsterGraphic graphic) const
	{
		return anims[static_cast<int>(graphic)];
	}
};

struct _beastiary {
	CMonster LevelMonsterTypes[MaxLvlMTypes];
	size_t LevelMonsterTypeCount; // eftodo remove and replace with vector count

	size_t AddMonsterType(_monster_id type, placeflag placeflag);
	inline size_t AddMonsterType(UniqueMonsterType uniqueType, placeflag placeflag)
	{
		return AddMonsterType(UniqueMonstersData[static_cast<size_t>(uniqueType)].mtype, placeflag);
	}

	void InitMonsterSND(CMonster &monsterType);
	void InitMonsterGFX(CMonster &monsterType, MonsterSpritesData &&spritesData = {});

	void InitAllMonsterGFX();

	void GetLevelMTypes();

	void FreeMonsters();

	void InitLevelMonsters();

	// private:
	size_t GetMonsterTypeIndex(_monster_id type);
};

extern _beastiary Beastiary;

} // namespace devilution
