#ifndef __HEX_MANAGER__
#define __HEX_MANAGER__

#include "Common.h"
#include "SpriteManager.h"
#include "Item.h"
#include "ItemManager.h"
#include "CritterCl.h"
#include "ItemHex.h"
#include "ProtoMap.h"

class Terrain;
typedef vector<Terrain*> TerrainVec;
typedef vector<Terrain*>::iterator TerrainVecIt;

#define MAX_FIND_PATH   (600)
#define TILE_ALPHA	    (0xFF)
#define VIEW_WIDTH      ((int)((MODE_WIDTH/GameOpt.MapHexWidth+((MODE_WIDTH%GameOpt.MapHexWidth)?1:0))*GameOpt.SpritesZoom))
#define VIEW_HEIGHT     ((int)((MODE_HEIGHT/GameOpt.MapHexLineHeight+((MODE_HEIGHT%GameOpt.MapHexLineHeight)?1:0))*GameOpt.SpritesZoom))
#define SCROLL_OX       (GameOpt.MapHexWidth)
#define SCROLL_OY       (GameOpt.MapHexLineHeight*2)
#define HEX_W           (GameOpt.MapHexWidth)
#define HEX_LINE_H      (GameOpt.MapHexLineHeight)
#define HEX_REAL_H      (GameOpt.MapHexHeight)
#define HEX_OX          (GameOpt.MapHexWidth/2)
#define HEX_OY          (GameOpt.MapHexHeight/2)
#define TILE_OX         (GameOpt.MapTileOffsX)
#define TILE_OY         (GameOpt.MapTileOffsY)
#define ROOF_OX         (GameOpt.MapRoofOffsX)
#define ROOF_OY         (GameOpt.MapRoofOffsY)
#define MAX_MOVE_OX     (99)
#define MAX_MOVE_OY     (99)

/************************************************************************/
/* ViewField                                                            */
/************************************************************************/

struct ViewField
{
	int HexX,HexY;
	int ScrX,ScrY;
	float ScrXf,ScrYf;

	ViewField():HexX(0),HexY(0),ScrX(0),ScrY(0),ScrXf(0.0f),ScrYf(0.0f){};
};

/************************************************************************/
/* LightSource                                                          */
/************************************************************************/

struct LightSource 
{
	WORD HexX;
	WORD HexY;
	DWORD ColorRGB;
	BYTE Distance;
	BYTE Flags;
	int Intensity;

	LightSource(WORD hx, WORD hy, DWORD color, BYTE distance, int inten, BYTE flags):HexX(hx),HexY(hy),ColorRGB(color),Intensity(inten),Distance(distance),Flags(flags){}
};
typedef vector<LightSource> LightSourceVec;
typedef vector<LightSource>::iterator LightSourceVecIt;

/************************************************************************/
/* Field                                                                */
/************************************************************************/

struct Field
{
	struct Tile
	{
		AnyFrames* Anim;
		short OffsX;
		short OffsY;
		BYTE Layer;
	};
	typedef vector<Tile> TileVec;

	CritterCl* Crit;
	CritVec DeadCrits;
	int ScrX;
	int ScrY;
	TileVec Tiles;
	TileVec Roofs;
	ItemHexVec Items;
	short RoofNum;
	bool ScrollBlock;
	bool IsWall;
	bool IsWallSAI;
	bool IsWallTransp;
	bool IsScen;
	bool IsExitGrid;
	bool IsNotPassed;
	bool IsNotRaked;
	BYTE Corner;
	bool IsNoLight;
	BYTE LightValues[3];
	bool IsMultihex;

	void Clear();
	void AddItem(ItemHex* item);
	void EraseItem(ItemHex* item);
	void ProcessCache();
	void AddTile(AnyFrames* anim, short ox, short oy, BYTE layer, bool is_roof);
	Field(){Clear();}
};

/************************************************************************/
/* Rain                                                                 */
/************************************************************************/

#define RAIN_TICK       (60)
#define RAIN_SPEED      (15)

struct Drop
{
	DWORD CurSprId;
	short OffsX;
	short OffsY;
	short GroundOffsY;
	short DropCnt;

	Drop():CurSprId(0),OffsX(0),OffsY(0),DropCnt(0),GroundOffsY(0){};
	Drop(WORD id, short x, short y, short ground_y):CurSprId(id),OffsX(x),OffsY(y),DropCnt(-1),GroundOffsY(ground_y){};
};

typedef vector<Drop*> DropVec;
typedef vector<Drop*>::iterator DropVecIt;

/************************************************************************/
/* HexField                                                             */
/************************************************************************/

class HexManager
{
	// Hexes
public:
	bool ResizeField(WORD w, WORD h);
	Field& GetField(WORD hx, WORD hy){return hexField[hy*maxHexX+hx];}
	bool& GetHexToDraw(WORD hx, WORD hy){return hexToDraw[hy*maxHexX+hx];}
	char& GetHexTrack(WORD hx, WORD hy){return hexTrack[hy*maxHexX+hx];}
	WORD GetMaxHexX(){return maxHexX;}
	WORD GetMaxHexY(){return maxHexY;}
	void ClearHexToDraw(){ZeroMemory(hexToDraw,maxHexX*maxHexY*sizeof(bool));}
	void ClearHexTrack(){ZeroMemory(hexTrack,maxHexX*maxHexY*sizeof(char));}
	void SwitchShowTrack();
	bool IsShowTrack(){return isShowTrack;};

	bool FindPath(CritterCl* cr, WORD start_x, WORD start_y, WORD& end_x, WORD& end_y, ByteVec& steps, int cut);
	bool CutPath(CritterCl* cr, WORD start_x, WORD start_y, WORD& end_x, WORD& end_y, int cut);
	bool TraceBullet(WORD hx, WORD hy, WORD tx, WORD ty, DWORD dist, float angle, CritterCl* find_cr, bool find_cr_safe,
		CritVec* critters, int find_type, WordPair* pre_block, WordPair* block, WordPairVec* steps, bool check_passed);

private:
	WORD maxHexX,maxHexY;
	Field* hexField;
	bool* hexToDraw;
	char* hexTrack;
	AnyFrames* picTrack1,*picTrack2;
	AnyFrames* picHexMask;
	bool isShowTrack;
	bool isShowHex;
	AnyFrames* picHex[3];
	string curDataPrefix;

	// Center
public:
	void FindSetCenter(int cx, int cy);

private:
	void FindSetCenterDir(WORD& hx, WORD& hy, int dirs[2], int steps);

	// Map load
private:
	WORD curPidMap;
	int curMapTime;
	int dayTime[4];
	BYTE dayColor[12];
	DWORD curHashTiles;
	DWORD curHashWalls;
	DWORD curHashScen;

public:
	bool IsMapLoaded(){return hexField!=NULL;}
	WORD GetCurPidMap(){return curPidMap;}
	bool LoadMap(WORD map_pid);
	void UnloadMap();
	void GetMapHash(WORD map_pid, DWORD& hash_tiles, DWORD& hash_walls, DWORD& hash_scen);
	bool GetMapData(WORD map_pid, ItemVec& items, WORD& maxhx, WORD& maxhy);
	bool ParseScenery(SceneryCl& scen);
	int GetDayTime();
	int GetMapTime();
	int* GetMapDayTime();
	BYTE* GetMapDayColor();

	// Init, finish, restore
private:
	Sprites mainTree;
	ViewField* viewField;

	int screenHexX,screenHexY;
	int hTop,hBottom,wLeft,wRight;
	int wVisible,hVisible;

	void InitView(int cx, int cy);
	bool IsVisible(DWORD spr_id, int ox, int oy);
	bool ProcessHexBorders(DWORD spr_id, int ox, int oy);

public:
	void ChangeZoom(int zoom); // <0 in, >0 out, 0 normalize
	void GetScreenHexes(int& sx, int& sy){sx=screenHexX;sy=screenHexY;}
	void GetHexCurrentPosition(WORD hx, WORD hy, int& x, int& y);

public:
	bool SpritesCanDrawMap;

	HexManager();
	bool Init();
	void Clear();
	void ReloadSprites();

	void PreRestore();
	void PostRestore();

	void RebuildMap(int rx, int ry);
	void DrawMap();
	bool Scroll();
	Sprites& GetDrawTree(){return mainTree;}
	void RefreshMap(){RebuildMap(screenHexX,screenHexY);}

	struct AutoScroll_
	{
		bool Active;
		bool CanStop;
		double OffsX,OffsY;
		double OffsXStep,OffsYStep;
		double Speed;
		DWORD LockedCritter;
	} AutoScroll;

	void ScrollToHex(int hx, int hy, double speed, bool can_stop);

private:
	bool ScrollCheckPos(int (&positions)[4], int dir1, int dir2);
	bool ScrollCheck(int xmod, int ymod);

public:
	void SwitchShowHex();
	void SwitchShowRain();
	void SetWeather(int time, BYTE rain);

	void SetCrit(CritterCl* cr);
	bool TransitCritter(CritterCl* cr, int hx, int hy, bool animate, bool force);

	// Critters
public:
	CritMap allCritters;
	DWORD chosenId;
	DWORD critterContourCrId;
	int critterContour,crittersContour;

public:
	CritterCl* GetCritter(DWORD crid){if(!crid) return NULL; CritMapIt it=allCritters.find(crid); return it!=allCritters.end()?(*it).second:NULL;}
	CritterCl* GetChosen(){if(!chosenId) return NULL; CritMapIt it=allCritters.find(chosenId); return it!=allCritters.end()?(*it).second:NULL;}
	void AddCrit(CritterCl* cr);
	void RemoveCrit(CritterCl* cr);
	void EraseCrit(DWORD crid);
	void ClearCritters();
	void GetCritters(WORD hx, WORD hy, CritVec& crits, int find_type);
	CritMap& GetCritters(){return allCritters;}
	void SetCritterContour(DWORD crid, int contour);
	void SetCrittersContour(int contour);
	void SetMultihex(WORD hx, WORD hy, DWORD multihex, bool set);

	// Items
private:
	ItemHexVec hexItems;

	void PlaceItemBlocks(WORD hx, WORD hy, ProtoItem* proto_item);
	void ReplaceItemBlocks(WORD hx, WORD hy, ProtoItem* proto_item);

public:
	bool AddItem(DWORD id, WORD pid, WORD hx, WORD hy, bool is_added, Item::ItemData* data);
	void ChangeItem(DWORD id, const Item::ItemData& data);
	void FinishItem(DWORD id, bool is_deleted);
	ItemHexVecIt DeleteItem(ItemHex* item, bool with_delete = true);
	void PushItem(ItemHex* item);
	ItemHex* GetItem(WORD hx, WORD hy, WORD pid);
	ItemHex* GetItemById(WORD hx, WORD hy, DWORD id);
	ItemHex* GetItemById(DWORD id);
	void GetItems(WORD hx, WORD hy, ItemHexVec& items);
	ItemHexVec& GetItems(){return hexItems;}
	INTRECT GetRectForText(WORD hx, WORD hy);
	void ProcessItems();

	// Light
private:
	bool requestRebuildLight;
	BYTE* hexLight;
	int lightPointsCount;
	PointVecVec lightPoints;
	PointVec lightSoftPoints;
	LightSourceVec lightSources;
	LightSourceVec lightSourcesScen;

	void MarkLight(WORD hx, WORD hy, DWORD inten);
	void MarkLightEndNeighbor(WORD hx, WORD hy, bool north_south, DWORD inten);
	void MarkLightEnd(WORD from_hx, WORD from_hy, WORD to_hx, WORD to_hy, DWORD inten);
	void MarkLightStep(WORD from_hx, WORD from_hy, WORD to_hx, WORD to_hy, DWORD inten);
	void TraceLight(WORD from_hx, WORD from_hy, WORD& hx, WORD& hy, int dist, DWORD inten);
	void ParseLightTriangleFan(LightSource& ls);
	void ParseLight(WORD hx, WORD hy, int dist, DWORD inten, DWORD flags);
	void RealRebuildLight();
	void CollectLightSources();

public:
	void ClearHexLight(){ZeroMemory(hexLight,maxHexX*maxHexY*sizeof(BYTE)*3);}
	BYTE* GetLightHex(WORD hx, WORD hy){return &hexLight[hy*maxHexX*3+hx*3];}
	void RebuildLight(){requestRebuildLight=true;}
	LightSourceVec& GetLights(){return lightSources;}

	// Tiles, roof
private:
	bool reprepareTiles;
	Sprites tilesTree;
	LPDIRECT3DSURFACE tileSurf;
	int tileSurfWidth,tileSurfHeight;
	int roofSkip;
	Sprites roofTree;
	TerrainVec tilesTerrain;

	bool CheckTilesBorder(Field::Tile& tile, bool is_roof);
	bool AddTerrain(DWORD name_hash, int hx, int hy);

public:
	bool InitTilesSurf();
	void RebuildTiles();
	void RebuildRoof();
	void SetSkipRoof(int hx, int hy);
	void MarkRoofNum(int hx, int hy, int num);

	// Pixel get
public:
	bool GetHexPixel(int x, int y, WORD& hx, WORD& hy);
	ItemHex* GetItemPixel(int x, int y, bool& item_egg); // With transparent egg
	CritterCl* GetCritterPixel(int x, int y, bool ignore_dead_and_chosen);
	void GetSmthPixel(int x, int y, ItemHex*& item, CritterCl*& cr);

	// Effects
public:
	bool RunEffect(WORD eff_pid, WORD from_hx, WORD from_hy, WORD to_hx, WORD to_hy);

	// Rain
private:
	DropVec rainData;
	int rainCapacity;
	AnyFrames* picRainDrop;
	AnyFrames* picRainDropA[7];
	Sprites roofRainTree;

public:
	void ProcessRain();

	// Cursor
public:
	void SetCursorPos(int x, int y, bool show_steps, bool refresh);
	void SetCursorVisible(bool visible){isShowCursor=visible;}
	void DrawCursor(DWORD spr_id);
	void DrawCursor(const char* text);

private:
	bool isShowCursor;
	int drawCursorX;
	AnyFrames* cursorPrePic,*cursorPostPic,*cursorXPic;
	int cursorX,cursorY;

/************************************************************************/
/* Mapper                                                               */
/************************************************************************/

#ifdef FONLINE_MAPPER
public:
	// Proto map
	ProtoMap* CurProtoMap;
	bool SetProtoMap(ProtoMap& pmap);

	// Selected tile, roof
public:
	void ClearSelTiles();
	void ParseSelTiles();
	void SetTile(DWORD name_hash, WORD hx, WORD hy, short ox, short oy, BYTE layer, bool is_roof, bool select);
	//void SetTerrain(WORD hx, WORD hy, DWORD name_hash);
	//void RebuildTerrain();

	// Ignore pids to draw
private:
	WordSet fastPids;
	WordSet ignorePids;

public:
	void AddFastPid(WORD pid);
	bool IsFastPid(WORD pid);
	void ClearFastPids();
	void AddIgnorePid(WORD pid);
	void SwitchIgnorePid(WORD pid);
	bool IsIgnorePid(WORD pid);
	void ClearIgnorePids();

	void GetHexesRect(INTRECT& rect, WordPairVec& hexes);
	void MarkPassedHexes();
	void AffectItem(MapObject* mobj, ItemHex* item);
	void AffectCritter(MapObject* mobj, CritterCl* cr);

#endif // FONLINE_MAPPER
};

#endif // __HEX_MANAGER__