#pragma once

#include "monster.h"

namespace devilution {

struct _monsterManager {

	size_t ActiveMonsterCount;
	size_t totalmonsters; // eftodo: move to MonsterManager  /** Tracks which missile files are already loaded */
	int uniquetrans;

	Monster *AddMonster(Point position, Direction dir, size_t mtype, bool inMap);
	void SetMapMonsters(const uint16_t *dunData, Point startPosition);
	void InitLevelMonsters();
	void ClrAllMonsters();

	

	// private:
	void PlaceQuestMonsters();
	void LoadDiabMonsts();

	private:

	void PlaceUniqueMonst(UniqueMonsterType uniqindex, size_t minionType, int bosspacksize);
	void PlaceUniqueMonsters();
	void PlaceMonster(size_t i, size_t typeIndex, Point position);
	void PlaceGroup(size_t typeIndex, size_t num, Monster *leader = nullptr, bool leashed = false);
	bool CanPlaceMonster(Point position);
	void DeleteMonster(size_t activeIndex);


};

extern _monsterManager MonsterManager;

} // namespace devilution
