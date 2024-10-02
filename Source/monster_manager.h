#pragma once

#include "monster.h"

namespace devilution {

struct _monsterManager {
	Monster *AddMonster(Point position, Direction dir, size_t mtype, bool inMap);
	void SetMapMonsters(const uint16_t *dunData, Point startPosition);

	// private:
	void PlaceQuestMonsters();
	void LoadDiabMonsts();
};

extern _monsterManager MonsterManager;

} // namespace devilution
