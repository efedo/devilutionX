#include "monster_manager.h"
#include "monster_beastiary.h"

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

	_monsterManager MonsterManager;

	namespace {

	} // anonymous namespace

	void _monsterManager::SetMapMonsters(const uint16_t *dunData, Point startPosition)
	{
		Beastiary.AddMonsterType(MT_GOLEM, PLACE_SPECIAL);
		if (setlevel)
			for (int i = 0; i < MAX_PLRS; i++)
				AddMonster(GolemHoldingCell, Direction::South, 0, false);

		if (setlevel && setlvlnum == SL_VILEBETRAYER) {
			Beastiary.AddMonsterType(UniqueMonsterType::Lazarus, PLACE_UNIQUE);
			Beastiary.AddMonsterType(UniqueMonsterType::RedVex, PLACE_UNIQUE);
			Beastiary.AddMonsterType(UniqueMonsterType::BlackJade, PLACE_UNIQUE);
			PlaceUniqueMonst(UniqueMonsterType::Lazarus, 0, 0);
			PlaceUniqueMonst(UniqueMonsterType::RedVex, 0, 0);
			PlaceUniqueMonst(UniqueMonsterType::BlackJade, 0, 0);
		}

		WorldTileSize size = GetDunSize(dunData);

		int layer2Offset = 2 + size.width * size.height;

		// The rest of the layers are at dPiece scale
		size *= static_cast<WorldTileCoord>(2);

		const uint16_t *monsterLayer = &dunData[layer2Offset + size.width * size.height];

		for (WorldTileCoord j = 0; j < size.height; j++) {
			for (WorldTileCoord i = 0; i < size.width; i++) {
				auto monsterId = static_cast<uint8_t>(SDL_SwapLE16(monsterLayer[j * size.width + i]));
				if (monsterId != 0) {
					const size_t typeIndex = Beastiary.AddMonsterType(MonstConvTbl[monsterId - 1], PLACE_SPECIAL);
					PlaceMonster(ActiveMonsterCount++, typeIndex, startPosition + Displacement { i, j });
				}
			}
		}
	}

	Monster *_monsterManager::AddMonster(Point position, Direction dir, size_t typeIndex, bool inMap)
	{
		if (ActiveMonsterCount < MaxMonsters) {
			Monster &monster = Monsters[ActiveMonsters[ActiveMonsterCount++]];
			if (inMap)
				monster.occupyTile(position, false);
		    monster.InitMonster(dir, typeIndex, position);
			return &monster;
		}

		return nullptr;
	}

	void _monsterManager::PlaceQuestMonsters()
    {
	    if (!setlevel) {
		    if (Quests[Q_BUTCHER].IsAvailable()) {
			    PlaceUniqueMonst(UniqueMonsterType::Butcher, 0, 0);
		    }

		    if (currlevel == Quests[Q_SKELKING]._qlevel && UseMultiplayerQuests()) {
			    for (size_t i = 0; i < Beastiary.LevelMonsterTypeCount; i++) {
				    if (IsSkel(Beastiary.LevelMonsterTypes[i].type)) {
					    PlaceUniqueMonst(UniqueMonsterType::SkeletonKing, i, 30);
					    break;
				    }
			    }
		    }

		    if (Quests[Q_LTBANNER].IsAvailable()) {
			    auto dunData = LoadFileInMem<uint16_t>("levels\\l1data\\banner1.dun");
			    SetMapMonsters(dunData.get(), SetPiece.position.megaToWorld());
		    }
		    if (Quests[Q_BLOOD].IsAvailable()) {
			    auto dunData = LoadFileInMem<uint16_t>("levels\\l2data\\blood2.dun");
			    SetMapMonsters(dunData.get(), SetPiece.position.megaToWorld());
		    }
		    if (Quests[Q_BLIND].IsAvailable()) {
			    auto dunData = LoadFileInMem<uint16_t>("levels\\l2data\\blind2.dun");
			    SetMapMonsters(dunData.get(), SetPiece.position.megaToWorld());
		    }
		    if (Quests[Q_ANVIL].IsAvailable()) {
			    auto dunData = LoadFileInMem<uint16_t>("levels\\l3data\\anvil.dun");
			    SetMapMonsters(dunData.get(), SetPiece.position.megaToWorld() + Displacement { 2, 2 });
		    }
		    if (Quests[Q_WARLORD].IsAvailable()) {
			    auto dunData = LoadFileInMem<uint16_t>("levels\\l4data\\warlord.dun");
			    SetMapMonsters(dunData.get(), SetPiece.position.megaToWorld());
			    Beastiary.AddMonsterType(UniqueMonsterType::WarlordOfBlood, PLACE_SCATTER);
		    }
		    if (Quests[Q_VEIL].IsAvailable()) {
			    Beastiary.AddMonsterType(UniqueMonsterType::Lachdan, PLACE_SCATTER);
		    }
		    if (Quests[Q_ZHAR].IsAvailable() && zharlib == -1) {
			    Quests[Q_ZHAR]._qactive = QUEST_NOTAVAIL;
		    }

		    if (currlevel == Quests[Q_BETRAYER]._qlevel && UseMultiplayerQuests()) {
			    Beastiary.AddMonsterType(UniqueMonsterType::Lazarus, PLACE_UNIQUE);
			    Beastiary.AddMonsterType(UniqueMonsterType::RedVex, PLACE_UNIQUE);
			    PlaceUniqueMonst(UniqueMonsterType::Lazarus, 0, 0);
			    PlaceUniqueMonst(UniqueMonsterType::RedVex, 0, 0);
			    PlaceUniqueMonst(UniqueMonsterType::BlackJade, 0, 0);
			    auto dunData = LoadFileInMem<uint16_t>("levels\\l4data\\vile1.dun");
			    SetMapMonsters(dunData.get(), SetPiece.position.megaToWorld());
		    }

		    if (currlevel == 24) {
			    UberDiabloMonsterIndex = -1;
			    const size_t typeIndex = Beastiary.GetMonsterTypeIndex(MT_NAKRUL);
			    if (typeIndex < Beastiary.LevelMonsterTypeCount) {
				    for (size_t i = 0; i < MonsterManager.ActiveMonsterCount; i++) {
					    Monster &monster = Monsters[i];
					    if (monster.isUnique() || monster.levelType == typeIndex) {
						    UberDiabloMonsterIndex = static_cast<int>(i);
						    break;
					    }
				    }
			    }
			    if (UberDiabloMonsterIndex == -1)
				    PlaceUniqueMonst(UniqueMonsterType::NaKrul, 0, 0);
		    }
	    } else if (setlvlnum == SL_SKELKING) {
		    PlaceUniqueMonst(UniqueMonsterType::SkeletonKing, 0, 0);
	    }
    }

    void _monsterManager::LoadDiabMonsts()
    {
	    {
		    auto dunData = LoadFileInMem<uint16_t>("levels\\l4data\\diab1.dun");
		    SetMapMonsters(dunData.get(), DiabloQuad1.megaToWorld());
	    }
	    {
		    auto dunData = LoadFileInMem<uint16_t>("levels\\l4data\\diab2a.dun");
		    SetMapMonsters(dunData.get(), DiabloQuad2.megaToWorld());
	    }
	    {
		    auto dunData = LoadFileInMem<uint16_t>("levels\\l4data\\diab3a.dun");
		    SetMapMonsters(dunData.get(), DiabloQuad3.megaToWorld());
	    }
	    {
		    auto dunData = LoadFileInMem<uint16_t>("levels\\l4data\\diab4a.dun");
		    SetMapMonsters(dunData.get(), DiabloQuad4.megaToWorld());
	    }
    }

	void _monsterManager::InitLevelMonsters()
    {
		// eftodo: should these be moved? stay?
	    Beastiary.LevelMonsterTypeCount = 0;
	    Beastiary.monstimgtot = 0;

	    for (CMonster &levelMonsterType : Beastiary.LevelMonsterTypes) {
		    levelMonsterType.placeFlags = 0;
	    }

	    ClrAllMonsters();
	    MonsterManager.ActiveMonsterCount = 0;
	    totalmonsters = MaxMonsters;

	    std::iota(std::begin(ActiveMonsters), std::end(ActiveMonsters), 0u);
	    uniquetrans = 0;
    }

	void _monsterManager::ClrAllMonsters()
    {
	    for (auto &monster : Monsters) {
		    monster.ClearMVars();
		    monster.goal = MonsterGoal::None;
		    monster.mode = MonsterMode::Stand;
		    monster.var1 = 0;
		    monster.var2 = 0;
		    monster.position.tile = { 0, 0 };
		    monster.position.future = { 0, 0 };
		    monster.position.old = { 0, 0 };
		    monster.direction = static_cast<Direction>(GenerateRnd(8));
		    monster.animInfo = {};
		    monster.flags = 0;
		    monster.isInvalid = false;
		    monster.enemy = GenerateRnd(gbActivePlayers);
		    monster.enemyPosition = Players[monster.enemy].position.future;
	    }
    }

	bool _monsterManager::CanPlaceMonster(Point position)
    {
	    return InDungeonBounds(position)
	        && dMonster[position.x][position.y] == 0
	        && dPlayer[position.x][position.y] == 0
	        && !IsTileVisible(position)
	        && !TileContainsSetPiece(position)
	        && !IsTileOccupied(position);
    }

    void _monsterManager::DeleteMonster(size_t activeIndex)
    {
	    const Monster &monster = Monsters[ActiveMonsters[activeIndex]];
	    if ((monster.flags & MFLAG_BERSERK) != 0) {
		    AddUnLight(monster.lightId);
	    }

	    MonsterManager.ActiveMonsterCount--;
	    std::swap(ActiveMonsters[activeIndex], ActiveMonsters[ActiveMonsterCount]); // This ensures alive monsters are before ActiveMonsterCount in the array and any deleted monster after
    }


	void _monsterManager::PlaceUniqueMonst(UniqueMonsterType uniqindex, size_t minionType, int bosspacksize)
    {
	    Monster &monster = Monsters[ActiveMonsterCount];
	    const auto &uniqueMonsterData = UniqueMonstersData[static_cast<size_t>(uniqindex)];

	    int count = 0;
	    Point position;
	    while (true) {
		    position = Point { GenerateRnd(80), GenerateRnd(80) } + Displacement { 16, 16 };
		    int count2 = 0;
		    for (int x = position.x - 3; x < position.x + 3; x++) {
			    for (int y = position.y - 3; y < position.y + 3; y++) {
				    if (InDungeonBounds({ x, y }) && CanPlaceMonster({ x, y })) {
					    count2++;
				    }
			    }
		    }

		    if (count2 < 9) {
			    count++;
			    if (count < 1000) {
				    continue;
			    }
		    }

		    if (CanPlaceMonster(position)) {
			    break;
		    }
	    }

	    if (uniqindex == UniqueMonsterType::SnotSpill) {
		    position = SetPiece.position.megaToWorld() + Displacement { 8, 12 };
	    }
	    if (uniqindex == UniqueMonsterType::WarlordOfBlood) {
		    position = SetPiece.position.megaToWorld() + Displacement { 6, 7 };
	    }
	    if (uniqindex == UniqueMonsterType::Zhar) {
		    for (int i = 0; i < themeCount; i++) {
			    if (i == zharlib) {
				    position = themeLoc[i].room.position.megaToWorld() + Displacement { 4, 4 };
				    break;
			    }
		    }
	    }
	    if (setlevel) {
		    if (uniqindex == UniqueMonsterType::Lazarus) {
			    position = { 32, 46 };
		    }
		    if (uniqindex == UniqueMonsterType::RedVex) {
			    position = { 40, 45 };
		    }
		    if (uniqindex == UniqueMonsterType::BlackJade) {
			    position = { 38, 49 };
		    }
		    if (uniqindex == UniqueMonsterType::SkeletonKing) {
			    position = { 35, 47 };
		    }
	    } else {
		    if (uniqindex == UniqueMonsterType::Lazarus) {
			    position = SetPiece.position.megaToWorld() + Displacement { 3, 6 };
		    }
		    if (uniqindex == UniqueMonsterType::RedVex) {
			    position = SetPiece.position.megaToWorld() + Displacement { 5, 3 };
		    }
		    if (uniqindex == UniqueMonsterType::BlackJade) {
			    position = SetPiece.position.megaToWorld() + Displacement { 5, 9 };
		    }
	    }
	    if (uniqindex == UniqueMonsterType::Butcher) {
		    position = SetPiece.position.megaToWorld() + Displacement { 4, 4 };
	    }

	    if (uniqindex == UniqueMonsterType::NaKrul) {
		    if (UberRow == 0 || UberCol == 0) {
			    UberDiabloMonsterIndex = -1;
			    return;
		    }
		    position = { UberRow - 2, UberCol };
		    UberDiabloMonsterIndex = static_cast<int>(ActiveMonsterCount);
	    }
	    const size_t typeIndex = Beastiary.GetMonsterTypeIndex(uniqueMonsterData.mtype);
	    PlaceMonster(ActiveMonsterCount, typeIndex, position);
	    MonsterManager.ActiveMonsterCount++;

	    PrepareUniqueMonst(monster, uniqindex, minionType, bosspacksize, uniqueMonsterData);
    }

	
void _monsterManager::PlaceUniqueMonsters()
    {
	    for (size_t u = 0; u < UniqueMonstersData.size(); ++u) {
		    if (UniqueMonstersData[u].mlevel != currlevel)
			    continue;

		    const size_t minionType = Beastiary.GetMonsterTypeIndex(UniqueMonstersData[u].mtype);
		    if (minionType == Beastiary.LevelMonsterTypeCount)
			    continue;

		    UniqueMonsterType uniqueType = static_cast<UniqueMonsterType>(u);
		    if (uniqueType == UniqueMonsterType::Garbud && Quests[Q_GARBUD]._qactive == QUEST_NOTAVAIL)
			    continue;
		    if (uniqueType == UniqueMonsterType::Zhar && Quests[Q_ZHAR]._qactive == QUEST_NOTAVAIL)
			    continue;
		    if (uniqueType == UniqueMonsterType::SnotSpill && Quests[Q_LTBANNER]._qactive == QUEST_NOTAVAIL)
			    continue;
		    if (uniqueType == UniqueMonsterType::Lachdan && Quests[Q_VEIL]._qactive == QUEST_NOTAVAIL)
			    continue;
		    if (uniqueType == UniqueMonsterType::WarlordOfBlood && Quests[Q_WARLORD]._qactive == QUEST_NOTAVAIL)
			    continue;

		    PlaceUniqueMonst(uniqueType, minionType, 8);
	    }
    }


	void _monsterManager::PlaceMonster(size_t i, size_t typeIndex, Point position)
    {
	    if (Beastiary.LevelMonsterTypes[typeIndex].type == MT_NAKRUL) {
		    for (size_t j = 0; j < MonsterManager.ActiveMonsterCount; j++) {
			    if (Monsters[j].levelType == typeIndex) {
				    return;
			    }
		    }
	    }
	    Monster &monster = Monsters[i];
	    monster.occupyTile(position, false);

	    auto rd = static_cast<Direction>(GenerateRnd(8));
	    monster.InitMonster(rd, typeIndex, position);
    }

    void _monsterManager::PlaceGroup(size_t typeIndex, size_t num, Monster *leader, bool leashed)
    {
	    uint8_t placed = 0;

	    for (int try1 = 0; try1 < 10; try1++) {
		    while (placed != 0) {
			    MonsterManager.ActiveMonsterCount--;
			    placed--;
			    const Point &position = Monsters[ActiveMonsterCount].position.tile;
			    dMonster[position.x][position.y] = 0;
		    }

		    int xp;
		    int yp;
		    if (leader != nullptr) {
			    int offset = GenerateRnd(8);
			    auto position = leader->position.tile + static_cast<Direction>(offset);
			    xp = position.x;
			    yp = position.y;
		    } else {
			    do {
				    xp = GenerateRnd(80) + 16;
				    yp = GenerateRnd(80) + 16;
			    } while (!CanPlaceMonster({ xp, yp }));
		    }
		    int x1 = xp;
		    int y1 = yp;

		    if (num + MonsterManager.ActiveMonsterCount > totalmonsters) {
			    num = totalmonsters - MonsterManager.ActiveMonsterCount;
		    }

		    unsigned j = 0;
		    for (unsigned try2 = 0; j < num && try2 < 100; xp += Displacement(static_cast<Direction>(GenerateRnd(8))).deltaX, yp += Displacement(static_cast<Direction>(GenerateRnd(8))).deltaX) { /// BUGFIX: `yp += Point.y`
			    if (!CanPlaceMonster({ xp, yp })
			        || (dTransVal[xp][yp] != dTransVal[x1][y1])
			        || (leashed && (std::abs(xp - x1) >= 4 || std::abs(yp - y1) >= 4))) {
				    try2++;
				    continue;
			    }

			    PlaceMonster(ActiveMonsterCount, typeIndex, { xp, yp });
			    if (leader != nullptr) {
				    Monster &minion = Monsters[ActiveMonsterCount];
				    minion.maxHitPoints *= 2;
				    minion.hitPoints = minion.maxHitPoints;
				    minion.intelligence = leader->intelligence;

				    if (leashed) {
					    minion.setLeader(leader);
				    }

				    if (minion.ai != MonsterAIID::Gargoyle) {
					    minion.changeAnimationData(MonsterGraphic::Stand);
					    minion.animInfo.currentFrame = GenerateRnd(minion.animInfo.numberOfFrames - 1);
					    minion.flags &= ~MFLAG_ALLOW_SPECIAL;
					    minion.mode = MonsterMode::Stand;
				    }
			    }
			    MonsterManager.ActiveMonsterCount++;
			    placed++;
			    j++;
		    }

		    if (placed >= num) {
			    break;
		    }
	    }

	    if (leashed) {
		    leader->packSize = placed;
	    }
    }



}
