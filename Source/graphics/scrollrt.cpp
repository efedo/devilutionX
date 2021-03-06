#include "all.h"

namespace dvl {

/**
 * Specifies the current light entry.
 */
int light_table_index;
DWORD sgdwCursWdtOld;
DWORD sgdwCursX;
DWORD sgdwCursY;
/**
 * Upper bound of back buffer.
 */
uint8_t *gpBufStart;
/**
 * Lower bound of back buffer.
 */
uint8_t *gpBufEnd;
DWORD sgdwCursHgt;

/**
 * Specifies the current MIN block of the level CEL file, as used during rendering of the level tiles.
 *
 * frameNum  := block & 0x0FFF
 * frameType := block & 0x7000 >> 12
 */
DWORD level_cel_block;
DWORD sgdwCursXOld;
DWORD sgdwCursYOld;
/**
 * Specifies the type of arches to render.
 */
char arch_draw_type;

/**
 * Specifies the current dungeon piece ID of the level, as used during rendering of the level tiles.
 */
//int level_piece_id;
DWORD sgdwCursWdt;
void (*DrawPlrProc)(int, int, int, int, int, uint8_t *, int, int, int, int);
uint8_t sgSaveBack[8192];
DWORD sgdwCursHgtOld;

bool dRendered[MAXDUNX][MAXDUNY];

/* data */

/* used in 1.00 debug */
char *szMonModeAssert[18] = {
	"standing",
	"walking (1)",
	"walking (2)",
	"walking (3)",
	"attacking",
	"getting hit",
	"dying",
	"attacking (special)",
	"fading in",
	"fading out",
	"attacking (ranged)",
	"standing (special)",
	"attacking (special ranged)",
	"delaying",
	"charging",
	"stoned",
	"healing",
	"talking"
};

char *szPlrModeAssert[12] = {
	"standing",
	"walking (1)",
	"walking (2)",
	"walking (3)",
	"attacking (melee)",
	"attacking (ranged)",
	"blocking",
	"getting hit",
	"dying",
	"casting a spell",
	"changing levels",
	"quitting"
};

/**
 * @brief Clear cursor state
 */
void ClearCursor() // CODE_FIX: this was supposed to be in cursor.cpp
{
	sgdwCursWdt = 0;
	sgdwCursWdtOld = 0;
}

/**
 * @brief Remove the cursor from the back buffer
 */
static void scrollrt_draw_cursor_back_buffer()
{
	int i;
	uint8_t *src, *dst;

	if (sgdwCursWdt == 0) {
		return;
	}

	assert(gpBuffer);
	src = sgSaveBack;
	dst = &gpBuffer[SCREENXY(sgdwCursX, sgdwCursY)];
	i = sgdwCursHgt;

	if (sgdwCursHgt != 0) {
		while (i--) {
			memcpy(dst, src, sgdwCursWdt);
			src += sgdwCursWdt;
			dst += BUFFER_WIDTH;
		}
	}

	sgdwCursXOld = sgdwCursX;
	sgdwCursYOld = sgdwCursY;
	sgdwCursWdtOld = sgdwCursWdt;
	sgdwCursHgtOld = sgdwCursHgt;
	sgdwCursWdt = 0;
}

/**
 * @brief Draw the cursor on the back buffer
 */
static void scrollrt_draw_cursor_item()
{
	int i, mx, my, col;
	uint8_t *src, *dst;

	assert(!sgdwCursWdt);

	if (pcurs <= Cursor::NONE || cursW == 0 || cursH == 0) {
		return;
	}

	if (sgbControllerActive && pcurs != Cursor::TELEPORT && !gui.invflag && (!chrflag || myplr().data._pStatPts <= 0)) {
		return;
	}

	mx = Mouse.x - 1;
	if (mx < 0 - cursW - 1) {
		return;
	} else if (mx > SCREEN_WIDTH - 1) {
		return;
	}
	my = Mouse.y - 1;
	if (my < 0 - cursH - 1) {
		return;
	} else if (my > SCREEN_HEIGHT - 1) {
		return;
	}

	sgdwCursX = mx;
	sgdwCursWdt = sgdwCursX + cursW + 1;
	if (sgdwCursWdt > SCREEN_WIDTH - 1) {
		sgdwCursWdt = SCREEN_WIDTH - 1;
	}
	sgdwCursX &= ~3;
	sgdwCursWdt |= 3;
	sgdwCursWdt -= sgdwCursX;
	sgdwCursWdt++;

	sgdwCursY = my;
	sgdwCursHgt = sgdwCursY + cursH + 1;
	if (sgdwCursHgt > SCREEN_HEIGHT - 1) {
		sgdwCursHgt = SCREEN_HEIGHT - 1;
	}
	sgdwCursHgt -= sgdwCursY;
	sgdwCursHgt++;

	assert(sgdwCursWdt * sgdwCursHgt < sizeof sgSaveBack);
	assert(gpBuffer);
	dst = sgSaveBack;
	src = &gpBuffer[SCREENXY(sgdwCursX, sgdwCursY)];

	for (i = sgdwCursHgt; i != 0; i--, dst += sgdwCursWdt, src += BUFFER_WIDTH) {
		memcpy(dst, src, sgdwCursWdt);
	}

	mx++;
	my++;
	gpBufEnd = &gpBuffer[BUFFER_WIDTH * (SCREEN_HEIGHT + SCREEN_Y) - cursW - 2];

	if (pcurs >= Cursor::FIRSTITEM) {
		col = PAL16_YELLOW + 5;
		if (!myplr().heldItem() || myplr().heldItem()->_iMagical != 0) {
			col = PAL16_BLUE + 5;
		}
		if (myplr().heldItem() && !myplr().heldItem()->_iStatFlag) {
			col = PAL16_RED + 5;
		}
		CelBlitOutline(col, { mx + SCREEN_X, my + cursH + SCREEN_Y - 1 }, pCursCels, pcurs, cursW);
		if (col != PAL16_RED + 5) {
			CelClippedDrawSafe({ mx + SCREEN_X, my + cursH + SCREEN_Y - 1 }, pCursCels, pcurs, cursW);
		} else {
			CelDrawLightRedSafe({ mx + SCREEN_X, my + cursH + SCREEN_Y - 1 }, pCursCels, pcurs, cursW, 1);
		}
	} else {
		CelClippedDrawSafe({ mx + SCREEN_X, my + cursH + SCREEN_Y - 1 }, pCursCels, pcurs, cursW);
	}
}

/**
 * @brief Render a missile sprite
 * @param m Pointer to MissileStruct struct
 * @param sx Back buffer coordinate
 * @param sy Back buffer coordinate
 * @param pre Is the sprite in the background
 */
void DrawMissilePrivate(MissileStruct *m, V2Di s, bool pre)
{
	int nCel, frames;
	V2Di mv;
	uint8_t *pCelBuff;

	if (m->_miPreFlag != pre || !m->_miDrawFlag)
		return;

	pCelBuff = m->_miAnimData;
	if (!pCelBuff) {
		// app_fatal("Draw Missile 2 type %d: NULL Cel Buffer", m->_mitype);
		return;
	}
	nCel = m->_miAnimFrame;
	frames = SDL_SwapLE32(*(DWORD *)pCelBuff);
	if (nCel < 1 || frames > 50 || nCel > frames) {
		// app_fatal("Draw Missile 2: frame %d of %d, missile type==%d", nCel, frames, m->_mitype);
		return;
	}
	mv.x = s.x + m->_mioff.x - m->_miAnimWidth2;
	mv.y = s.y + m->_mioff.y;
	if (m->_miUniqTrans)
		Cl2DrawLightTbl(mv, m->_miAnimData, m->_miAnimFrame, m->_miAnimWidth, m->_miUniqTrans + 3);
	else if (m->_miLightFlag)
		Cl2DrawLight(mv, m->_miAnimData, m->_miAnimFrame, m->_miAnimWidth);
	else
		Cl2Draw(mv, m->_miAnimData, m->_miAnimFrame, m->_miAnimWidth);
}

/**
 * @brief Render a missile sprites for a given tile
 * @param x dPiece coordinate
 * @param y dPiece coordinate
 * @param sx Back buffer coordinate
 * @param sy Back buffer coordinate
 * @param pre Is the sprite in the background
 */
void DrawMissile(V2Di p, V2Di s, bool pre)
{
	int i;
	MissileStruct *m;

	if (!(grid.at(p).dFlags & DunTileFlag::MISSILE))
		return;

	if (grid.at(p).getMissile() != -1) {
		m = &missile[grid.at(p).getMissile() - 1];
		DrawMissilePrivate(m, s, pre);
		return;
	}

	for (i = 0; i < nummissiles; i++) {
		assert(missileactive[i] < MAXMISSILES);
		m = &missile[missileactive[i]];
		if (m->_mi.x != p.x || m->_mi.y != p.y)
			continue;
		DrawMissilePrivate(m, s, pre);
	}
}

/**
 * @brief Render a monster sprite
 * @param x dPiece coordinate
 * @param y dPiece coordinate
 * @param mx Back buffer coordinate
 * @param my Back buffer coordinate
 * @param m Id of monster
 */
static void DrawMonster(V2Di p, V2Di mv, int m)
{
	int nCel, frames;
	char trans;
	uint8_t *pCelBuff;

	if ((DWORD)m >= MAXMONSTERS) {
		// app_fatal("Draw Monster: tried to draw illegal monster %d", m);
		return;
	}

	pCelBuff = monsters[m].data._mAnimData;
	if (!pCelBuff) {
		// app_fatal("Draw Monster \"%s\": NULL Cel Buffer", monsters[m].data.mName);
		return;
	}

	nCel = monsters[m].data._mAnimFrame;
	frames = SDL_SwapLE32(*(DWORD *)pCelBuff);
	if (nCel < 1 || frames > 50 || nCel > frames) {
		/*
		const char *szMode = "unknown action";
		if(monsters[m].data._mmode <= 17)
			szMode = szMonModeAssert[monsters[m].data._mmode];
		app_fatal(
			"Draw Monster \"%s\" %s: facing %d, frame %d of %d",
			monsters[m].data.mName,
			szMode,
			monsters[m].data._mdir,
			nCel,
			frames);
		*/
		return;
	}

	if (!(grid.at(p).dFlags & DunTileFlag::LIT)) {
		Cl2DrawLightTbl(mv, monsters[m].data._mAnimData, monsters[m].data._mAnimFrame, monsters[m].data.MType->width, 1);
	} else {
		trans = 0;
		if (monsters[m].data._uniqtype)
			trans = monsters[m].data._uniqtrans + 4;
		if (monsters[m].data._mmode == MonsterMode::STONE)
			trans = 2;
		if (myplr().data._pInfraFlag && light_table_index > 8)
			trans = 1;
		if (trans)
			Cl2DrawLightTbl(mv, monsters[m].data._mAnimData, monsters[m].data._mAnimFrame, monsters[m].data.MType->width, trans);
		else
			Cl2DrawLight(mv, monsters[m].data._mAnimData, monsters[m].data._mAnimFrame, monsters[m].data.MType->width);
	}
}

/**
 * @brief Render a player sprite
 * @param pnum Player id
 * @param x dPiece coordinate
 * @param y dPiece coordinate
 * @param px Back buffer coordinate
 * @param py Back buffer coordinate
 * @param pCelBuff sprite buffer
 * @param nCel frame
 * @param nWidth width
 */
static void DrawPlayer(int pnum, V2Di n, V2Di p, uint8_t *pCelBuff, int nCel, int nWidth)
{
	int x = n.x;
	int y = n.y;
	int l, frames;

	if (grid[x][y].dFlags & DunTileFlag::LIT || myplr().data._pInfraFlag || !lvl.setlevel && !lvl.currlevel) {
		if (!pCelBuff) {
			// app_fatal("Drawing player %d \"%s\": NULL Cel Buffer", pnum, plr[pnum].data._pName);
			return;
		}
		frames = SDL_SwapLE32(*(DWORD *)pCelBuff);
		if (nCel < 1 || frames > 50 || nCel > frames) {
			/*
			const char *szMode = "unknown action";
			if(plr[pnum].data._pmode <= PlayerMode::QUIT)
				szMode = szPlrModeAssert[plr[pnum].data._pmode];
			app_fatal(
				"Drawing player %d \"%s\" %s: facing %d, frame %d of %d",
				pnum,
				plr[pnum].data._pName,
				szMode,
				plr[pnum].data._pdir,
				nCel,
				frames);
			*/
			return;
		}
		if (pnum == pcursplr)
			Cl2DrawOutline(165, p, pCelBuff, nCel, nWidth);
		if (pnum == myplr()) {
			Cl2Draw(p, pCelBuff, nCel, nWidth);
			if (plr[pnum].data.pManaShield)
				Cl2Draw(
				    { p.x + plr[pnum].data._pAnimWidth2 - misfiledata[MissileGraphic::MANASHLD].mAnimWidth2[0], p.y },
				    misfiledata[MissileGraphic::MANASHLD].mAnimData[0], 1,
				    misfiledata[MissileGraphic::MANASHLD].mAnimWidth[0]);
		} else if (!(grid[x][y].dFlags & DunTileFlag::LIT) || myplr().data._pInfraFlag && light_table_index > 8) {
			Cl2DrawLightTbl(p, pCelBuff, nCel, nWidth, 1);
			if (plr[pnum].data.pManaShield)
				Cl2DrawLightTbl(
				    { p.x + plr[pnum].data._pAnimWidth2 - misfiledata[MissileGraphic::MANASHLD].mAnimWidth2[0], p.y },
				    misfiledata[MissileGraphic::MANASHLD].mAnimData[0], 1,
				    misfiledata[MissileGraphic::MANASHLD].mAnimWidth[0], 1);
		} else {
			l = light_table_index;
			if (light_table_index < 5)
				light_table_index = 0;
			else
				light_table_index -= 5;
			Cl2DrawLight(p, pCelBuff, nCel, nWidth);
			if (plr[pnum].data.pManaShield)
				Cl2DrawLight(
				    { p.x + plr[pnum].data._pAnimWidth2 - misfiledata[MissileGraphic::MANASHLD].mAnimWidth2[0], p.y },
				    misfiledata[MissileGraphic::MANASHLD].mAnimData[0], 1,
				    misfiledata[MissileGraphic::MANASHLD].mAnimWidth[0]);
			light_table_index = l;
		}
	}
}

/**
 * @brief Render a player sprite
 * @param x dPiece coordinate
 * @param y dPiece coordinate
 * @param sx Back buffer coordinate
 * @param sy Back buffer coordinate
 */
void DrawDeadPlayer(V2Di pn, V2Di s)
{
	int i, nCel, frames;
	V2Di pv;
	PlayerStruct *p;
	uint8_t *pCelBuff;

	grid.at(pn).dFlags &= ~DunTileFlag::DEAD_PLAYER;

	for (i = 0; i < MAX_PLRS; i++) {
		p = &plr[i].data;
		if (p->plractive && p->_hp <= 0 && p->plrlevel == (uint8_t)lvl.currlevel && plr[i].pos() == pn) {
			pCelBuff = p->_pAnimData;
			if (!pCelBuff) {
				// app_fatal("Drawing dead player %d \"%s\": NULL Cel Buffer", i, p->_pName);
				break;
			}
			nCel = p->_pAnimFrame;
			frames = SDL_SwapLE32(*(DWORD *)pCelBuff);
			if (nCel < 1 || frames > 50 || nCel > frames) {
				// app_fatal("Drawing dead player %d \"%s\": facing %d, frame %d of %d", i, p->_pName, p->_pdir, nCel, frame);
				break;
			}
			grid.at(pn).dFlags |= DunTileFlag::DEAD_PLAYER;
			pv.x = s.x + p->_poff.x - p->_pAnimWidth2;
			pv.y = s.y + p->_poff.y;
			DrawPlayer(i, pn, pv, p->_pAnimData, p->_pAnimFrame, p->_pAnimWidth);
		}
	}
}

/**
 * @brief Render an object sprite
 * @param x dPiece coordinate
 * @param y dPiece coordinate
 * @param ox Back buffer coordinate
 * @param oy Back buffer coordinate
 * @param pre Is the sprite in the background
 */
static void DrawObject(int x, int y, int ox, int oy, bool pre)
{
	int sx, sy, xx, yy, nCel, frames;
	char bv;
	uint8_t *pCelBuff;

	if (!grid[x][y].isObject() || light_table_index >= lightmax) return;

	//if (grid[x][y].dObject > 0) {
		bv = grid[x][y].getObject();
		if (object[bv]._oPreFlag != pre)
			return;
		sx = ox - object[bv]._oAnimWidth2;
		sy = oy;
	/* } else { // Not sure what hte significance of negative objects is yet
		bv = -(grid[x][y].dObject + 1);
		if (object[bv]._oPreFlag != pre)
			return;
		xx = object[bv]._o.x - x;
		yy = object[bv]._o.y - y;
		sx = (xx << 5) + ox - object[bv]._oAnimWidth2 - (yy << 5);
		sy = oy + (yy << 4) + (xx << 4);
	}*/

	assert((unsigned char)bv < MAXOBJECTS);

	pCelBuff = object[bv]._oAnimData;
	if (!pCelBuff) {
		// app_fatal("Draw Object type %d: NULL Cel Buffer", object[bv]._otype);
		return;
	}

	nCel = object[bv]._oAnimFrame;
	frames = SDL_SwapLE32(*(DWORD *)pCelBuff);
	if (nCel < 1 || frames > 50 || nCel > (int)frames) {
		// app_fatal("Draw Object: frame %d of %d, object type==%d", nCel, frames, object[bv]._otype);
		return;
	}

	if (bv == pcursobj)
		CelBlitOutline(194, { sx, sy }, object[bv]._oAnimData, object[bv]._oAnimFrame, object[bv]._oAnimWidth);
	if (object[bv]._oLight) {
		CelClippedDrawLight({ sx, sy }, object[bv]._oAnimData, object[bv]._oAnimFrame, object[bv]._oAnimWidth);
	} else {
		CelClippedDraw({ sx, sy }, object[bv]._oAnimData, object[bv]._oAnimFrame, object[bv]._oAnimWidth);
	}
}

static void scrollrt_draw_dungeon(int sx, int sy, int dx, int dy);

/**
 * @brief Render a cell
 * @param x dPiece coordinate
 * @param y dPiece coordinate
 * @param sx Back buffer coordinate
 * @param sy Back buffer coordinate
 */
static void drawCell(int x, int y, int sx, int sy)
{
	uint8_t *dst;
	MICROS *pMap;

	dst = &gpBuffer[sx + sy * BUFFER_WIDTH];
	pMap = &grid[x][y].dpiece_defs_map_2;
	int level_piece_id = grid[x][y].getPiece();

	// Specifies whether transparency is active for the current CEL file being decoded.
	int cel_transparency_active = (uint8_t)(pieces[level_piece_id].trans & lvl.TransList[grid[x][y].dTransVal]);

	// Specifies whether foliage(tile has extra content that overlaps previous tile) being rendered.
	int cel_foliage_active = !pieces[level_piece_id].solid;

	for (int i = 0; i < MicroTileLen >> 1; i++) {
		level_cel_block = pMap->mt[2 * i];
		if (level_cel_block != 0) {
			arch_draw_type = i == 0 ? 1 : 0;
			RenderTile(dst, level_piece_id, cel_transparency_active, cel_foliage_active);
		}
		level_cel_block = pMap->mt[2 * i + 1];
		if (level_cel_block != 0) {
			arch_draw_type = i == 0 ? 2 : 0;
			RenderTile(dst + TILE_WIDTH / 2, level_piece_id, cel_transparency_active, cel_foliage_active);
		}
		dst -= BUFFER_WIDTH * TILE_HEIGHT;
	}
	cel_foliage_active = false;
}

/**
 * @brief Render a floor tiles
 * @param x dPiece coordinate
 * @param y dPiece coordinate
 * @param sx Back buffer coordinate
 * @param sy Back buffer coordinate
 */
static void drawFloor(int x, int y, int sx, int sy)
{
	int cel_transparency_active = 0;
	light_table_index = grid[x][y].dLight;

	uint8_t *dst = &gpBuffer[sx + sy * BUFFER_WIDTH];
	arch_draw_type = 1; // Left
	int level_piece_id = grid[x][y].getPieceUnsafe();
	level_cel_block = grid[x][y].dpiece_defs_map_2.mt[0];
	if (level_cel_block != 0) {
		RenderTile(dst, level_piece_id, cel_transparency_active, false);
	}
	arch_draw_type = 2; // Right
	level_cel_block = grid[x][y].dpiece_defs_map_2.mt[1];
	if (level_cel_block != 0) {
		RenderTile(dst + TILE_WIDTH / 2, level_piece_id, cel_transparency_active, false);
	}
}

/**
 * @brief Draw item for a given tile
 * @param y dPiece coordinate
 * @param x dPiece coordinate
 * @param sx Back buffer coordinate
 * @param sy Back buffer coordinate
 * @param pre Is the sprite in the background
 */
static void DrawItem(int x, int y, int sx, int sy, bool pre)
{
	if (!grid[x][y].isItem()) return;
	Item * item = grid[x][y].getItem();
	if (!item) return;
	if (item->_iPostDraw == pre) return;
	int px = sx - item->_iAnimWidth2;
	if (item == pcursitem) {
		CelBlitOutline(181, {px, sy}, item->_iAnimData, item->_iAnimFrame,
		               item->_iAnimWidth);
	}
	CelClippedDrawLight({px, sy}, item->_iAnimData, item->_iAnimFrame,
	                    item->_iAnimWidth);
}

/**
 * @brief Check if and how a mosnter should be rendered
 * @param y dPiece coordinate
 * @param x dPiece coordinate
 * @param oy dPiece Y offset
 * @param sx Back buffer coordinate
 * @param sy Back buffer coordinate
 */
static void DrawMonsterHelper(int x, int y, int oy, int sx, int sy)
{
	int px, py;
	MonsterStruct *pMonster;

	int mi = grid[x][y + oy].getActor();

	//if (mi == 3) {
	//	static int x_last;
	//	static int y_last;

	//	if (x_last != x || y_last != y) {
	//		std::cout << "???";
	//	}

	//	x_last = x;
	//	y_last = y;
	//}

	if (lvl.type() == DunType::town) {
		px = sx - towner[mi]._tAnimWidth2;
		if (mi == pcursmonst) {
			CelBlitOutline(166, { px, sy }, towner[mi]._tAnimData, towner[mi]._tAnimFrame, towner[mi]._tAnimWidth);
		}
		assert(towner[mi]._tAnimData);
		CelClippedDraw({ px, sy }, towner[mi]._tAnimData, towner[mi]._tAnimFrame, towner[mi]._tAnimWidth);
		return;
	}

	if (!(grid[x][y].dFlags & DunTileFlag::LIT) && !myplr().data._pInfraFlag)
		return;

	if ((DWORD)mi >= MAXMONSTERS) {
		// app_fatal("Draw Monster: tried to draw illegal monster %d", mi);
	}

	pMonster = &monsters[mi].data;
	if (pMonster->_mFlags & MonsterFlag::hidden) {
		return;
	}

	if (pMonster->MType != NULL) {
		// app_fatal("Draw Monster \"%s\": uninitialized monster", pMonster->mName);
	}

	px = sx + pMonster->_moff.x - pMonster->MType->width2;
	py = sy + pMonster->_moff.y;
	if (mi == pcursmonst) {
		Cl2DrawOutline(233, { px, py }, pMonster->_mAnimData, pMonster->_mAnimFrame, pMonster->MType->width);
	}
	DrawMonster({ x, y }, { px, py }, mi);
}

/**
 * @brief Check if and how a player should be rendered
 * @param y dPiece coordinate
 * @param x dPiece coordinate
 * @param oy dPiece Y offset
 * @param sx Back buffer coordinate
 * @param sy Back buffer coordinate
 */
static void DrawPlayerHelper(int x, int y, int oy, int sx, int sy)
{
	const int p = grid[x][y + oy].getPlayerDraw();
	Player & player = plr[p];
	int px = sx + player.data._poff.x - player.data._pAnimWidth2;
	int py = sy + player.data._poff.y;
	DrawPlayer(p, { x, y + oy }, { px, py }, player.data._pAnimData, player.data._pAnimFrame, player.data._pAnimWidth);
}

/**
 * @brief Render object sprites
 * @param sx dPiece coordinate
 * @param sy dPiece coordinate
 * @param dx Back buffer coordinate
 * @param dy Back buffer coordinate
 */
static void scrollrt_draw_dungeon(int sx, int sy, int dx, int dy)
{
	int mi, nCel, frames;
	char bObj, bItem, bPlr, bArch, negPlr, dd;
	DeadStruct *pDeadGuy;
	uint8_t *pCelBuff;

	assert((DWORD)sx < MAXDUNX);
	assert((DWORD)sy < MAXDUNY);

	if (dRendered[sx][sy]) return;
	dRendered[sx][sy] = true;

	light_table_index = grid[sx][sy].dLight;

	drawCell(sx, sy, dx, dy);

	char bFlag = grid[sx][sy].dFlags;
	char bDead = grid[sx][sy].dDead;
	char bMap = grid[sx][sy].dTransVal;

	bool negMon = false;
	if (sy > 0) negMon = grid[sx][sy - 1].isActor();

	if (visiondebug && bFlag & DunTileFlag::LIT) {
		CelClippedDraw({ dx, dy }, pSquareCel, 1, 64);
	}

	if (MissilePreFlag) {
		DrawMissile({ sx, sy }, { dx, dy }, true);
	}

	int px, py;
	if (light_table_index < lightmax && bDead != 0) {
		pDeadGuy = &dead[(bDead & 0x1F) - 1];
		dd = (bDead >> 5) & 7;
		px = dx - pDeadGuy->_deadWidth2;
		pCelBuff = pDeadGuy->_deadData[dd];
		assert(pDeadGuy->_deadData[dd] != NULL);
		if (pCelBuff != NULL) {
			if (pDeadGuy->_deadtrans != 0) {
				Cl2DrawLightTbl({ px, dy }, pCelBuff, pDeadGuy->_deadFrame, pDeadGuy->_deadWidth, pDeadGuy->_deadtrans);
			} else {
				Cl2DrawLight({ px, dy }, pCelBuff, pDeadGuy->_deadFrame, pDeadGuy->_deadWidth);
			}
		}
	}
	DrawObject(sx, sy, dx, dy, 1);
	DrawItem(sx, sy, dx, dy, 1);
	//if (bFlag & DunTileFlag::PLAYERLR) {
	//	assert((DWORD)(sy - 1) < MAXDUNY);
	//	DrawPlayerHelper(sx, sy, -1, dx, dy);
	//}
	//if (bFlag & DunTileFlag::MONSTLR && negMon < 0) {
	//	DrawMonsterHelper(sx, sy, -1, dx, dy);
	//}
	if (bFlag & DunTileFlag::DEAD_PLAYER) {
		DrawDeadPlayer({ sx, sy }, { dx, dy });
	}
	if (grid[sx][sy].isPlayerDraw()) {
		DrawPlayerHelper(sx, sy, 0, dx, dy);
	}
	if (grid[sx][sy].isActor()) {
		DrawMonsterHelper(sx, sy, 0, dx, dy);
	}
	DrawMissile({ sx, sy }, { dx, dy }, false);
	DrawObject(sx, sy, dx, dy, 0);
	DrawItem(sx, sy, dx, dy, 0);

	if (lvl.type() != DunType::town) {
		bArch = grid[sx][sy].dSpecial;
		if (bArch != 0) {
			int cel_transparency_active = lvl.TransList[bMap];
			CelClippedBlitLightTrans(&gpBuffer[dx + BUFFER_WIDTH * dy], lvl.pSpecialCels, bArch, 64, cel_transparency_active);
		}
	} else {
		// Tree leafs should always cover player when entering or leaving the tile,
		// So delay the rendering untill after the next row is being drawn.
		// This could probably have been better solved by sprites in screen space.
		if (sx > 0 && sy > 0 && dy > TILE_HEIGHT + SCREEN_Y) {
			bArch = grid[sx - 1][sy - 1].dSpecial;
			if (bArch != 0) {
				CelBlitFrame(&gpBuffer[dx + BUFFER_WIDTH * (dy - TILE_HEIGHT)], lvl.pSpecialCels, bArch, 64);
			}
		}
	}
}

/**
 * @brief Render a row of floor tiles
 * @param x dPiece coordinate
 * @param y dPiece coordinate
 * @param sx Back buffer coordinate
 * @param sy Back buffer coordinate
 * @param rows Number of rows
 * @param columns Tile in a row
 */
static void scrollrt_drawFloor(int x, int y, int sx, int sy, int rows, int columns)
{
	assert(gpBuffer);

	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < columns; j++) {
			if (x >= 0 && x < MAXDUNX && y >= 0 && y < MAXDUNY) {
				if (grid[x][y].isPiece()) {
					//int level_piece_id = grid[x][y].getPiece();
					//if (!pieces[level_piece_id].solid)
					drawFloor(x, y, sx, sy);
				} else {
					world_draw_black_tile(sx, sy);
				}
				if (grid[x][y].isPlayer()) {
					//world_draw_red_tile(sx, sy);
					world_draw_gray_tile(sx, sy);
				}
				if (grid[x][y].isPlayerDraw()) {
					world_draw_red_tile(sx, sy);
				}
			} else {
				world_draw_black_tile(sx, sy);
			}
			ShiftGrid(&x, &y, 1, 0);
			sx += TILE_WIDTH;
		}
		// Return to start of row
		ShiftGrid(&x, &y, -columns, 0);
		sx -= columns * TILE_WIDTH;

		// Jump to next row
		sy += TILE_HEIGHT / 2;
		if (i & 1) {
			x++;
			columns--;
			sx += TILE_WIDTH / 2;
		} else {
			y++;
			columns++;
			sx -= TILE_WIDTH / 2;
		}
	}
}

/**
 * @brief Render a row of tile
 * @param x dPiece coordinate
 * @param y dPiece coordinate
 * @param sx Back buffer coordinate
 * @param sy Back buffer coordinate
 * @param rows Number of rows
 * @param columns Tile in a row
 */
static void scrollrt_draw(int x, int y, int sx, int sy, int rows, int columns)
{
	assert(gpBuffer);

	// Keep evaluating until MicroTiles can't affect screen
	rows += MicroTileLen;
	memset(dRendered, 0, sizeof(dRendered));

	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < columns ; j++) {
			if (x >= 0 && x < MAXDUNX && y >= 0 && y < MAXDUNY) {
				bool ispiece = grid[x][y].isPiece();
				if (ispiece) {
					if (x + 1 < MAXDUNX && y - 1 >= 0 && sx + TILE_WIDTH <= SCREEN_X + SCREEN_WIDTH) {
						// Render objects behind walls first to prevent sprites, that are moving
						// between tiles, from poking through the walls as they exceed the tile bounds.
						// A proper fix for this would probably be to layout the sceen and render by
						// sprite screen position rather than tile position.

						if (grid[x][y].isWall() && (grid[x + 1][y].isWall() || (x > 0 && grid[x - 1][y].isWall()))) { // Part of a wall aligned on the x-axis
							if (grid[x + 1][y - 1].isWalkable() && grid[x][y - 1].isWalkable()) {                     // Has walkable area behind it
								scrollrt_draw_dungeon(x + 1, y - 1, sx + TILE_WIDTH, sy);
							}
						}
					}
					scrollrt_draw_dungeon(x, y, sx, sy);
				}
			}
			ShiftGrid(&x, &y, 1, 0);
			sx += TILE_WIDTH;
		}
		// Return to start of row
		ShiftGrid(&x, &y, -columns, 0);
		sx -= columns * TILE_WIDTH;

		// Jump to next row
		sy += TILE_HEIGHT / 2;
		if (i & 1) {
			x++;
			columns--;
			sx += TILE_WIDTH / 2;
		} else {
			y++;
			columns++;
			sx -= TILE_WIDTH / 2;
		}
	}
}

/**
 * @brief Scale up the rendered part of the back buffer to take up the full view
 */
static void Zoom()
{
	int wdt = SCREEN_WIDTH / 2;
	int nSrcOff = SCREENXY(SCREEN_WIDTH / 2 - 1, VIEWPORT_HEIGHT / 2 - 1);
	int nDstOff = SCREENXY(SCREEN_WIDTH - 1, VIEWPORT_HEIGHT - 1);

	if (PANELS_COVER) {
		if (chrflag || questlog) {
			wdt >>= 1;
			nSrcOff -= wdt;
		} else if (gui.invflag || sbookflag) {
			wdt >>= 1;
			nSrcOff -= wdt;
			nDstOff -= SPANEL_WIDTH;
		}
	}

	uint8_t *src = &gpBuffer[nSrcOff];
	uint8_t *dst = &gpBuffer[nDstOff];

	for (int hgt = 0; hgt < VIEWPORT_HEIGHT / 2; hgt++) {
		for (int i = 0; i < wdt; i++) {
			*dst-- = *src;
			*dst-- = *src;
			src--;
		}
		memcpy(dst - BUFFER_WIDTH, dst, wdt * 2 + 1);
		src -= BUFFER_WIDTH - wdt;
		dst -= 2 * (BUFFER_WIDTH - wdt);
	}
}

/**
 * @brief Shifting the view area along the logical grid
 *        Note: this won't allow you to shift between even and odd rows
 * @param horizontal Shift the screen left or right
 * @param vertical Shift the screen up or down
 */
void ShiftGrid(int *x, int *y, int horizontal, int vertical)
{
	*x += vertical + horizontal;
	*y += vertical - horizontal;
}

/**
 * @brief Gets the number of rows covered by the main panel
 */
int RowsCoveredByPanel()
{
	if (SCREEN_WIDTH <= PANEL_WIDTH) {
		return 0;
	}

	int rows = PANEL_HEIGHT / TILE_HEIGHT * 2;
	if (!zoomflag) {
		rows /= 2;
	}

	return rows;
}

/**
 * @brief Calculate the offset needed for centering tiles in view area
 * @param offsetX Offset in pixels
 * @param offsetY Offset in pixels
 */
void CalcTileOffset(int *offsetX, int *offsetY)
{
	int x, y;

	if (zoomflag) {
		x = SCREEN_WIDTH % TILE_WIDTH;
		y = VIEWPORT_HEIGHT % TILE_HEIGHT;
	} else {
		x = (SCREEN_WIDTH / 2) % TILE_WIDTH;
		y = (VIEWPORT_HEIGHT / 2 + TILE_HEIGHT / 2) % TILE_HEIGHT;
	}

	if (x)
		x = (TILE_WIDTH - x) / 2;
	if (y)
		y = (TILE_HEIGHT - y) / 2;

	*offsetX = x;
	*offsetY = y;
}

/**
 * @brief Calculate the needed diamond tile to cover the view area
 * @param columns Tiles needed per row
 * @param rows Both even and odd rows
 */
void TilesInView(int *rcolumns, int *rrows)
{
	int columns = SCREEN_WIDTH / TILE_WIDTH;
	if (SCREEN_WIDTH % TILE_WIDTH) {
		columns++;
	}
	int rows = VIEWPORT_HEIGHT / (TILE_HEIGHT / 2);
	if (VIEWPORT_HEIGHT % (TILE_HEIGHT / 2)) {
		rows++;
	}

	if (!zoomflag) {
		// Half the number of tiles, rounded up
		if (columns % 2) {
			columns++;
		}
		columns /= 2;
		if (rows % 2) {
			rows++;
		}
		rows /= 2;
	}
	rows++; // Cover lower edge saw tooth, right edge accounted for in scrollrt_draw()

	*rcolumns = columns;
	*rrows = rows;
}

/**
 * @brief Configure render and process screen rows
 * @param x Center of view in dPiece coordinate
 * @param y Center of view in dPiece coordinate
 */
static void DrawGame(int x, int y)
{
	int i, sx, sy, columns, rows, xo, yo;

	// Limit rendering to the view area
	if (zoomflag)
		gpBufEnd = &gpBuffer[BUFFER_WIDTH * (VIEWPORT_HEIGHT + SCREEN_Y)];
	else
		gpBufEnd = &gpBuffer[BUFFER_WIDTH * (VIEWPORT_HEIGHT / 2 + SCREEN_Y)];

	// Adjust by player offset and tile grid alignment
	CalcTileOffset(&xo, &yo);
	sx = ScrollInfo._soff.x - xo + SCREEN_X;
	sy = ScrollInfo._soff.y - yo + SCREEN_Y + (TILE_HEIGHT / 2 - 1);

	// Center player tile on screen
	TilesInView(&columns, &rows);
	ShiftGrid(&x, &y, -columns / 2, -(rows - RowsCoveredByPanel()) / 4);
	if ((columns % 2) == 0) {
		y--;
	}

	// Skip rendering parts covered by the panels
	if (PANELS_COVER) {
		if (zoomflag) {
			if (chrflag || questlog) {
				ShiftGrid(&x, &y, 2, 0);
				columns -= 4;
				sx += SPANEL_WIDTH - TILE_WIDTH / 2;
			}
			if (gui.invflag || sbookflag) {
				ShiftGrid(&x, &y, 2, 0);
				columns -= 4;
				sx += -TILE_WIDTH / 2;
			}
		} else {
			if (chrflag || questlog) {
				ShiftGrid(&x, &y, 1, 0);
				columns -= 2;
				sx += -TILE_WIDTH / 2 / 2; // SPANEL_WIDTH accounted for in Zoom()
			}
			if (invflag || sbookflag) {
				ShiftGrid(&x, &y, 1, 0);
				columns -= 2;
				sx += -TILE_WIDTH / 2 / 2;
			}
		}
	}

 	// Draw areas moving in and out of the screen
 	switch (ScrollInfo._sdir) {
	case ScrollDir::N:
		sy -= TILE_HEIGHT;
		ShiftGrid(&x, &y, 0, -1);
		rows += 2;
		break;
	case ScrollDir::NE:
		sy -= TILE_HEIGHT;
		ShiftGrid(&x, &y, 0, -1);
		columns++;
		rows += 2;
		break;
	case ScrollDir::E:
		columns++;
		break;
	case ScrollDir::SE:
		columns++;
		rows++;
		break;
	case ScrollDir::S:
		rows += 2;
		break;
	case ScrollDir::SW:
		sx -= TILE_WIDTH;
		ShiftGrid(&x, &y, -1, 0);
		columns++;
		rows++;
		break;
	case ScrollDir::W:
		sx -= TILE_WIDTH;
		ShiftGrid(&x, &y, -1, 0);
		columns++;
		break;
	case ScrollDir::NW:
		sx -= TILE_WIDTH / 2;
		sy -= TILE_HEIGHT / 2;
		x--;
		columns++;
		rows++;
		break;
	}

	scrollrt_drawFloor(x, y, sx, sy, rows, columns); // s positions are fine here
	scrollrt_draw(x, y, sx, sy, rows, columns);

	// Allow rendering to the whole screen
	gpBufEnd = &gpBuffer[BUFFER_WIDTH * (SCREEN_HEIGHT + SCREEN_Y)];

	if (!zoomflag) {
		Zoom();
	}
}

// DevilutionX extension.
extern void DrawControllerModifierHints();

/**
 * @brief Start rendering of screen, town variation
 * @param StartX Center of view in dPiece coordinate
 * @param StartY Center of view in dPiece coordinate
 */
void DrawView(V2Di Start)
{
	DrawGame(Start.x, Start.y);
	if (automap.enabled()) {
		automap.draw();
	}
	if (stextflag && !qtextflag)
		DrawSText();
	if (invflag) {
		DrawInv();
	} else if (sbookflag) {
		DrawSpellBook();
	}

	DrawDurIcon();

	if (chrflag) {
		DrawChr();
	} else if (questlog) {
		DrawQuestLog();
	}
	if (!chrflag && myplr().data._pStatPts != 0 && !spselflag
	    && (!questlog || SCREEN_HEIGHT >= SPANEL_HEIGHT + PANEL_HEIGHT + 74 || SCREEN_WIDTH >= 4 * SPANEL_WIDTH)) {
		DrawLevelUpIcon();
	}
	if (uitemflag) {
		DrawUniqueInfo();
	}
	if (qtextflag) {
		DrawQText();
	}
	if (spselflag) {
		DrawSpellList();
	}
	if (dropGoldFlag) {
		DrawGoldSplit(dropGoldValue);
	}
	if (helpflag) {
		DrawHelp();
	}
	if (msgflag) {
		DrawDiabloMsg();
	}
	if (deathflag) {
		RedBack();
	} else if (PauseMode != 0) {
		gmenu_draw_pause();
	}

	DrawControllerModifierHints();
	DrawPlrMsg();
	gmenu_draw();
	doom_draw();
	DrawInfoBox();
	DrawLifeFlask();
	DrawManaFlask();
}

extern SDL_Surface *pal_surface;

/**
 * @brief Render the whole screen black
 */
void ClearScreenBuffer()
{
	lock_buf(3);

	assert(pal_surface != NULL);

	SDL_Rect SrcRect = {
		SCREEN_X,
		SCREEN_Y,
		SCREEN_WIDTH,
		SCREEN_HEIGHT,
	};
	SDL_FillRect(pal_surface, &SrcRect, 0);

	unlock_buf(3);
}

#ifdef _DEBUG
/**
 * @brief Scroll the screen when mouse is close to the edge
 */
void ScrollView()
{
	bool scroll;

	if (pcurs >= Cursor::FIRSTITEM)
		return;

	scroll = false;

	if (Mouse.x < 20) {
		if (lvl.dmax.y - 1 <= View.y || lvl.dmin.x >= View.x) {
			if (lvl.dmax.y - 1 > View.y) {
				View.y++;
				scroll = true;
			}
			if (lvl.dmin.x < View.x) {
				View.x--;
				scroll = true;
			}
		} else {
			View.y++;
			View.x--;
			scroll = true;
		}
	}
	if (Mouse.x > SCREEN_WIDTH - 20) {
		if (lvl.dmax.x - 1 <= View.x || lvl.dmin.y >= View.y) {
			if (lvl.dmax.x - 1 > View.x) {
				View.x++;
				scroll = true;
			}
			if (lvl.dmin.y < View.y) {
				View.y--;
				scroll = true;
			}
		} else {
			View.y--;
			View.x++;
			scroll = true;
		}
	}
	if (Mouse.y < 20) {
		if (lvl.dmin.y >= View.y || lvl.dmin.x >= View.x) {
			if (lvl.dmin.y < View.y) {
				View.y--;
				scroll = true;
			}
			if (lvl.dmin.x < View.x) {
				View.x--;
				scroll = true;
			}
		} else {
			View.x--;
			View.y--;
			scroll = true;
		}
	}
	if (Mouse.y > SCREEN_HEIGHT - 20) {
		if (lvl.dmax.y - 1 <= View.y || lvl.dmax.x - 1 <= View.x) {
			if (lvl.dmax.y - 1 > View.y) {
				View.y++;
				scroll = true;
			}
			if (lvl.dmax.x - 1 > View.x) {
				View.x++;
				scroll = true;
			}
		} else {
			View.x++;
			View.y++;
			scroll = true;
		}
	}
	if (scroll) ScrollInfo._sdir = ScrollDir::NONE;
}
#endif

/**
 * @brief Initialize the FPS meter
 */
void EnableFrameCount()
{
	frameflag = frameflag == 0;
	framestart = SDL_GetTicks();
}

/**
 * @brief Display the current average FPS over 1 sec
 */
static void DrawFPS()
{
	DWORD tc, frames;
	char String[12];
	HDC hdc;

	if (frameflag && gbActive && pPanelText) {
		frameend++;
		tc = SDL_GetTicks();
		frames = tc - framestart;
		if (tc - framestart >= 1000) {
			framestart = tc;
			framerate = 1000 * frameend / frames;
			frameend = 0;
		}
		snprintf(String, 12, "%d FPS", framerate);
		PrintGameStr({ 8, 65 }, String, COL_RED);
	}
}

/**
 * @brief Update part of the screen from the back buffer
 * @param dwX Back buffer coordinate
 * @param dwY Back buffer coordinate
 * @param dwWdt Back buffer coordinate
 * @param dwHgt Back buffer coordinate
 */
static void DoBlitScreen(DWORD dwX, DWORD dwY, DWORD dwWdt, DWORD dwHgt)
{
	SDL_Rect SrcRect = {
		dwX + SCREEN_X,
		dwY + SCREEN_Y,
		dwWdt,
		dwHgt,
	};
	SDL_Rect DstRect = {
		dwX,
		dwY,
		dwWdt,
		dwHgt,
	};

	BltFast(&SrcRect, &DstRect);
}

/**
 * @brief Check render pipline and blit indivudal screen parts
 * @param dwHgt Section of screen to update from top to bottom
 * @param draw_desc Render info box
 * @param draw_hp Render halth bar
 * @param draw_mana Render mana bar
 * @param draw_sbar Render belt
 * @param draw_btn Render panel buttons
 */
static void DrawMain(int dwHgt, bool draw_desc, bool draw_hp, bool draw_mana, bool draw_sbar, bool draw_btn)
{
	int ysize;
	DWORD dwTicks;
	bool retry;

	ysize = dwHgt;

	if (!gbActive) {
		return;
	}

	assert(ysize >= 0 && ysize <= SCREEN_HEIGHT);

	if (ysize > 0) {
		DoBlitScreen(0, 0, SCREEN_WIDTH, ysize);
	}
	if (ysize < SCREEN_HEIGHT) {
		if (draw_sbar) {
			DoBlitScreen(PANEL_LEFT + 204, PANEL_TOP + 5, 232, 28);
		}
		if (draw_desc) {
			DoBlitScreen(PANEL_LEFT + 176, PANEL_TOP + 46, 288, 60);
		}
		if (draw_mana) {
			DoBlitScreen(PANEL_LEFT + 460, PANEL_TOP, 88, 72);
			DoBlitScreen(PANEL_LEFT + 564, PANEL_TOP + 64, 56, 56);
		}
		if (draw_hp) {
			DoBlitScreen(PANEL_LEFT + 96, PANEL_TOP, 88, 72);
		}
		if (draw_btn) {
			DoBlitScreen(PANEL_LEFT + 8, PANEL_TOP + 5, 72, 119);
			DoBlitScreen(PANEL_LEFT + 556, PANEL_TOP + 5, 72, 48);
			if (game.isMultiplayer()) {
				DoBlitScreen(PANEL_LEFT + 84, PANEL_TOP + 91, 36, 32);
				DoBlitScreen(PANEL_LEFT + 524, PANEL_TOP + 91, 36, 32);
			}
		}
		if (sgdwCursWdtOld != 0) {
			DoBlitScreen(sgdwCursXOld, sgdwCursYOld, sgdwCursWdtOld, sgdwCursHgtOld);
		}
		if (sgdwCursWdt != 0) {
			DoBlitScreen(sgdwCursX, sgdwCursY, sgdwCursWdt, sgdwCursHgt);
		}
	}
}

/**
 * @brief Redraw screen
 * @param draw_cursor
 */
void scrollrt_draw_game_screen(bool draw_cursor)
{
	int hgt;

	if (force_redraw == 255) {
		force_redraw = 0;
		hgt = SCREEN_HEIGHT;
	} else {
		hgt = 0;
	}

	if (draw_cursor) {
		lock_buf(0);
		scrollrt_draw_cursor_item();
		unlock_buf(0);
	}

	DrawMain(hgt, false, false, false, false, false);

	if (draw_cursor) {
		lock_buf(0);
		scrollrt_draw_cursor_back_buffer();
		unlock_buf(0);
	}
	RenderPresent();
}

/**
 * @brief Render the game
 */
void DrawAndBlit()
{
	int hgt;
	bool ddsdesc, ctrlPan;

	if (!gbRunGame) {
		return;
	}

	if (SCREEN_WIDTH > PANEL_WIDTH || SCREEN_HEIGHT > VIEWPORT_HEIGHT + PANEL_HEIGHT || force_redraw == 255) {
		redrawhpflag = true;
		drawmanaflag = true;
		drawbtnflag = true;
		drawsbarflag = true;
		ddsdesc = false;
		ctrlPan = true;
		hgt = SCREEN_HEIGHT;
	} else {
		ddsdesc = true;
		ctrlPan = false;
		hgt = VIEWPORT_HEIGHT;
	}

	force_redraw = 0;

	lock_buf(0);
	DrawView(View);
	if (ctrlPan) {
		DrawCtrlPan();
	}
	if (redrawhpflag) {
		UpdateLifeFlask();
	}
	if (drawmanaflag) {
		UpdateManaFlask();
	}
	if (drawbtnflag) {
		DrawCtrlBtns();
	}

	DrawInvBelt();

	if (talkflag) {
		DrawTalkPan();
		hgt = SCREEN_HEIGHT;
	}
	scrollrt_draw_cursor_item();

	DrawFPS();

	unlock_buf(0);

	DrawMain(hgt, ddsdesc, redrawhpflag, drawmanaflag, true, drawbtnflag);

	lock_buf(0);
	scrollrt_draw_cursor_back_buffer();
	unlock_buf(0);
	RenderPresent();

	redrawhpflag = false;
	drawmanaflag = false;
	drawbtnflag = false;
	drawsbarflag = false;
}

}
