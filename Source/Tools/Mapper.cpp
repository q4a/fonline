//      __________        ___               ______            _
//     / ____/ __ \____  / (_)___  ___     / ____/___  ____ _(_)___  ___
//    / /_  / / / / __ \/ / / __ \/ _ \   / __/ / __ \/ __ `/ / __ \/ _ \
//   / __/ / /_/ / / / / / / / / /  __/  / /___/ / / / /_/ / / / / /  __/
//  /_/    \____/_/ /_/_/_/_/ /_/\___/  /_____/_/ /_/\__, /_/_/ /_/\___/
//                                                  /____/
// FOnline Engine
// https://fonline.ru
// https://github.com/cvet/fonline
//
// MIT License
//
// Copyright (c) 2006 - present, Anton Tsvetinskiy aka cvet <cvet@tut.by>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "Mapper.h"
#include "3dStuff.h"
#include "DiskFileSystem.h"
#include "GenericUtils.h"
#include "Log.h"
#include "MessageBox.h"
#include "StringUtils.h"
#include "Testing.h"
#include "Timer.h"
#include "Version_Include.h"
#include "WinApi_Include.h"

#include "sha1.h"
#include "sha2.h"

#define SCRIPT_ERROR_R(error, ...) \
    do \
    { \
        Self->ScriptSys.RaiseException(_str(error, ##__VA_ARGS__)); \
        return; \
    } while (0)
#define SCRIPT_ERROR_R0(error, ...) \
    do \
    { \
        Self->ScriptSys.RaiseException(_str(error, ##__VA_ARGS__)); \
        return 0; \
    } while (0)

bool FOMapper::SpritesCanDraw;
FOMapper* FOMapper::Self;
MapView* FOMapper::SScriptFunc::ClientCurMap;
LocationView* FOMapper::SScriptFunc::ClientCurLocation;

FOMapper::IfaceAnim::IfaceAnim(AnyFrames* frm, AtlasType res_type) : Frames(frm), ResType(res_type)
{
    LastTick = Timer::FastTick();
}

FOMapper::FOMapper(MapperSettings& sett) :
    Settings {sett},
    GeomHelper(Settings),
    IfaceIni(""),
    FileMngr(),
    ServerFileMngr(),
    ScriptSys(Settings, FileMngr),
    Cache("Data/Cache.bin"),
    ProtoMngr(),
    EffectMngr(Settings, FileMngr),
    SprMngr(Settings, FileMngr, EffectMngr, ScriptSys),
    ResMngr(FileMngr, SprMngr, ScriptSys),
    HexMngr(true, Settings, ProtoMngr, SprMngr, EffectMngr, ResMngr, ScriptSys),
    Keyb(Settings, SprMngr)
{
    Self = this;
    DrawCrExtInfo = 0;
    Animations.resize(10000);
    ConsoleHistory.clear();
    ConsoleHistoryCur = 0;
    InspectorEntity = nullptr;

    // Mouse
    int mx = 0, my = 0;
    App::Window::GetMousePosition(mx, my);
    Settings.MouseX = CLAMP(mx, 0, Settings.ScreenWidth - 1);
    Settings.MouseY = CLAMP(my, 0, Settings.ScreenHeight - 1);

    // Options
    Settings.ScrollCheck = false;

    // Setup write paths
    ServerWritePath = Settings.ServerDir;
    ClientWritePath = Settings.WorkDir;

    // Resources
    FileMngr.AddDataSource("$Basic", false);
    FileMngr.AddDataSource(ClientWritePath + "Data/", false);
    ServerFileMngr.AddDataSource("$Basic", false);
    ServerFileMngr.AddDataSource(ServerWritePath, false);

    // Default effects
    EffectMngr.LoadDefaultEffects();

    // Fonts
    SprMngr.PushAtlasType(AtlasType::Static);
    bool load_fonts_ok = true;
    if (!SprMngr.LoadFontFO(FONT_FO, "OldDefault", false) || !SprMngr.LoadFontFO(FONT_NUM, "Numbers", true) ||
        !SprMngr.LoadFontFO(FONT_BIG_NUM, "BigNumbers", true) ||
        !SprMngr.LoadFontFO(FONT_SAND_NUM, "SandNumbers", false) ||
        !SprMngr.LoadFontFO(FONT_SPECIAL, "Special", false) || !SprMngr.LoadFontFO(FONT_DEFAULT, "Default", false) ||
        !SprMngr.LoadFontFO(FONT_THIN, "Thin", false) || !SprMngr.LoadFontFO(FONT_FAT, "Fat", false) ||
        !SprMngr.LoadFontFO(FONT_BIG, "Big", false))
        load_fonts_ok = false;
    RUNTIME_ASSERT(load_fonts_ok);
    SprMngr.BuildFonts();
    SprMngr.SetDefaultFont(FONT_DEFAULT, COLOR_TEXT);

    SprMngr.BeginScene(COLOR_RGB(100, 100, 100));
    SprMngr.EndScene();

    bool init_face_ok = (InitIface() == 0);
    RUNTIME_ASSERT(init_face_ok);

    // Script system
    bool init_scripts_ok = InitScriptSystem();
    RUNTIME_ASSERT(init_scripts_ok);

    // Language Packs
    CurLang.LoadFromFiles(FileMngr, Settings.Language);

    // Prototypes
    bool protos_ok = ProtoMngr.LoadProtosFromFiles(FileMngr);
    RUNTIME_ASSERT(protos_ok);

    // Initialize tabs
    const auto& cr_protos = ProtoMngr.GetProtoCritters();
    for (auto& kv : cr_protos)
    {
        ProtoCritter* proto = kv.second;
        Tabs[INT_MODE_CRIT][DEFAULT_SUB_TAB].NpcProtos.push_back(proto);
        Tabs[INT_MODE_CRIT][proto->CollectionName].NpcProtos.push_back(proto);
    }
    for (auto& kv : Tabs[INT_MODE_CRIT])
    {
        std::sort(kv.second.NpcProtos.begin(), kv.second.NpcProtos.end(),
            [](ProtoCritter* a, ProtoCritter* b) { return a->GetName().compare(b->GetName()); });
    }

    const auto& item_protos = ProtoMngr.GetProtoItems();
    for (auto& kv : item_protos)
    {
        ProtoItem* proto = kv.second;
        Tabs[INT_MODE_ITEM][DEFAULT_SUB_TAB].ItemProtos.push_back(proto);
        Tabs[INT_MODE_ITEM][proto->CollectionName].ItemProtos.push_back(proto);
    }
    for (auto& kv : Tabs[INT_MODE_ITEM])
    {
        std::sort(kv.second.ItemProtos.begin(), kv.second.ItemProtos.end(),
            [](ProtoItem* a, ProtoItem* b) { return a->GetName().compare(b->GetName()); });
    }

    for (int i = 0; i < TAB_COUNT; i++)
    {
        if (Tabs[i].empty())
            Tabs[i][DEFAULT_SUB_TAB].Scroll = 0;
        TabsActive[i] = &(*Tabs[i].begin()).second;
    }

    // Initialize tabs scroll and names
    memzero(TabsScroll, sizeof(TabsScroll));
    for (int i = INT_MODE_CUSTOM0; i <= INT_MODE_CUSTOM9; i++)
        TabsName[i] = "-";
    TabsName[INT_MODE_ITEM] = "Item";
    TabsName[INT_MODE_TILE] = "Tile";
    TabsName[INT_MODE_CRIT] = "Crit";
    TabsName[INT_MODE_FAST] = "Fast";
    TabsName[INT_MODE_IGNORE] = "Ign";
    TabsName[INT_MODE_INCONT] = "Inv";
    TabsName[INT_MODE_MESS] = "Msg";
    TabsName[INT_MODE_LIST] = "Maps";

    // Hex manager
    HexMngr.ReloadSprites();
    HexMngr.SwitchShowTrack();
    ChangeGameTime();

    if (!Settings.StartMap.empty())
    {
        string map_name = Settings.StartMap;
        ProtoMap* pmap = new ProtoMap(_str(map_name).toHash());
        bool initialized = 0;
        // pmap->EditorLoad(ServerFileMngr, ProtoMngr, SprMngr, ResMngr); // Todo: need attention!

        if (initialized && HexMngr.SetProtoMap(*pmap))
        {
            int hexX = Settings.StartHexX;
            int hexY = Settings.StartHexY;
            if (hexX < 0 || hexX >= pmap->GetWidth())
                hexX = pmap->GetWorkHexX();
            if (hexY < 0 || hexY >= pmap->GetHeight())
                hexY = pmap->GetWorkHexY();
            HexMngr.FindSetCenter(hexX, hexY);

            MapView* map = new MapView(0, pmap);
            ActiveMap = map;
            LoadedMaps.push_back(map);
            RunMapLoadScript(map);
        }
    }

    // Start script
    RunStartScript();

    // Refresh resources after start script executed
    for (int tab = 0; tab < TAB_COUNT; tab++)
        RefreshTiles(tab);
    RefreshCurProtos();

    // Load console history
    string history_str = Cache.GetCache("mapper_console.txt");
    size_t pos = 0, prev = 0, count = 0;
    while ((pos = history_str.find("\n", prev)) != std::string::npos)
    {
        string history_part;
        history_part.assign(&history_str.c_str()[prev], pos - prev);
        ConsoleHistory.push_back(history_part);
        prev = pos + 1;
    }
    ConsoleHistory = _str(history_str).normalizeLineEndings().split('\n');
    while (ConsoleHistory.size() > Settings.ConsoleHistorySize)
        ConsoleHistory.erase(ConsoleHistory.begin());
    ConsoleHistoryCur = (int)ConsoleHistory.size();
}

int FOMapper::InitIface()
{
    WriteLog("Init interface.\n");

    ConfigFile& ini = IfaceIni;

    ini = FileMngr.ReadConfigFile("mapper_default.ini");
    if (!ini)
    {
        WriteLog("File 'mapper_default.ini' not found.\n");
        return __LINE__;
    }

    // Interface
    IntX = ini.GetInt("", "IntX", -1);
    IntY = ini.GetInt("", "IntY", -1);

    IfaceLoadRect(IntWMain, "IntMain");
    if (IntX == -1)
        IntX = (Settings.ScreenWidth - IntWMain.W()) / 2;
    if (IntY == -1)
        IntY = Settings.ScreenHeight - IntWMain.H();

    IfaceLoadRect(IntWWork, "IntWork");
    IfaceLoadRect(IntWHint, "IntHint");

    IfaceLoadRect(IntBCust[0], "IntCustom0");
    IfaceLoadRect(IntBCust[1], "IntCustom1");
    IfaceLoadRect(IntBCust[2], "IntCustom2");
    IfaceLoadRect(IntBCust[3], "IntCustom3");
    IfaceLoadRect(IntBCust[4], "IntCustom4");
    IfaceLoadRect(IntBCust[5], "IntCustom5");
    IfaceLoadRect(IntBCust[6], "IntCustom6");
    IfaceLoadRect(IntBCust[7], "IntCustom7");
    IfaceLoadRect(IntBCust[8], "IntCustom8");
    IfaceLoadRect(IntBCust[9], "IntCustom9");
    IfaceLoadRect(IntBItem, "IntItem");
    IfaceLoadRect(IntBTile, "IntTile");
    IfaceLoadRect(IntBCrit, "IntCrit");
    IfaceLoadRect(IntBFast, "IntFast");
    IfaceLoadRect(IntBIgnore, "IntIgnore");
    IfaceLoadRect(IntBInCont, "IntInCont");
    IfaceLoadRect(IntBMess, "IntMess");
    IfaceLoadRect(IntBList, "IntList");
    IfaceLoadRect(IntBScrBack, "IntScrBack");
    IfaceLoadRect(IntBScrBackFst, "IntScrBackFst");
    IfaceLoadRect(IntBScrFront, "IntScrFront");
    IfaceLoadRect(IntBScrFrontFst, "IntScrFrontFst");

    IfaceLoadRect(IntBShowItem, "IntShowItem");
    IfaceLoadRect(IntBShowScen, "IntShowScen");
    IfaceLoadRect(IntBShowWall, "IntShowWall");
    IfaceLoadRect(IntBShowCrit, "IntShowCrit");
    IfaceLoadRect(IntBShowTile, "IntShowTile");
    IfaceLoadRect(IntBShowRoof, "IntShowRoof");
    IfaceLoadRect(IntBShowFast, "IntShowFast");

    IfaceLoadRect(IntBSelectItem, "IntSelectItem");
    IfaceLoadRect(IntBSelectScen, "IntSelectScen");
    IfaceLoadRect(IntBSelectWall, "IntSelectWall");
    IfaceLoadRect(IntBSelectCrit, "IntSelectCrit");
    IfaceLoadRect(IntBSelectTile, "IntSelectTile");
    IfaceLoadRect(IntBSelectRoof, "IntSelectRoof");

    IfaceLoadRect(SubTabsRect, "SubTabs");

    IntVisible = true;
    IntFix = true;
    IntMode = INT_MODE_MESS;
    SelectType = SELECT_TYPE_NEW;
    ProtoWidth = ini.GetInt("", "ProtoWidth", 50);
    ProtosOnScreen = (IntWWork[2] - IntWWork[0]) / ProtoWidth;
    memzero(TabIndex, sizeof(TabIndex));
    NpcDir = 3;
    CurMode = CUR_MODE_DEFAULT;
    IsSelectItem = true;
    IsSelectScen = true;
    IsSelectWall = true;
    IsSelectCrit = true;

    // Object
    IfaceLoadRect(ObjWMain, "ObjMain");
    IfaceLoadRect(ObjWWork, "ObjWork");
    IfaceLoadRect(ObjBToAll, "ObjToAll");

    ObjVisible = true;

    // Console
    ConsolePicX = ini.GetInt("", "ConsolePicX", 0);
    ConsolePicY = ini.GetInt("", "ConsolePicY", 0);
    ConsoleTextX = ini.GetInt("", "ConsoleTextX", 0);
    ConsoleTextY = ini.GetInt("", "ConsoleTextY", 0);

    ResMngr.ItemHexDefaultAnim = SprMngr.LoadAnimation(ini.GetStr("", "ItemStub", "art/items/reserved.frm"), true);
    ResMngr.CritterDefaultAnim =
        SprMngr.LoadAnimation(ini.GetStr("", "CritterStub", "art/critters/reservaa.frm"), true);

    // Cursor
    CurPDef = SprMngr.LoadAnimation(ini.GetStr("", "CurDefault", "actarrow.frm"), true);
    CurPHand = SprMngr.LoadAnimation(ini.GetStr("", "CurHand", "hand.frm"), true);

    // Iface
    IntMainPic = SprMngr.LoadAnimation(ini.GetStr("", "IntMainPic", "error"), true);
    IntPTab = SprMngr.LoadAnimation(ini.GetStr("", "IntTabPic", "error"), true);
    IntPSelect = SprMngr.LoadAnimation(ini.GetStr("", "IntSelectPic", "error"), true);
    IntPShow = SprMngr.LoadAnimation(ini.GetStr("", "IntShowPic", "error"), true);

    // Object
    ObjWMainPic = SprMngr.LoadAnimation(ini.GetStr("", "ObjMainPic", "error"), true);
    ObjPBToAllDn = SprMngr.LoadAnimation(ini.GetStr("", "ObjToAllPicDn", "error"), true);

    // Sub tabs
    SubTabsPic = SprMngr.LoadAnimation(ini.GetStr("", "SubTabsPic", "error"), true);

    // Console
    ConsolePic = SprMngr.LoadAnimation(ini.GetStr("", "ConsolePic", "error"), true);

    WriteLog("Init interface complete.\n");
    return 0;
}

bool FOMapper::IfaceLoadRect(Rect& comp, const char* name)
{
    string res = IfaceIni.GetStr("", name);
    if (res.empty())
    {
        WriteLog("Signature '{}' not found.\n", name);
        return false;
    }

    if (sscanf(res.c_str(), "%d%d%d%d", &comp[0], &comp[1], &comp[2], &comp[3]) != 4)
    {
        comp.Clear();
        WriteLog("Unable to parse signature '{}'.\n", name);
        return false;
    }

    return true;
}

void FOMapper::ChangeGameTime()
{
    uint color =
        GenericUtils::GetColorDay(HexMngr.GetMapDayTime(), HexMngr.GetMapDayColor(), HexMngr.GetMapTime(), nullptr);
    SprMngr.SetSpritesColor(COLOR_GAME_RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF));
    if (HexMngr.IsMapLoaded())
        HexMngr.RefreshMap();
}

uint FOMapper::AnimLoad(uint name_hash, AtlasType res_type)
{
    AnyFrames* anim = ResMngr.GetAnim(name_hash, res_type);
    if (!anim)
        return 0;
    IfaceAnim* ianim = new IfaceAnim(anim, res_type);
    if (!ianim)
        return 0;

    uint index = 1;
    for (uint j = (uint)Animations.size(); index < j; index++)
        if (!Animations[index])
            break;
    if (index < (uint)Animations.size())
        Animations[index] = ianim;
    else
        Animations.push_back(ianim);
    return index;
}

uint FOMapper::AnimLoad(const char* fname, AtlasType res_type)
{
    AnyFrames* anim = ResMngr.GetAnim(_str(fname).toHash(), res_type);
    if (!anim)
        return 0;
    IfaceAnim* ianim = new IfaceAnim(anim, res_type);
    if (!ianim)
        return 0;

    uint index = 1;
    for (uint j = (uint)Animations.size(); index < j; index++)
        if (!Animations[index])
            break;
    if (index < (uint)Animations.size())
        Animations[index] = ianim;
    else
        Animations.push_back(ianim);
    return index;
}

uint FOMapper::AnimGetCurSpr(uint anim_id)
{
    if (anim_id >= Animations.size() || !Animations[anim_id])
        return 0;
    return Animations[anim_id]->Frames->Ind[Animations[anim_id]->CurSpr];
}

uint FOMapper::AnimGetCurSprCnt(uint anim_id)
{
    if (anim_id >= Animations.size() || !Animations[anim_id])
        return 0;
    return Animations[anim_id]->CurSpr;
}

uint FOMapper::AnimGetSprCount(uint anim_id)
{
    if (anim_id >= Animations.size() || !Animations[anim_id])
        return 0;
    return Animations[anim_id]->Frames->CntFrm;
}

AnyFrames* FOMapper::AnimGetFrames(uint anim_id)
{
    if (anim_id >= Animations.size() || !Animations[anim_id])
        return 0;
    return Animations[anim_id]->Frames;
}

void FOMapper::AnimRun(uint anim_id, uint flags)
{
    if (anim_id >= Animations.size() || !Animations[anim_id])
        return;
    IfaceAnim* anim = Animations[anim_id];

    // Set flags
    anim->Flags = flags & 0xFFFF;
    flags >>= 16;

    // Set frm
    uchar cur_frm = flags & 0xFF;
    if (cur_frm > 0)
    {
        cur_frm--;
        if (cur_frm >= anim->Frames->CntFrm)
            cur_frm = anim->Frames->CntFrm - 1;
        anim->CurSpr = cur_frm;
    }
    // flags>>=8;
}

void FOMapper::AnimProcess()
{
    for (auto it = Animations.begin(), end = Animations.end(); it != end; ++it)
    {
        IfaceAnim* anim = *it;
        if (!anim || !anim->Flags)
            continue;

        if (FLAG(anim->Flags, ANIMRUN_STOP))
        {
            anim->Flags = 0;
            continue;
        }

        if (FLAG(anim->Flags, ANIMRUN_TO_END) || FLAG(anim->Flags, ANIMRUN_FROM_END))
        {
            uint cur_tick = Timer::FastTick();
            if (cur_tick - anim->LastTick < anim->Frames->Ticks / anim->Frames->CntFrm)
                continue;

            anim->LastTick = cur_tick;
            uint end_spr = anim->Frames->CntFrm - 1;
            if (FLAG(anim->Flags, ANIMRUN_FROM_END))
                end_spr = 0;

            if (anim->CurSpr < end_spr)
                anim->CurSpr++;
            else if (anim->CurSpr > end_spr)
                anim->CurSpr--;
            else
            {
                if (FLAG(anim->Flags, ANIMRUN_CYCLE))
                {
                    if (FLAG(anim->Flags, ANIMRUN_TO_END))
                        anim->CurSpr = 0;
                    else
                        anim->CurSpr = end_spr;
                }
                else
                {
                    anim->Flags = 0;
                }
            }
        }
    }
}

void FOMapper::AnimFree(AtlasType res_type)
{
    ResMngr.FreeResources(res_type);
    for (auto it = Animations.begin(), end = Animations.end(); it != end; ++it)
    {
        IfaceAnim* anim = *it;
        if (anim && anim->ResType == res_type)
        {
            delete anim;
            (*it) = nullptr;
        }
    }
}

void FOMapper::ProcessInputEvents()
{
    // Stop processing if window not active
    if (!SprMngr.IsWindowFocused())
    {
        InputEvent event;
        while (App::Input::PollEvent(event))
            continue;

        Keyb.Lost();
        ScriptSys.RaiseInternalEvent(ClientFunctions.InputLost);
        return;
    }

    InputEvent event;
    while (App::Input::PollEvent(event))
        ProcessInputEvent(event);
}

void FOMapper::ProcessInputEvent(const InputEvent& event)
{
    // Process events
    /*for (uint i = 0; i < events.size(); i += 2)
    {
        // Event data
        int event = events[i];
        int event_key = events[i + 1];
        const char* event_text = events_text[i / 2].c_str();

        // Keys codes mapping
        uchar dikdw = 0;
        uchar dikup = 0;
        if (event == SDL_KEYDOWN)
            dikdw = Keyb.MapKey(event_key);
        else if (event == SDL_KEYUP)
            dikup = Keyb.MapKey(event_key);
        if (!dikdw && !dikup)
            continue;

        // Avoid repeating
        static bool key_pressed[0x100];
        if (dikdw && key_pressed[dikdw])
            continue;
        if (dikup && !key_pressed[dikup])
            continue;

        // Keyboard states, to know outside function
        key_pressed[dikup] = false;
        key_pressed[dikdw] = true;

        // Key script event
        bool script_result = true;
        if (dikdw)
        {
            string event_text_script = event_text;
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.KeyDown, dikdw, &event_text_script);
        }
        if (dikup)
        {
            string event_text_script = event_text;
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.KeyUp, dikup, &event_text_script);
        }

        // Disable keyboard events
        if (!script_result || Settings.DisableKeyboardEvents)
        {
            if (dikdw == KeyCode::DIK_ESCAPE && Keyb.ShiftDwn)
                Settings.Quit = true;
            continue;
        }

        // Control keys
        if (dikdw == KeyCode::DIK_RCONTROL || dikdw == KeyCode::DIK_LCONTROL)
            Keyb.CtrlDwn = true;
        else if (dikdw == KeyCode::DIK_LMENU || dikdw == KeyCode::DIK_RMENU)
            Keyb.AltDwn = true;
        else if (dikdw == KeyCode::DIK_LSHIFT || dikdw == KeyCode::DIK_RSHIFT)
            Keyb.ShiftDwn = true;
        if (dikup == KeyCode::DIK_RCONTROL || dikup == KeyCode::DIK_LCONTROL)
            Keyb.CtrlDwn = false;
        else if (dikup == KeyCode::DIK_LMENU || dikup == KeyCode::DIK_RMENU)
            Keyb.AltDwn = false;
        else if (dikup == KeyCode::DIK_LSHIFT || dikup == KeyCode::DIK_RSHIFT)
            Keyb.ShiftDwn = false;

        // Hotkeys
        if (!Keyb.AltDwn && !Keyb.CtrlDwn && !Keyb.ShiftDwn)
        {
            switch (dikdw)
            {
            case KeyCode::DIK_F1:
                Settings.ShowItem = !Settings.ShowItem;
                HexMngr.RefreshMap();
                break;
            case KeyCode::DIK_F2:
                Settings.ShowScen = !Settings.ShowScen;
                HexMngr.RefreshMap();
                break;
            case KeyCode::DIK_F3:
                Settings.ShowWall = !Settings.ShowWall;
                HexMngr.RefreshMap();
                break;
            case KeyCode::DIK_F4:
                Settings.ShowCrit = !Settings.ShowCrit;
                HexMngr.RefreshMap();
                break;
            case KeyCode::DIK_F5:
                Settings.ShowTile = !Settings.ShowTile;
                HexMngr.RefreshMap();
                break;
            case KeyCode::DIK_F6:
                Settings.ShowFast = !Settings.ShowFast;
                HexMngr.RefreshMap();
                break;
            case KeyCode::DIK_F7:
                IntVisible = !IntVisible;
                break;
            case KeyCode::DIK_F8:
                Settings.MouseScroll = !Settings.MouseScroll;
                break;
            case KeyCode::DIK_F9:
                ObjVisible = !ObjVisible;
                break;
            case KeyCode::DIK_F10:
                HexMngr.SwitchShowHex();
                break;

            // Fullscreen
            case KeyCode::DIK_F11:
                if (!Settings.FullScreen)
                {
                    if (SprMngr.EnableFullscreen())
                        Settings.FullScreen = true;
                }
                else
                {
                    if (SprMngr.DisableFullscreen())
                        Settings.FullScreen = false;
                }
                SprMngr.RefreshViewport();
                continue;
            // Minimize
            case KeyCode::DIK_F12:
                SprMngr.MinimizeWindow();
                continue;

            case KeyCode::DIK_DELETE:
                SelectDelete();
                break;
            case KeyCode::DIK_ADD:
                if (!ConsoleEdit && SelectedEntities.empty())
                {
                    int day_time = HexMngr.GetDayTime();
                    day_time += 60;
                    Globals->SetMinute(day_time % 60);
                    Globals->SetHour(day_time / 60 % 24);
                    ChangeGameTime();
                }
                break;
            case KeyCode::DIK_SUBTRACT:
                if (!ConsoleEdit && SelectedEntities.empty())
                {
                    int day_time = HexMngr.GetDayTime();
                    day_time -= 60;
                    Globals->SetMinute(day_time % 60);
                    Globals->SetHour(day_time / 60 % 24);
                    ChangeGameTime();
                }
                break;
            case KeyCode::DIK_TAB:
                SelectType = (SelectType == SELECT_TYPE_OLD ? SELECT_TYPE_NEW : SELECT_TYPE_OLD);
                break;
            default:
                break;
            }
        }

        if (Keyb.ShiftDwn)
        {
            switch (dikdw)
            {
            case KeyCode::DIK_F7:
                IntFix = !IntFix;
                break;
            case KeyCode::DIK_F9:
                ObjFix = !ObjFix;
                break;
            case KeyCode::DIK_F10:
                HexMngr.SwitchShowRain();
                break;
            case KeyCode::DIK_F11:
                SprMngr.DumpAtlases();
                break;
            case KeyCode::DIK_ESCAPE:
                exit(0);
                break;
            case KeyCode::DIK_ADD:
                if (!ConsoleEdit && SelectedEntities.empty())
                {
                    int day_time = HexMngr.GetDayTime();
                    day_time += 1;
                    Globals->SetMinute(day_time % 60);
                    Globals->SetHour(day_time / 60 % 24);
                    ChangeGameTime();
                }
                break;
            case KeyCode::DIK_SUBTRACT:
                if (!ConsoleEdit && SelectedEntities.empty())
                {
                    int day_time = HexMngr.GetDayTime();
                    day_time -= 60;
                    Globals->SetMinute(day_time % 60);
                    Globals->SetHour(day_time / 60 % 24);
                    ChangeGameTime();
                }
                break;
            case KeyCode::DIK_0:
            case KeyCode::DIK_NUMPAD0:
                TileLayer = 0;
                break;
            case KeyCode::DIK_1:
            case KeyCode::DIK_NUMPAD1:
                TileLayer = 1;
                break;
            case KeyCode::DIK_2:
            case KeyCode::DIK_NUMPAD2:
                TileLayer = 2;
                break;
            case KeyCode::DIK_3:
            case KeyCode::DIK_NUMPAD3:
                TileLayer = 3;
                break;
            case KeyCode::DIK_4:
            case KeyCode::DIK_NUMPAD4:
                TileLayer = 4;
                break;
            default:
                break;
            }
        }

        if (Keyb.CtrlDwn)
        {
            switch (dikdw)
            {
            case KeyCode::DIK_X:
                BufferCut();
                break;
            case KeyCode::DIK_C:
                BufferCopy();
                break;
            case KeyCode::DIK_V:
                BufferPaste(50, 50);
                break;
            case KeyCode::DIK_A:
                SelectAll();
                break;
            case KeyCode::DIK_S:
                if (ActiveMap)
                {
                    HexMngr.GetProtoMap(*(ProtoMap*)ActiveMap->Proto);
                    // Todo: need attention!
                    // ((ProtoMap*)ActiveMap->Proto)->EditorSave(FileMngr, "");
                    AddMess("Map saved.");
                    RunMapSaveScript(ActiveMap);
                }
                break;
            case KeyCode::DIK_D:
                Settings.ScrollCheck = !Settings.ScrollCheck;
                break;
            case KeyCode::DIK_B:
                HexMngr.MarkPassedHexes();
                break;
            case KeyCode::DIK_Q:
                Settings.ShowCorners = !Settings.ShowCorners;
                break;
            case KeyCode::DIK_W:
                Settings.ShowSpriteCuts = !Settings.ShowSpriteCuts;
                break;
            case KeyCode::DIK_E:
                Settings.ShowDrawOrder = !Settings.ShowDrawOrder;
                break;
            case KeyCode::DIK_M:
                DrawCrExtInfo++;
                if (DrawCrExtInfo > DRAW_CR_INFO_MAX)
                    DrawCrExtInfo = 0;
                break;
            case KeyCode::DIK_L:
                SaveLogFile();
                break;
            default:
                break;
            }
        }

        // Key down
        if (dikdw)
        {
            if (ObjVisible && !SelectedEntities.empty())
            {
                ObjKeyDown(dikdw, event_text);
            }
            else
            {
                ConsoleKeyDown(dikdw, event_text);
                if (!ConsoleEdit)
                {
                    switch (dikdw)
                    {
                    case KeyCode::DIK_LEFT:
                        Settings.ScrollKeybLeft = true;
                        break;
                    case KeyCode::DIK_RIGHT:
                        Settings.ScrollKeybRight = true;
                        break;
                    case KeyCode::DIK_UP:
                        Settings.ScrollKeybUp = true;
                        break;
                    case KeyCode::DIK_DOWN:
                        Settings.ScrollKeybDown = true;
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        // Key up
        if (dikup)
        {
            ConsoleKeyUp(dikup);

            switch (dikup)
            {
            case KeyCode::DIK_LEFT:
                Settings.ScrollKeybLeft = false;
                break;
            case KeyCode::DIK_RIGHT:
                Settings.ScrollKeybRight = false;
                break;
            case KeyCode::DIK_UP:
                Settings.ScrollKeybUp = false;
                break;
            case KeyCode::DIK_DOWN:
                Settings.ScrollKeybDown = false;
                break;
            default:
                break;
            }
        }
    }
}

void FOMapper::ParseMouse()
{
    // Mouse position
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    Settings.MouseX = CLAMP(mx, 0, Settings.ScreenWidth - 1);
    Settings.MouseY = CLAMP(my, 0, Settings.ScreenHeight - 1);

    // Stop processing if window not active
    if (!SprMngr.IsWindowFocused())
    {
        Settings.MainWindowMouseEvents.clear();
        IntHold = INT_NONE;
        ScriptSys.RaiseInternalEvent(MapperFunctions.InputLost);
        return;
    }

    // Mouse move
    if (Settings.LastMouseX != Settings.MouseX || Settings.LastMouseY != Settings.MouseY)
    {
        int ox = Settings.MouseX - Settings.LastMouseX;
        int oy = Settings.MouseY - Settings.LastMouseY;
        Settings.LastMouseX = Settings.MouseX;
        Settings.LastMouseY = Settings.MouseY;

        ScriptSys.RaiseInternalEvent(MapperFunctions.MouseMove, ox, oy);

        IntMouseMove();
    }

    // Mouse Scroll
    if (Settings.MouseScroll)
    {
        if (Settings.MouseX >= Settings.ScreenWidth - 1)
            Settings.ScrollMouseRight = true;
        else
            Settings.ScrollMouseRight = false;

        if (Settings.MouseX <= 0)
            Settings.ScrollMouseLeft = true;
        else
            Settings.ScrollMouseLeft = false;

        if (Settings.MouseY >= Settings.ScreenHeight - 1)
            Settings.ScrollMouseDown = true;
        else
            Settings.ScrollMouseDown = false;

        if (Settings.MouseY <= 0)
            Settings.ScrollMouseUp = true;
        else
            Settings.ScrollMouseUp = false;
    }

    // Get buffered data
    if (Settings.MainWindowMouseEvents.empty())
        return;
    IntVec events = Settings.MainWindowMouseEvents;
    Settings.MainWindowMouseEvents.clear();

    // Process events
    for (uint i = 0; i < events.size(); i += 3)
    {
        int event = events[i];
        int event_button = events[i + 1];
        int event_dy = -events[i + 2];

        // Scripts
        bool script_result = true;
        if (event == SDL_MOUSEWHEEL)
            script_result = ScriptSys.RaiseInternalEvent(
                MapperFunctions.MouseDown, event_dy > 0 ? MOUSE_BUTTON_WHEEL_UP : MOUSE_BUTTON_WHEEL_DOWN);
        if (event == SDL_MOUSEBUTTONDOWN && event_button == SDL_BUTTON_LEFT)
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseDown, MOUSE_BUTTON_LEFT);
        if (event == SDL_MOUSEBUTTONUP && event_button == SDL_BUTTON_LEFT)
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseUp, MOUSE_BUTTON_LEFT);
        if (event == SDL_MOUSEBUTTONDOWN && event_button == SDL_BUTTON_RIGHT)
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseDown, MOUSE_BUTTON_RIGHT);
        if (event == SDL_MOUSEBUTTONUP && event_button == SDL_BUTTON_RIGHT)
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseUp, MOUSE_BUTTON_RIGHT);
        if (event == SDL_MOUSEBUTTONDOWN && event_button == SDL_BUTTON_MIDDLE)
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseDown, MOUSE_BUTTON_MIDDLE);
        if (event == SDL_MOUSEBUTTONUP && event_button == SDL_BUTTON_MIDDLE)
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseUp, MOUSE_BUTTON_MIDDLE);
        if (event == SDL_MOUSEBUTTONDOWN && event_button == SDL_BUTTON(4))
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseDown, MOUSE_BUTTON_EXT0);
        if (event == SDL_MOUSEBUTTONUP && event_button == SDL_BUTTON(4))
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseUp, MOUSE_BUTTON_EXT0);
        if (event == SDL_MOUSEBUTTONDOWN && event_button == SDL_BUTTON(5))
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseDown, MOUSE_BUTTON_EXT1);
        if (event == SDL_MOUSEBUTTONUP && event_button == SDL_BUTTON(5))
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseUp, MOUSE_BUTTON_EXT1);
        if (event == SDL_MOUSEBUTTONDOWN && event_button == SDL_BUTTON(6))
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseDown, MOUSE_BUTTON_EXT2);
        if (event == SDL_MOUSEBUTTONUP && event_button == SDL_BUTTON(6))
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseUp, MOUSE_BUTTON_EXT2);
        if (event == SDL_MOUSEBUTTONDOWN && event_button == SDL_BUTTON(7))
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseDown, MOUSE_BUTTON_EXT3);
        if (event == SDL_MOUSEBUTTONUP && event_button == SDL_BUTTON(7))
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseUp, MOUSE_BUTTON_EXT3);
        if (event == SDL_MOUSEBUTTONDOWN && event_button == SDL_BUTTON(8))
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseDown, MOUSE_BUTTON_EXT4);
        if (event == SDL_MOUSEBUTTONUP && event_button == SDL_BUTTON(8))
            script_result = ScriptSys.RaiseInternalEvent(MapperFunctions.MouseUp, MOUSE_BUTTON_EXT4);
        if (!script_result || Settings.DisableMouseEvents)
            continue;

        // Wheel
        if (event == SDL_MOUSEWHEEL)
        {
            if (IntVisible && SubTabsActive && IsCurInRect(SubTabsRect, SubTabsX, SubTabsY))
            {
                int step = 4;
                if (Keyb.ShiftDwn)
                    step = 8;
                else if (Keyb.CtrlDwn)
                    step = 20;
                else if (Keyb.AltDwn)
                    step = 50;

                int data = event_dy;
                if (data > 0)
                    TabsScroll[SubTabsActiveTab] += step;
                else
                    TabsScroll[SubTabsActiveTab] -= step;
                if (TabsScroll[SubTabsActiveTab] < 0)
                    TabsScroll[SubTabsActiveTab] = 0;
            }
            else if (IntVisible && IsCurInRect(IntWWork, IntX, IntY) &&
                (IsObjectMode() || IsTileMode() || IsCritMode()))
            {
                int step = 1;
                if (Keyb.ShiftDwn)
                    step = ProtosOnScreen;
                else if (Keyb.CtrlDwn)
                    step = 100;
                else if (Keyb.AltDwn)
                    step = 1000;

                int data = event_dy;
                if (data > 0)
                {
                    if (IsObjectMode() || IsTileMode() || IsCritMode())
                    {
                        (*CurProtoScroll) -= step;
                        if (*CurProtoScroll < 0)
                            *CurProtoScroll = 0;
                    }
                    else if (IntMode == INT_MODE_INCONT)
                    {
                        InContScroll -= step;
                        if (InContScroll < 0)
                            InContScroll = 0;
                    }
                    else if (IntMode == INT_MODE_LIST)
                    {
                        ListScroll -= step;
                        if (ListScroll < 0)
                            ListScroll = 0;
                    }
                }
                else
                {
                    if (IsObjectMode() && (*CurItemProtos).size())
                    {
                        (*CurProtoScroll) += step;
                        if (*CurProtoScroll >= (int)(*CurItemProtos).size())
                            *CurProtoScroll = (int)(*CurItemProtos).size() - 1;
                    }
                    else if (IsTileMode() && CurTileHashes->size())
                    {
                        (*CurProtoScroll) += step;
                        if (*CurProtoScroll >= (int)CurTileHashes->size())
                            *CurProtoScroll = (int)CurTileHashes->size() - 1;
                    }
                    else if (IsCritMode() && CurNpcProtos->size())
                    {
                        (*CurProtoScroll) += step;
                        if (*CurProtoScroll >= (int)CurNpcProtos->size())
                            *CurProtoScroll = (int)CurNpcProtos->size() - 1;
                    }
                    else if (IntMode == INT_MODE_INCONT)
                        InContScroll += step;
                    else if (IntMode == INT_MODE_LIST)
                        ListScroll += step;
                }
            }
            else
            {
                if (event_dy)
                    HexMngr.ChangeZoom(event_dy > 0 ? -1 : 1);
            }
            continue;
        }

        // Middle down
        if (event == SDL_MOUSEBUTTONDOWN && event_button == SDL_BUTTON_MIDDLE)
        {
            CurMMouseDown();
            continue;
        }

        // Left Button Down
        if (event == SDL_MOUSEBUTTONDOWN && event_button == SDL_BUTTON_LEFT)
        {
            IntLMouseDown();
            continue;
        }

        // Left Button Up
        if (event == SDL_MOUSEBUTTONUP && event_button == SDL_BUTTON_LEFT)
        {
            IntLMouseUp();
            continue;
        }

        // Right Button Up
        if (event == SDL_MOUSEBUTTONUP && event_button == SDL_BUTTON_RIGHT)
        {
            CurRMouseUp();
            continue;
        }
    }*/
}

void FOMapper::MainLoop()
{
    Timer::UpdateTick();

    // Fixed FPS
    double start_loop = Timer::AccurateTick();

    // FPS counter
    static uint last_call = Timer::FastTick();
    static uint call_counter = 0;
    if ((Timer::FastTick() - last_call) >= 1000)
    {
        Settings.FPS = call_counter;
        call_counter = 0;
        last_call = Timer::FastTick();
    }
    else
    {
        call_counter++;
    }

    // Input events
    /*SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_MOUSEMOTION)
        {
            int sw = 0, sh = 0;
            SprMngr.GetWindowSize(sw, sh);
            int x = (int)(event.motion.x / (float)sw * (float)Settings.ScreenWidth);
            int y = (int)(event.motion.y / (float)sh * (float)Settings.ScreenHeight);
            Settings.MouseX = CLAMP(x, 0, Settings.ScreenWidth - 1);
            Settings.MouseY = CLAMP(y, 0, Settings.ScreenHeight - 1);
        }
        else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
        {
            Settings.MainWindowKeyboardEvents.push_back(event.type);
            Settings.MainWindowKeyboardEvents.push_back(event.key.keysym.scancode);
            Settings.MainWindowKeyboardEventsText.push_back("");
        }
        else if (event.type == SDL_TEXTINPUT)
        {
            Settings.MainWindowKeyboardEvents.push_back(SDL_KEYDOWN);
            Settings.MainWindowKeyboardEvents.push_back(510);
            Settings.MainWindowKeyboardEventsText.push_back(event.text.text);
            Settings.MainWindowKeyboardEvents.push_back(SDL_KEYUP);
            Settings.MainWindowKeyboardEvents.push_back(510);
            Settings.MainWindowKeyboardEventsText.push_back(event.text.text);
        }
        else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP)
        {
            Settings.MainWindowMouseEvents.push_back(event.type);
            Settings.MainWindowMouseEvents.push_back(event.button.button);
            Settings.MainWindowMouseEvents.push_back(0);
        }
        else if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERUP)
        {
            Settings.MainWindowMouseEvents.push_back(
                event.type == SDL_FINGERDOWN ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP);
            Settings.MainWindowMouseEvents.push_back(SDL_BUTTON_LEFT);
            Settings.MainWindowMouseEvents.push_back(0);
            Settings.MouseX = (int)(event.tfinger.x * (float)Settings.ScreenWidth);
            Settings.MouseY = (int)(event.tfinger.y * (float)Settings.ScreenHeight);
        }
        else if (event.type == SDL_MOUSEWHEEL)
        {
            Settings.MainWindowMouseEvents.push_back(event.type);
            Settings.MainWindowMouseEvents.push_back(SDL_BUTTON_MIDDLE);
            Settings.MainWindowMouseEvents.push_back(-event.wheel.y);
        }
        else if (event.type == SDL_QUIT)
        {
            Settings.Quit = true;
        }
    }*/

    // Script loop
    ScriptSys.RaiseInternalEvent(MapperFunctions.Loop);

    // Input
    ConsoleProcess();
    ProcessInputEvents();

    // Process
    AnimProcess();

    if (HexMngr.IsMapLoaded())
    {
        for (auto it = HexMngr.GetCritters().begin(), end = HexMngr.GetCritters().end(); it != end; ++it)
        {
            CritterView* cr = it->second;
            cr->Process();

            if (cr->IsNeedMove())
            {
                bool err_move = ((!cr->IsRunning && cr->GetIsNoWalk()) || (cr->IsRunning && cr->GetIsNoRun()));
                ushort old_hx = cr->GetHexX();
                ushort old_hy = cr->GetHexY();
                if (!err_move &&
                    HexMngr.TransitCritter(cr, cr->MoveSteps[0].first, cr->MoveSteps[0].second, true, false))
                    cr->MoveSteps.erase(cr->MoveSteps.begin());
                else
                    cr->MoveSteps.clear();
                HexMngr.RebuildLight();
            }
        }

        HexMngr.Scroll();
        HexMngr.ProcessItems();
        HexMngr.ProcessRain();
    }

    // Start render
    SprMngr.BeginScene(COLOR_RGB(100, 100, 100));

    // Process pending invocations
    ScriptSys.ProcessDeferredCalls();

    // Suspended contexts
    ScriptSys.RunSuspended();

    DrawIfaceLayer(0);
    if (HexMngr.IsMapLoaded())
    {
        HexMngr.DrawMap();

        // Texts on heads
        if (DrawCrExtInfo)
        {
            for (auto it = HexMngr.GetCritters().begin(), end = HexMngr.GetCritters().end(); it != end; ++it)
            {
                CritterView* cr = it->second;
                if (cr->SprDrawValid)
                {
                    if (DrawCrExtInfo == 1)
                        cr->SetText(_str("|0xffaabbcc ProtoId...{}\n|0xffff1122 DialogId...{}\n", cr->GetName(),
                                        cr->GetDialogId())
                                        .c_str(),
                            COLOR_TEXT_WHITE, 60000000);
                    else
                        cr->SetText("", COLOR_TEXT_WHITE, 60000000);
                    cr->DrawTextOnHead();
                }
            }
        }

        // Texts on map
        uint tick = Timer::FastTick();
        for (auto it = GameMapTexts.begin(); it != GameMapTexts.end();)
        {
            MapText& t = (*it);
            if (tick >= t.StartTick + t.Tick)
                it = GameMapTexts.erase(it);
            else
            {
                int procent = GenericUtils::Procent(t.Tick, tick - t.StartTick);
                Rect r = t.Pos.Interpolate(t.EndPos, procent);
                Field& f = HexMngr.GetField(t.HexX, t.HexY);
                int x = (int)((f.ScrX + Settings.MapHexWidth / 2 + Settings.ScrOx) / Settings.SpritesZoom - 100.0f -
                    (float)(t.Pos.L - r.L));
                int y = (int)((f.ScrY + Settings.MapHexLineHeight / 2 - t.Pos.H() - (t.Pos.T - r.T) + Settings.ScrOy) /
                        Settings.SpritesZoom -
                    70.0f);
                uint color = t.Color;
                if (t.Fade)
                    color = (color ^ 0xFF000000) | ((0xFF * (100 - procent) / 100) << 24);
                SprMngr.DrawStr(
                    Rect(x, y, x + 200, y + 70), t.Text.c_str(), FT_CENTERX | FT_BOTTOM | FT_BORDERED, color);
                it++;
            }
        }
    }

    // Iface
    DrawIfaceLayer(1);
    IntDraw();
    DrawIfaceLayer(2);
    ConsoleDraw();
    DrawIfaceLayer(3);
    ObjDraw();
    DrawIfaceLayer(4);
    CurDraw();
    DrawIfaceLayer(5);
    SprMngr.EndScene();

    // Fixed FPS
    if (!Settings.VSync && Settings.FixedFPS)
    {
        if (Settings.FixedFPS > 0)
        {
            static double balance = 0.0;
            double elapsed = Timer::AccurateTick() - start_loop;
            double need_elapsed = 1000.0 / (double)Settings.FixedFPS;
            if (need_elapsed > elapsed)
            {
                double sleep = need_elapsed - elapsed + balance;
                balance = fmod(sleep, 1.0);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(sleep)));
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(-Settings.FixedFPS));
        }
    }
}

void FOMapper::RefreshTiles(int tab)
{
    static const string formats[] = {
        "frm", "fofrm", "bmp", "dds", "dib", "hdr", "jpg", "jpeg", "pfm", "png", "tga", "spr", "til", "zar", "art"};

    // Clear old tile names
    for (auto it = Tabs[tab].begin(); it != Tabs[tab].end();)
    {
        SubTab& stab = it->second;
        if (stab.TileNames.size())
        {
            if (TabsActive[tab] == &stab)
                TabsActive[tab] = nullptr;
            Tabs[tab].erase(it++);
        }
        else
            ++it;
    }

    // Find names
    TileTab& ttab = TabsTiles[tab];
    if (ttab.TileDirs.empty())
        return;

    Tabs[tab].clear();
    Tabs[tab][DEFAULT_SUB_TAB].Index = 0; // Add default

    StrUIntMap PathIndex;

    for (uint t = 0, tt = (uint)ttab.TileDirs.size(); t < tt; t++)
    {
        string& path = ttab.TileDirs[t];
        bool include_subdirs = ttab.TileSubDirs[t];

        StrVec tiles;
        FileCollection tile_files = FileMngr.FilterFiles("", path, include_subdirs);
        while (tile_files.MoveNext())
            tiles.push_back(tile_files.GetCurFileHeader().GetPath());

        std::sort(tiles.begin(), tiles.end(), [](const string& left, const string& right) {
            for (auto lit = left.begin(), rit = right.begin(); lit != left.end() && rit != right.end(); ++lit, ++rit)
            {
                int lc = tolower(*lit);
                int rc = tolower(*rit);
                if (lc < rc)
                    return true;
                else if (lc > rc)
                    return false;
            }
            return left.size() < right.size();
        });

        for (auto it = tiles.begin(), end = tiles.end(); it != end; ++it)
        {
            const string& fname = *it;
            string ext = _str(fname).getFileExtension();
            if (ext.empty())
                continue;

            // Check format availability
            bool format_aviable = false;
            for (auto& format : formats)
            {
                if (format == ext)
                {
                    format_aviable = true;
                    break;
                }
            }
            if (!format_aviable)
                format_aviable = Is3dExtensionSupported(ext);

            if (format_aviable)
            {
                // Make primary collection name
                string dir = _str(fname).extractDir();
                if (dir.empty())
                    dir = "root";
                uint path_index = PathIndex[dir];
                if (!path_index)
                {
                    path_index = (uint)PathIndex.size();
                    PathIndex[dir] = path_index;
                }
                string collection_name = _str("{:03} - {}", path_index, dir);

                // Make secondary collection name
                string collection_name_ex;
                if (Settings.SplitTilesCollection)
                {
                    size_t pos = fname.find_last_of('/');
                    if (pos == string::npos)
                        pos = 0;
                    else
                        pos++;
                    for (uint i = (uint)pos, j = (uint)fname.size(); i < j; i++)
                    {
                        if (fname[i] >= '0' && fname[i] <= '9')
                        {
                            if (i - pos)
                            {
                                collection_name_ex += collection_name;
                                collection_name_ex += fname.substr(pos, i - pos);
                            }
                            break;
                        }
                    }
                    if (!collection_name_ex.length())
                    {
                        collection_name_ex += collection_name;
                        collection_name_ex += "<other>";
                    }
                }

                // Write tile
                hash hash = _str(fname).toHash();
                Tabs[tab][DEFAULT_SUB_TAB].TileHashes.push_back(hash);
                Tabs[tab][DEFAULT_SUB_TAB].TileNames.push_back(fname);
                Tabs[tab][collection_name].TileHashes.push_back(hash);
                Tabs[tab][collection_name].TileNames.push_back(fname);
                Tabs[tab][collection_name_ex].TileHashes.push_back(hash);
                Tabs[tab][collection_name_ex].TileNames.push_back(fname);
            }
        }
    }

    // Set default active tab
    TabsActive[tab] = &(*Tabs[tab].begin()).second;
}

uint FOMapper::GetProtoItemCurSprId(ProtoItem* proto_item)
{
    AnyFrames* anim = ResMngr.GetItemAnim(proto_item->GetPicMap());
    if (!anim)
        return 0;

    uint beg = 0, end = 0;
    if (proto_item->GetIsShowAnim())
    {
        beg = 0;
        end = anim->CntFrm - 1;
    }
    if (proto_item->GetIsShowAnimExt())
    {
        beg = proto_item->GetAnimStay0();
        end = proto_item->GetAnimStay1();
    }
    if (beg >= anim->CntFrm)
        beg = anim->CntFrm - 1;
    if (end >= anim->CntFrm)
        end = anim->CntFrm - 1;
    if (beg > end)
        std::swap(beg, end);

    uint count = end - beg + 1;
    uint ticks = anim->Ticks / anim->CntFrm * count;
    return anim->Ind[beg + ((Timer::GameTick() % ticks) * 100 / ticks) * count / 100];
}

void FOMapper::IntDraw()
{
    if (!IntVisible)
        return;

    SprMngr.DrawSprite(IntMainPic, IntX, IntY);

    switch (IntMode)
    {
    case INT_MODE_CUSTOM0:
        SprMngr.DrawSprite(IntPTab, IntBCust[0][0] + IntX, IntBCust[0][1] + IntY);
        break;
    case INT_MODE_CUSTOM1:
        SprMngr.DrawSprite(IntPTab, IntBCust[1][0] + IntX, IntBCust[1][1] + IntY);
        break;
    case INT_MODE_CUSTOM2:
        SprMngr.DrawSprite(IntPTab, IntBCust[2][0] + IntX, IntBCust[2][1] + IntY);
        break;
    case INT_MODE_CUSTOM3:
        SprMngr.DrawSprite(IntPTab, IntBCust[3][0] + IntX, IntBCust[3][1] + IntY);
        break;
    case INT_MODE_CUSTOM4:
        SprMngr.DrawSprite(IntPTab, IntBCust[4][0] + IntX, IntBCust[4][1] + IntY);
        break;
    case INT_MODE_CUSTOM5:
        SprMngr.DrawSprite(IntPTab, IntBCust[5][0] + IntX, IntBCust[5][1] + IntY);
        break;
    case INT_MODE_CUSTOM6:
        SprMngr.DrawSprite(IntPTab, IntBCust[6][0] + IntX, IntBCust[6][1] + IntY);
        break;
    case INT_MODE_CUSTOM7:
        SprMngr.DrawSprite(IntPTab, IntBCust[7][0] + IntX, IntBCust[7][1] + IntY);
        break;
    case INT_MODE_CUSTOM8:
        SprMngr.DrawSprite(IntPTab, IntBCust[8][0] + IntX, IntBCust[8][1] + IntY);
        break;
    case INT_MODE_CUSTOM9:
        SprMngr.DrawSprite(IntPTab, IntBCust[9][0] + IntX, IntBCust[9][1] + IntY);
        break;
    case INT_MODE_ITEM:
        SprMngr.DrawSprite(IntPTab, IntBItem[0] + IntX, IntBItem[1] + IntY);
        break;
    case INT_MODE_TILE:
        SprMngr.DrawSprite(IntPTab, IntBTile[0] + IntX, IntBTile[1] + IntY);
        break;
    case INT_MODE_CRIT:
        SprMngr.DrawSprite(IntPTab, IntBCrit[0] + IntX, IntBCrit[1] + IntY);
        break;
    case INT_MODE_FAST:
        SprMngr.DrawSprite(IntPTab, IntBFast[0] + IntX, IntBFast[1] + IntY);
        break;
    case INT_MODE_IGNORE:
        SprMngr.DrawSprite(IntPTab, IntBIgnore[0] + IntX, IntBIgnore[1] + IntY);
        break;
    case INT_MODE_INCONT:
        SprMngr.DrawSprite(IntPTab, IntBInCont[0] + IntX, IntBInCont[1] + IntY);
        break;
    case INT_MODE_MESS:
        SprMngr.DrawSprite(IntPTab, IntBMess[0] + IntX, IntBMess[1] + IntY);
        break;
    case INT_MODE_LIST:
        SprMngr.DrawSprite(IntPTab, IntBList[0] + IntX, IntBList[1] + IntY);
        break;
    default:
        break;
    }

    for (int i = INT_MODE_CUSTOM0; i <= INT_MODE_CUSTOM9; i++)
        SprMngr.DrawStr(Rect(IntBCust[i], IntX, IntY), TabsName[INT_MODE_CUSTOM0 + i].c_str(),
            FT_NOBREAK | FT_CENTERX | FT_CENTERY, COLOR_TEXT_WHITE);
    SprMngr.DrawStr(Rect(IntBItem, IntX, IntY), TabsName[INT_MODE_ITEM].c_str(), FT_NOBREAK | FT_CENTERX | FT_CENTERY,
        COLOR_TEXT_WHITE);
    SprMngr.DrawStr(Rect(IntBTile, IntX, IntY), TabsName[INT_MODE_TILE].c_str(), FT_NOBREAK | FT_CENTERX | FT_CENTERY,
        COLOR_TEXT_WHITE);
    SprMngr.DrawStr(Rect(IntBCrit, IntX, IntY), TabsName[INT_MODE_CRIT].c_str(), FT_NOBREAK | FT_CENTERX | FT_CENTERY,
        COLOR_TEXT_WHITE);
    SprMngr.DrawStr(Rect(IntBFast, IntX, IntY), TabsName[INT_MODE_FAST].c_str(), FT_NOBREAK | FT_CENTERX | FT_CENTERY,
        COLOR_TEXT_WHITE);
    SprMngr.DrawStr(Rect(IntBIgnore, IntX, IntY), TabsName[INT_MODE_IGNORE].c_str(),
        FT_NOBREAK | FT_CENTERX | FT_CENTERY, COLOR_TEXT_WHITE);
    SprMngr.DrawStr(Rect(IntBInCont, IntX, IntY), TabsName[INT_MODE_INCONT].c_str(),
        FT_NOBREAK | FT_CENTERX | FT_CENTERY, COLOR_TEXT_WHITE);
    SprMngr.DrawStr(Rect(IntBMess, IntX, IntY), TabsName[INT_MODE_MESS].c_str(), FT_NOBREAK | FT_CENTERX | FT_CENTERY,
        COLOR_TEXT_WHITE);
    SprMngr.DrawStr(Rect(IntBList, IntX, IntY), TabsName[INT_MODE_LIST].c_str(), FT_NOBREAK | FT_CENTERX | FT_CENTERY,
        COLOR_TEXT_WHITE);

    if (Settings.ShowItem)
        SprMngr.DrawSprite(IntPShow, IntBShowItem[0] + IntX, IntBShowItem[1] + IntY);
    if (Settings.ShowScen)
        SprMngr.DrawSprite(IntPShow, IntBShowScen[0] + IntX, IntBShowScen[1] + IntY);
    if (Settings.ShowWall)
        SprMngr.DrawSprite(IntPShow, IntBShowWall[0] + IntX, IntBShowWall[1] + IntY);
    if (Settings.ShowCrit)
        SprMngr.DrawSprite(IntPShow, IntBShowCrit[0] + IntX, IntBShowCrit[1] + IntY);
    if (Settings.ShowTile)
        SprMngr.DrawSprite(IntPShow, IntBShowTile[0] + IntX, IntBShowTile[1] + IntY);
    if (Settings.ShowRoof)
        SprMngr.DrawSprite(IntPShow, IntBShowRoof[0] + IntX, IntBShowRoof[1] + IntY);
    if (Settings.ShowFast)
        SprMngr.DrawSprite(IntPShow, IntBShowFast[0] + IntX, IntBShowFast[1] + IntY);

    if (IsSelectItem)
        SprMngr.DrawSprite(IntPSelect, IntBSelectItem[0] + IntX, IntBSelectItem[1] + IntY);
    if (IsSelectScen)
        SprMngr.DrawSprite(IntPSelect, IntBSelectScen[0] + IntX, IntBSelectScen[1] + IntY);
    if (IsSelectWall)
        SprMngr.DrawSprite(IntPSelect, IntBSelectWall[0] + IntX, IntBSelectWall[1] + IntY);
    if (IsSelectCrit)
        SprMngr.DrawSprite(IntPSelect, IntBSelectCrit[0] + IntX, IntBSelectCrit[1] + IntY);
    if (IsSelectTile)
        SprMngr.DrawSprite(IntPSelect, IntBSelectTile[0] + IntX, IntBSelectTile[1] + IntY);
    if (IsSelectRoof)
        SprMngr.DrawSprite(IntPSelect, IntBSelectRoof[0] + IntX, IntBSelectRoof[1] + IntY);

    int x = IntWWork[0] + IntX;
    int y = IntWWork[1] + IntY;
    int h = IntWWork[3] - IntWWork[1];
    int w = ProtoWidth;

    if (IsObjectMode())
    {
        int i = *CurProtoScroll;
        int j = i + ProtosOnScreen;
        if (j > (int)(*CurItemProtos).size())
            j = (int)(*CurItemProtos).size();

        for (; i < j; i++, x += w)
        {
            ProtoItem* proto_item = (*CurItemProtos)[i];
            uint col = (i == (int)GetTabIndex() ? COLOR_IFACE_RED : COLOR_IFACE);
            SprMngr.DrawSpriteSize(GetProtoItemCurSprId(proto_item), x, y, w, h / 2, false, true, col);

            if (proto_item->GetPicInv())
            {
                AnyFrames* anim = ResMngr.GetInvAnim(proto_item->GetPicInv());
                if (anim)
                    SprMngr.DrawSpriteSize(anim->GetCurSprId(), x, y + h / 2, w, h / 2, false, true, col);
            }

            SprMngr.DrawStr(Rect(x, y + h - 15, x + w, y + h), proto_item->GetName(), FT_NOBREAK, COLOR_TEXT_WHITE);
        }

        if (GetTabIndex() < (uint)(*CurItemProtos).size())
        {
            ProtoItem* proto_item = (*CurItemProtos)[GetTabIndex()];
            auto it = std::find(proto_item->TextsLang.begin(), proto_item->TextsLang.end(), CurLang.Name);
            if (it != proto_item->TextsLang.end())
            {
                uint index = (uint)std::distance(proto_item->TextsLang.begin(), it);
                string info = proto_item->Texts[0]->GetStr(ITEM_STR_ID(proto_item->ProtoId, 1));
                info += " - ";
                info += proto_item->Texts[0]->GetStr(ITEM_STR_ID(proto_item->ProtoId, 2));
                SprMngr.DrawStr(Rect(IntWHint, IntX, IntY), info.c_str(), 0);
            }
        }
    }
    else if (IsTileMode())
    {
        int i = *CurProtoScroll;
        int j = i + ProtosOnScreen;
        if (j > (int)CurTileHashes->size())
            j = (int)CurTileHashes->size();

        for (; i < j; i++, x += w)
        {
            AnyFrames* anim = ResMngr.GetItemAnim((*CurTileHashes)[i]);
            if (!anim)
                anim = ResMngr.ItemHexDefaultAnim;

            uint col = (i == (int)GetTabIndex() ? COLOR_IFACE_RED : COLOR_IFACE);
            SprMngr.DrawSpriteSize(anim->GetCurSprId(), x, y, w, h / 2, false, true, col);

            string& name = (*CurTileNames)[i];
            size_t pos = name.find_last_of('/');
            if (pos != string::npos)
                SprMngr.DrawStr(
                    Rect(x, y + h - 15, x + w, y + h), name.substr(pos + 1).c_str(), FT_NOBREAK, COLOR_TEXT_WHITE);
            else
                SprMngr.DrawStr(Rect(x, y + h - 15, x + w, y + h), name.c_str(), FT_NOBREAK, COLOR_TEXT_WHITE);
        }

        if (GetTabIndex() < CurTileNames->size())
            SprMngr.DrawStr(Rect(IntWHint, IntX, IntY), (*CurTileNames)[GetTabIndex()].c_str(), 0);
    }
    else if (IsCritMode())
    {
        uint i = *CurProtoScroll;
        uint j = i + ProtosOnScreen;
        if (j > CurNpcProtos->size())
            j = (uint)CurNpcProtos->size();

        for (; i < j; i++, x += w)
        {
            ProtoCritter* proto = (*CurNpcProtos)[i];

            hash model_name = proto->Props.GetPropValue<hash>(CritterView::PropertyModelName);
            uint spr_id =
                ResMngr.GetCritSprId(model_name, 1, 1, NpcDir, nullptr); // &proto->Params[ ST_ANIM3D_LAYER_BEGIN ] );
            if (!spr_id)
                continue;

            uint col = COLOR_IFACE;
            if (i == GetTabIndex())
                col = COLOR_IFACE_RED;

            SprMngr.DrawSpriteSize(spr_id, x, y, w, h / 2, false, true, col);
            SprMngr.DrawStr(Rect(x, y + h - 15, x + w, y + h), proto->GetName(), FT_NOBREAK, COLOR_TEXT_WHITE);
        }

        if (GetTabIndex() < CurNpcProtos->size())
        {
            ProtoCritter* proto = (*CurNpcProtos)[GetTabIndex()];
            SprMngr.DrawStr(Rect(IntWHint, IntX, IntY), proto->GetName(), 0);
        }
    }
    else if (IntMode == INT_MODE_INCONT && !SelectedEntities.empty())
    {
        Entity* entity = SelectedEntities[0];
        EntityVec children; // Todo: need attention!
        // = entity->GetChildren();
        uint i = InContScroll;
        uint j = i + ProtosOnScreen;
        if (j > children.size())
            j = (uint)children.size();

        for (; i < j; i++, x += w)
        {
            RUNTIME_ASSERT(children[i]->Type == EntityType::ItemView);
            ItemView* child = (ItemView*)children[i];

            AnyFrames* anim = ResMngr.GetInvAnim(child->GetPicInv());
            if (!anim)
                continue;

            uint col = COLOR_IFACE;
            if (child == InContItem)
                col = COLOR_IFACE_RED;

            SprMngr.DrawSpriteSize(anim->GetCurSprId(), x, y, w, h, false, true, col);

            SprMngr.DrawStr(
                Rect(x, y + h - 15, x + w, y + h), _str("x{}", child->GetCount()), FT_NOBREAK, COLOR_TEXT_WHITE);
            if (child->GetAccessory() == ITEM_ACCESSORY_CRITTER && child->GetCritSlot())
                SprMngr.DrawStr(
                    Rect(x, y, x + w, y + h), _str("Slot {}", child->GetCritSlot()), FT_NOBREAK, COLOR_TEXT_WHITE);
        }
    }
    else if (IntMode == INT_MODE_LIST)
    {
        int i = ListScroll;
        int j = (int)LoadedMaps.size();

        for (; i < j; i++, x += w)
        {
            MapView* map = LoadedMaps[i];
            SprMngr.DrawStr(Rect(x, y, x + w, y + h), _str(" '{}'", map->GetName()), 0,
                map == ActiveMap ? COLOR_IFACE_RED : COLOR_TEXT);
        }
    }

    // Message box
    if (IntMode == INT_MODE_MESS)
        MessBoxDraw();

    // Sub tabs
    if (SubTabsActive)
    {
        SprMngr.DrawSprite(SubTabsPic, SubTabsX, SubTabsY);

        int line_height = SprMngr.GetLineHeight() + 1;
        int posy = SubTabsRect.H() - line_height - 2;
        int i = 0;
        SubTabMap& stabs = Tabs[SubTabsActiveTab];
        for (auto it = stabs.begin(), end = stabs.end(); it != end; ++it)
        {
            i++;
            if (i - 1 < TabsScroll[SubTabsActiveTab])
                continue;

            string name = it->first;
            SubTab& stab = it->second;

            uint color = (TabsActive[SubTabsActiveTab] == &stab ? COLOR_TEXT_WHITE : COLOR_TEXT);
            Rect r = Rect(SubTabsRect.L + SubTabsX + 5, SubTabsRect.T + SubTabsY + posy,
                SubTabsRect.L + SubTabsX + 5 + Settings.ScreenWidth, SubTabsRect.T + SubTabsY + posy + line_height - 1);
            if (IsCurInRect(r))
                color = COLOR_TEXT_DWHITE;

            uint count = (uint)stab.TileNames.size();
            if (!count)
                count = (uint)stab.NpcProtos.size();
            if (!count)
                count = (uint)stab.ItemProtos.size();
            name += _str(" ({})", count);
            SprMngr.DrawStr(r, name.c_str(), 0, color);

            posy -= line_height;
            if (posy < 0)
                break;
        }
    }

    // Map info
    if (HexMngr.IsMapLoaded())
    {
        bool hex_thru = false;
        ushort hx, hy;
        if (HexMngr.GetHexPixel(Settings.MouseX, Settings.MouseY, hx, hy))
            hex_thru = true;
        int day_time = HexMngr.GetDayTime();
        SprMngr.DrawStr(Rect(Settings.ScreenWidth - 100, 0, Settings.ScreenWidth, Settings.ScreenHeight),
            _str("Map '{}'\n"
                 "Hex {} {}\n"
                 "Time {} : {}\n"
                 "Fps {}\n"
                 "Tile layer {}\n"
                 "{}",
                ActiveMap->GetName(), hex_thru ? hx : -1, hex_thru ? hy : -1, day_time / 60 % 24, day_time % 60,
                Settings.FPS, TileLayer, Settings.ScrollCheck ? "Scroll check" : "")
                .c_str(),
            FT_NOBREAK_LINE);
    }
}

void FOMapper::ObjDraw()
{
    if (!ObjVisible)
        return;

    Entity* entity = GetInspectorEntity();
    if (!entity)
        return;

    ItemView* item =
        (entity->Type == EntityType::ItemView || entity->Type == EntityType::ItemHexView ? (ItemView*)entity : nullptr);
    CritterView* cr = (entity->Type == EntityType::CritterView ? (CritterView*)entity : nullptr);
    Rect r = Rect(ObjWWork, ObjX, ObjY);
    int x = r.L;
    int y = r.T;
    int w = r.W();
    int h = r.H();

    SprMngr.DrawSprite(ObjWMainPic, ObjX, ObjY);
    if (ObjToAll)
        SprMngr.DrawSprite(ObjPBToAllDn, ObjBToAll[0] + ObjX, ObjBToAll[1] + ObjY);

    if (item)
    {
        AnyFrames* anim = ResMngr.GetItemAnim(item->GetPicMap());
        if (!anim)
            anim = ResMngr.ItemHexDefaultAnim;
        SprMngr.DrawSpriteSize(anim->GetCurSprId(), x + w - ProtoWidth, y, ProtoWidth, ProtoWidth, false, true);

        if (item->GetPicInv())
        {
            AnyFrames* anim = ResMngr.GetInvAnim(item->GetPicInv());
            if (anim)
                SprMngr.DrawSpriteSize(
                    anim->GetCurSprId(), x + w - ProtoWidth, y + ProtoWidth, ProtoWidth, ProtoWidth, false, true);
        }
    }

    DrawLine("Id", "", _str("{} ({})", entity->Id, (int)entity->Id), true, r);
    DrawLine("ProtoName", "", _str().parseHash(entity->GetProtoId()), true, r);
    if (cr)
        DrawLine("Type", "", "Critter", true, r);
    else if (item && !item->IsStatic())
        DrawLine("Type", "", "Item", true, r);
    else if (item && item->IsStatic())
        DrawLine("Type", "", "Static Item", true, r);
    else
        throw UnreachablePlaceException(LINE_STR);

    for (auto& prop : ShowProps)
    {
        if (prop)
        {
            string value = entity->Props.SavePropertyToText(prop);
            DrawLine(prop->GetName(), prop->GetTypeName(), value, prop->IsConst(), r);
        }
        else
        {
            r.T += DRAW_NEXT_HEIGHT;
            r.B += DRAW_NEXT_HEIGHT;
        }
    }
}

void FOMapper::DrawLine(const string& name, const string& type_name, const string& text, bool is_const, Rect& r)
{
    uint col = COLOR_TEXT;
    int x = r.L;
    int y = r.T;
    int w = r.W();
    int h = r.H();
    col = COLOR_TEXT;
    if (is_const)
        col = COLOR_TEXT_DWHITE;

    string result_text = text;
    if (ObjCurLine == (y - ObjWWork[1] - ObjY) / DRAW_NEXT_HEIGHT)
    {
        col = COLOR_TEXT_WHITE;
        if (!is_const && ObjCurLineValue != ObjCurLineInitValue)
        {
            col = COLOR_TEXT_RED;
            result_text = ObjCurLineValue;
        }
    }

    string str = _str("{}{}{}{}", name, !type_name.empty() ? " (" : "", !type_name.empty() ? type_name : "",
        !type_name.empty() ? ")" : "");
    str += "........................................................................................................";
    SprMngr.DrawStr(Rect(Rect(x, y, x + w / 2, y + h), 0, 0), str, FT_NOBREAK, col);
    SprMngr.DrawStr(Rect(Rect(x + w / 2, y, x + w, y + h), 0, 0), result_text, FT_NOBREAK, col);
    r.T += DRAW_NEXT_HEIGHT;
    r.B += DRAW_NEXT_HEIGHT;
}

void FOMapper::ObjKeyDown(KeyCode dik, const char* dik_text)
{
    if (dik == KeyCode::DIK_RETURN || dik == KeyCode::DIK_NUMPADENTER)
    {
        if (ObjCurLineInitValue != ObjCurLineValue)
        {
            Entity* entity = GetInspectorEntity();
            RUNTIME_ASSERT(entity);
            ObjKeyDownApply(entity);

            if (!SelectedEntities.empty() && SelectedEntities[0] == entity && ObjToAll)
            {
                for (size_t i = 1; i < SelectedEntities.size(); i++)
                    if (SelectedEntities[i]->Type == entity->Type)
                        ObjKeyDownApply(SelectedEntities[i]);
            }

            SelectEntityProp(ObjCurLine);
            HexMngr.RebuildLight();
        }
    }
    else if (dik == KeyCode::DIK_UP)
    {
        SelectEntityProp(ObjCurLine - 1);
    }
    else if (dik == KeyCode::DIK_DOWN)
    {
        SelectEntityProp(ObjCurLine + 1);
    }
    else if (dik == KeyCode::DIK_ESCAPE)
    {
        ObjCurLineValue = ObjCurLineInitValue;
    }
    else
    {
        if (!ObjCurLineIsConst)
            Keyb.GetChar(dik, dik_text, ObjCurLineValue, nullptr, MAX_FOTEXT, KIF_NO_SPEC_SYMBOLS);
    }
}

void FOMapper::ObjKeyDownApply(Entity* entity)
{
    const int start_line = 3;
    RUNTIME_ASSERT((entity->Type == EntityType::CritterView || entity->Type == EntityType::Item ||
        entity->Type == EntityType::ItemHexView));
    if (ObjCurLine >= start_line && ObjCurLine - start_line < (int)ShowProps.size())
    {
        Property* prop = ShowProps[ObjCurLine - start_line];
        if (prop)
        {
            if (entity->Props.LoadPropertyFromText(prop, ObjCurLineValue.c_str()))
            {
                if (entity->Type == EntityType::ItemHexView &&
                    (prop == ItemHexView::PropertyOffsetX || prop == ItemHexView::PropertyOffsetY))
                    ((ItemHexView*)entity)->SetAnimOffs();
                if (entity->Type == EntityType::ItemHexView && prop == ItemHexView::PropertyPicMap)
                    ((ItemHexView*)entity)->RefreshAnim();
            }
            else
            {
                entity->Props.LoadPropertyFromText(prop, ObjCurLineInitValue.c_str());
            }
        }
    }
}

void FOMapper::SelectEntityProp(int line)
{
    const int start_line = 3;
    ObjCurLine = line;
    if (ObjCurLine < 0)
        ObjCurLine = 0;
    ObjCurLineInitValue = ObjCurLineValue = "";
    ObjCurLineIsConst = true;

    Entity* entity = GetInspectorEntity();
    if (entity)
    {
        RUNTIME_ASSERT((entity->Type == EntityType::CritterView || entity->Type == EntityType::Item ||
            entity->Type == EntityType::ItemHexView));
        if (ObjCurLine - start_line >= (int)ShowProps.size())
            ObjCurLine = (int)ShowProps.size() + start_line - 1;
        if (ObjCurLine >= start_line && ObjCurLine - start_line < (int)ShowProps.size() &&
            ShowProps[ObjCurLine - start_line])
        {
            ObjCurLineInitValue = ObjCurLineValue =
                entity->Props.SavePropertyToText(ShowProps[ObjCurLine - start_line]);
            ObjCurLineIsConst = ShowProps[ObjCurLine - start_line]->IsConst();
        }
    }
}

Entity* FOMapper::GetInspectorEntity()
{
    Entity* entity =
        (IntMode == INT_MODE_INCONT && InContItem ? InContItem :
                                                    (!SelectedEntities.empty() ? SelectedEntities[0] : nullptr));
    if (entity == InspectorEntity)
        return entity;

    InspectorEntity = entity;
    ShowProps.clear();

    if (entity)
    {
        CScriptArray* arr = ScriptSys.CreateArray("int[]");
        RUNTIME_ASSERT(arr);

        ScriptSys.RaiseInternalEvent(MapperFunctions.InspectorProperties, entity, arr);

        IntVec enum_values;
        ScriptSys.AssignScriptArrayInVector(enum_values, arr);
        for (auto enum_value : enum_values)
            ShowProps.push_back(enum_value ? entity->Props.FindByEnum(enum_value) : nullptr);

        arr->Release();
    }

    SelectEntityProp(ObjCurLine);
    return entity;
}

void FOMapper::IntLMouseDown()
{
    IntHold = INT_NONE;

    // Sub tabs
    if (IntVisible && SubTabsActive)
    {
        if (IsCurInRect(SubTabsRect, SubTabsX, SubTabsY))
        {
            int line_height = SprMngr.GetLineHeight() + 1;
            int posy = SubTabsRect.H() - line_height - 2;
            int i = 0;
            SubTabMap& stabs = Tabs[SubTabsActiveTab];
            for (auto it = stabs.begin(), end = stabs.end(); it != end; ++it)
            {
                i++;
                if (i - 1 < TabsScroll[SubTabsActiveTab])
                    continue;

                const string& name = it->first;
                SubTab& stab = it->second;

                Rect r = Rect(SubTabsRect.L + SubTabsX + 5, SubTabsRect.T + SubTabsY + posy,
                    SubTabsRect.L + SubTabsX + 5 + SubTabsRect.W(), SubTabsRect.T + SubTabsY + posy + line_height - 1);
                if (IsCurInRect(r))
                {
                    TabsActive[SubTabsActiveTab] = &stab;
                    RefreshCurProtos();
                    break;
                }

                posy -= line_height;
                if (posy < 0)
                    break;
            }

            return;
        }

        if (!IsCurInRect(IntWMain, IntX, IntY))
        {
            SubTabsActive = false;
            return;
        }
    }

    // Map
    if ((!IntVisible || !IsCurInRect(IntWMain, IntX, IntY)) &&
        (!ObjVisible || SelectedEntities.empty() || !IsCurInRect(ObjWMain, ObjX, ObjY)))
    {
        InContItem = nullptr;

        if (!HexMngr.GetHexPixel(Settings.MouseX, Settings.MouseY, SelectHX1, SelectHY1))
            return;
        SelectHX2 = SelectHX1;
        SelectHY2 = SelectHY1;
        SelectX = Settings.MouseX;
        SelectY = Settings.MouseY;

        if (CurMode == CUR_MODE_DEFAULT)
        {
            if (Keyb.ShiftDwn)
            {
                for (auto& entity : SelectedEntities)
                {
                    if (entity->Type == EntityType::CritterView)
                    {
                        CritterView* cr = (CritterView*)entity;
                        bool is_run =
                            (cr->MoveSteps.size() && cr->MoveSteps[cr->MoveSteps.size() - 1].first == SelectHX1 &&
                                cr->MoveSteps[cr->MoveSteps.size() - 1].second == SelectHY1);

                        cr->MoveSteps.clear();
                        if (!is_run && cr->GetIsNoWalk())
                            break;

                        ushort hx = cr->GetHexX();
                        ushort hy = cr->GetHexY();
                        UCharVec steps;
                        if (HexMngr.FindPath(nullptr, hx, hy, SelectHX1, SelectHY1, steps, -1))
                        {
                            for (uint k = 0; k < steps.size(); k++)
                            {
                                GeomHelper.MoveHexByDir(hx, hy, steps[k], HexMngr.GetWidth(), HexMngr.GetHeight());
                                cr->MoveSteps.push_back(UShortPair(hx, hy));
                            }
                            cr->IsRunning = is_run;
                        }

                        break;
                    }
                }
            }
            else if (!Keyb.CtrlDwn)
                SelectClear();

            IntHold = INT_SELECT;
        }
        else if (CurMode == CUR_MODE_MOVE_SELECTION)
        {
            IntHold = INT_SELECT;
        }
        else if (CurMode == CUR_MODE_PLACE_OBJECT)
        {
            if (IsObjectMode() && (*CurItemProtos).size())
                AddItem((*CurItemProtos)[GetTabIndex()]->ProtoId, SelectHX1, SelectHY1, nullptr);
            else if (IsTileMode() && CurTileHashes->size())
                AddTile((*CurTileHashes)[GetTabIndex()], SelectHX1, SelectHY1, 0, 0, TileLayer, DrawRoof);
            else if (IsCritMode() && CurNpcProtos->size())
                AddCritter((*CurNpcProtos)[GetTabIndex()]->ProtoId, SelectHX1, SelectHY1);
        }

        return;
    }

    // Object editor
    if (ObjVisible && !SelectedEntities.empty() && IsCurInRect(ObjWMain, ObjX, ObjY))
    {
        if (IsCurInRect(ObjWWork, ObjX, ObjY))
        {
            SelectEntityProp((Settings.MouseY - ObjY - ObjWWork[1]) / DRAW_NEXT_HEIGHT);
        }

        if (IsCurInRect(ObjBToAll, ObjX, ObjY))
        {
            ObjToAll = !ObjToAll;
            IntHold = INT_BUTTON;
            return;
        }
        else if (!ObjFix)
        {
            IntHold = INT_OBJECT;
            ItemVectX = Settings.MouseX - ObjX;
            ItemVectY = Settings.MouseY - ObjY;
        }

        return;
    }

    // Interface
    if (!IntVisible || !IsCurInRect(IntWMain, IntX, IntY))
        return;

    if (IsCurInRect(IntWWork, IntX, IntY))
    {
        int ind = (Settings.MouseX - IntX - IntWWork[0]) / ProtoWidth;

        if (IsObjectMode() && (*CurItemProtos).size())
        {
            ind += *CurProtoScroll;
            if (ind >= (int)(*CurItemProtos).size())
                ind = (int)(*CurItemProtos).size() - 1;
            SetTabIndex(ind);

            // Switch ignore pid to draw
            if (Keyb.CtrlDwn)
            {
                hash pid = (*CurItemProtos)[ind]->ProtoId;

                SubTab& stab = Tabs[INT_MODE_IGNORE][DEFAULT_SUB_TAB];
                bool founded = false;
                for (auto it = stab.ItemProtos.begin(); it != stab.ItemProtos.end(); ++it)
                {
                    if ((*it)->ProtoId == pid)
                    {
                        founded = true;
                        stab.ItemProtos.erase(it);
                        break;
                    }
                }
                if (!founded)
                    stab.ItemProtos.push_back((*CurItemProtos)[ind]);

                HexMngr.SwitchIgnorePid(pid);
                HexMngr.RefreshMap();
            }
            // Add to container
            else if (Keyb.AltDwn && SelectedEntities.size())
            {
                bool add = true;
                ProtoItem* proto_item = (*CurItemProtos)[ind];

                if (proto_item->GetStackable())
                {
                    // Todo: need attention!
                    /*for (auto& child : SelectedEntities[0]->GetChildren())
                    {
                        if (proto_item->ProtoId == child->GetProtoId())
                        {
                            add = false;
                            break;
                        }
                    }*/
                }

                if (add)
                    AddItem(proto_item->ProtoId, 0, 0, SelectedEntities[0]);
            }
        }
        else if (IsTileMode() && CurTileHashes->size())
        {
            ind += *CurProtoScroll;
            if (ind >= (int)CurTileHashes->size())
                ind = (int)CurTileHashes->size() - 1;
            SetTabIndex(ind);
        }
        else if (IsCritMode() && CurNpcProtos->size())
        {
            ind += *CurProtoScroll;
            if (ind >= (int)CurNpcProtos->size())
                ind = (int)CurNpcProtos->size() - 1;
            SetTabIndex(ind);
        }
        else if (IntMode == INT_MODE_INCONT)
        {
            InContItem = nullptr;
            ind += InContScroll;
            EntityVec children;
            // Todo: need attention!
            // if (!SelectedEntities.empty())
            //    children = SelectedEntities[0]->GetChildren();

            if (!children.empty())
            {
                if (ind < (int)children.size())
                    InContItem = (ItemView*)children[ind];

                // Delete child
                if (Keyb.AltDwn && InContItem)
                {
                    if (InContItem->GetAccessory() == ITEM_ACCESSORY_CRITTER)
                    {
                        CritterView* owner = HexMngr.GetCritter(InContItem->GetCritId());
                        RUNTIME_ASSERT(owner);
                        owner->DeleteItem(InContItem, true);
                    }
                    else if (InContItem->GetAccessory() == ITEM_ACCESSORY_CONTAINER)
                    {
                        ItemView* owner = HexMngr.GetItemById(InContItem->GetContainerId());
                        RUNTIME_ASSERT(owner);
                        // owner->ContEraseItem(InContItem); // Todo: need attention!
                        InContItem->Release();
                    }
                    else
                    {
                        throw UnreachablePlaceException(LINE_STR);
                    }
                    InContItem = nullptr;

                    // Reselect
                    Entity* tmp = SelectedEntities[0];
                    SelectClear();
                    SelectAdd(tmp);
                }
                // Change child slot
                else if (Keyb.ShiftDwn && InContItem && SelectedEntities[0]->Type == EntityType::CritterView)
                {
                    CritterView* cr = (CritterView*)SelectedEntities[0];

                    int to_slot = InContItem->GetCritSlot() + 1;
                    while (!CritterView::SlotEnabled[to_slot % 256])
                        to_slot++;
                    to_slot %= 256;

                    // Todo: need attention!
                    // for (auto& child : cr->GetChildren())
                    //    if (((ItemView*)child)->GetCritSlot() == to_slot)
                    //        ((ItemView*)child)->SetCritSlot(0);

                    InContItem->SetCritSlot(to_slot);

                    cr->AnimateStay();
                }
                HexMngr.RebuildLight();
            }
        }
        else if (IntMode == INT_MODE_LIST)
        {
            ind += ListScroll;

            if (ind < (int)LoadedMaps.size() && ActiveMap != LoadedMaps[ind])
            {
                SelectClear();

                if (ActiveMap)
                    HexMngr.GetProtoMap(*(ProtoMap*)ActiveMap->Proto);
                if (HexMngr.SetProtoMap(*(ProtoMap*)LoadedMaps[ind]->Proto))
                {
                    ActiveMap = LoadedMaps[ind];
                    HexMngr.FindSetCenter(ActiveMap->GetWorkHexX(), ActiveMap->GetWorkHexY());
                }
            }
        }
    }
    else if (IsCurInRect(IntBCust[0], IntX, IntY))
        IntSetMode(INT_MODE_CUSTOM0);
    else if (IsCurInRect(IntBCust[1], IntX, IntY))
        IntSetMode(INT_MODE_CUSTOM1);
    else if (IsCurInRect(IntBCust[2], IntX, IntY))
        IntSetMode(INT_MODE_CUSTOM2);
    else if (IsCurInRect(IntBCust[3], IntX, IntY))
        IntSetMode(INT_MODE_CUSTOM3);
    else if (IsCurInRect(IntBCust[4], IntX, IntY))
        IntSetMode(INT_MODE_CUSTOM4);
    else if (IsCurInRect(IntBCust[5], IntX, IntY))
        IntSetMode(INT_MODE_CUSTOM5);
    else if (IsCurInRect(IntBCust[6], IntX, IntY))
        IntSetMode(INT_MODE_CUSTOM6);
    else if (IsCurInRect(IntBCust[7], IntX, IntY))
        IntSetMode(INT_MODE_CUSTOM7);
    else if (IsCurInRect(IntBCust[8], IntX, IntY))
        IntSetMode(INT_MODE_CUSTOM8);
    else if (IsCurInRect(IntBCust[9], IntX, IntY))
        IntSetMode(INT_MODE_CUSTOM9);
    else if (IsCurInRect(IntBItem, IntX, IntY))
        IntSetMode(INT_MODE_ITEM);
    else if (IsCurInRect(IntBTile, IntX, IntY))
        IntSetMode(INT_MODE_TILE);
    else if (IsCurInRect(IntBCrit, IntX, IntY))
        IntSetMode(INT_MODE_CRIT);
    else if (IsCurInRect(IntBFast, IntX, IntY))
        IntSetMode(INT_MODE_FAST);
    else if (IsCurInRect(IntBIgnore, IntX, IntY))
        IntSetMode(INT_MODE_IGNORE);
    else if (IsCurInRect(IntBInCont, IntX, IntY))
        IntSetMode(INT_MODE_INCONT);
    else if (IsCurInRect(IntBMess, IntX, IntY))
        IntSetMode(INT_MODE_MESS);
    else if (IsCurInRect(IntBList, IntX, IntY))
        IntSetMode(INT_MODE_LIST);
    else if (IsCurInRect(IntBScrBack, IntX, IntY))
    {
        if (IsObjectMode() || IsTileMode() || IsCritMode())
        {
            (*CurProtoScroll)--;
            if (*CurProtoScroll < 0)
                *CurProtoScroll = 0;
        }
        else if (IntMode == INT_MODE_INCONT)
        {
            InContScroll--;
            if (InContScroll < 0)
                InContScroll = 0;
        }
        else if (IntMode == INT_MODE_LIST)
        {
            ListScroll--;
            if (ListScroll < 0)
                ListScroll = 0;
        }
    }
    else if (IsCurInRect(IntBScrBackFst, IntX, IntY))
    {
        if (IsObjectMode() || IsTileMode() || IsCritMode())
        {
            (*CurProtoScroll) -= ProtosOnScreen;
            if (*CurProtoScroll < 0)
                *CurProtoScroll = 0;
        }
        else if (IntMode == INT_MODE_INCONT)
        {
            InContScroll -= ProtosOnScreen;
            if (InContScroll < 0)
                InContScroll = 0;
        }
        else if (IntMode == INT_MODE_LIST)
        {
            ListScroll -= ProtosOnScreen;
            if (ListScroll < 0)
                ListScroll = 0;
        }
    }
    else if (IsCurInRect(IntBScrFront, IntX, IntY))
    {
        if (IsObjectMode() && (*CurItemProtos).size())
        {
            (*CurProtoScroll)++;
            if (*CurProtoScroll >= (int)(*CurItemProtos).size())
                *CurProtoScroll = (int)(*CurItemProtos).size() - 1;
        }
        else if (IsTileMode() && CurTileHashes->size())
        {
            (*CurProtoScroll)++;
            if (*CurProtoScroll >= (int)CurTileHashes->size())
                *CurProtoScroll = (int)CurTileHashes->size() - 1;
        }
        else if (IsCritMode() && CurNpcProtos->size())
        {
            (*CurProtoScroll)++;
            if (*CurProtoScroll >= (int)CurNpcProtos->size())
                *CurProtoScroll = (int)CurNpcProtos->size() - 1;
        }
        else if (IntMode == INT_MODE_INCONT)
            InContScroll++;
        else if (IntMode == INT_MODE_LIST)
            ListScroll++;
    }
    else if (IsCurInRect(IntBScrFrontFst, IntX, IntY))
    {
        if (IsObjectMode() && (*CurItemProtos).size())
        {
            (*CurProtoScroll) += ProtosOnScreen;
            if (*CurProtoScroll >= (int)(*CurItemProtos).size())
                *CurProtoScroll = (int)(*CurItemProtos).size() - 1;
        }
        else if (IsTileMode() && CurTileHashes->size())
        {
            (*CurProtoScroll) += ProtosOnScreen;
            if (*CurProtoScroll >= (int)CurTileHashes->size())
                *CurProtoScroll = (int)CurTileHashes->size() - 1;
        }
        else if (IsCritMode() && CurNpcProtos->size())
        {
            (*CurProtoScroll) += ProtosOnScreen;
            if (*CurProtoScroll >= (int)CurNpcProtos->size())
                *CurProtoScroll = (int)CurNpcProtos->size() - 1;
        }
        else if (IntMode == INT_MODE_INCONT)
            InContScroll += ProtosOnScreen;
        else if (IntMode == INT_MODE_LIST)
            ListScroll += ProtosOnScreen;
    }
    else if (IsCurInRect(IntBShowItem, IntX, IntY))
    {
        Settings.ShowItem = !Settings.ShowItem;
        HexMngr.RefreshMap();
    }
    else if (IsCurInRect(IntBShowScen, IntX, IntY))
    {
        Settings.ShowScen = !Settings.ShowScen;
        HexMngr.RefreshMap();
    }
    else if (IsCurInRect(IntBShowWall, IntX, IntY))
    {
        Settings.ShowWall = !Settings.ShowWall;
        HexMngr.RefreshMap();
    }
    else if (IsCurInRect(IntBShowCrit, IntX, IntY))
    {
        Settings.ShowCrit = !Settings.ShowCrit;
        HexMngr.RefreshMap();
    }
    else if (IsCurInRect(IntBShowTile, IntX, IntY))
    {
        Settings.ShowTile = !Settings.ShowTile;
        HexMngr.RefreshMap();
    }
    else if (IsCurInRect(IntBShowRoof, IntX, IntY))
    {
        Settings.ShowRoof = !Settings.ShowRoof;
        HexMngr.RefreshMap();
    }
    else if (IsCurInRect(IntBShowFast, IntX, IntY))
    {
        Settings.ShowFast = !Settings.ShowFast;
        HexMngr.RefreshMap();
    }
    else if (IsCurInRect(IntBSelectItem, IntX, IntY))
        IsSelectItem = !IsSelectItem;
    else if (IsCurInRect(IntBSelectScen, IntX, IntY))
        IsSelectScen = !IsSelectScen;
    else if (IsCurInRect(IntBSelectWall, IntX, IntY))
        IsSelectWall = !IsSelectWall;
    else if (IsCurInRect(IntBSelectCrit, IntX, IntY))
        IsSelectCrit = !IsSelectCrit;
    else if (IsCurInRect(IntBSelectTile, IntX, IntY))
        IsSelectTile = !IsSelectTile;
    else if (IsCurInRect(IntBSelectRoof, IntX, IntY))
        IsSelectRoof = !IsSelectRoof;
    else if (!IntFix)
    {
        IntHold = INT_MAIN;
        IntVectX = Settings.MouseX - IntX;
        IntVectY = Settings.MouseY - IntY;
        return;
    }
    else
        return;

    IntHold = INT_BUTTON;
}

void FOMapper::IntLMouseUp()
{
    if (IntHold == INT_SELECT && HexMngr.GetHexPixel(Settings.MouseX, Settings.MouseY, SelectHX2, SelectHY2))
    {
        if (CurMode == CUR_MODE_DEFAULT)
        {
            if (SelectHX1 != SelectHX2 || SelectHY1 != SelectHY2)
            {
                HexMngr.ClearHexTrack();
                UShortPairVec h;

                if (SelectType == SELECT_TYPE_OLD)
                {
                    int fx = MIN(SelectHX1, SelectHX2);
                    int tx = MAX(SelectHX1, SelectHX2);
                    int fy = MIN(SelectHY1, SelectHY2);
                    int ty = MAX(SelectHY1, SelectHY2);

                    for (int i = fx; i <= tx; i++)
                        for (int j = fy; j <= ty; j++)
                            h.push_back(std::make_pair(i, j));
                }
                else // SELECT_TYPE_NEW
                {
                    HexMngr.GetHexesRect(Rect(SelectHX1, SelectHY1, SelectHX2, SelectHY2), h);
                }

                ItemHexViewVec items;
                CritterViewVec critters;
                for (uint i = 0, j = (uint)h.size(); i < j; i++)
                {
                    ushort hx = h[i].first;
                    ushort hy = h[i].second;

                    // Items, critters
                    HexMngr.GetItems(hx, hy, items);
                    HexMngr.GetCritters(hx, hy, critters, FIND_ALL);

                    // Tile, roof
                    if (IsSelectTile && Settings.ShowTile)
                        SelectAddTile(hx, hy, false);
                    if (IsSelectRoof && Settings.ShowRoof)
                        SelectAddTile(hx, hy, true);
                }

                for (uint k = 0; k < items.size(); k++)
                {
                    hash pid = items[k]->GetProtoId();
                    if (HexMngr.IsIgnorePid(pid))
                        continue;
                    if (!Settings.ShowFast && HexMngr.IsFastPid(pid))
                        continue;

                    if (!items[k]->IsAnyScenery() && IsSelectItem && Settings.ShowItem)
                        SelectAddItem(items[k]);
                    else if (items[k]->IsScenery() && IsSelectScen && Settings.ShowScen)
                        SelectAddItem(items[k]);
                    else if (items[k]->IsWall() && IsSelectWall && Settings.ShowWall)
                        SelectAddItem(items[k]);
                    else if (Settings.ShowFast && HexMngr.IsFastPid(pid))
                        SelectAddItem(items[k]);
                }

                for (uint l = 0; l < critters.size(); l++)
                {
                    if (IsSelectCrit && Settings.ShowCrit)
                        SelectAddCrit(critters[l]);
                }
            }
            else
            {
                ItemHexView* item;
                CritterView* cr;
                HexMngr.GetSmthPixel(Settings.MouseX, Settings.MouseY, item, cr);

                if (item)
                {
                    if (!HexMngr.IsIgnorePid(item->GetProtoId()))
                        SelectAddItem(item);
                }
                else if (cr)
                {
                    SelectAddCrit(cr);
                }
            }

            // Crits or item container
            // Todo: need attention!
            // if (!SelectedEntities.empty() && !SelectedEntities[0]->GetChildren().empty())
            //    IntSetMode(INT_MODE_INCONT);
        }

        HexMngr.RefreshMap();
    }

    IntHold = INT_NONE;
}

void FOMapper::IntMouseMove()
{
    if (IntHold == INT_SELECT)
    {
        HexMngr.ClearHexTrack();
        if (!HexMngr.GetHexPixel(Settings.MouseX, Settings.MouseY, SelectHX2, SelectHY2))
        {
            if (SelectHX2 || SelectHY2)
            {
                HexMngr.RefreshMap();
                SelectHX2 = SelectHY2 = 0;
            }
            return;
        }

        if (CurMode == CUR_MODE_DEFAULT)
        {
            if (SelectHX1 != SelectHX2 || SelectHY1 != SelectHY2)
            {
                if (SelectType == SELECT_TYPE_OLD)
                {
                    int fx = MIN(SelectHX1, SelectHX2);
                    int tx = MAX(SelectHX1, SelectHX2);
                    int fy = MIN(SelectHY1, SelectHY2);
                    int ty = MAX(SelectHY1, SelectHY2);

                    for (int i = fx; i <= tx; i++)
                        for (int j = fy; j <= ty; j++)
                            HexMngr.GetHexTrack(i, j) = 1;
                }
                else if (SelectType == SELECT_TYPE_NEW)
                {
                    UShortPairVec h;
                    HexMngr.GetHexesRect(Rect(SelectHX1, SelectHY1, SelectHX2, SelectHY2), h);

                    for (uint i = 0, j = (uint)h.size(); i < j; i++)
                        HexMngr.GetHexTrack(h[i].first, h[i].second) = 1;
                }

                HexMngr.RefreshMap();
            }
        }
        else if (CurMode == CUR_MODE_MOVE_SELECTION)
        {
            int offs_hx = (int)SelectHX2 - (int)SelectHX1;
            int offs_hy = (int)SelectHY2 - (int)SelectHY1;
            int offs_x = Settings.MouseX - SelectX;
            int offs_y = Settings.MouseY - SelectY;
            if (SelectMove(!Keyb.ShiftDwn, offs_hx, offs_hy, offs_x, offs_y))
            {
                SelectHX1 += offs_hx;
                SelectHY1 += offs_hy;
                SelectX += offs_x;
                SelectY += offs_y;
                HexMngr.RefreshMap();
            }
        }
    }
    else if (IntHold == INT_MAIN)
    {
        IntX = Settings.MouseX - IntVectX;
        IntY = Settings.MouseY - IntVectY;
    }
    else if (IntHold == INT_OBJECT)
    {
        ObjX = Settings.MouseX - ItemVectX;
        ObjY = Settings.MouseY - ItemVectY;
    }
}

uint FOMapper::GetTabIndex()
{
    if (IntMode < TAB_COUNT)
        return TabsActive[IntMode]->Index;
    return TabIndex[IntMode];
}

void FOMapper::SetTabIndex(uint index)
{
    if (IntMode < TAB_COUNT)
        TabsActive[IntMode]->Index = index;
    TabIndex[IntMode] = index;
}

void FOMapper::RefreshCurProtos()
{
    // Select protos and scroll
    CurItemProtos = nullptr;
    CurProtoScroll = nullptr;
    CurTileHashes = nullptr;
    CurTileNames = nullptr;
    CurNpcProtos = nullptr;
    InContItem = nullptr;

    if (IntMode >= 0 && IntMode < TAB_COUNT)
    {
        SubTab* stab = TabsActive[IntMode];
        if (stab->TileNames.size())
        {
            CurTileNames = &stab->TileNames;
            CurTileHashes = &stab->TileHashes;
        }
        else if (stab->NpcProtos.size())
        {
            CurNpcProtos = &stab->NpcProtos;
        }
        else
        {
            CurItemProtos = &stab->ItemProtos;
        }
        CurProtoScroll = &stab->Scroll;
    }

    if (IntMode == INT_MODE_INCONT)
        InContScroll = 0;

    // Update fast pids
    HexMngr.ClearFastPids();
    ProtoItemVec& fast_pids = TabsActive[INT_MODE_FAST]->ItemProtos;
    for (uint i = 0, j = (uint)fast_pids.size(); i < j; i++)
        HexMngr.AddFastPid(fast_pids[i]->ProtoId);

    // Update ignore pids
    HexMngr.ClearIgnorePids();
    ProtoItemVec& ignore_pids = TabsActive[INT_MODE_IGNORE]->ItemProtos;
    for (uint i = 0, j = (uint)ignore_pids.size(); i < j; i++)
        HexMngr.AddIgnorePid(ignore_pids[i]->ProtoId);

    // Refresh map
    if (HexMngr.IsMapLoaded())
        HexMngr.RefreshMap();
}

void FOMapper::IntSetMode(int mode)
{
    if (SubTabsActive && mode == SubTabsActiveTab)
    {
        SubTabsActive = false;
        return;
    }

    if (!SubTabsActive && mode == IntMode && mode >= 0 && mode < TAB_COUNT)
    {
        // Show sub tabs screen
        SubTabsActive = true;
        SubTabsActiveTab = mode;

        // Calculate position
        if (mode <= INT_MODE_CUSTOM9)
            SubTabsX = IntBCust[mode - INT_MODE_CUSTOM0].CX(), SubTabsY = IntBCust[mode - INT_MODE_CUSTOM0].T;
        else if (mode == INT_MODE_ITEM)
            SubTabsX = IntBItem.CX(), SubTabsY = IntBItem.T;
        else if (mode == INT_MODE_TILE)
            SubTabsX = IntBTile.CX(), SubTabsY = IntBTile.T;
        else if (mode == INT_MODE_CRIT)
            SubTabsX = IntBCrit.CX(), SubTabsY = IntBCrit.T;
        else if (mode == INT_MODE_FAST)
            SubTabsX = IntBFast.CX(), SubTabsY = IntBFast.T;
        else if (mode == INT_MODE_IGNORE)
            SubTabsX = IntBIgnore.CX(), SubTabsY = IntBIgnore.T;
        else
            SubTabsX = SubTabsY = 0;
        SubTabsX += IntX - SubTabsRect.W() / 2;
        SubTabsY += IntY - SubTabsRect.H();
        if (SubTabsX < 0)
            SubTabsX = 0;
        if (SubTabsX + SubTabsRect.W() > Settings.ScreenWidth)
            SubTabsX -= SubTabsX + SubTabsRect.W() - Settings.ScreenWidth;
        if (SubTabsY < 0)
            SubTabsY = 0;
        if (SubTabsY + SubTabsRect.H() > Settings.ScreenHeight)
            SubTabsY -= SubTabsY + SubTabsRect.H() - Settings.ScreenHeight;

        return;
    }

    IntMode = mode;
    IntHold = INT_NONE;

    RefreshCurProtos();

    if (SubTabsActive)
    {
        // Reinit sub tabs
        SubTabsActive = false;
        IntSetMode(IntMode);
    }
}

void FOMapper::MoveEntity(Entity* entity, ushort hx, ushort hy)
{
    if (hx >= HexMngr.GetWidth() || hy >= HexMngr.GetHeight())
        return;

    if (entity->Type == EntityType::CritterView)
    {
        CritterView* cr = (CritterView*)entity;
        if (cr->IsDead() || !HexMngr.GetField(hx, hy).Crit)
        {
            HexMngr.RemoveCritter(cr);
            cr->SetHexX(hx);
            cr->SetHexY(hy);
            HexMngr.SetCritter(cr);
        }
    }
    else if (entity->Type == EntityType::ItemHexView)
    {
        ItemHexView* item = (ItemHexView*)entity;
        HexMngr.DeleteItem(item, false);
        item->SetHexX(hx);
        item->SetHexY(hy);
        HexMngr.PushItem(item);
    }
}

void FOMapper::DeleteEntity(Entity* entity)
{
    auto it = std::find(SelectedEntities.begin(), SelectedEntities.end(), entity);
    if (it != SelectedEntities.end())
        SelectedEntities.erase(it);

    if (entity->Type == EntityType::CritterView)
        HexMngr.DeleteCritter(entity->Id);
    else if (entity->Type == EntityType::ItemHexView)
        HexMngr.FinishItem(entity->Id, false);
}

void FOMapper::SelectClear()
{
    // Clear map objects
    for (auto& entity : SelectedEntities)
    {
        if (entity->Type == EntityType::ItemHexView)
            ((ItemHexView*)entity)->RestoreAlpha();
        else if (entity->Type == EntityType::CritterView)
            ((CritterView*)entity)->Alpha = 0xFF;
    }
    SelectedEntities.clear();

    // Clear tiles
    if (SelectedTile.size())
        HexMngr.ParseSelTiles();
    SelectedTile.clear();
}

void FOMapper::SelectAddItem(ItemHexView* item)
{
    RUNTIME_ASSERT(item);
    SelectAdd(item);
}

void FOMapper::SelectAddCrit(CritterView* npc)
{
    RUNTIME_ASSERT(npc);
    SelectAdd(npc);
}

void FOMapper::SelectAddTile(ushort hx, ushort hy, bool is_roof)
{
    Field& f = HexMngr.GetField(hx, hy);
    if (!is_roof && !f.GetTilesCount(false))
        return;
    if (is_roof && !f.GetTilesCount(true))
        return;

    // Helper
    for (uint i = 0, j = (uint)SelectedTile.size(); i < j; i++)
    {
        SelMapTile& stile = SelectedTile[i];
        if (stile.HexX == hx && stile.HexY == hy && stile.IsRoof == is_roof)
            return;
    }
    SelectedTile.push_back(SelMapTile(hx, hy, is_roof));

    // Select
    MapTileVec& tiles = HexMngr.GetTiles(hx, hy, is_roof);
    for (uint i = 0, j = (uint)tiles.size(); i < j; i++)
        tiles[i].IsSelected = true;
}

void FOMapper::SelectAdd(Entity* entity)
{
    auto it = std::find(SelectedEntities.begin(), SelectedEntities.end(), entity);
    if (it == SelectedEntities.end())
    {
        SelectedEntities.push_back(entity);

        if (entity->Type == EntityType::CritterView)
            ((CritterView*)entity)->Alpha = HexMngr.SelectAlpha;
        if (entity->Type == EntityType::ItemHexView)
            ((ItemHexView*)entity)->Alpha = HexMngr.SelectAlpha;
    }
}

void FOMapper::SelectErase(Entity* entity)
{
    auto it = std::find(SelectedEntities.begin(), SelectedEntities.end(), entity);
    if (it != SelectedEntities.end())
    {
        SelectedEntities.erase(it);

        if (entity->Type == EntityType::CritterView)
            ((CritterView*)entity)->Alpha = 0xFF;
        if (entity->Type == EntityType::ItemHexView)
            ((ItemHexView*)entity)->RestoreAlpha();
    }
}

void FOMapper::SelectAll()
{
    SelectClear();

    for (uint i = 0; i < HexMngr.GetWidth(); i++)
    {
        for (uint j = 0; j < HexMngr.GetHeight(); j++)
        {
            if (IsSelectTile && Settings.ShowTile)
                SelectAddTile(i, j, false);
            if (IsSelectRoof && Settings.ShowRoof)
                SelectAddTile(i, j, true);
        }
    }

    ItemHexViewVec& items = HexMngr.GetItems();
    for (uint i = 0; i < items.size(); i++)
    {
        if (HexMngr.IsIgnorePid(items[i]->GetProtoId()))
            continue;

        if (!items[i]->IsAnyScenery() && IsSelectItem && Settings.ShowItem)
            SelectAddItem(items[i]);
        else if (items[i]->IsScenery() && IsSelectScen && Settings.ShowScen)
            SelectAddItem(items[i]);
        else if (items[i]->IsWall() && IsSelectWall && Settings.ShowWall)
            SelectAddItem(items[i]);
    }

    if (IsSelectCrit && Settings.ShowCrit)
    {
        CritterViewMap& crits = HexMngr.GetCritters();
        for (auto it = crits.begin(), end = crits.end(); it != end; ++it)
            SelectAddCrit(it->second);
    }

    HexMngr.RefreshMap();
}

struct TileToMove
{
    Field* field;
    Field::Tile tile;
    MapTileVec* ptiles;
    MapTile ptile;
    bool roof;
    TileToMove() {}
    TileToMove(Field* f, Field::Tile& t, MapTileVec* pts, MapTile& pt, bool r) :
        field(f), tile(t), ptiles(pts), ptile(pt), roof(r)
    {
    }
};
bool FOMapper::SelectMove(bool hex_move, int& offs_hx, int& offs_hy, int& offs_x, int& offs_y)
{
    if (!hex_move && (!offs_x && !offs_y))
        return false;
    if (hex_move && (!offs_hx && !offs_hy))
        return false;

    // Tile step
    if (hex_move && !SelectedTile.empty())
    {
        if (abs(offs_hx) < Settings.MapTileStep && abs(offs_hy) < Settings.MapTileStep)
            return false;
        offs_hx -= offs_hx % Settings.MapTileStep;
        offs_hy -= offs_hy % Settings.MapTileStep;
    }

    // Setup hex moving switcher
    int switcher = 0;
    if (!SelectedEntities.empty())
        switcher =
            (SelectedEntities[0]->Type == EntityType::CritterView ? ((CritterView*)SelectedEntities[0])->GetHexX() :
                                                                    ((ItemHexView*)SelectedEntities[0])->GetHexX()) %
            2;
    else if (!SelectedTile.empty())
        switcher = SelectedTile[0].HexX % 2;

    // Change moving speed on zooming
    if (!hex_move)
    {
        static float small_ox = 0.0f, small_oy = 0.0f;
        float ox = (float)offs_x * Settings.SpritesZoom + small_ox;
        float oy = (float)offs_y * Settings.SpritesZoom + small_oy;
        if (offs_x && fabs(ox) < 1.0f)
            small_ox = ox;
        else
            small_ox = 0.0f;
        if (offs_y && fabs(oy) < 1.0f)
            small_oy = oy;
        else
            small_oy = 0.0f;
        offs_x = (int)ox;
        offs_y = (int)oy;
    }
    // Check borders
    else
    {
        // Objects
        for (auto& entity : SelectedEntities)
        {
            int hx = (entity->Type == EntityType::CritterView ? ((CritterView*)entity)->GetHexX() :
                                                                ((ItemHexView*)entity)->GetHexX());
            int hy = (entity->Type == EntityType::CritterView ? ((CritterView*)entity)->GetHexY() :
                                                                ((ItemHexView*)entity)->GetHexY());
            if (Settings.MapHexagonal)
            {
                int sw = switcher;
                for (int k = 0, l = abs(offs_hx); k < l; k++, sw++)
                    GeomHelper.MoveHexByDirUnsafe(hx, hy, offs_hx > 0 ? ((sw & 1) ? 4 : 3) : ((sw & 1) ? 0 : 1));
                for (int k = 0, l = abs(offs_hy); k < l; k++)
                    GeomHelper.MoveHexByDirUnsafe(hx, hy, offs_hy > 0 ? 2 : 5);
            }
            else
            {
                hx += offs_hx;
                hy += offs_hy;
            }
            if (hx < 0 || hy < 0 || hx >= HexMngr.GetWidth() || hy >= HexMngr.GetHeight())
                return false; // Disable moving
        }

        // Tiles
        for (uint i = 0, j = (uint)SelectedTile.size(); i < j; i++)
        {
            SelMapTile& stile = SelectedTile[i];
            int hx = stile.HexX;
            int hy = stile.HexY;
            if (Settings.MapHexagonal)
            {
                int sw = switcher;
                for (int k = 0, l = abs(offs_hx); k < l; k++, sw++)
                    GeomHelper.MoveHexByDirUnsafe(hx, hy, offs_hx > 0 ? ((sw & 1) ? 4 : 3) : ((sw & 1) ? 0 : 1));
                for (int k = 0, l = abs(offs_hy); k < l; k++)
                    GeomHelper.MoveHexByDirUnsafe(hx, hy, offs_hy > 0 ? 2 : 5);
            }
            else
            {
                hx += offs_hx;
                hy += offs_hy;
            }
            if (hx < 0 || hy < 0 || hx >= HexMngr.GetWidth() || hy >= HexMngr.GetHeight())
                return false; // Disable moving
        }
    }

    // Move map objects
    for (auto& entity : SelectedEntities)
    {
        if (!hex_move)
        {
            if (entity->Type != EntityType::ItemHexView)
                continue;

            ItemHexView* item = (ItemHexView*)entity;
            int ox = item->GetOffsetX() + offs_x;
            int oy = item->GetOffsetY() + offs_y;
            if (Keyb.AltDwn)
                ox = oy = 0;

            item->SetOffsetX(ox);
            item->SetOffsetY(oy);
            item->RefreshAnim();
        }
        else
        {
            int hx = (entity->Type == EntityType::CritterView ? ((CritterView*)entity)->GetHexX() :
                                                                ((ItemHexView*)entity)->GetHexX());
            int hy = (entity->Type == EntityType::CritterView ? ((CritterView*)entity)->GetHexY() :
                                                                ((ItemHexView*)entity)->GetHexY());
            if (Settings.MapHexagonal)
            {
                int sw = switcher;
                for (int k = 0, l = abs(offs_hx); k < l; k++, sw++)
                    GeomHelper.MoveHexByDirUnsafe(hx, hy, offs_hx > 0 ? ((sw & 1) ? 4 : 3) : ((sw & 1) ? 0 : 1));
                for (int k = 0, l = abs(offs_hy); k < l; k++)
                    GeomHelper.MoveHexByDirUnsafe(hx, hy, offs_hy > 0 ? 2 : 5);
            }
            else
            {
                hx += offs_hx;
                hy += offs_hy;
            }
            hx = CLAMP(hx, 0, HexMngr.GetWidth() - 1);
            hy = CLAMP(hy, 0, HexMngr.GetHeight() - 1);

            if (entity->Type == EntityType::ItemHexView)
            {
                ItemHexView* item = (ItemHexView*)entity;
                HexMngr.DeleteItem(item, false);
                item->SetHexX(hx);
                item->SetHexY(hy);
                HexMngr.PushItem(item);
            }
            else if (entity->Type == EntityType::CritterView)
            {
                CritterView* cr = (CritterView*)entity;
                HexMngr.RemoveCritter(cr);
                cr->SetHexX(hx);
                cr->SetHexY(hy);
                HexMngr.SetCritter(cr);
            }
        }
    }

    // Move tiles
    vector<TileToMove> tiles_to_move;
    tiles_to_move.reserve(1000);

    for (uint i = 0, j = (uint)SelectedTile.size(); i < j; i++)
    {
        SelMapTile& stile = SelectedTile[i];

        if (!hex_move)
        {
            Field& f = HexMngr.GetField(stile.HexX, stile.HexY);
            MapTileVec& tiles = HexMngr.GetTiles(stile.HexX, stile.HexY, stile.IsRoof);

            for (uint k = 0, l = (uint)tiles.size(); k < l; k++)
            {
                if (tiles[k].IsSelected)
                {
                    int ox = tiles[k].OffsX + offs_x;
                    int oy = tiles[k].OffsY + offs_y;
                    if (Keyb.AltDwn)
                        ox = oy = 0;

                    tiles[k].OffsX = ox;
                    tiles[k].OffsY = oy;
                    Field::Tile ftile = f.GetTile(k, stile.IsRoof);
                    f.EraseTile(k, stile.IsRoof);
                    f.AddTile(ftile.Anim, ox, oy, ftile.Layer, stile.IsRoof);
                }
            }
        }
        else
        {
            int hx = stile.HexX;
            int hy = stile.HexY;
            if (Settings.MapHexagonal)
            {
                int sw = switcher;
                for (int k = 0, l = abs(offs_hx); k < l; k++, sw++)
                    GeomHelper.MoveHexByDirUnsafe(hx, hy, offs_hx > 0 ? ((sw & 1) ? 4 : 3) : ((sw & 1) ? 0 : 1));
                for (int k = 0, l = abs(offs_hy); k < l; k++)
                    GeomHelper.MoveHexByDirUnsafe(hx, hy, offs_hy > 0 ? 2 : 5);
            }
            else
            {
                hx += offs_hx;
                hy += offs_hy;
            }
            hx = CLAMP(hx, 0, HexMngr.GetWidth() - 1);
            hy = CLAMP(hy, 0, HexMngr.GetHeight() - 1);
            if (stile.HexX == hx && stile.HexY == hy)
                continue;

            Field& f = HexMngr.GetField(stile.HexX, stile.HexY);
            MapTileVec& tiles = HexMngr.GetTiles(stile.HexX, stile.HexY, stile.IsRoof);

            for (uint k = 0; k < tiles.size();)
            {
                if (tiles[k].IsSelected)
                {
                    tiles[k].HexX = hx;
                    tiles[k].HexY = hy;
                    Field::Tile& ftile = f.GetTile(k, stile.IsRoof);
                    tiles_to_move.push_back(TileToMove(&HexMngr.GetField(hx, hy), ftile,
                        &HexMngr.GetTiles(hx, hy, stile.IsRoof), tiles[k], stile.IsRoof));
                    tiles.erase(tiles.begin() + k);
                    f.EraseTile(k, stile.IsRoof);
                }
                else
                    k++;
            }
            stile.HexX = hx;
            stile.HexY = hy;
        }
    }

    for (auto it = tiles_to_move.begin(), end = tiles_to_move.end(); it != end; ++it)
    {
        TileToMove& ttm = *it;
        ttm.field->AddTile(ttm.tile.Anim, ttm.tile.OffsX, ttm.tile.OffsY, ttm.tile.Layer, ttm.roof);
        ttm.ptiles->push_back(ttm.ptile);
    }
    return true;
}

void FOMapper::SelectDelete()
{
    auto entities = SelectedEntities;
    for (auto& entity : entities)
        DeleteEntity(entity);

    for (uint i = 0, j = (uint)SelectedTile.size(); i < j; i++)
    {
        SelMapTile& stile = SelectedTile[i];
        Field& f = HexMngr.GetField(stile.HexX, stile.HexY);
        MapTileVec& tiles = HexMngr.GetTiles(stile.HexX, stile.HexY, stile.IsRoof);

        for (uint k = 0; k < tiles.size();)
        {
            if (tiles[k].IsSelected)
            {
                tiles.erase(tiles.begin() + k);
                f.EraseTile(k, stile.IsRoof);
            }
            else
                k++;
        }
    }

    SelectedEntities.clear();
    SelectedTile.clear();
    HexMngr.ClearSelTiles();
    SelectClear();
    HexMngr.RefreshMap();
    IntHold = INT_NONE;
    CurMode = CUR_MODE_DEFAULT;
}

CritterView* FOMapper::AddCritter(hash pid, ushort hx, ushort hy)
{
    RUNTIME_ASSERT(ActiveMap);

    ProtoCritter* proto = ProtoMngr.GetProtoCritter(pid);
    if (!proto)
        return nullptr;

    if (hx >= HexMngr.GetWidth() || hy >= HexMngr.GetHeight())
        return nullptr;
    if (HexMngr.GetField(hx, hy).Crit)
        return nullptr;

    SelectClear();

    CritterView* cr = 0; // Todo: need attention!
    // new CritterView(--((ProtoMap*)ActiveMap->Proto)->LastEntityId, proto, Settings, SprMngr, ResMngr);
    cr->SetHexX(hx);
    cr->SetHexY(hy);
    cr->SetDir(NpcDir);
    cr->SetCond(COND_LIFE);
    cr->Init();

    HexMngr.AddCritter(cr);
    SelectAdd(cr);

    HexMngr.RefreshMap();
    CurMode = CUR_MODE_DEFAULT;

    return cr;
}

ItemView* FOMapper::AddItem(hash pid, ushort hx, ushort hy, Entity* owner)
{
    RUNTIME_ASSERT(ActiveMap);

    // Checks
    ProtoItem* proto_item = ProtoMngr.GetProtoItem(pid);
    if (!proto_item)
        return nullptr;
    if (!owner && (hx >= HexMngr.GetWidth() || hy >= HexMngr.GetHeight()))
        return nullptr;
    if (proto_item->IsStatic() && owner)
        return nullptr;

    // Clear selection
    if (!owner)
        SelectClear();

    // Create
    ItemView* item;
    if (owner)
    {
        // Todo: need attention!
        /*item = new ItemView(--((ProtoMap*)ActiveMap->Proto)->LastEntityId, proto_item);
        if (owner->Type == EntityType::CritterView)
            ((CritterView*)owner)->AddItem(item);
        else if (owner->Type == EntityType::Item || owner->Type == EntityType::ItemHexView)
            ((ItemView*)owner)->ContSetItem(item);*/
    }
    else
    {
        // Todo: need attention!
        // uint id = HexMngr.AddItem(--((ProtoMap*)ActiveMap->Proto)->LastEntityId, pid, hx, hy, 0, nullptr);
        // item = HexMngr.GetItemById(id);
    }

    // Select
    if (!owner)
    {
        SelectAdd(item);
        CurMode = CUR_MODE_DEFAULT;
    }
    else
    {
        IntSetMode(INT_MODE_INCONT);
        InContItem = item;
    }

    return item;
}

void FOMapper::AddTile(hash name, ushort hx, ushort hy, short ox, short oy, uchar layer, bool is_roof)
{
    RUNTIME_ASSERT(ActiveMap);

    hx -= hx % Settings.MapTileStep;
    hy -= hy % Settings.MapTileStep;

    if (hx >= HexMngr.GetWidth() || hy >= HexMngr.GetHeight())
        return;

    SelectClear();

    HexMngr.SetTile(name, hx, hy, ox, oy, layer, is_roof, false);
    CurMode = CUR_MODE_DEFAULT;
}

Entity* FOMapper::CloneEntity(Entity* entity)
{
    RUNTIME_ASSERT(ActiveMap);

    int hx = (entity->Type == EntityType::CritterView ? ((CritterView*)entity)->GetHexX() :
                                                        ((ItemHexView*)entity)->GetHexX());
    int hy = (entity->Type == EntityType::CritterView ? ((CritterView*)entity)->GetHexY() :
                                                        ((ItemHexView*)entity)->GetHexY());
    if (hx >= HexMngr.GetWidth() || hy >= HexMngr.GetHeight())
        return nullptr;

    Entity* owner = nullptr;
    if (entity->Type == EntityType::CritterView)
    {
        if (HexMngr.GetField(hx, hy).Crit)
        {
            bool place_founded = false;
            for (int d = 0; d < 6; d++)
            {
                ushort hx_ = hx;
                ushort hy_ = hy;
                GeomHelper.MoveHexByDir(hx_, hy_, d, HexMngr.GetWidth(), HexMngr.GetHeight());
                if (!HexMngr.GetField(hx_, hy_).Crit)
                {
                    hx = hx_;
                    hy = hy_;
                    place_founded = true;
                    break;
                }
            }
            if (!place_founded)
                return nullptr;
        }

        CritterView* cr = 0; // Todo: need attention!
                             // new CritterView(
                             //--((ProtoMap*)ActiveMap->Proto)->LastEntityId, (ProtoCritter*)entity->Proto, Settings,
                             // SprMngr, ResMngr);
        cr->Props = ((CritterView*)entity)->Props;
        cr->SetHexX(hx);
        cr->SetHexY(hy);
        cr->Init();
        HexMngr.AddCritter(cr);
        SelectAdd(cr);
        owner = cr;
    }
    else if (entity->Type == EntityType::ItemHexView)
    {
        uint id = 0; // Todo: need attention!
        // HexMngr.AddItem(
        //  --((ProtoMap*)ActiveMap->Proto)->LastEntityId, entity->GetProtoId(), hx, hy, false, nullptr);
        ItemHexView* item = HexMngr.GetItemById(id);
        SelectAdd(item);
        owner = item;
    }
    else
    {
        throw UnreachablePlaceException(LINE_STR);
    }

    auto pmap = (ProtoMap*)ActiveMap->Proto;
    std::function<void(Entity*, Entity*)> add_entity_children = [&add_entity_children, &pmap](
                                                                    Entity* from, Entity* to) {
        // Todo: need attention!
        /*for (auto& from_child : from->GetChildren())
        {
            RUNTIME_ASSERT(from_child->Type == EntityType::Item);
            ItemView* to_child = new ItemView(--pmap->LastEntityId, (ProtoItem*)from_child->Proto);
            to_child->Props = from_child->Props;
            if (to->Type == EntityType::CritterView)
                ((CritterView*)to)->AddItem(to_child);
            else
                ((ItemView*)to)->ContSetItem(to_child);
            add_entity_children(from_child, to_child);
        }*/
    };
    add_entity_children(entity, owner);

    return owner;
}

void FOMapper::BufferCopy()
{
    // Clear buffers
    std::function<void(EntityBuf*)> free_entity = [&free_entity](EntityBuf* entity_buf) {
        delete entity_buf->Props;
        for (auto& child : entity_buf->Children)
        {
            free_entity(child);
            delete child;
        }
    };
    for (auto& entity_buf : EntitiesBuffer)
        free_entity(&entity_buf);
    EntitiesBuffer.clear();
    TilesBuffer.clear();

    // Add entities to buffer
    std::function<void(EntityBuf*, Entity*)> add_entity = [&add_entity](EntityBuf* entity_buf, Entity* entity) {
        entity_buf->HexX = (entity->Type == EntityType::CritterView ? ((CritterView*)entity)->GetHexX() :
                                                                      ((ItemHexView*)entity)->GetHexX());
        entity_buf->HexY = (entity->Type == EntityType::CritterView ? ((CritterView*)entity)->GetHexY() :
                                                                      ((ItemHexView*)entity)->GetHexY());
        entity_buf->Type = entity->Type;
        entity_buf->Proto = entity->Proto;
        entity_buf->Props = new Properties(entity->Props);
        // Todo: need attention!
        /*for (auto& child : entity->GetChildren())
        {
            EntityBuf* child_buf = new EntityBuf();
            add_entity(child_buf, child);
            entity_buf->Children.push_back(child_buf);
        }*/
    };
    for (auto& entity : SelectedEntities)
    {
        EntitiesBuffer.push_back(EntityBuf());
        add_entity(&EntitiesBuffer.back(), entity);
    }

    // Add tiles to buffer
    for (auto& tile : SelectedTile)
    {
        MapTileVec& hex_tiles = HexMngr.GetTiles(tile.HexX, tile.HexY, tile.IsRoof);
        for (auto& hex_tile : hex_tiles)
        {
            if (hex_tile.IsSelected)
            {
                TileBuf tb;
                tb.Name = hex_tile.Name;
                tb.HexX = tile.HexX;
                tb.HexY = tile.HexY;
                tb.OffsX = hex_tile.OffsX;
                tb.OffsY = hex_tile.OffsY;
                tb.Layer = hex_tile.Layer;
                tb.IsRoof = tile.IsRoof;
                TilesBuffer.push_back(tb);
            }
        }
    }
}

void FOMapper::BufferCut()
{
    BufferCopy();
    SelectDelete();
}

void FOMapper::BufferPaste(int, int)
{
    if (!ActiveMap)
        return;

    SelectClear();

    // Paste map objects
    for (auto& entity_buf : EntitiesBuffer)
    {
        if (entity_buf.HexX >= HexMngr.GetWidth() || entity_buf.HexY >= HexMngr.GetHeight())
            continue;

        ushort hx = entity_buf.HexX;
        ushort hy = entity_buf.HexY;

        Entity* owner = nullptr;
        if (entity_buf.Type == EntityType::CritterView)
        {
            if (HexMngr.GetField(hx, hy).Crit)
            {
                bool place_founded = false;
                for (int d = 0; d < 6; d++)
                {
                    ushort hx_ = entity_buf.HexX;
                    ushort hy_ = entity_buf.HexY;
                    GeomHelper.MoveHexByDir(hx_, hy_, d, HexMngr.GetWidth(), HexMngr.GetHeight());
                    if (!HexMngr.GetField(hx_, hy_).Crit)
                    {
                        hx = hx_;
                        hy = hy_;
                        place_founded = true;
                        break;
                    }
                }
                if (!place_founded)
                    continue;
            }

            CritterView* cr = 0; // Todo: need attention!
            // new CritterView(--((ProtoMap*)ActiveMap->Proto)->LastEntityId,
            //  (ProtoCritter*)entity_buf.Proto, Settings, SprMngr, ResMngr);
            cr->Props = *entity_buf.Props;
            cr->SetHexX(hx);
            cr->SetHexY(hy);
            cr->Init();
            HexMngr.AddCritter(cr);
            SelectAdd(cr);
            owner = cr;
        }
        else if (entity_buf.Type == EntityType::ItemHexView)
        {
            uint id = 0; // Todo: need attention!
            // HexMngr.AddItem(
            //  --((ProtoMap*)ActiveMap->Proto)->LastEntityId, entity_buf.Proto->ProtoId, hx, hy, false, nullptr);
            ItemHexView* item = HexMngr.GetItemById(id);
            item->Props = *entity_buf.Props;
            SelectAdd(item);
            owner = item;
        }

        auto pmap = (ProtoMap*)ActiveMap->Proto;
        std::function<void(EntityBuf*, Entity*)> add_entity_children = [&add_entity_children, &pmap](
                                                                           EntityBuf* entity_buf, Entity* entity) {
            for (auto& child_buf : entity_buf->Children)
            {
                // Todo: need attention!
                /*RUNTIME_ASSERT(child_buf->Type == EntityType::Item);
                ItemView* child = new ItemView(--pmap->LastEntityId, (ProtoItem*)child_buf->Proto);
                child->Props = *child_buf->Props;
                if (entity->Type == EntityType::CritterView)
                    ((CritterView*)entity)->AddItem(child);
                else
                    ((ItemView*)entity)->ContSetItem(child);
                add_entity_children(child_buf, child);*/
            }
        };
        add_entity_children(&entity_buf, owner);
    }

    // Paste tiles
    for (auto& tile_buf : TilesBuffer)
    {
        if (tile_buf.HexX < HexMngr.GetWidth() && tile_buf.HexY < HexMngr.GetHeight())
        {
            // Create
            HexMngr.SetTile(tile_buf.Name, tile_buf.HexX, tile_buf.HexY, tile_buf.OffsX, tile_buf.OffsY, tile_buf.Layer,
                tile_buf.IsRoof, true);

            // Select helper
            bool sel_added = false;
            for (uint i = 0, j = (uint)SelectedTile.size(); i < j; i++)
            {
                SelMapTile& stile = SelectedTile[i];
                if (stile.HexX == tile_buf.HexX && stile.HexY == tile_buf.HexY && stile.IsRoof == tile_buf.IsRoof)
                {
                    sel_added = true;
                    break;
                }
            }
            if (!sel_added)
                SelectedTile.push_back(SelMapTile(tile_buf.HexX, tile_buf.HexY, tile_buf.IsRoof));
        }
    }
}

void FOMapper::CurDraw()
{
    switch (CurMode)
    {
    case CUR_MODE_DEFAULT:
    case CUR_MODE_MOVE_SELECTION: {
        AnyFrames* anim = (CurMode == CUR_MODE_DEFAULT ? CurPDef : CurPHand);
        if (anim)
        {
            SpriteInfo* si = SprMngr.GetSpriteInfo(anim->GetCurSprId());
            if (si)
                SprMngr.DrawSprite(anim, Settings.MouseX, Settings.MouseY, COLOR_IFACE);
        }
    }
    break;
    case CUR_MODE_PLACE_OBJECT:
        if (IsObjectMode() && (*CurItemProtos).size())
        {
            ProtoItem* proto_item = (*CurItemProtos)[GetTabIndex()];

            ushort hx, hy;
            if (!HexMngr.GetHexPixel(Settings.MouseX, Settings.MouseY, hx, hy))
                break;

            uint spr_id = GetProtoItemCurSprId(proto_item);
            SpriteInfo* si = SprMngr.GetSpriteInfo(spr_id);
            if (si)
            {
                int x = HexMngr.GetField(hx, hy).ScrX - (si->Width / 2) + si->OffsX + (Settings.MapHexWidth / 2) +
                    Settings.ScrOx + proto_item->GetOffsetX();
                int y = HexMngr.GetField(hx, hy).ScrY - si->Height + si->OffsY + (Settings.MapHexHeight / 2) +
                    Settings.ScrOy + proto_item->GetOffsetY();
                SprMngr.DrawSpriteSize(spr_id, (int)(x / Settings.SpritesZoom), (int)(y / Settings.SpritesZoom),
                    (int)(si->Width / Settings.SpritesZoom), (int)(si->Height / Settings.SpritesZoom), true, false);
            }
        }
        else if (IsTileMode() && CurTileHashes->size())
        {
            AnyFrames* anim = ResMngr.GetItemAnim((*CurTileHashes)[GetTabIndex()]);
            if (!anim)
                anim = ResMngr.ItemHexDefaultAnim;

            ushort hx, hy;
            if (!HexMngr.GetHexPixel(Settings.MouseX, Settings.MouseY, hx, hy))
                break;

            SpriteInfo* si = SprMngr.GetSpriteInfo(anim->GetCurSprId());
            if (si)
            {
                hx -= hx % Settings.MapTileStep;
                hy -= hy % Settings.MapTileStep;
                int x = HexMngr.GetField(hx, hy).ScrX - (si->Width / 2) + si->OffsX;
                int y = HexMngr.GetField(hx, hy).ScrY - si->Height + si->OffsY;
                if (!DrawRoof)
                {
                    x += Settings.MapTileOffsX;
                    y += Settings.MapTileOffsY;
                }
                else
                {
                    x += Settings.MapRoofOffsX;
                    y += Settings.MapRoofOffsY;
                }

                SprMngr.DrawSpriteSize(anim, (int)((x + Settings.ScrOx) / Settings.SpritesZoom),
                    (int)((y + Settings.ScrOy) / Settings.SpritesZoom), (int)(si->Width / Settings.SpritesZoom),
                    (int)(si->Height / Settings.SpritesZoom), true, false);
            }
        }
        else if (IsCritMode() && CurNpcProtos->size())
        {
            hash model_name = (*CurNpcProtos)[GetTabIndex()]->Props.GetPropValue<hash>(CritterView::PropertyModelName);
            uint spr_id = ResMngr.GetCritSprId(model_name, 1, 1, NpcDir);
            if (!spr_id)
                spr_id = ResMngr.ItemHexDefaultAnim->GetSprId(0);

            ushort hx, hy;
            if (!HexMngr.GetHexPixel(Settings.MouseX, Settings.MouseY, hx, hy))
                break;

            SpriteInfo* si = SprMngr.GetSpriteInfo(spr_id);
            if (si)
            {
                int x = HexMngr.GetField(hx, hy).ScrX - (si->Width / 2) + si->OffsX;
                int y = HexMngr.GetField(hx, hy).ScrY - si->Height + si->OffsY;

                SprMngr.DrawSpriteSize(spr_id,
                    (int)((x + Settings.ScrOx + (Settings.MapHexWidth / 2)) / Settings.SpritesZoom),
                    (int)((y + Settings.ScrOy + (Settings.MapHexHeight / 2)) / Settings.SpritesZoom),
                    (int)(si->Width / Settings.SpritesZoom), (int)(si->Height / Settings.SpritesZoom), true, false);
            }
        }
        else
        {
            CurMode = CUR_MODE_DEFAULT;
        }
        break;
    default:
        CurMode = CUR_MODE_DEFAULT;
        break;
    }
}

void FOMapper::CurRMouseUp()
{
    if (IntHold == INT_NONE)
    {
        if (CurMode == CUR_MODE_MOVE_SELECTION)
        {
            CurMode = CUR_MODE_DEFAULT;
        }
        else if (CurMode == CUR_MODE_PLACE_OBJECT)
        {
            CurMode = CUR_MODE_DEFAULT;
        }
        else if (CurMode == CUR_MODE_DEFAULT)
        {
            if (SelectedEntities.size() || SelectedTile.size())
                CurMode = CUR_MODE_MOVE_SELECTION;
            else if (IsObjectMode() || IsTileMode() || IsCritMode())
                CurMode = CUR_MODE_PLACE_OBJECT;
        }
    }
}

void FOMapper::CurMMouseDown()
{
    if (SelectedEntities.empty())
    {
        NpcDir++;
        if (NpcDir >= Settings.MapDirCount)
            NpcDir = 0;

        DrawRoof = !DrawRoof;
    }
    else
    {
        for (auto& entity : SelectedEntities)
        {
            if (entity->Type == EntityType::CritterView)
            {
                CritterView* cr = (CritterView*)entity;
                int dir = cr->GetDir() + 1;
                if (dir >= Settings.MapDirCount)
                    dir = 0;
                cr->ChangeDir(dir);
            }
        }
    }
}

bool FOMapper::IsCurInRect(Rect& rect, int ax, int ay)
{
    return (Settings.MouseX >= rect[0] + ax && Settings.MouseY >= rect[1] + ay && Settings.MouseX <= rect[2] + ax &&
        Settings.MouseY <= rect[3] + ay);
}

bool FOMapper::IsCurInRect(Rect& rect)
{
    return (Settings.MouseX >= rect[0] && Settings.MouseY >= rect[1] && Settings.MouseX <= rect[2] &&
        Settings.MouseY <= rect[3]);
}

bool FOMapper::IsCurInRectNoTransp(uint spr_id, Rect& rect, int ax, int ay)
{
    return IsCurInRect(rect, ax, ay) &&
        SprMngr.IsPixNoTransp(spr_id, Settings.MouseX - rect.L - ax, Settings.MouseY - rect.T - ay, false);
}

bool FOMapper::IsCurInInterface()
{
    if (IntVisible && SubTabsActive && IsCurInRectNoTransp(SubTabsPic->GetCurSprId(), SubTabsRect, SubTabsX, SubTabsY))
        return true;
    if (IntVisible && IsCurInRectNoTransp(IntMainPic->GetCurSprId(), IntWMain, IntX, IntY))
        return true;
    if (ObjVisible && !SelectedEntities.empty() &&
        IsCurInRectNoTransp(ObjWMainPic->GetCurSprId(), ObjWMain, ObjX, ObjY))
        return true;
    return false;
}

bool FOMapper::GetCurHex(ushort& hx, ushort& hy, bool ignore_interface)
{
    hx = hy = 0;
    if (!ignore_interface && IsCurInInterface())
        return false;
    return HexMngr.GetHexPixel(Settings.MouseX, Settings.MouseY, hx, hy);
}

void FOMapper::ConsoleDraw()
{
    if (ConsoleEdit)
        SprMngr.DrawSprite(ConsolePic, IntX + ConsolePicX, (IntVisible ? IntY : Settings.ScreenHeight) + ConsolePicY);

    if (ConsoleEdit)
    {
        string buf = ConsoleStr;
        buf.insert(ConsoleCur, Timer::FastTick() % 800 < 400 ? "!" : ".");
        SprMngr.DrawStr(Rect(IntX + ConsoleTextX, (IntVisible ? IntY : Settings.ScreenHeight) + ConsoleTextY,
                            Settings.ScreenWidth, Settings.ScreenHeight),
            buf, FT_NOBREAK);
    }
}

void FOMapper::ConsoleKeyDown(KeyCode dik, const char* dik_text)
{
    if (dik == KeyCode::DIK_RETURN || dik == KeyCode::DIK_NUMPADENTER)
    {
        if (ConsoleEdit)
        {
            if (ConsoleStr.empty())
            {
                ConsoleEdit = false;
            }
            else
            {
                // Modify console history
                ConsoleHistory.push_back(ConsoleStr);
                for (uint i = 0; i < ConsoleHistory.size() - 1; i++)
                {
                    if (ConsoleHistory[i] == ConsoleHistory[ConsoleHistory.size() - 1])
                    {
                        ConsoleHistory.erase(ConsoleHistory.begin() + i);
                        i = -1;
                    }
                }
                while (ConsoleHistory.size() > Settings.ConsoleHistorySize)
                    ConsoleHistory.erase(ConsoleHistory.begin());
                ConsoleHistoryCur = (int)ConsoleHistory.size();

                // Save console history
                string history_str = "";
                for (size_t i = 0, j = ConsoleHistory.size(); i < j; i++)
                    history_str += ConsoleHistory[i] + "\n";
                Cache.SetCache("mapper_console.txt", history_str);

                // Process command
                bool process_command = true;
                process_command = ScriptSys.RaiseInternalEvent(MapperFunctions.ConsoleMessage, &ConsoleStr);

                AddMess(ConsoleStr.c_str());
                if (process_command)
                    ParseCommand(ConsoleStr.c_str());
                ConsoleStr = "";
                ConsoleCur = 0;
            }
        }
        else
        {
            ConsoleEdit = true;
            ConsoleStr = "";
            ConsoleCur = 0;
            ConsoleHistoryCur = (int)ConsoleHistory.size();
        }

        return;
    }

    switch (dik)
    {
    case KeyCode::DIK_UP:
        if (ConsoleHistoryCur - 1 < 0)
            return;
        ConsoleHistoryCur--;
        ConsoleStr = ConsoleHistory[ConsoleHistoryCur];
        ConsoleCur = (uint)ConsoleStr.length();
        return;
    case KeyCode::DIK_DOWN:
        if (ConsoleHistoryCur + 1 >= (int)ConsoleHistory.size())
        {
            ConsoleHistoryCur = (int)ConsoleHistory.size();
            ConsoleStr = "";
            ConsoleCur = 0;
            return;
        }
        ConsoleHistoryCur++;
        ConsoleStr = ConsoleHistory[ConsoleHistoryCur];
        ConsoleCur = (uint)ConsoleStr.length();
        return;
    default:
        Keyb.GetChar(dik, dik_text, ConsoleStr, &ConsoleCur, MAX_CHAT_MESSAGE, KIF_NO_SPEC_SYMBOLS);
        ConsoleLastKey = dik;
        ConsoleLastKeyText = dik_text;
        ConsoleKeyTick = Timer::FastTick();
        ConsoleAccelerate = 1;
        return;
    }
}

void FOMapper::ConsoleKeyUp(KeyCode key)
{
    ConsoleLastKey = KeyCode::DIK_NONE;
    ConsoleLastKeyText = "";
}

void FOMapper::ConsoleProcess()
{
    if (ConsoleLastKey == KeyCode::DIK_NONE)
        return;

    if ((int)(Timer::FastTick() - ConsoleKeyTick) >= CONSOLE_KEY_TICK - ConsoleAccelerate)
    {
        ConsoleKeyTick = Timer::FastTick();
        ConsoleAccelerate = CONSOLE_MAX_ACCELERATE;
        Keyb.GetChar(
            ConsoleLastKey, ConsoleLastKeyText, ConsoleStr, &ConsoleCur, MAX_CHAT_MESSAGE, KIF_NO_SPEC_SYMBOLS);
    }
}

void FOMapper::ParseCommand(const string& command)
{
    if (command.empty())
        return;

    // Load map
    if (command[0] == '~')
    {
        string map_name = _str(command.substr(1)).trim();
        if (map_name.empty())
        {
            AddMess("Error parse map name.");
            return;
        }

        ProtoMap* pmap = new ProtoMap(_str(map_name).toHash());
        // Todo: need attention!
        /*if (!pmap->EditorLoad(ServerFileMngr, ProtoMngr, SprMngr, ResMngr))
        {
            AddMess("File not found or truncated.");
            return;
        }*/

        SelectClear();

        if (ActiveMap)
            HexMngr.GetProtoMap(*(ProtoMap*)ActiveMap->Proto);
        if (!HexMngr.SetProtoMap(*pmap))
        {
            AddMess("Load map fail.");
            return;
        }

        HexMngr.FindSetCenter(pmap->GetWorkHexX(), pmap->GetWorkHexY());

        MapView* map = new MapView(0, pmap);
        ActiveMap = map;
        LoadedMaps.push_back(map);

        AddMess("Load map complete.");

        RunMapLoadScript(map);
    }
    // Save map
    else if (command[0] == '^')
    {
        string map_name = _str(command.substr(1)).trim();
        if (map_name.empty())
        {
            AddMess("Error parse map name.");
            return;
        }

        if (!ActiveMap)
        {
            AddMess("Map not loaded.");
            return;
        }

        HexMngr.GetProtoMap(*(ProtoMap*)ActiveMap->Proto);

        // Todo: need attention!
        // ((ProtoMap*)ActiveMap->Proto)->EditorSave(ServerFileMngr, map_name);

        AddMess("Save map success.");
        RunMapSaveScript(ActiveMap);
    }
    // Run script
    else if (command[0] == '#')
    {
        istringstream icmd(command.substr(1));
        string func_name;
        if (!(icmd >> func_name))
        {
            AddMess("Function name not typed.");
            return;
        }

        // Reparse module
        uint bind_id = ScriptSys.BindByFuncName(func_name, "string %s(string)", true);
        if (bind_id)
        {
            string str = _str(command).substringAfter(' ').trim();
            ScriptSys.PrepareContext(bind_id, "Mapper");
            ScriptSys.SetArgObject(&str);
            if (ScriptSys.RunPrepared())
            {
                string result = *(string*)ScriptSys.GetReturnedRawAddress();
                AddMess(_str("Result: {}", result).c_str());
            }
            else
            {
                AddMess("Script execution fail.");
            }
        }
        else
        {
            AddMess("Function not found.");
            return;
        }
    }
    // Critter animations
    else if (command[0] == '@')
    {
        AddMess("Playing critter animations.");

        if (!ActiveMap)
        {
            AddMess("Map not loaded.");
            return;
        }

        IntVec anims = _str(command.substr(1)).splitToInt(' ');
        if (anims.empty())
            return;

        if (!SelectedEntities.empty())
        {
            for (auto& entity : SelectedEntities)
            {
                if (entity->Type == EntityType::CritterView)
                {
                    CritterView* cr = (CritterView*)entity;
                    cr->ClearAnim();
                    for (uint j = 0; j < anims.size() / 2; j++)
                        cr->Animate(anims[j * 2], anims[j * 2 + 1], nullptr);
                }
            }
        }
        else
        {
            for (auto& cr_kv : HexMngr.GetCritters())
            {
                CritterView* cr = cr_kv.second;
                cr->ClearAnim();
                for (uint j = 0; j < anims.size() / 2; j++)
                    cr->Animate(anims[j * 2], anims[j * 2 + 1], nullptr);
            }
        }
    }
    // Other
    else if (command[0] == '*')
    {
        istringstream icommand(command.substr(1));
        string command_ext;
        if (!(icommand >> command_ext))
            return;

        if (command_ext == "new")
        {
            ProtoMap* pmap = new ProtoMap(_str("new").toHash());

            pmap->SetWidth(MAXHEX_DEF);
            pmap->SetHeight(MAXHEX_DEF);

            // Morning	 5.00 -  9.59	 300 - 599
            // Day		10.00 - 18.59	 600 - 1139
            // Evening	19.00 - 22.59	1140 - 1379
            // Nigh		23.00 -  4.59	1380
            IntVec vec = {300, 600, 1140, 1380};
            UCharVec vec2 = {18, 128, 103, 51, 18, 128, 95, 40, 53, 128, 86, 29};
            CScriptArray* arr = ScriptSys.CreateArray("int[]");
            ScriptSys.AppendVectorToArray(vec, arr);
            pmap->SetDayTime(arr);
            arr->Release();
            CScriptArray* arr2 = ScriptSys.CreateArray("uint8[]");
            ScriptSys.AppendVectorToArray(vec2, arr2);
            pmap->SetDayColor(arr2);
            arr2->Release();

            if (ActiveMap)
                HexMngr.GetProtoMap(*(ProtoMap*)ActiveMap->Proto);

            if (!HexMngr.SetProtoMap(*pmap))
            {
                AddMess("Create map fail, see log.");
                return;
            }

            AddMess("Create map success.");
            HexMngr.FindSetCenter(150, 150);

            MapView* map = new MapView(0, pmap);
            ActiveMap = map;
            LoadedMaps.push_back(map);
        }
        else if (command_ext == "unload")
        {
            AddMess("Unload map.");

            auto it = std::find(LoadedMaps.begin(), LoadedMaps.end(), ActiveMap);
            if (it == LoadedMaps.end())
                return;

            LoadedMaps.erase(it);
            SelectedEntities.clear();
            ActiveMap->Proto->Release();
            ActiveMap->Release();
            ActiveMap = nullptr;

            if (LoadedMaps.empty())
            {
                HexMngr.UnloadMap();
                return;
            }

            if (HexMngr.SetProtoMap(*(ProtoMap*)LoadedMaps[0]->Proto))
            {
                ActiveMap = LoadedMaps[0];
                HexMngr.FindSetCenter(ActiveMap->GetWorkHexX(), ActiveMap->GetWorkHexY());
                return;
            }
        }
        else if (command_ext == "scripts")
        {
            if (InitScriptSystem())
                RunStartScript();
            AddMess("Scripts reloaded.");
        }
        else if (command_ext == "size" && ActiveMap)
        {
            AddMess("Resize map.");

            int maxhx, maxhy;
            if (!(icommand >> maxhx >> maxhy))
            {
                AddMess("Invalid args.");
                return;
            }

            FOMapper::SScriptFunc::Global_ResizeMap(maxhx, maxhy);
        }
    }
    else
    {
        AddMess("Unknown command.");
    }
}

void FOMapper::AddMess(const char* message_text)
{
    string str = _str("|{} - {}\n", COLOR_TEXT, message_text);

    DateTimeStamp dt;
    Timer::GetCurrentDateTime(dt);
    string mess_time = _str("{:02}:{:02}:{:02} ", dt.Hour, dt.Minute, dt.Second);

    MessBox.push_back(MessBoxMessage(0, str.c_str(), mess_time.c_str()));
    MessBoxScroll = 0;
    MessBoxGenerate();
}

void FOMapper::MessBoxGenerate()
{
    MessBoxCurText = "";
    if (MessBox.empty())
        return;

    Rect ir(IntWWork[0] + IntX, IntWWork[1] + IntY, IntWWork[2] + IntX, IntWWork[3] + IntY);
    int max_lines = ir.H() / 10;
    if (ir.IsZero())
        max_lines = 20;

    int cur_mess = (int)MessBox.size() - 1;
    for (int i = 0, j = 0; cur_mess >= 0; cur_mess--)
    {
        MessBoxMessage& m = MessBox[cur_mess];
        // Scroll
        j++;
        if (j <= MessBoxScroll)
            continue;
        // Add to message box
        MessBoxCurText = m.Mess + MessBoxCurText;
        i++;
        if (i >= max_lines)
            break;
    }
}

void FOMapper::MessBoxDraw()
{
    if (!IntVisible)
        return;
    if (MessBoxCurText.empty())
        return;

    uint flags = FT_UPPER | FT_BOTTOM;
    SprMngr.DrawStr(Rect(IntWWork[0] + IntX, IntWWork[1] + IntY, IntWWork[2] + IntX, IntWWork[3] + IntY),
        MessBoxCurText.c_str(), flags);
}

bool FOMapper::SaveLogFile()
{
    if (MessBox.empty())
        return false;

    DateTimeStamp dt;
    Timer::GetCurrentDateTime(dt);
    string log_path = _str("mapper_messbox_{:02}-{:02}-{}_{:02}-{:02}-{:02}.txt", dt.Day, dt.Month, dt.Year, dt.Hour,
        dt.Minute, dt.Second);

    DiskFileSystem::ResetCurDir();
    DiskFile f = DiskFileSystem::OpenFile(log_path, true);
    if (!f)
        return false;

    string cur_mess;
    string fmt_log;
    for (uint i = 0; i < MessBox.size(); ++i)
    {
        cur_mess = _str(MessBox[i].Mess).erase('|', ' ');
        fmt_log += MessBox[i].Time + string(cur_mess);
    }

    f.Write(fmt_log.c_str(), (uint)fmt_log.length());
    return true;
}

namespace MapperBind
{
#undef BIND_SERVER
#undef BIND_CLIENT
#undef BIND_MAPPER
#undef BIND_CLASS
#undef BIND_ASSERT
#undef BIND_DUMMY_DATA
#define BIND_MAPPER
#define BIND_CLASS FOMapper::SScriptFunc::
#define BIND_ASSERT(x) \
    if ((x) < 0) \
    { \
        WriteLog("Bind error, line {}.\n", __LINE__); \
        errors++; \
    }
#include "ScriptBind_Include.h"
} // namespace MapperBind

bool FOMapper::InitScriptSystem()
{
    WriteLog("Script system initialization...\n");

    // Init
    ScriptPragmaCallback* pragma_callback = new ScriptPragmaCallback(PRAGMA_MAPPER, &ProtoMngr, nullptr);
    if (!ScriptSys.Init(pragma_callback, "MAPPER"))
    {
        WriteLog("Script system initialization fail.\n");
        return false;
    }
    ScriptSys.SetExceptionCallback([](const string& str) {
        CreateDump("ScriptException", str);
        MessageBox::ShowErrorMessage(str, "");
        exit(1);
    });

    // Bind vars and functions, look bind.h
    asIScriptEngine* engine = ScriptSys.GetEngine();
    PropertyRegistrator** registrators = pragma_callback->GetPropertyRegistrators();
    int errors = MapperBind::Bind(engine, registrators);
    if (errors)
        return false;

    // Load scripts
    ScriptSys.Undef("");
    ScriptSys.Define("__MAPPER");
    ScriptSys.Define(_str("__VERSION {}", FO_VERSION));
    if (!ScriptSys.ReloadScripts("Mapper"))
    {
        WriteLog("Invalid mapper scripts.\n");
        return false;
    }

#define BIND_INTERNAL_EVENT(name) MapperFunctions.name = ScriptSys.FindInternalEvent("Event" #name)
#define BIND_INTERNAL_EVENT_CLIENT(name) ClientFunctions.name = ScriptSys.FindInternalEvent("Event" #name)
    BIND_INTERNAL_EVENT(Start);
    BIND_INTERNAL_EVENT(Finish);
    BIND_INTERNAL_EVENT(Loop);
    BIND_INTERNAL_EVENT(ConsoleMessage);
    BIND_INTERNAL_EVENT(RenderIface);
    BIND_INTERNAL_EVENT_CLIENT(RenderMap);
    BIND_INTERNAL_EVENT(MouseDown);
    BIND_INTERNAL_EVENT(MouseUp);
    BIND_INTERNAL_EVENT(MouseMove);
    BIND_INTERNAL_EVENT(KeyDown);
    BIND_INTERNAL_EVENT(KeyUp);
    BIND_INTERNAL_EVENT(InputLost);
    BIND_INTERNAL_EVENT_CLIENT(CritterAnimation);
    BIND_INTERNAL_EVENT_CLIENT(CritterAnimationSubstitute);
    BIND_INTERNAL_EVENT_CLIENT(CritterAnimationFallout);
    BIND_INTERNAL_EVENT(MapLoad);
    BIND_INTERNAL_EVENT(MapSave);
    BIND_INTERNAL_EVENT(InspectorProperties);
#undef BIND_INTERNAL_EVENT
#undef BIND_INTERNAL_EVENT_CLIENT

    GlobalVars::SetPropertyRegistrator(registrators[0]);
    SAFEDEL(Globals);
    Globals = new GlobalVars();
    CritterView::SetPropertyRegistrator(registrators[1]);
    ProtoCritter::SetPropertyRegistrator(registrators[1]);
    ItemView::SetPropertyRegistrator(registrators[2]);
    ProtoItem::SetPropertyRegistrator(registrators[2]);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("IsColorize", OnSetItemFlags);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("IsBadItem", OnSetItemFlags);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("IsShootThru", OnSetItemFlags);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("IsLightThru", OnSetItemFlags);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("IsNoBlock", OnSetItemFlags);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("IsLight", OnSetItemSomeLight);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("LightIntensity", OnSetItemSomeLight);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("LightDistance", OnSetItemSomeLight);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("LightFlags", OnSetItemSomeLight);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("LightColor", OnSetItemSomeLight);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("PicMap", OnSetItemPicMap);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("OffsetX", OnSetItemOffsetXY);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("OffsetY", OnSetItemOffsetXY);
    ItemView::PropertiesRegistrator->SetNativeSetCallback("Opened", OnSetItemOpened);
    MapView::SetPropertyRegistrator(registrators[3]);
    ProtoMap::SetPropertyRegistrator(registrators[3]);
    LocationView::SetPropertyRegistrator(registrators[4]);
    ProtoLocation::SetPropertyRegistrator(registrators[4]);

    if (!ScriptSys.PostInitScriptSystem())
    {
        WriteLog("Post init fail.\n");
        return false;
    }

    if (!ScriptSys.RunModuleInitFunctions())
    {
        WriteLog("Run module init functions fail.\n");
        return false;
    }

    WriteLog("Script system initialization complete.\n");
    return true;
}

void FOMapper::RunStartScript()
{
    ScriptSys.RaiseInternalEvent(MapperFunctions.Start);
}

void FOMapper::RunMapLoadScript(MapView* map)
{
    RUNTIME_ASSERT(map);
    ScriptSys.RaiseInternalEvent(MapperFunctions.MapLoad, map);
}

void FOMapper::RunMapSaveScript(MapView* map)
{
    RUNTIME_ASSERT(map);
    ScriptSys.RaiseInternalEvent(MapperFunctions.MapSave, map);
}

void FOMapper::DrawIfaceLayer(uint layer)
{
    SpritesCanDraw = true;
    ScriptSys.RaiseInternalEvent(MapperFunctions.RenderIface, layer);
    SpritesCanDraw = false;
}

void FOMapper::OnSetItemFlags(Entity* entity, Property* prop, void* cur_value, void* old_value)
{
    // IsColorize, IsBadItem, IsShootThru, IsLightThru, IsNoBlock

    ItemView* item = (ItemView*)entity;
    if (item->GetAccessory() == ITEM_ACCESSORY_HEX && Self->HexMngr.IsMapLoaded())
    {
        ItemHexView* hex_item = (ItemHexView*)item;
        bool rebuild_cache = false;
        if (prop == ItemView::PropertyIsColorize)
            hex_item->RefreshAlpha();
        else if (prop == ItemView::PropertyIsBadItem)
            hex_item->SetSprite(nullptr);
        else if (prop == ItemView::PropertyIsShootThru)
            rebuild_cache = true;
        else if (prop == ItemView::PropertyIsLightThru)
            Self->HexMngr.RebuildLight(), rebuild_cache = true;
        else if (prop == ItemView::PropertyIsNoBlock)
            rebuild_cache = true;
        if (rebuild_cache)
            Self->HexMngr.GetField(hex_item->GetHexX(), hex_item->GetHexY()).ProcessCache();
    }
}

void FOMapper::OnSetItemSomeLight(Entity* entity, Property* prop, void* cur_value, void* old_value)
{
    // IsLight, LightIntensity, LightDistance, LightFlags, LightColor

    if (Self->HexMngr.IsMapLoaded())
        Self->HexMngr.RebuildLight();
}

void FOMapper::OnSetItemPicMap(Entity* entity, Property* prop, void* cur_value, void* old_value)
{
    ItemView* item = (ItemView*)entity;

    if (item->GetAccessory() == ITEM_ACCESSORY_HEX)
    {
        ItemHexView* hex_item = (ItemHexView*)item;
        hex_item->RefreshAnim();
    }
}

void FOMapper::OnSetItemOffsetXY(Entity* entity, Property* prop, void* cur_value, void* old_value)
{
    // OffsetX, OffsetY

    ItemView* item = (ItemView*)entity;

    if (item->GetAccessory() == ITEM_ACCESSORY_HEX && Self->HexMngr.IsMapLoaded())
    {
        ItemHexView* hex_item = (ItemHexView*)item;
        hex_item->SetAnimOffs();
        Self->HexMngr.ProcessHexBorders(hex_item);
    }
}

void FOMapper::OnSetItemOpened(Entity* entity, Property* prop, void* cur_value, void* old_value)
{
    ItemView* item = (ItemView*)entity;
    bool cur = *(bool*)cur_value;
    bool old = *(bool*)old_value;

    if (item->GetIsCanOpen())
    {
        ItemHexView* hex_item = (ItemHexView*)item;
        if (!old && cur)
            hex_item->SetAnimFromStart();
        if (old && !cur)
            hex_item->SetAnimFromEnd();
    }
}

ItemView* FOMapper::SScriptFunc::Item_AddChild(ItemView* item, hash pid)
{
    ProtoItem* proto_item = Self->ProtoMngr.GetProtoItem(pid);
    if (!proto_item || proto_item->IsStatic())
        SCRIPT_ERROR_R0("Added child is not item.");

    return Self->AddItem(pid, 0, 0, item);
}

ItemView* FOMapper::SScriptFunc::Crit_AddChild(CritterView* cr, hash pid)
{
    ProtoItem* proto_item = Self->ProtoMngr.GetProtoItem(pid);
    if (!proto_item || proto_item->IsStatic())
        SCRIPT_ERROR_R0("Added child is not item.");

    return Self->AddItem(pid, 0, 0, cr);
}

CScriptArray* FOMapper::SScriptFunc::Item_GetChildren(ItemView* item)
{
    ItemViewVec children;
    // Todo: need attention!
    // item->ContGetItems(children, 0);
    return Self->ScriptSys.CreateArrayRef("Item[]", children);
}

CScriptArray* FOMapper::SScriptFunc::Crit_GetChildren(CritterView* cr)
{
    return Self->ScriptSys.CreateArrayRef("Item[]", cr->InvItems);
}

ItemView* FOMapper::SScriptFunc::Global_AddItem(hash pid, ushort hx, ushort hy)
{
    if (hx >= Self->HexMngr.GetWidth() || hy >= Self->HexMngr.GetHeight())
        SCRIPT_ERROR_R0("Invalid hex args.");
    ProtoItem* proto = Self->ProtoMngr.GetProtoItem(pid);
    if (!proto)
        SCRIPT_ERROR_R0("Invalid item prototype.");

    return Self->AddItem(pid, hx, hy, nullptr);
}

CritterView* FOMapper::SScriptFunc::Global_AddCritter(hash pid, ushort hx, ushort hy)
{
    if (hx >= Self->HexMngr.GetWidth() || hy >= Self->HexMngr.GetHeight())
        SCRIPT_ERROR_R0("Invalid hex args.");
    ProtoCritter* proto = Self->ProtoMngr.GetProtoCritter(pid);
    if (!proto)
        SCRIPT_ERROR_R0("Invalid critter prototype.");

    return Self->AddCritter(pid, hx, hy);
}

ItemView* FOMapper::SScriptFunc::Global_GetItemByHex(ushort hx, ushort hy)
{
    return Self->HexMngr.GetItem(hx, hy, 0);
}

CScriptArray* FOMapper::SScriptFunc::Global_GetItemsByHex(ushort hx, ushort hy)
{
    ItemHexViewVec items;
    Self->HexMngr.GetItems(hx, hy, items);
    return Self->ScriptSys.CreateArrayRef("Item[]", items);
}

CritterView* FOMapper::SScriptFunc::Global_GetCritterByHex(ushort hx, ushort hy, int find_type)
{
    CritterViewVec critters_;
    Self->HexMngr.GetCritters(hx, hy, critters_, find_type);
    return !critters_.empty() ? critters_[0] : nullptr;
}

CScriptArray* FOMapper::SScriptFunc::Global_GetCrittersByHex(ushort hx, ushort hy, int find_type)
{
    CritterViewVec critters;
    Self->HexMngr.GetCritters(hx, hy, critters, find_type);
    return Self->ScriptSys.CreateArrayRef("Critter[]", critters);
}

void FOMapper::SScriptFunc::Global_MoveEntity(Entity* entity, ushort hx, ushort hy)
{
    if (hx >= Self->HexMngr.GetWidth())
        hx = Self->HexMngr.GetWidth() - 1;
    if (hy >= Self->HexMngr.GetHeight())
        hy = Self->HexMngr.GetHeight() - 1;
    Self->MoveEntity(entity, hx, hy);
}

void FOMapper::SScriptFunc::Global_DeleteEntity(Entity* entity)
{
    Self->DeleteEntity(entity);
}

void FOMapper::SScriptFunc::Global_DeleteEntities(CScriptArray* entities)
{
    for (int i = 0, j = entities->GetSize(); i < j; i++)
    {
        Entity* entity = *(Entity**)entities->At(i);
        if (entity && !entity->IsDestroyed)
            Self->DeleteEntity(entity);
    }
}

void FOMapper::SScriptFunc::Global_SelectEntity(Entity* entity, bool set)
{
    if (!entity)
        SCRIPT_ERROR_R("Entity arg is null.");
    if (entity->IsDestroyed)
        SCRIPT_ERROR_R("Entity arg is destroyed.");

    if (set)
        Self->SelectAdd(entity);
    else
        Self->SelectErase(entity);
}

void FOMapper::SScriptFunc::Global_SelectEntities(CScriptArray* entities, bool set)
{
    for (int i = 0, j = entities->GetSize(); i < j; i++)
    {
        Entity* entity = *(Entity**)entities->At(i);
        if (entity)
        {
            if (entity->IsDestroyed)
                SCRIPT_ERROR_R("Entity in array is destroyed.");

            if (set)
                Self->SelectAdd(entity);
            else
                Self->SelectErase(entity);
        }
    }
}

Entity* FOMapper::SScriptFunc::Global_GetSelectedEntity()
{
    return Self->SelectedEntities.size() ? Self->SelectedEntities[0] : nullptr;
}

CScriptArray* FOMapper::SScriptFunc::Global_GetSelectedEntities()
{
    EntityVec entities;
    entities.reserve(Self->SelectedEntities.size());
    for (uint i = 0, j = (uint)Self->SelectedEntities.size(); i < j; i++)
        entities.push_back(Self->SelectedEntities[i]);
    return Self->ScriptSys.CreateArrayRef("Entity[]", entities);
}

uint FOMapper::SScriptFunc::Global_GetTilesCount(ushort hx, ushort hy, bool roof)
{
    if (!Self->HexMngr.IsMapLoaded())
        SCRIPT_ERROR_R0("Map not loaded.");
    if (hx >= Self->HexMngr.GetWidth())
        SCRIPT_ERROR_R0("Invalid hex x arg.");
    if (hy >= Self->HexMngr.GetHeight())
        SCRIPT_ERROR_R0("Invalid hex y arg.");

    MapTileVec& tiles = Self->HexMngr.GetTiles(hx, hy, roof);
    return (uint)tiles.size();
}

void FOMapper::SScriptFunc::Global_DeleteTile(ushort hx, ushort hy, bool roof, int layer)
{
    if (!Self->HexMngr.IsMapLoaded())
        SCRIPT_ERROR_R("Map not loaded.");
    if (hx >= Self->HexMngr.GetWidth())
        SCRIPT_ERROR_R("Invalid hex x arg.");
    if (hy >= Self->HexMngr.GetHeight())
        SCRIPT_ERROR_R("Invalid hex y arg.");

    bool deleted = false;
    MapTileVec& tiles = Self->HexMngr.GetTiles(hx, hy, roof);
    Field& f = Self->HexMngr.GetField(hx, hy);
    if (layer < 0)
    {
        deleted = !tiles.empty();
        tiles.clear();
        while (f.GetTilesCount(roof))
            f.EraseTile(0, roof);
    }
    else
    {
        for (size_t i = 0, j = tiles.size(); i < j; i++)
        {
            if (tiles[i].Layer == layer)
            {
                tiles.erase(tiles.begin() + i);
                f.EraseTile((uint)i, roof);
                deleted = true;
                break;
            }
        }
    }

    if (deleted)
    {
        if (roof)
            Self->HexMngr.RebuildRoof();
        else
            Self->HexMngr.RebuildTiles();
    }
}

hash FOMapper::SScriptFunc::Global_GetTileHash(ushort hx, ushort hy, bool roof, int layer)
{
    if (!Self->HexMngr.IsMapLoaded())
        SCRIPT_ERROR_R0("Map not loaded.");
    if (hx >= Self->HexMngr.GetWidth())
        SCRIPT_ERROR_R0("Invalid hex x arg.");
    if (hy >= Self->HexMngr.GetHeight())
        SCRIPT_ERROR_R0("Invalid hex y arg.");

    MapTileVec& tiles = Self->HexMngr.GetTiles(hx, hy, roof);
    for (size_t i = 0, j = tiles.size(); i < j; i++)
    {
        if (tiles[i].Layer == layer)
            return tiles[i].Name;
    }
    return 0;
}

void FOMapper::SScriptFunc::Global_AddTileHash(
    ushort hx, ushort hy, int ox, int oy, int layer, bool roof, hash pic_hash)
{
    if (!Self->HexMngr.IsMapLoaded())
        SCRIPT_ERROR_R("Map not loaded.");
    if (hx >= Self->HexMngr.GetWidth())
        SCRIPT_ERROR_R("Invalid hex x arg.");
    if (hy >= Self->HexMngr.GetHeight())
        SCRIPT_ERROR_R("Invalid hex y arg.");

    if (!pic_hash)
        return;

    layer = CLAMP(layer, DRAW_ORDER_TILE, DRAW_ORDER_TILE_END);

    Self->HexMngr.SetTile(pic_hash, hx, hy, ox, oy, layer, roof, false);
}

string FOMapper::SScriptFunc::Global_GetTileName(ushort hx, ushort hy, bool roof, int layer)
{
    if (!Self->HexMngr.IsMapLoaded())
        SCRIPT_ERROR_R0("Map not loaded.");
    if (hx >= Self->HexMngr.GetWidth())
        SCRIPT_ERROR_R0("Invalid hex x arg.");
    if (hy >= Self->HexMngr.GetHeight())
        SCRIPT_ERROR_R0("Invalid hex y arg.");

    MapTileVec& tiles = Self->HexMngr.GetTiles(hx, hy, roof);
    for (size_t i = 0, j = tiles.size(); i < j; i++)
    {
        if (tiles[i].Layer == layer)
            return _str().parseHash(tiles[i].Name);
    }
    return "";
}

void FOMapper::SScriptFunc::Global_AddTileName(
    ushort hx, ushort hy, int ox, int oy, int layer, bool roof, string pic_name)
{
    if (!Self->HexMngr.IsMapLoaded())
        SCRIPT_ERROR_R("Map not loaded.");
    if (hx >= Self->HexMngr.GetWidth())
        SCRIPT_ERROR_R("Invalid hex x arg.");
    if (hy >= Self->HexMngr.GetHeight())
        SCRIPT_ERROR_R("Invalid hex y arg.");

    if (pic_name.empty())
        return;

    layer = CLAMP(layer, DRAW_ORDER_TILE, DRAW_ORDER_TILE_END);

    hash pic_hash = _str(pic_name).toHash();
    Self->HexMngr.SetTile(pic_hash, hx, hy, ox, oy, layer, roof, false);
}

void FOMapper::SScriptFunc::Global_AllowSlot(uchar index, bool enable_send)
{
    //
}

void FOMapper::SScriptFunc::Global_SetPropertyGetCallback(asIScriptGeneric* gen)
{
    int prop_enum_value = gen->GetArgDWord(0);
    void* ref = gen->GetArgAddress(1);
    gen->SetReturnByte(0);
    RUNTIME_ASSERT(ref);

    Property* prop = GlobalVars::PropertiesRegistrator->FindByEnum(prop_enum_value);
    prop = (prop ? prop : CritterView::PropertiesRegistrator->FindByEnum(prop_enum_value));
    prop = (prop ? prop : ItemView::PropertiesRegistrator->FindByEnum(prop_enum_value));
    prop = (prop ? prop : MapView::PropertiesRegistrator->FindByEnum(prop_enum_value));
    prop = (prop ? prop : LocationView::PropertiesRegistrator->FindByEnum(prop_enum_value));
    prop = (prop ? prop : GlobalVars::PropertiesRegistrator->FindByEnum(prop_enum_value));
    if (!prop)
        SCRIPT_ERROR_R("Property '{}' not found.", _str().parseHash(prop_enum_value));

    string result = prop->SetGetCallback(*(asIScriptFunction**)ref);
    if (result != "")
        SCRIPT_ERROR_R(result.c_str());

    gen->SetReturnByte(1);
}

MapView* FOMapper::SScriptFunc::Global_LoadMap(string file_name)
{
    ProtoMap* pmap = new ProtoMap(_str(file_name).toHash());
    // Todo: need attention!
    // if (!pmap->EditorLoad(Self->ServerFileMngr, Self->ProtoMngr, Self->SprMngr, Self->ResMngr))
    //     return nullptr;

    MapView* map = new MapView(0, pmap);
    Self->LoadedMaps.push_back(map);
    Self->RunMapLoadScript(map);
    return map;
}

void FOMapper::SScriptFunc::Global_UnloadMap(MapView* map)
{
    if (!map)
        SCRIPT_ERROR_R("Proto map arg nullptr.");

    auto it = std::find(Self->LoadedMaps.begin(), Self->LoadedMaps.end(), map);
    if (it != Self->LoadedMaps.end())
        Self->LoadedMaps.erase(it);

    if (map == Self->ActiveMap)
    {
        Self->HexMngr.UnloadMap();
        Self->SelectedEntities.clear();
        Self->ActiveMap = nullptr;
    }

    map->Proto->Release();
    map->Release();
}

bool FOMapper::SScriptFunc::Global_SaveMap(MapView* map, string custom_name)
{
    if (!map)
        SCRIPT_ERROR_R0("Proto map arg nullptr.");

    // Todo: need attention!
    //((ProtoMap*)map->Proto)->EditorSave(Self->ServerFileMngr, custom_name);
    Self->RunMapSaveScript(map);
    return true;
}

bool FOMapper::SScriptFunc::Global_ShowMap(MapView* map)
{
    if (!map)
        SCRIPT_ERROR_R0("Proto map arg nullptr.");

    if (Self->ActiveMap == map)
        return true;

    Self->SelectClear();
    if (!Self->HexMngr.SetProtoMap(*(ProtoMap*)map->Proto))
        return false;
    Self->HexMngr.FindSetCenter(map->GetWorkHexX(), map->GetWorkHexY());
    Self->ActiveMap = map;
    return true;
}

CScriptArray* FOMapper::SScriptFunc::Global_GetLoadedMaps(int& index)
{
    index = -1;
    for (int i = 0, j = (int)Self->LoadedMaps.size(); i < j; i++)
    {
        MapView* map = Self->LoadedMaps[i];
        if (map == Self->ActiveMap)
            index = i;
    }
    return Self->ScriptSys.CreateArrayRef("Map[]", Self->LoadedMaps);
}

CScriptArray* FOMapper::SScriptFunc::Global_GetMapFileNames(string dir)
{
    CScriptArray* names = Self->ScriptSys.CreateArray("string[]");
    FileCollection map_files = Self->ServerFileMngr.FilterFiles("fomap", dir);
    while (map_files.MoveNext())
    {
        FileHeader file_header = map_files.GetCurFileHeader();
        string fname = file_header.GetName();
        names->InsertLast(&fname);
    }
    return names;
}

void FOMapper::SScriptFunc::Global_ResizeMap(ushort width, ushort height)
{
    if (!Self->HexMngr.IsMapLoaded())
        SCRIPT_ERROR_R("Map not loaded.");

    RUNTIME_ASSERT(Self->ActiveMap);
    ProtoMap* pmap = (ProtoMap*)Self->ActiveMap->Proto;

    // Unload current
    Self->HexMngr.GetProtoMap(*pmap);
    Self->SelectClear();
    Self->HexMngr.UnloadMap();

    // Check size
    int maxhx = CLAMP(width, MAXHEX_MIN, MAXHEX_MAX);
    int maxhy = CLAMP(height, MAXHEX_MIN, MAXHEX_MAX);
    int old_maxhx = pmap->GetWidth();
    int old_maxhy = pmap->GetHeight();
    maxhx = CLAMP(maxhx, MAXHEX_MIN, MAXHEX_MAX);
    maxhy = CLAMP(maxhy, MAXHEX_MIN, MAXHEX_MAX);
    if (pmap->GetWorkHexX() >= maxhx)
        pmap->SetWorkHexX(maxhx - 1);
    if (pmap->GetWorkHexY() >= maxhy)
        pmap->SetWorkHexY(maxhy - 1);
    pmap->SetWidth(maxhx);
    pmap->SetHeight(maxhy);

    // Delete truncated entities
    if (maxhx < old_maxhx || maxhy < old_maxhy)
    {
        // Todo: need attention!
        /*for (auto it = pmap->AllEntities.begin(); it != pmap->AllEntities.end();)
        {
            Entity* entity = *it;
            int hx = (entity->Type == EntityType::CritterView ? ((CritterView*)entity)->GetHexX() :
                                                                ((ItemHexView*)entity)->GetHexX());
            int hy = (entity->Type == EntityType::CritterView ? ((CritterView*)entity)->GetHexY() :
                                                                ((ItemHexView*)entity)->GetHexY());
            if (hx >= maxhx || hy >= maxhy)
            {
                entity->Release();
                it = pmap->AllEntities.erase(it);
            }
            else
            {
                ++it;
            }
        }*/
    }

    // Delete truncated tiles
    if (maxhx < old_maxhx || maxhy < old_maxhy)
    {
        // Todo: need attention!
        /*for (auto it = pmap->Tiles.begin(); it != pmap->Tiles.end();)
        {
            MapTile& tile = *it;
            if (tile.HexX >= maxhx || tile.HexY >= maxhy)
                it = pmap->Tiles.erase(it);
            else
                ++it;
        }*/
    }

    // Update visibility
    Self->HexMngr.SetProtoMap(*pmap);
    Self->HexMngr.FindSetCenter(pmap->GetWorkHexX(), pmap->GetWorkHexY());
}

uint FOMapper::SScriptFunc::Global_TabGetTileDirs(int tab, CScriptArray* dir_names, CScriptArray* include_subdirs)
{
    if (tab < 0 || tab >= TAB_COUNT)
        SCRIPT_ERROR_R0("Wrong tab arg.");

    TileTab& ttab = Self->TabsTiles[tab];
    if (dir_names)
    {
        asUINT i = dir_names->GetSize();
        dir_names->Resize(dir_names->GetSize() + (uint)ttab.TileDirs.size());
        for (uint k = 0, l = (uint)ttab.TileDirs.size(); k < l; k++, i++)
        {
            string& p = *(string*)dir_names->At(i);
            p = ttab.TileDirs[k];
        }
    }
    if (include_subdirs)
        Self->ScriptSys.AppendVectorToArray(ttab.TileSubDirs, include_subdirs);
    return (uint)ttab.TileDirs.size();
}

uint FOMapper::SScriptFunc::Global_TabGetItemPids(int tab, string sub_tab, CScriptArray* item_pids)
{
    if (tab < 0 || tab >= TAB_COUNT)
        SCRIPT_ERROR_R0("Wrong tab arg.");
    if (!sub_tab.empty() && !Self->Tabs[tab].count(sub_tab))
        return 0;

    SubTab& stab = Self->Tabs[tab][!sub_tab.empty() ? sub_tab : DEFAULT_SUB_TAB];
    if (item_pids)
        Self->ScriptSys.AppendVectorToArray(stab.ItemProtos, item_pids);
    return (uint)stab.ItemProtos.size();
}

uint FOMapper::SScriptFunc::Global_TabGetCritterPids(int tab, string sub_tab, CScriptArray* critter_pids)
{
    if (tab < 0 || tab >= TAB_COUNT)
        SCRIPT_ERROR_R0("Wrong tab arg.");
    if (!sub_tab.empty() && !Self->Tabs[tab].count(sub_tab))
        return 0;

    SubTab& stab = Self->Tabs[tab][!sub_tab.empty() ? sub_tab : DEFAULT_SUB_TAB];
    if (critter_pids)
        Self->ScriptSys.AppendVectorToArray(stab.NpcProtos, critter_pids);
    return (uint)stab.NpcProtos.size();
}

void FOMapper::SScriptFunc::Global_TabSetTileDirs(int tab, CScriptArray* dir_names, CScriptArray* include_subdirs)
{
    if (tab < 0 || tab >= TAB_COUNT)
        SCRIPT_ERROR_R("Wrong tab arg.");
    if (dir_names && include_subdirs && dir_names->GetSize() != include_subdirs->GetSize())
        return;

    TileTab& ttab = Self->TabsTiles[tab];
    ttab.TileDirs.clear();
    ttab.TileSubDirs.clear();

    if (dir_names)
    {
        for (uint i = 0, j = dir_names->GetSize(); i < j; i++)
        {
            string& name = *(string*)dir_names->At(i);
            if (!name.empty())
            {
                ttab.TileDirs.push_back(name);
                ttab.TileSubDirs.push_back(include_subdirs ? *(bool*)include_subdirs->At(i) : false);
            }
        }
    }

    Self->RefreshTiles(tab);
}

void FOMapper::SScriptFunc::Global_TabSetItemPids(int tab, string sub_tab, CScriptArray* item_pids)
{
    if (tab < 0 || tab >= TAB_COUNT)
        SCRIPT_ERROR_R("Wrong tab arg.");
    if (sub_tab.empty() || sub_tab == DEFAULT_SUB_TAB)
        return;

    // Add protos to sub tab
    if (item_pids && item_pids->GetSize())
    {
        ProtoItemVec proto_items;
        for (int i = 0, j = item_pids->GetSize(); i < j; i++)
        {
            hash pid = *(hash*)item_pids->At(i);
            ProtoItem* proto_item = Self->ProtoMngr.GetProtoItem(pid);
            if (proto_item)
                proto_items.push_back(proto_item);
        }

        if (proto_items.size())
        {
            SubTab& stab = Self->Tabs[tab][sub_tab];
            stab.ItemProtos = proto_items;
        }
    }
    // Delete sub tab
    else
    {
        auto it = Self->Tabs[tab].find(sub_tab);
        if (it != Self->Tabs[tab].end())
        {
            if (Self->TabsActive[tab] == &it->second)
                Self->TabsActive[tab] = nullptr;
            Self->Tabs[tab].erase(it);
        }
    }

    // Recalculate whole pids
    SubTab& stab_default = Self->Tabs[tab][DEFAULT_SUB_TAB];
    stab_default.ItemProtos.clear();
    for (auto it = Self->Tabs[tab].begin(), end = Self->Tabs[tab].end(); it != end; ++it)
    {
        SubTab& stab = it->second;
        if (&stab == &stab_default)
            continue;
        for (uint i = 0, j = (uint)stab.ItemProtos.size(); i < j; i++)
            stab_default.ItemProtos.push_back(stab.ItemProtos[i]);
    }
    if (!Self->TabsActive[tab])
        Self->TabsActive[tab] = &stab_default;

    // Refresh
    Self->RefreshCurProtos();
}

void FOMapper::SScriptFunc::Global_TabSetCritterPids(int tab, string sub_tab, CScriptArray* critter_pids)
{
    if (tab < 0 || tab >= TAB_COUNT)
        SCRIPT_ERROR_R("Wrong tab arg.");
    if (sub_tab.empty() || sub_tab == DEFAULT_SUB_TAB)
        return;

    // Add protos to sub tab
    if (critter_pids && critter_pids->GetSize())
    {
        ProtoCritterVec cr_protos;
        for (int i = 0, j = critter_pids->GetSize(); i < j; i++)
        {
            hash pid = *(hash*)critter_pids->At(i);
            ProtoCritter* cr_data = Self->ProtoMngr.GetProtoCritter(pid);
            if (cr_data)
                cr_protos.push_back(cr_data);
        }

        if (cr_protos.size())
        {
            SubTab& stab = Self->Tabs[tab][sub_tab];
            stab.NpcProtos = cr_protos;
        }
    }
    // Delete sub tab
    else
    {
        auto it = Self->Tabs[tab].find(sub_tab);
        if (it != Self->Tabs[tab].end())
        {
            if (Self->TabsActive[tab] == &it->second)
                Self->TabsActive[tab] = nullptr;
            Self->Tabs[tab].erase(it);
        }
    }

    // Recalculate whole pids
    SubTab& stab_default = Self->Tabs[tab][DEFAULT_SUB_TAB];
    stab_default.NpcProtos.clear();
    for (auto it = Self->Tabs[tab].begin(), end = Self->Tabs[tab].end(); it != end; ++it)
    {
        SubTab& stab = it->second;
        if (&stab == &stab_default)
            continue;
        for (uint i = 0, j = (uint)stab.NpcProtos.size(); i < j; i++)
            stab_default.NpcProtos.push_back(stab.NpcProtos[i]);
    }
    if (!Self->TabsActive[tab])
        Self->TabsActive[tab] = &stab_default;

    // Refresh
    Self->RefreshCurProtos();
}

void FOMapper::SScriptFunc::Global_TabDelete(int tab)
{
    if (tab < 0 || tab >= TAB_COUNT)
        SCRIPT_ERROR_R("Wrong tab arg.");

    Self->Tabs[tab].clear();
    SubTab& stab_default = Self->Tabs[tab][DEFAULT_SUB_TAB];
    Self->TabsActive[tab] = &stab_default;
}

void FOMapper::SScriptFunc::Global_TabSelect(int tab, string sub_tab, bool show)
{
    if (tab < 0 || tab >= INT_MODE_COUNT)
        SCRIPT_ERROR_R("Wrong tab arg.");

    if (show)
        Self->IntSetMode(tab);

    if (tab < 0 || tab >= TAB_COUNT)
        return;

    auto it = Self->Tabs[tab].find(!sub_tab.empty() ? sub_tab : DEFAULT_SUB_TAB);
    if (it != Self->Tabs[tab].end())
        Self->TabsActive[tab] = &it->second;
}

void FOMapper::SScriptFunc::Global_TabSetName(int tab, string tab_name)
{
    if (tab < 0 || tab >= INT_MODE_COUNT)
        SCRIPT_ERROR_R("Wrong tab arg.");

    Self->TabsName[tab] = tab_name;
}

void FOMapper::SScriptFunc::Global_MoveScreenToHex(ushort hx, ushort hy, uint speed, bool can_stop)
{
    if (hx >= Self->HexMngr.GetWidth() || hy >= Self->HexMngr.GetHeight())
        SCRIPT_ERROR_R("Invalid hex args.");
    if (!Self->HexMngr.IsMapLoaded())
        SCRIPT_ERROR_R("Map is not loaded.");

    if (!speed)
        Self->HexMngr.FindSetCenter(hx, hy);
    else
        Self->HexMngr.ScrollToHex(hx, hy, (float)speed / 1000.0f, can_stop);
}

void FOMapper::SScriptFunc::Global_MoveScreenOffset(int ox, int oy, uint speed, bool can_stop)
{
    if (!Self->HexMngr.IsMapLoaded())
        SCRIPT_ERROR_R("Map is not loaded.");

    Self->HexMngr.ScrollOffset(ox, oy, (float)speed / 1000.0f, can_stop);
}

void FOMapper::SScriptFunc::Global_MoveHexByDir(ushort& hx, ushort& hy, uchar dir, uint steps)
{
    if (!Self->HexMngr.IsMapLoaded())
        SCRIPT_ERROR_R("Map not loaded.");
    if (dir >= Self->Settings.MapDirCount)
        SCRIPT_ERROR_R("Invalid dir arg.");
    if (!steps)
        SCRIPT_ERROR_R("Steps arg is zero.");
    int hx_ = hx, hy_ = hy;
    if (steps > 1)
    {
        for (uint i = 0; i < steps; i++)
            Self->GeomHelper.MoveHexByDirUnsafe(hx_, hy_, dir);
    }
    else
    {
        Self->GeomHelper.MoveHexByDirUnsafe(hx_, hy_, dir);
    }
    if (hx_ < 0)
        hx_ = 0;
    if (hy_ < 0)
        hy_ = 0;
    hx = hx_;
    hy = hy_;
}

string FOMapper::SScriptFunc::Global_GetIfaceIniStr(string key)
{
    return Self->IfaceIni.GetStr("", key, "");
}

void FOMapper::SScriptFunc::Global_Message(string msg)
{
    Self->AddMess(msg.c_str());
}

void FOMapper::SScriptFunc::Global_MessageMsg(int text_msg, uint str_num)
{
    if (text_msg >= TEXTMSG_COUNT)
        SCRIPT_ERROR_R("Invalid text msg arg.");
    Self->AddMess(Self->CurLang.Msg[text_msg].GetStr(str_num).c_str());
}

void FOMapper::SScriptFunc::Global_MapMessage(
    string text, ushort hx, ushort hy, uint ms, uint color, bool fade, int ox, int oy)
{
    FOMapper::MapText t;
    t.HexX = hx;
    t.HexY = hy;
    t.Color = (color ? color : COLOR_TEXT);
    t.Fade = fade;
    t.StartTick = Timer::FastTick();
    t.Tick = ms;
    t.Text = text;
    t.Pos = Self->HexMngr.GetRectForText(hx, hy);
    t.EndPos = Rect(t.Pos, ox, oy);
    auto it = std::find(Self->GameMapTexts.begin(), Self->GameMapTexts.end(), t);
    if (it != Self->GameMapTexts.end())
        Self->GameMapTexts.erase(it);
    Self->GameMapTexts.push_back(t);
}

string FOMapper::SScriptFunc::Global_GetMsgStr(int text_msg, uint str_num)
{
    if (text_msg >= TEXTMSG_COUNT)
        SCRIPT_ERROR_R0("Invalid text msg arg.");

    return Self->CurLang.Msg[text_msg].GetStr(str_num);
}

string FOMapper::SScriptFunc::Global_GetMsgStrSkip(int text_msg, uint str_num, uint skip_count)
{
    if (text_msg >= TEXTMSG_COUNT)
        SCRIPT_ERROR_R0("Invalid text msg arg.");

    return Self->CurLang.Msg[text_msg].GetStr(str_num, skip_count);
}

uint FOMapper::SScriptFunc::Global_GetMsgStrNumUpper(int text_msg, uint str_num)
{
    if (text_msg >= TEXTMSG_COUNT)
        SCRIPT_ERROR_R0("Invalid text msg arg.");

    return Self->CurLang.Msg[text_msg].GetStrNumUpper(str_num);
}

uint FOMapper::SScriptFunc::Global_GetMsgStrNumLower(int text_msg, uint str_num)
{
    if (text_msg >= TEXTMSG_COUNT)
        SCRIPT_ERROR_R0("Invalid text msg arg.");

    return Self->CurLang.Msg[text_msg].GetStrNumLower(str_num);
}

uint FOMapper::SScriptFunc::Global_GetMsgStrCount(int text_msg, uint str_num)
{
    if (text_msg >= TEXTMSG_COUNT)
        SCRIPT_ERROR_R0("Invalid text msg arg.");

    return Self->CurLang.Msg[text_msg].Count(str_num);
}

bool FOMapper::SScriptFunc::Global_IsMsgStr(int text_msg, uint str_num)
{
    if (text_msg >= TEXTMSG_COUNT)
        SCRIPT_ERROR_R0("Invalid text msg arg.");

    return Self->CurLang.Msg[text_msg].Count(str_num) > 0;
}

string FOMapper::SScriptFunc::Global_ReplaceTextStr(string text, string replace, string str)
{
    size_t pos = text.find(replace, 0);
    if (pos == std::string::npos)
        return text;
    return string(text).replace(pos, replace.length(), text);
}

string FOMapper::SScriptFunc::Global_ReplaceTextInt(string text, string replace, int i)
{
    size_t pos = text.find(replace, 0);
    if (pos == std::string::npos)
        return text;
    return string(text).replace(pos, replace.length(), _str("{}", i));
}

void FOMapper::SScriptFunc::Global_GetHexInPath(
    ushort from_hx, ushort from_hy, ushort& to_hx, ushort& to_hy, float angle, uint dist)
{
    UShortPair pre_block, block;
    Self->HexMngr.TraceBullet(
        from_hx, from_hy, to_hx, to_hy, dist, angle, nullptr, false, nullptr, 0, &block, &pre_block, nullptr, true);
    to_hx = pre_block.first;
    to_hy = pre_block.second;
}

uint FOMapper::SScriptFunc::Global_GetPathLengthHex(
    ushort from_hx, ushort from_hy, ushort to_hx, ushort to_hy, uint cut)
{
    if (from_hx >= Self->HexMngr.GetWidth() || from_hy >= Self->HexMngr.GetHeight())
        SCRIPT_ERROR_R0("Invalid from hexes args.");
    if (to_hx >= Self->HexMngr.GetWidth() || to_hy >= Self->HexMngr.GetHeight())
        SCRIPT_ERROR_R0("Invalid to hexes args.");

    if (cut > 0 && !Self->HexMngr.CutPath(nullptr, from_hx, from_hy, to_hx, to_hy, cut))
        return 0;
    UCharVec steps;
    if (!Self->HexMngr.FindPath(nullptr, from_hx, from_hy, to_hx, to_hy, steps, -1))
        return 0;
    return (uint)steps.size();
}

bool FOMapper::SScriptFunc::Global_GetHexPos(ushort hx, ushort hy, int& x, int& y)
{
    x = y = 0;
    if (Self->HexMngr.IsMapLoaded() && hx < Self->HexMngr.GetWidth() && hy < Self->HexMngr.GetHeight())
    {
        Self->HexMngr.GetHexCurrentPosition(hx, hy, x, y);
        x += Self->Settings.ScrOx + (Self->Settings.MapHexWidth / 2);
        y += Self->Settings.ScrOy + (Self->Settings.MapHexHeight / 2);
        x = (int)(x / Self->Settings.SpritesZoom);
        y = (int)(y / Self->Settings.SpritesZoom);
        return true;
    }
    return false;
}

bool FOMapper::SScriptFunc::Global_GetMonitorHex(int x, int y, ushort& hx, ushort& hy, bool ignore_interface)
{
    ushort hx_, hy_;
    int old_x = Self->Settings.MouseX;
    int old_y = Self->Settings.MouseY;
    Self->Settings.MouseX = x;
    Self->Settings.MouseY = y;
    bool result = Self->GetCurHex(hx_, hy_, ignore_interface);
    Self->Settings.MouseX = old_x;
    Self->Settings.MouseY = old_y;
    if (result)
    {
        hx = hx_;
        hy = hy_;
        return true;
    }
    return false;
}

Entity* FOMapper::SScriptFunc::Global_GetMonitorObject(int x, int y, bool ignore_interface)
{
    if (!Self->HexMngr.IsMapLoaded())
        SCRIPT_ERROR_R0("Map not loaded.");

    if (!ignore_interface && Self->IsCurInInterface())
        return nullptr;

    ItemHexView* item;
    CritterView* cr;
    Self->HexMngr.GetSmthPixel(Self->Settings.MouseX, Self->Settings.MouseY, item, cr);

    Entity* mobj = nullptr;
    if (item)
        mobj = item;
    else if (cr)
        mobj = cr;
    return mobj;
}

void FOMapper::SScriptFunc::Global_AddDataSource(string dat_name)
{
    Self->FileMngr.AddDataSource(dat_name, false);

    for (int tab = 0; tab < TAB_COUNT; tab++)
        Self->RefreshTiles(tab);
}

bool FOMapper::SScriptFunc::Global_LoadFont(int font_index, string font_fname)
{
    bool result;
    if (font_fname.length() > 0 && font_fname[0] == '*')
        result = Self->SprMngr.LoadFontFO(font_index, font_fname.c_str() + 1, false);
    else
        result = Self->SprMngr.LoadFontBMF(font_index, font_fname.c_str());
    if (result)
        Self->SprMngr.BuildFonts();
    return result;
}

void FOMapper::SScriptFunc::Global_SetDefaultFont(int font, uint color)
{
    Self->SprMngr.SetDefaultFont(font, color);
}

/*static int MouseButtonToSdlButton(int button)
{
    if (button == MOUSE_BUTTON_LEFT)
        return SDL_BUTTON_LEFT;
    if (button == MOUSE_BUTTON_RIGHT)
        return SDL_BUTTON_RIGHT;
    if (button == MOUSE_BUTTON_MIDDLE)
        return SDL_BUTTON_MIDDLE;
    if (button == MOUSE_BUTTON_EXT0)
        return SDL_BUTTON(4);
    if (button == MOUSE_BUTTON_EXT1)
        return SDL_BUTTON(5);
    if (button == MOUSE_BUTTON_EXT2)
        return SDL_BUTTON(6);
    if (button == MOUSE_BUTTON_EXT3)
        return SDL_BUTTON(7);
    if (button == MOUSE_BUTTON_EXT4)
        return SDL_BUTTON(8);
    return -1;
}*/

void FOMapper::SScriptFunc::Global_MouseClick(int x, int y, int button)
{
    /*IntVec prev_events = Self->Settings.MainWindowMouseEvents;
    Self->Settings.MainWindowMouseEvents.clear();
    int prev_x = Self->Settings.MouseX;
    int prev_y = Self->Settings.MouseY;
    int last_prev_x = Self->Settings.LastMouseX;
    int last_prev_y = Self->Settings.LastMouseY;
    int prev_cursor = Self->CurMode;
    Self->Settings.MouseX = Self->Settings.LastMouseX = x;
    Self->Settings.MouseY = Self->Settings.LastMouseY = y;
    Self->Settings.MainWindowMouseEvents.push_back(SDL_MOUSEBUTTONDOWN);
    Self->Settings.MainWindowMouseEvents.push_back(MouseButtonToSdlButton(button));
    Self->Settings.MainWindowMouseEvents.push_back(SDL_MOUSEBUTTONUP);
    Self->Settings.MainWindowMouseEvents.push_back(MouseButtonToSdlButton(button));
    Self->ParseMouse();
    Self->Settings.MainWindowMouseEvents = prev_events;
    Self->Settings.MouseX = prev_x;
    Self->Settings.MouseY = prev_y;
    Self->Settings.LastMouseX = last_prev_x;
    Self->Settings.LastMouseY = last_prev_y;*/
}

void FOMapper::SScriptFunc::Global_KeyboardPress(uchar key1, uchar key2, string key1_text, string key2_text)
{
    if (!key1 && !key2)
        return;

    if (key1)
        Self->ProcessInputEvent({InputEvent::KeyDown({(KeyCode)key1, key1_text})});

    if (key2)
    {
        Self->ProcessInputEvent({InputEvent::KeyDown({(KeyCode)key2, key2_text})});
        Self->ProcessInputEvent({InputEvent::KeyUp({(KeyCode)key2})});
    }

    if (key1)
        Self->ProcessInputEvent({InputEvent::KeyUp({(KeyCode)key1})});
}

void FOMapper::SScriptFunc::Global_SetRainAnimation(string fall_anim_name, string drop_anim_name)
{
    Self->HexMngr.SetRainAnimation(!fall_anim_name.empty() ? fall_anim_name.c_str() : nullptr,
        !drop_anim_name.empty() ? drop_anim_name.c_str() : nullptr);
}

void FOMapper::SScriptFunc::Global_ChangeZoom(float target_zoom)
{
    if (target_zoom == Self->Settings.SpritesZoom)
        return;

    if (target_zoom == 1.0f)
    {
        Self->HexMngr.ChangeZoom(0);
    }
    else if (target_zoom > Self->Settings.SpritesZoom)
    {
        while (target_zoom > Self->Settings.SpritesZoom)
        {
            float old_zoom = Self->Settings.SpritesZoom;
            Self->HexMngr.ChangeZoom(1);
            if (Self->Settings.SpritesZoom == old_zoom)
                break;
        }
    }
    else if (target_zoom < Self->Settings.SpritesZoom)
    {
        while (target_zoom < Self->Settings.SpritesZoom)
        {
            float old_zoom = Self->Settings.SpritesZoom;
            Self->HexMngr.ChangeZoom(-1);
            if (Self->Settings.SpritesZoom == old_zoom)
                break;
        }
    }
}

uint FOMapper::SScriptFunc::Global_LoadSprite(string spr_name)
{
    return Self->AnimLoad(spr_name.c_str(), AtlasType::Static);
}

uint FOMapper::SScriptFunc::Global_LoadSpriteHash(uint name_hash)
{
    return Self->AnimLoad(name_hash, AtlasType::Static);
}

int FOMapper::SScriptFunc::Global_GetSpriteWidth(uint spr_id, int spr_index)
{
    AnyFrames* anim = Self->AnimGetFrames(spr_id);
    if (!anim || spr_index >= (int)anim->CntFrm)
        return 0;
    SpriteInfo* si = Self->SprMngr.GetSpriteInfo(spr_index < 0 ? anim->GetCurSprId() : anim->GetSprId(spr_index));
    if (!si)
        return 0;
    return si->Width;
}

int FOMapper::SScriptFunc::Global_GetSpriteHeight(uint spr_id, int spr_index)
{
    AnyFrames* anim = Self->AnimGetFrames(spr_id);
    if (!anim || spr_index >= (int)anim->CntFrm)
        return 0;
    SpriteInfo* si = Self->SprMngr.GetSpriteInfo(spr_index < 0 ? anim->GetCurSprId() : anim->GetSprId(spr_index));
    if (!si)
        return 0;
    return si->Height;
}

uint FOMapper::SScriptFunc::Global_GetSpriteCount(uint spr_id)
{
    AnyFrames* anim = Self->AnimGetFrames(spr_id);
    return anim ? anim->CntFrm : 0;
}

uint FOMapper::SScriptFunc::Global_GetSpriteTicks(uint spr_id)
{
    AnyFrames* anim = Self->AnimGetFrames(spr_id);
    return anim ? anim->Ticks : 0;
}

uint FOMapper::SScriptFunc::Global_GetPixelColor(uint spr_id, int frame_index, int x, int y)
{
    if (!spr_id)
        return 0;

    AnyFrames* anim = Self->AnimGetFrames(spr_id);
    if (!anim || frame_index >= (int)anim->CntFrm)
        return 0;

    uint spr_id_ = (frame_index < 0 ? anim->GetCurSprId() : anim->GetSprId(frame_index));
    return Self->SprMngr.GetPixColor(spr_id_, x, y, false);
}

void FOMapper::SScriptFunc::Global_GetTextInfo(
    string text, int w, int h, int font, int flags, int& tw, int& th, int& lines)
{
    Self->SprMngr.GetTextInfo(w, h, !text.empty() ? text.c_str() : nullptr, font, flags, tw, th, lines);
}

void FOMapper::SScriptFunc::Global_DrawSprite(uint spr_id, int frame_index, int x, int y, uint color, bool offs)
{
    if (!SpritesCanDraw || !spr_id)
        return;
    AnyFrames* anim = Self->AnimGetFrames(spr_id);
    if (!anim || frame_index >= (int)anim->CntFrm)
        return;
    uint spr_id_ = (frame_index < 0 ? anim->GetCurSprId() : anim->GetSprId(frame_index));
    if (offs)
    {
        SpriteInfo* si = Self->SprMngr.GetSpriteInfo(spr_id_);
        if (!si)
            return;
        x += -si->Width / 2 + si->OffsX;
        y += -si->Height + si->OffsY;
    }
    Self->SprMngr.DrawSprite(spr_id_, x, y, COLOR_SCRIPT_SPRITE(color));
}

void FOMapper::SScriptFunc::Global_DrawSpriteSize(
    uint spr_id, int frame_index, int x, int y, int w, int h, bool zoom, uint color, bool offs)
{
    if (!SpritesCanDraw || !spr_id)
        return;
    AnyFrames* anim = Self->AnimGetFrames(spr_id);
    if (!anim || frame_index >= (int)anim->CntFrm)
        return;
    uint spr_id_ = (frame_index < 0 ? anim->GetCurSprId() : anim->GetSprId(frame_index));
    if (offs)
    {
        SpriteInfo* si = Self->SprMngr.GetSpriteInfo(spr_id_);
        if (!si)
            return;
        x += si->OffsX;
        y += si->OffsY;
    }
    Self->SprMngr.DrawSpriteSizeExt(spr_id_, x, y, w, h, zoom, true, true, COLOR_SCRIPT_SPRITE(color));
}

void FOMapper::SScriptFunc::Global_DrawSpritePattern(
    uint spr_id, int frame_index, int x, int y, int w, int h, int spr_width, int spr_height, uint color)
{
    if (!SpritesCanDraw || !spr_id)
        return;
    AnyFrames* anim = Self->AnimGetFrames(spr_id);
    if (!anim || frame_index >= (int)anim->CntFrm)
        return;
    Self->SprMngr.DrawSpritePattern(frame_index < 0 ? anim->GetCurSprId() : anim->GetSprId(frame_index), x, y, w, h,
        spr_width, spr_height, COLOR_SCRIPT_SPRITE(color));
}

void FOMapper::SScriptFunc::Global_DrawText(string text, int x, int y, int w, int h, uint color, int font, int flags)
{
    if (!SpritesCanDraw)
        return;
    if (text.length() == 0)
        return;
    if (w < 0)
        w = -w, x -= w;
    if (h < 0)
        h = -h, y -= h;
    Self->SprMngr.DrawStr(Rect(x, y, x + w, y + h), text.c_str(), flags, COLOR_SCRIPT_TEXT(color), font);
}

void FOMapper::SScriptFunc::Global_DrawPrimitive(int primitive_type, CScriptArray* data)
{
    if (!SpritesCanDraw || data->GetSize() == 0)
        return;

    RenderPrimitiveType prim;
    switch (primitive_type)
    {
    case 0:
        prim = RenderPrimitiveType::PointList;
        break;
    case 1:
        prim = RenderPrimitiveType::LineList;
        break;
    case 2:
        prim = RenderPrimitiveType::LineStrip;
        break;
    case 3:
        prim = RenderPrimitiveType::TriangleList;
        break;
    case 4:
        prim = RenderPrimitiveType::TriangleStrip;
        break;
    case 5:
        prim = RenderPrimitiveType::TriangleFan;
        break;
    default:
        return;
    }

    static PointVec points;
    int size = data->GetSize() / 3;
    points.resize(size);

    for (int i = 0; i < size; i++)
    {
        PrepPoint& pp = points[i];
        pp.PointX = *(int*)data->At(i * 3);
        pp.PointY = *(int*)data->At(i * 3 + 1);
        pp.PointColor = *(int*)data->At(i * 3 + 2);
        pp.PointOffsX = nullptr;
        pp.PointOffsY = nullptr;
    }

    Self->SprMngr.DrawPoints(points, prim);
}

void FOMapper::SScriptFunc::Global_DrawMapSprite(MapSprite* map_spr)
{
    if (!map_spr)
        SCRIPT_ERROR_R("Map sprite arg is null.");

    if (!Self->HexMngr.IsMapLoaded())
        return;
    if (map_spr->HexX >= Self->HexMngr.GetWidth() || map_spr->HexY >= Self->HexMngr.GetHeight())
        return;
    if (!Self->HexMngr.IsHexToDraw(map_spr->HexX, map_spr->HexY))
        return;

    AnyFrames* anim = Self->AnimGetFrames(map_spr->SprId);
    if (!anim || map_spr->FrameIndex >= (int)anim->CntFrm)
        return;

    uint color = map_spr->Color;
    bool is_flat = map_spr->IsFlat;
    bool no_light = map_spr->NoLight;
    int draw_order = map_spr->DrawOrder;
    int draw_order_hy_offset = map_spr->DrawOrderHyOffset;
    int corner = map_spr->Corner;
    bool disable_egg = map_spr->DisableEgg;
    uint contour_color = map_spr->ContourColor;

    if (map_spr->ProtoId)
    {
        ProtoItem* proto_item = Self->ProtoMngr.GetProtoItem(map_spr->ProtoId);
        if (!proto_item)
            return;

        color = (proto_item->GetIsColorize() ? proto_item->GetLightColor() : 0);
        is_flat = proto_item->GetIsFlat();
        bool is_item = !proto_item->IsAnyScenery();
        no_light = (is_flat && !is_item);
        draw_order = (is_flat ? (is_item ? DRAW_ORDER_FLAT_ITEM : DRAW_ORDER_FLAT_SCENERY) :
                                (is_item ? DRAW_ORDER_ITEM : DRAW_ORDER_SCENERY));
        draw_order_hy_offset = proto_item->GetDrawOrderOffsetHexY();
        corner = proto_item->GetCorner();
        disable_egg = proto_item->GetDisableEgg();
        contour_color = (proto_item->GetIsBadItem() ? COLOR_RGB(255, 0, 0) : 0);
    }

    Field& f = Self->HexMngr.GetField(map_spr->HexX, map_spr->HexY);
    Sprites& tree = Self->HexMngr.GetDrawTree();
    Sprite& spr = tree.InsertSprite(draw_order, map_spr->HexX, map_spr->HexY + draw_order_hy_offset, 0,
        (Self->Settings.MapHexWidth / 2) + map_spr->OffsX, (Self->Settings.MapHexHeight / 2) + map_spr->OffsY, &f.ScrX,
        &f.ScrY, map_spr->FrameIndex < 0 ? anim->GetCurSprId() : anim->GetSprId(map_spr->FrameIndex), nullptr,
        map_spr->IsTweakOffs ? &map_spr->TweakOffsX : nullptr, map_spr->IsTweakOffs ? &map_spr->TweakOffsY : nullptr,
        map_spr->IsTweakAlpha ? &map_spr->TweakAlpha : nullptr, nullptr, nullptr);

    spr.MapSpr = map_spr;
    map_spr->AddRef();

    if (!no_light)
        spr.SetLight(corner, Self->HexMngr.GetLightHex(0, 0), Self->HexMngr.GetWidth(), Self->HexMngr.GetHeight());

    if (!is_flat && !disable_egg)
    {
        int egg_type = 0;
        switch (corner)
        {
        case CORNER_SOUTH:
            egg_type = EGG_X_OR_Y;
            break;
        case CORNER_NORTH:
            egg_type = EGG_X_AND_Y;
            break;
        case CORNER_EAST_WEST:
        case CORNER_WEST:
            egg_type = EGG_Y;
            break;
        default:
            egg_type = EGG_X;
            break;
        }
        spr.SetEgg(egg_type);
    }

    if (color)
    {
        spr.SetColor(color & 0xFFFFFF);
        spr.SetFixedAlpha(color >> 24);
    }

    if (contour_color)
        spr.SetContour(CONTOUR_CUSTOM, contour_color);
}

void FOMapper::SScriptFunc::Global_DrawCritter2d(hash model_name, uint anim1, uint anim2, uchar dir, int l, int t,
    int r, int b, bool scratch, bool center, uint color)
{
    AnyFrames* anim = Self->ResMngr.GetCrit2dAnim(model_name, anim1, anim2, dir);
    if (anim)
        Self->SprMngr.DrawSpriteSize(anim->Ind[0], l, t, r - l, b - t, scratch, center, COLOR_SCRIPT_SPRITE(color));
}

static Animation3dVec DrawCritter3dAnim;
static UIntVec DrawCritter3dCrType;
static BoolVec DrawCritter3dFailToLoad;
static int DrawCritter3dLayers[LAYERS3D_COUNT];
void FOMapper::SScriptFunc::Global_DrawCritter3d(
    uint instance, hash model_name, uint anim1, uint anim2, CScriptArray* layers, CScriptArray* position, uint color)
{
    // x y
    // rx ry rz
    // sx sy sz
    // speed
    // scissor l t r b
    if (instance >= DrawCritter3dAnim.size())
    {
        DrawCritter3dAnim.resize(instance + 1);
        DrawCritter3dCrType.resize(instance + 1);
        DrawCritter3dFailToLoad.resize(instance + 1);
    }

    if (DrawCritter3dFailToLoad[instance] && DrawCritter3dCrType[instance] == model_name)
        return;

    Animation3d*& anim3d = DrawCritter3dAnim[instance];
    if (!anim3d || DrawCritter3dCrType[instance] != model_name)
    {
        if (anim3d)
            Self->SprMngr.FreePure3dAnimation(anim3d);
        anim3d = Self->SprMngr.LoadPure3dAnimation(_str().parseHash(model_name).c_str(), false);
        DrawCritter3dCrType[instance] = model_name;
        DrawCritter3dFailToLoad[instance] = false;

        if (!anim3d)
        {
            DrawCritter3dFailToLoad[instance] = true;
            return;
        }
        anim3d->EnableShadow(false);
        anim3d->SetTimer(false);
    }

    uint count = (position ? position->GetSize() : 0);
    float x = (count > 0 ? *(float*)position->At(0) : 0.0f);
    float y = (count > 1 ? *(float*)position->At(1) : 0.0f);
    float rx = (count > 2 ? *(float*)position->At(2) : 0.0f);
    float ry = (count > 3 ? *(float*)position->At(3) : 0.0f);
    float rz = (count > 4 ? *(float*)position->At(4) : 0.0f);
    float sx = (count > 5 ? *(float*)position->At(5) : 1.0f);
    float sy = (count > 6 ? *(float*)position->At(6) : 1.0f);
    float sz = (count > 7 ? *(float*)position->At(7) : 1.0f);
    float speed = (count > 8 ? *(float*)position->At(8) : 1.0f);
    float period = (count > 9 ? *(float*)position->At(9) : 0.0f);
    float stl = (count > 10 ? *(float*)position->At(10) : 0.0f);
    float stt = (count > 11 ? *(float*)position->At(11) : 0.0f);
    float str = (count > 12 ? *(float*)position->At(12) : 0.0f);
    float stb = (count > 13 ? *(float*)position->At(13) : 0.0f);
    if (count > 13)
        Self->SprMngr.PushScissor((int)stl, (int)stt, (int)str, (int)stb);

    memzero(DrawCritter3dLayers, sizeof(DrawCritter3dLayers));
    for (uint i = 0, j = (layers ? layers->GetSize() : 0); i < j && i < LAYERS3D_COUNT; i++)
        DrawCritter3dLayers[i] = *(int*)layers->At(i);

    anim3d->SetDirAngle(0);
    anim3d->SetRotation(rx * PI_VALUE / 180.0f, ry * PI_VALUE / 180.0f, rz * PI_VALUE / 180.0f);
    anim3d->SetScale(sx, sy, sz);
    anim3d->SetSpeed(speed);
    anim3d->SetAnimation(
        anim1, anim2, DrawCritter3dLayers, ANIMATION_PERIOD((int)(period * 100.0f)) | ANIMATION_NO_SMOOTH);

    Self->SprMngr.Draw3d((int)x, (int)y, anim3d, COLOR_SCRIPT_SPRITE(color));

    if (count > 13)
        Self->SprMngr.PopScissor();
}

void FOMapper::SScriptFunc::Global_PushDrawScissor(int x, int y, int w, int h)
{
    Self->SprMngr.PushScissor(x, y, x + w, y + h);
}

void FOMapper::SScriptFunc::Global_PopDrawScissor()
{
    Self->SprMngr.PopScissor();
}