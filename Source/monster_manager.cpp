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

	_monsterManager MonsterManager;

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
			InitMonster(monster, dir, typeIndex, position);
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
				    for (size_t i = 0; i < ActiveMonsterCount; i++) {
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

    }
