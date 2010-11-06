#ifndef __SPRITE_MANAGER__
#define __SPRITE_MANAGER__

#include "Common.h"
#include "FileManager.h"
#include "Text.h"
#include "3dStuff\Animation.h"
#include <3dStuff\Loader.h>

// Font flags
#define FT_NOBREAK             (0x0001)
#define FT_NOBREAK_LINE        (0x0002)
#define FT_CENTERX             (0x0004)
#define FT_CENTERY             (0x0008)
#define FT_CENTERR             (0x0010)
#define FT_BOTTOM              (0x0020)
#define FT_UPPER               (0x0040)
#define FT_COLORIZE            (0x0080)
#define FT_ALIGN               (0x0100)
#define FT_BORDERED            (0x0200)
#define FT_SKIPLINES(l)        (0x0400|((l)<<16))
#define FT_SKIPLINES_END(l)    (0x0800|((l)<<16))

// Colors
#define COLOR_CHANGE_ALPHA(v,a)  ((((v)|0xFF000000)^0xFF000000)|((DWORD)(a)&0xFF)<<24)
#define COLOR_IFACE_FIX          D3DCOLOR_XRGB(103,95,86)
#define COLOR_IFACE              SpriteManager::GetColor(((COLOR_IFACE_FIX>>16)&0xFF)+GameOpt.Light,((COLOR_IFACE_FIX>>8)&0xFF)+GameOpt.Light,(COLOR_IFACE_FIX&0xFF)+GameOpt.Light)
#define COLOR_IFACE_A(a)         ((COLOR_IFACE^0xFF000000)|((a)<<24))
#define COLOR_GAME_RGB(r,g,b)    SpriteManager::GetColor((r)+GameOpt.Light,(g)+GameOpt.Light,(b)+GameOpt.Light)
#define COLOR_IFACE_RED          (COLOR_IFACE|(0xFF<<16))
#define COLOR_IFACE_GREEN        (COLOR_IFACE|(0xFF<<8))
#define COLOR_CRITTER_NAME       D3DCOLOR_XRGB(0xAD,0xAD,0xB9)
#define COLOR_TEXT               D3DCOLOR_XRGB(60,248,0)
#define COLOR_TEXT_WHITE         D3DCOLOR_XRGB(0xFF,0xFF,0xFF)
#define COLOR_TEXT_RED           D3DCOLOR_XRGB(0xC8,0,0)
#define COLOR_TEXT_DRED          D3DCOLOR_XRGB(0xAA,0,0)
#define COLOR_TEXT_DDRED         D3DCOLOR_XRGB(0x66,0,0)
#define COLOR_TEXT_LRED          D3DCOLOR_XRGB(0xFF,0,0)
#define COLOR_TEXT_BLUE          D3DCOLOR_XRGB(0,0,0xC8)
#define COLOR_TEXT_DBLUE         D3DCOLOR_XRGB(0,0,0xAA)
#define COLOR_TEXT_LBLUE         D3DCOLOR_XRGB(0,0,0xFF)
#define COLOR_TEXT_GREEN         D3DCOLOR_XRGB(0,0xC8,0)
#define COLOR_TEXT_DGREEN        D3DCOLOR_XRGB(0,0xAA,0)
#define COLOR_TEXT_DDGREEN       D3DCOLOR_XRGB(0,0x66,0)
#define COLOR_TEXT_LGREEN        D3DCOLOR_XRGB(0,0xFF,0)
#define COLOR_TEXT_BLACK         D3DCOLOR_XRGB(0,0,0)
#define COLOR_TEXT_SBLACK        D3DCOLOR_XRGB(0x10,0x10,0x10)
#define COLOR_TEXT_DARK          D3DCOLOR_XRGB(0x30,0x30,0x30)
#define COLOR_TEXT_GREEN_RED     D3DCOLOR_XRGB(0,0xC8,0xC8)
#define COLOR_TEXT_SAND          D3DCOLOR_XRGB(0x8F,0x6F,0)

// Default effects
#define DEFAULT_EFFECT_NONE      (0)
#define DEFAULT_EFFECT_GENERIC   (1)
#define DEFAULT_EFFECT_TILE      (2)
#define DEFAULT_EFFECT_ROOF      (3)
#define DEFAULT_EFFECT_IFACE     (4)
#define DEFAULT_EFFECT_POINT     (5)
#define DEFAULT_EFFECT_COUNT     (6)

struct Surface
{
	int Type;
	LPDIRECT3DTEXTURE Texture;
	int Width,Height; // Texture size
	int BusyH; // Height point position
	int FreeX,FreeY; // Busy positions on current surface

	Surface():Type(0),Texture(NULL),Width(0),Height(0),BusyH(0),FreeX(0),FreeY(0){}
	~Surface(){SAFEREL(Texture);}
};
typedef vector<Surface*> SurfVec;
typedef vector<Surface*>::iterator SurfVecIt;

struct MYVERTEX
{
	FLOAT x,y,z,rhw;
	DWORD Diffuse;
	FLOAT tu,tv;
	FLOAT tu2,tv2;

	MYVERTEX():x(0),y(0),z(0),rhw(1),tu(0),tv(0),tu2(0),tv2(0),Diffuse(0){};
};
#define D3DFVF_MYVERTEX (D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX2)

struct MYVERTEX_PRIMITIVE
{
	FLOAT x,y,z,rhw;
	DWORD Diffuse;

	MYVERTEX_PRIMITIVE():x(0),y(0),z(0),rhw(1),Diffuse(0){};
};
#define D3DFVF_MYVERTEX_PRIMITIVE (D3DFVF_XYZRHW|D3DFVF_DIFFUSE)

struct SpriteInfo
{
	Surface* Surf;
	FLTRECT SprRect;
	WORD Width;
	WORD Height;
	short OffsX;
	short OffsY;
	EffectEx* Effect;
	Animation3d* Anim3d;
	SpriteInfo():Surf(NULL),Width(0),Height(0),OffsX(0),OffsY(0),Effect(NULL),Anim3d(NULL){};
};
typedef vector<SpriteInfo*> SprInfoVec;
typedef vector<SpriteInfo*>::iterator SprInfoVecIt;

struct DipData
{
	LPDIRECT3DTEXTURE Texture;
	EffectEx* Effect;
	DWORD SprCount;
	DipData(LPDIRECT3DTEXTURE tex, EffectEx* effect):Texture(tex),Effect(effect),SprCount(1){};
};
typedef vector<DipData> DipDataVec;
typedef vector<DipData>::iterator DipDataVecIt;

class Sprite
{
public:
	DWORD MapPos;
	DWORD MapPosInd;
	int ScrX;
	int ScrY;
	DWORD SprId;
	DWORD* PSprId;
	short* OffsX;
	short* OffsY;
	BYTE* Alpha;
	BYTE* Light;
	bool* ValidCallback;
	bool Valid;
	enum EggType : BYTE{EggNone=0,EggAlways,EggX,EggY,EggXandY,EggXorY} Egg;
	enum ContourType : BYTE{ContourNone=0,ContourRed,ContourYellow,ContourCustom} Contour;
	DWORD ContourColor;
	DWORD Color;
	DWORD FlashMask;

	Sprite(){ZeroMemory(this,sizeof(Sprite));}
	void Unvalidate();
};
typedef vector<Sprite*> SpriteVec;
typedef vector<Sprite*>::iterator SpriteVecIt;

class Sprites
{
private:
	// Pool
	static SpriteVec spritesPool;
	static void GrowPool(size_t size);
	static void ClearPool();

	// Data
	SpriteVec spritesTree;
	size_t spritesTreeSize;
	Sprite& PutSprite(size_t index, DWORD map_pos, int x, int y, DWORD id, DWORD* id_ptr, short* ox, short* oy, BYTE* alpha, BYTE* light, bool* callback);

public:
	Sprites():spritesTreeSize(0){}
	~Sprites(){Resize(0);}
	SpriteVecIt Begin(){return spritesTree.begin();}
	SpriteVecIt End(){return spritesTree.begin()+spritesTreeSize;}
	size_t Size(){return spritesTreeSize;}
	Sprite& AddSprite(DWORD map_pos, int x, int y, DWORD id, DWORD* id_ptr, short* ox, short* oy, BYTE* alpha, BYTE* light, bool* callback);
	Sprite& InsertSprite(DWORD map_pos, int x, int y, DWORD id, DWORD* id_ptr, short* ox, short* oy, BYTE* alpha, BYTE* light, bool* callback);
	void Resize(size_t size);
	void Clear(){Resize(0);}
	void Unvalidate();
	void SortBySurfaces();
	void SortByMapPos();
};

struct AnyFrames 
{
	short OffsX;
	short OffsY;
	DWORD* Ind;   // Sprite Ids
	short* NextX; // Offsets
	short* NextY; // Offsets
	DWORD CntFrm; // Frames count
	DWORD Ticks;  // Time of playing animation

	DWORD GetSprId(int num_frm){return Ind[num_frm];}
	int GetCnt(){return CntFrm;}
	DWORD GetCurSprId(){return CntFrm>1?Ind[((Timer::GameTick()%Ticks)*100/Ticks)*CntFrm/100]:Ind[0];}

	AnyFrames():Ind(NULL),NextX(NULL),NextY(NULL),OffsX(0),OffsY(0),CntFrm(0),Ticks(0){};
	~AnyFrames(){SAFEDELA(Ind);SAFEDELA(NextX);SAFEDELA(NextY);}
};
typedef map<DWORD,AnyFrames*,less<DWORD> > AnimMap;
typedef map<DWORD,AnyFrames*,less<DWORD> >::iterator AnimMapIt;
typedef map<DWORD,AnyFrames*,less<DWORD> >::value_type AnimMapVal;

struct PrepPoint
{
	short PointX;
	short PointY;
	short* PointOffsX;
	short* PointOffsY;
	DWORD PointColor;

	PrepPoint():PointX(0),PointY(0),PointColor(0),PointOffsX(NULL),PointOffsY(NULL){}
	PrepPoint(short x, short y, DWORD color, short* ox = NULL, short* oy = NULL):
		PointX(x),PointY(y),PointColor(color),PointOffsX(ox),PointOffsY(oy){}
};
typedef vector<PrepPoint> PointVec;
typedef vector<PrepPoint>::iterator PointVecIt;
typedef vector<PointVec> PointVecVec;
typedef vector<PointVec>::iterator PointVecVecIt;

struct SpriteMngrParams
{
	HWND WndHeader;
	void (*PreRestoreFunc)();
	void (*PostRestoreFunc)();
};

class SpriteManager
{
public:
	bool isInit;
	SpriteMngrParams mngrParams;
	HWND hWnd;
	LPDIRECT3D direct3D;
	LPDIRECT3DDEVICE d3dDevice;
	//D3DDISPLAYMODE displayMode;
	D3DPRESENT_PARAMETERS presentParams;
	D3DCAPS9 deviceCaps;
	int modeWidth,modeHeight;
	bool sceneBeginned;

public:
	SpriteManager();
	bool Init(SpriteMngrParams& params);
	bool InitBuffers();
	bool InitRenderStates();
	bool IsInit(){return isInit;}
	void Clear();
	LPDIRECT3DDEVICE GetDevice(){return d3dDevice;}
	bool BeginScene(DWORD clear_color);
	void EndScene();
	bool Restore();
	void (*PreRestore)();
	void (*PostRestore)();
	bool CreateRenderTarget(LPDIRECT3DSURFACE& surf, int w, int h);
	bool ClearRenderTarget(LPDIRECT3DSURFACE& surf, DWORD color);
	bool ClearCurRenderTarget(DWORD color);

	// Surfaces
public:
	int SurfType;

	void FreeSurfaces(int surf_type);
	void SaveSufaces();

private:
	int baseTexture;
	SurfVec surfList;

	Surface* CreateNewSurface(int w, int h);
	Surface* FindSurfacePlace(SpriteInfo* si, int& x, int& y);
	DWORD FillSurfaceFromMemory(SpriteInfo* si, void* data, DWORD size);

	// Load sprites
public:
	DWORD LoadSprite(const char* fname, int path_type, int dir = 0);
	DWORD ReloadSprite(DWORD spr_id, const char* fname, int path_type);
	AnyFrames* LoadAnyAnimation(const char* fname, int path_type, bool anim_pix, int dir);
	Animation3d* Load3dAnimation(const char* fname, int path_type);

private:
	SprInfoVec sprData;
	LPDIRECT3DSURFACE spr3dRT,spr3dRTEx,spr3dDS,spr3dRTData;
	int spr3dSurfWidth,spr3dSurfHeight;

	DWORD LoadRix(const char* fname, int path_type);
	DWORD LoadSpriteAlt(const char* fname, int path_type);
	DWORD LoadSprite3d(const char* fname, int path_type, int dir);
	AnyFrames* LoadAnyAnimationFofrm(const char* fname, int path_type, int dir);
	AnyFrames* LoadAnyAnimationOneSpr(const char* fname, int path_type, int dir);

	// Draw
public:
	static DWORD GetColor(int r, int g, int b);
	void SetSpritesColor(DWORD c){baseColor=c;}
	DWORD GetSpritesColor(){return baseColor;}
	SpriteInfo* GetSpriteInfo(DWORD id){return sprData[id];}
	LPDIRECT3DVERTEXBUFFER GetVB(){return pVB;}
	LPDIRECT3DINDEXBUFFER GetIB(){return pIB;}
	void GetDrawCntrRect(Sprite* prep, INTRECT* prect);
	bool IsPixNoTransp(DWORD spr_id, int offs_x, int offs_y, bool with_zoom = true);
	bool IsEggTransp(int pix_x, int pix_y);

	void PrepareSquare(PointVec& points, FLTRECT& r, DWORD color);
	bool PrepareBuffer(Sprites& dtree, LPDIRECT3DSURFACE surf, BYTE alpha);
	bool Flush();

	bool DrawSprite(DWORD id, int x, int y, DWORD color = 0);
	bool DrawSpriteSize(DWORD id, int x, int y, float w, float h, bool stretch_up, bool center, DWORD color = 0);
	bool DrawSprites(Sprites& dtree, bool collect_contours, bool use_egg, DWORD pos_min, DWORD pos_max);
	bool DrawPrepared(LPDIRECT3DSURFACE& surf);
	bool DrawSurface(LPDIRECT3DSURFACE& surf, RECT& dst);
	bool DrawPoints(PointVec& points, D3DPRIMITIVETYPE prim, float* zoom = NULL, FLTRECT* stencil = NULL, FLTPOINT* offset = NULL);
	bool Draw3d(int x, int y, float scale, Animation3d* anim3d, FLTRECT* stencil, DWORD color);
	bool Draw3dSize(FLTRECT rect, bool stretch_up, bool center, Animation3d* anim3d, FLTRECT* stencil, DWORD color);

	void SetDefaultEffect2D(DWORD index, EffectEx* effect){sprDefaultEffect[index]=effect;}
	void SetCurEffect2D(DWORD index){curDefaultEffect=sprDefaultEffect[index];}

private:
	LPDIRECT3DVERTEXBUFFER pVB;
	LPDIRECT3DINDEXBUFFER pIB;
	MYVERTEX* waitBuf;
	DipDataVec dipQueue;
	DWORD baseColor;
	int flushSprCnt; // Max sprites to flush
	int curSprCnt; // Current sprites to flush
	LPDIRECT3DVERTEXBUFFER vbPoints;
	int vbPointsSize;
	EffectEx* sprDefaultEffect[DEFAULT_EFFECT_COUNT];
	EffectEx* curDefaultEffect;

	void FlushDIP();

	// Contours
public:
	bool DrawContours();
	void ClearSpriteContours(){createdSpriteContours.clear();}

private:
	LPDIRECT3DTEXTURE contoursTexture;
	LPDIRECT3DSURFACE contoursTextureSurf;
	LPDIRECT3DTEXTURE contoursMidTexture;
	LPDIRECT3DSURFACE contoursMidTextureSurf;
	LPDIRECT3DSURFACE contours3dRT;
	IDirect3DPixelShader9* contoursPS;
	ID3DXConstantTable* contoursCT;
	D3DXHANDLE contoursConstWidthStep,contoursConstHeightStep,
		contoursConstSpriteBorders,contoursConstSpriteBordersHeight,
		contoursConstContourColor,contoursConstContourColorOffs;
	bool contoursAdded;
	DwordMap createdSpriteContours;
	Sprites spriteContours;

	bool CollectContour(int x, int y, SpriteInfo* si, Sprite* spr); // Must called after Draw3d!
	DWORD GetSpriteContour(SpriteInfo* si, Sprite* spr);
	void WriteContour4(DWORD* buf, DWORD buf_w, D3DLOCKED_RECT& r, DWORD w, DWORD h, DWORD color);
	void WriteContour8(DWORD* buf, DWORD buf_w, D3DLOCKED_RECT& r, DWORD w, DWORD h, DWORD color);

	// Transparent egg
private:
	bool eggValid;
	WORD eggHx,eggHy;
	int eggX,eggY;
	short* eggOX,*eggOY;
	SpriteInfo* sprEgg;
	int eggSprWidth,eggSprHeight;
	float eggSurfWidth,eggSurfHeight;

public:
	bool CompareHexEgg(WORD hx, WORD hy, Sprite::EggType egg);
	void SetEgg(WORD hx, WORD hy, Sprite* spr);
	void EggNotValid(){eggValid=false;}
	void GetEggPos(int& ex, int& ey){ex=eggHx;ey=eggHy;}

	// Fonts
public:
	void SetDefaultFont(int index, DWORD color);
	void SetFontEffect(int index, EffectEx* effect);
	bool LoadFontOld(int index, const char* font_name, int size_mod);
	bool LoadFontAAF(int index, const char* font_name, int size_mod);
	bool LoadFontBMF(int index, const char* font_name);
	bool DrawStr(INTRECT& r, const char* str, DWORD flags, DWORD col = 0, int num_font = -1);
	int GetLinesCount(int width, int height, const char* str, int num_font = -1);
	int GetLinesHeight(int width, int height, const char* str, int num_font = -1);
	int SplitLines(INTRECT& r, const char* cstr, int num_font, StrVec& str_vec);
};

#endif // __SPRITE_MANAGER__