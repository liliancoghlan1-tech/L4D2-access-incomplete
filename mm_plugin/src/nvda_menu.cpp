#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>

// ── IServerPluginCallbacks stub ──────────────────────────────────────────────
// Vtable layout for ISERVERPLUGINCALLBACKS003 (L4D2 engine).
// We only care about Load/Unload; all other slots are empty stubs.
// void* replaces types we don't need to avoid Source SDK dependency.

typedef void *(*CreateInterfaceFn)(const char *, int *);

class IServerPluginCallbacks
{
public:
    virtual bool  Load(CreateInterfaceFn ifaceFn, CreateInterfaceFn gameFn) = 0;              // 0
    virtual void  Unload() = 0;                                                                // 1
    virtual void  Pause() = 0;                                                                 // 2
    virtual void  UnPause() = 0;                                                               // 3
    virtual const char *GetPluginDescription() = 0;                                           // 4
    virtual void  LevelInit(const char *pMapName) = 0;                                        // 5
    virtual void  ServerActivate(void *pEdictList, int edictCount, int clientMax) = 0;        // 6
    virtual void  GameFrame(bool simulating) = 0;                                              // 7
    virtual void  LevelShutdown() = 0;                                                         // 8
    virtual void  ClientActive(void *pEntity) = 0;                                            // 9
    virtual void  ClientDisconnect(void *pEntity) = 0;                                        // 10
    virtual void  ClientPutInServer(void *pEntity, const char *playername) = 0;               // 11
    virtual void  SetCommandClient(int index) = 0;                                             // 12
    virtual void  ClientSettingsChanged(void *pEdict) = 0;                                    // 13
    virtual int   ClientConnect(bool *bAllowConnect, void *pEntity,
                                const char *pszName, const char *pszAddress,
                                char *reject, int maxrejectlen) = 0;                           // 14
    virtual int   ClientCommand(void *pEntity, void *args) = 0;                               // 15
    virtual int   NetworkIDValidated(const char *pszUser, const char *pszNetID) = 0;          // 16
    virtual void  OnQueryCvarValueFinished(int iCookie, void *pPlayer,
                                           int eStatus, const char *pName,
                                           const char *pValue) = 0;                           // 17
    virtual void  OnEdictAllocated(void *edict) = 0;                                          // 18
    virtual void  OnEdictFreed(const void *edict) = 0;                                        // 19
};

// ── VGUI stubs ───────────────────────────────────────────────────────────────
// Same vtable offsets as extension.cpp — confirmed in-game.

typedef unsigned int VPANEL;

class IVGuiInput
{
public:
    virtual void   _vi_dtor() = 0;    // 0
    virtual void   _vi0(VPANEL) = 0;  // 1: SetMouseFocus
    virtual void   _vi1(VPANEL) = 0;  // 2: SetMouseCapture
    virtual void   _vi2() = 0;        // 3: GetKeyCodeText
    virtual VPANEL GetFocus() = 0;    // 4
};

class IVGuiPanel
{
public:
    virtual void   _vp_dtor() = 0;
    virtual void   _vp00() = 0; // Init
    virtual void   _vp01() = 0; // SetPos
    virtual void   _vp02() = 0; // GetPos
    virtual void   _vp03() = 0; // SetSize
    virtual void   _vp04() = 0; // GetSize
    virtual void   _vp05() = 0; // SetMinimumSize
    virtual void   _vp06() = 0; // GetMinimumSize
    virtual void   _vp07() = 0; // SetZPos
    virtual void   _vp08() = 0; // GetZPos
    virtual void   _vp09() = 0; // GetAbsPos
    virtual void   _vp10() = 0; // GetClipRect
    virtual void   _vp11() = 0; // SetInset
    virtual void   _vp12() = 0; // GetInset
    virtual void   _vp13() = 0; // SetVisible
    virtual void   _vp14() = 0; // IsVisible
    virtual void   _vp15() = 0; // SetParent
    virtual int    GetChildCount(VPANEL vguiPanel) = 0; // 17
    virtual VPANEL GetChild(VPANEL vguiPanel, int index) = 0; // 18
    virtual VPANEL GetParent(VPANEL vguiPanel) = 0;           // 19
    virtual void   _vp19() = 0; // MoveToFront
    virtual void   _vp20() = 0; // MoveToBack
    virtual void   _vp21() = 0; // HasParent
    virtual void   _vp22() = 0; // IsPopup
    virtual void   _vp23() = 0; // SetPopup
    virtual void   _vp24() = 0; // IsFullyVisible
    virtual void   _vp25() = 0; // GetScheme
    virtual void   _vp26() = 0; // IsProportional
    virtual void   _vp27() = 0; // IsAutoDeleteSet
    virtual void   _vp28() = 0; // DeletePanel
    virtual void   _vp29() = 0; // SetKeyBoardInputEnabled
    virtual void   _vp30() = 0; // SetMouseInputEnabled
    virtual void   _vp31() = 0; // IsKeyBoardInputEnabled
    virtual void   _vp32() = 0; // IsMouseInputEnabled
    virtual void   _vp33() = 0; // Solve
    virtual void   _vp34() = 0; // GetName placeholder (shifts real GetName to 35)
    virtual const char *GetName(VPANEL vguiPanel) = 0; // 35
};

// ── NVDA ──────────────────────────────────────────────────────────────────────

typedef int (__stdcall *nvdaController_speakText_t)(const wchar_t *);
typedef int (__stdcall *nvdaController_cancelSpeech_t)();

static HMODULE          g_nvdaLib      = nullptr;
static nvdaController_speakText_t    g_speakText    = nullptr;
static nvdaController_cancelSpeech_t g_cancelSpeech = nullptr;

static IVGuiInput *g_pVGuiInput = nullptr;
static IVGuiPanel *g_pVGuiPanel = nullptr;

static char  g_last_panel[64]  = {};
static char  g_last_group[64]  = {};
static int   g_focusFails      = 0;

static volatile bool g_polling   = false;
static HANDLE        g_pollThread = NULL;

static FILE *g_log = nullptr;

static void WriteLog(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

// ── Panel-to-speech lookup ────────────────────────────────────────────────────
// group: non-null panels belong to a named section. When focus first enters a
// section, the section name is prepended to the speech ("Campaign: Dead Center").
// Subsequent items in the same section speak without the prefix ("Dark Carnival").
// Moving to a panel with a different (or no) group resets the section prefix.

static bool PanelLookup(const char *panel, char *speechOut, int speechLen,
                        const char **groupOut)
{
    static const struct { const char *key; const char *speech; const char *group; } table[] = {
        // ── Main menu — game mode selector (no group — self-explanatory) ────────
        {"BtnCoOp",             "Campaign",         nullptr},
        {"BtnVersus",           "Versus",            nullptr},
        {"BtnSurvival",         "Survival",          nullptr},
        {"BtnScavenge",         "Scavenge",          nullptr},
        {"BtnPlayRealism",      "Realism",           nullptr},
        {"BtnPlayChallenge",    "Mutations",         nullptr},
        {"BtnRealismVersus",    "Realism Versus",    nullptr},
        {"BtnVersusSurvival",   "Versus Survival",   nullptr},
        // ── Main menu — bottom buttons ────────────────────────────────────────
        {"BtnAddons",           "Add-Ons",           nullptr},
        {"BtnExtras",           "Extras",            nullptr},
        {"BtnBlogPost",         "Blog Post",         nullptr},
        // ── Game mode submenus ────────────────────────────────────────────────
        {"BtnQuickMatch",               "Quick Match",          nullptr},
        {"BtnSinglePlayer",             "Single Player",        nullptr},
        {"BtnPlayOnGroupServer",        "Play on Group Server", nullptr},
        {"BtnStartGame",                "Start Game",           nullptr},
        {"BtnPlayCoopWithAnyone",       "Play with Anyone",     nullptr},
        {"BtnPlayCoopWithFriends",      "Play with Friends",    nullptr},
        {"BtnPlayRealismWithAnyone",    "Play with Anyone",     nullptr},
        {"BtnPlayRealismWithFriends",   "Play with Friends",    nullptr},
        {"BtnPlayVersusWithAnyone",     "Play with Anyone",     nullptr},
        {"BtnPlayVersusWithFriends",    "Play with Friends",    nullptr},
        {"BtnPlayVersusWithTeam",       "Play with Team",       nullptr},
        {"BtnPlayRealismVersusWithAnyone",  "Play with Anyone", nullptr},
        {"BtnPlayRealismVersusWithFriends", "Play with Friends",nullptr},
        {"BtnPlayRealismVersusWithTeam",    "Play with Team",   nullptr},
        {"BtnPlayVersusSurvivalWithAnyone",  "Play with Anyone",nullptr},
        {"BtnPlayVersusSurvivalWithFriends", "Play with Friends",nullptr},
        {"BtnPlayVersusSurvivalWithTeam",    "Play with Team",  nullptr},
        {"BtnPlaySurvivalWithAnyone",   "Play with Anyone",     nullptr},
        {"BtnPlaySurvivalWithFriends",  "Play with Friends",    nullptr},
        {"BtnPlaySurvivalLeaderboards", "Leaderboards",         nullptr},
        {"BtnPlayScavengeWithAnyone",   "Play with Anyone",     nullptr},
        {"BtnPlayScavengeWithFriends",  "Play with Friends",    nullptr},
        {"BtnPlayScavengeWithTeam",     "Play with Team",       nullptr},
        {"BtnChangeMutation",           "Change Mutation",      nullptr},
        // ── Extras submenu ────────────────────────────────────────────────────
        {"BtnCommentary",       "Developer Commentary", nullptr},
        {"BtnCredits",          "Credits",              nullptr},
        // ── Add-Ons / Blog Post panels ────────────────────────────────────────
        {"GplAddons",           "Add-Ons list",  nullptr},
        {"BlogPost",            "Blog Post",     nullptr},
        // ── Campaign select (grouped) ─────────────────────────────────────────
        {"BtnCampaign1",      "Dead Center",          "Campaign"},
        {"BtnCampaign2",      "Dark Carnival",         "Campaign"},
        {"BtnCampaign3",      "Swamp Fever",           "Campaign"},
        {"BtnCampaign4",      "Hard Rain",             "Campaign"},
        {"BtnCampaign5",      "The Parish",            "Campaign"},
        {"BtnCampaign6",      "The Passing",           "Campaign"},
        {"BtnCampaign7",      "No Mercy",              "Campaign"},
        {"BtnCampaign8",      "Crash Course",          "Campaign"},
        {"BtnCampaign9",      "Death Toll",            "Campaign"},
        {"BtnCampaign10",     "Dead Air",              "Campaign"},
        {"BtnCampaign11",     "Blood Harvest",         "Campaign"},
        {"BtnCampaign12",     "The Sacrifice",         "Campaign"},
        {"BtnCampaign13",     "Cold Stream",           "Campaign"},
        {"BtnCampaign14",     "The Last Stand",        "Campaign"},
        {"BtnCampaignCustom", "Community Campaigns",   "Campaign"},
        // ── Chapter select (grouped) ──────────────────────────────────────────
        {"BtnChapter1",  "Chapter 1",  "Chapters"},
        {"BtnChapter2",  "Chapter 2",  "Chapters"},
        {"BtnChapter3",  "Chapter 3",  "Chapters"},
        {"BtnChapter4",  "Chapter 4",  "Chapters"},
        {"BtnChapter5",  "Chapter 5",  "Chapters"},
        // ── Difficulty select (grouped) ───────────────────────────────────────
        {"BtnEasy",       "Easy",      "Difficulty"},
        {"BtnNormal",     "Normal",    "Difficulty"},
        {"BtnHard",       "Advanced",  "Difficulty"},
        {"BtnImpossible", "Expert",    "Difficulty"},
        // ── Character select (grouped) ────────────────────────────────────────
        {"BtnRandom",    "Random",    "Character"},
        {"BtnGambler",   "Nick",      "Character"},
        {"BtnProducer",  "Rochelle",  "Character"},
        {"BtnCoach",     "Coach",     "Character"},
        {"BtnMechanic",  "Ellis",     "Character"},
        // ── Pause menu ────────────────────────────────────────────────────────
        {"InGameMainMenu",          "Pause menu",           nullptr},
        {"BtnReturnToGame",         "Return to Game",       nullptr},
        {"BtnGoIdle",               "Take a Break",         nullptr},
        {"BtnCallAVote",            "Call a Vote",          nullptr},
        {"BtnInviteFriends",        "Invite Friends",       nullptr},
        {"BtnLeaderboard",          "Leaderboards",         nullptr},
        {"BtnStatsAndAchievements", "Achievements",         nullptr},
        {"BtnOptions",              "Options",              nullptr},
        {"BtnExitToMainMenu",       "Exit to Main Menu",    nullptr},
        {"BtnQuit",                 "Exit to Desktop",      nullptr},
        // ── Confirmation popup ────────────────────────────────────────────────
        {"GenericConfirmation",     "Confirm?",             nullptr},
        // ── Options tabs ──────────────────────────────────────────────────────
        {"BtnVideo",            "Video",            nullptr},
        {"BtnAudio",            "Audio",            nullptr},
        {"BtnSteamController",  "Steam Controller", nullptr},
        {"BtnController",       "Controller",       nullptr},
        {"BtnMultiplayer",      "Multiplayer",      nullptr},
        {"BtnCloud",            "Steam Cloud",      nullptr},
        {"BtnKeyboard",         "Keyboard",         nullptr},
        {"BtnMouse",            "Mouse",            nullptr},
        // ── Options submenus ──────────────────────────────────────────────────
        {"BtnDone",             "Done",                     nullptr},
        {"BtnCancel",           "Cancel",                   nullptr},
        {"BtnAdvanced",         "Advanced Video Settings",  nullptr},
        {"BtnUseRecommended",   "Use Recommended Settings", nullptr},
        {"BtnTestMicrophone",   "Test Microphone",          nullptr},
        {"BtnEditBindings",     "Edit Key Bindings",        nullptr},
        {"BtnBrowseSpraypaint", "Browse Spray Paint",       nullptr},
        {"Btn3rdPartyCredits",  "Third Party Credits",      nullptr},
        {"BtnDropButton",       "Dropdown",                 nullptr},
        // ── Achievements ──────────────────────────────────────────────────────
        {"GplAchievements",     "Achievements list",        nullptr},
        // ── Common dialogs ────────────────────────────────────────────────────
        {"ApplyButton",  "Apply",   nullptr},
        {"CancelButton", "Cancel",  nullptr},
        {"OkButton",     "OK",      nullptr},
        {nullptr, nullptr, nullptr}
    };

    for (int i = 0; table[i].key; i++)
    {
        if (strcmp(panel, table[i].key) == 0)
        {
            strncpy(speechOut, table[i].speech, speechLen - 1);
            speechOut[speechLen - 1] = '\0';
            if (groupOut) *groupOut = table[i].group;
            return true;
        }
    }
    return false;
}

// ── Polling ───────────────────────────────────────────────────────────────────

static void NVDASpeak(const char *text)
{
    if (!g_speakText || !text || !text[0]) return;
    wchar_t wtext[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, 256);
    __try { g_speakText(wtext); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void PollVguiFocus()
{
    if (!g_pVGuiInput || !g_pVGuiPanel) return;
    if (g_focusFails >= 5) return;

    VPANEL focused = 0;
    __try { focused = g_pVGuiInput->GetFocus(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { g_focusFails++; return; }
    g_focusFails = 0;

    if (!focused) { g_last_panel[0] = '\0'; return; }

    const char *name = nullptr;
    __try { name = g_pVGuiPanel->GetName(focused); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    if (!name || !name[0]) return;

    // Walk up to parent for generic control names
    bool isGeneric = (strcmp(name, "BtnDropButton") == 0 ||
                      strcmp(name, "BtnCancel")     == 0);
    if (isGeneric)
    {
        VPANEL parent = 0;
        __try { parent = g_pVGuiPanel->GetParent(focused); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (parent)
        {
            const char *pname = nullptr;
            __try { pname = g_pVGuiPanel->GetName(parent); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
            if (pname && pname[0] && strcmp(pname, "Panel") != 0)
                name = pname;
        }
    }

    if (strcmp(name, g_last_panel) == 0) return;

    strncpy(g_last_panel, name, sizeof(g_last_panel) - 1);
    g_last_panel[sizeof(g_last_panel) - 1] = '\0';

    char speech[128];
    const char *group = nullptr;
    if (PanelLookup(name, speech, sizeof(speech), &group))
    {
        char full[256];
        if (group && strcmp(group, g_last_group) != 0)
        {
            snprintf(full, sizeof(full), "%s: %s", group, speech);
            strncpy(g_last_group, group, sizeof(g_last_group) - 1);
            g_last_group[sizeof(g_last_group) - 1] = '\0';
        }
        else
        {
            if (!group) g_last_group[0] = '\0';
            strncpy(full, speech, sizeof(full) - 1);
            full[sizeof(full) - 1] = '\0';
        }
        NVDASpeak(full);
    }
    else
    {
        WriteLog("[NVDA_MENU] unknown panel: \"%s\"\n", name);
    }
}

static DWORD WINAPI PollThread(LPVOID)
{
    while (g_polling)
    {
        PollVguiFocus();
        Sleep(100);
    }
    return 0;
}

// ── Plugin class ──────────────────────────────────────────────────────────────

class NVDAMenuPlugin : public IServerPluginCallbacks
{
public:
    bool Load(CreateInterfaceFn /*ifaceFn*/, CreateInterfaceFn /*gameFn*/) override
    {
        // Resolve this DLL's own directory so we can find nvdaControllerClient32.dll
        // next to it regardless of CWD.
        HMODULE hMe = NULL;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&PollThread, &hMe);

        char myDir[MAX_PATH] = {};
        GetModuleFileNameA(hMe, myDir, sizeof(myDir));
        char *slash = strrchr(myDir, '\\');
        if (slash) *(slash + 1) = '\0';

        // Open log (append so we see history across sessions)
        char logPath[MAX_PATH];
        snprintf(logPath, sizeof(logPath), "%snvda_menu_log.txt", myDir);
        g_log = fopen(logPath, "a");
        WriteLog("=== NVDA Menu Plugin Load ===\n");

        // Load nvdaControllerClient32.dll
        char nvdaPath[MAX_PATH];
        snprintf(nvdaPath, sizeof(nvdaPath), "%snvdaControllerClient32.dll", myDir);
        WriteLog("Loading NVDA client: %s\n", nvdaPath);
        g_nvdaLib = LoadLibraryA(nvdaPath);
        if (!g_nvdaLib)
        {
            WriteLog("FAILED (error %lu) — NVDA speech unavailable\n", GetLastError());
            // Don't return false — let the plugin load so it at least logs panel names
        }
        else
        {
            g_speakText    = (nvdaController_speakText_t)
                             GetProcAddress(g_nvdaLib, "nvdaController_speakText");
            g_cancelSpeech = (nvdaController_cancelSpeech_t)
                             GetProcAddress(g_nvdaLib, "nvdaController_cancelSpeech");
            WriteLog("speakText=%p  cancelSpeech=%p\n", g_speakText, g_cancelSpeech);
        }

        // Get VGUI interfaces (vgui2.dll is already loaded by the engine)
        HMODULE vgui2 = GetModuleHandleA("vgui2.dll");
        if (vgui2)
        {
            typedef void *(*CIF_t)(const char *, int *);
            CIF_t vf = (CIF_t)GetProcAddress(vgui2, "CreateInterface");
            if (vf)
            {
                g_pVGuiInput = (IVGuiInput *)vf("VGUI_Input005", nullptr);
                g_pVGuiPanel = (IVGuiPanel *)vf("VGUI_Panel009", nullptr);
            }
        }
        WriteLog("VGUI input=%p  panel=%p\n", g_pVGuiInput, g_pVGuiPanel);

        g_focusFails      = 0;
        g_last_panel[0]   = '\0';
        g_last_group[0]   = '\0';

        // Start background polling thread — runs for the lifetime of the game,
        // covering the main menu before any map loads.
        g_polling   = true;
        g_pollThread = CreateThread(NULL, 0, PollThread, NULL, 0, NULL);
        WriteLog("Poll thread started (%p)\n", g_pollThread);

        return true;
    }

    void Unload() override
    {
        g_polling = false;
        if (g_pollThread)
        {
            WaitForSingleObject(g_pollThread, 1000);
            CloseHandle(g_pollThread);
            g_pollThread = NULL;
        }
        g_speakText    = nullptr;
        g_cancelSpeech = nullptr;
        if (g_nvdaLib) { FreeLibrary(g_nvdaLib); g_nvdaLib = nullptr; }
        WriteLog("=== NVDA Menu Plugin Unload ===\n");
        if (g_log) { fclose(g_log); g_log = nullptr; }
    }

    void  Pause()  override {}
    void  UnPause() override {}
    const char *GetPluginDescription() override { return "NVDA Menu Accessibility v1.0"; }
    void  LevelInit(const char *) override {}
    void  ServerActivate(void *, int, int) override {}
    void  GameFrame(bool) override {}
    void  LevelShutdown() override {}
    void  ClientActive(void *) override {}
    void  ClientDisconnect(void *) override {}
    void  ClientPutInServer(void *, const char *) override {}
    void  SetCommandClient(int) override {}
    void  ClientSettingsChanged(void *) override {}
    int   ClientConnect(bool *, void *, const char *, const char *, char *, int) override { return 0; }
    int   ClientCommand(void *, void *) override { return 0; }
    int   NetworkIDValidated(const char *, const char *) override { return 0; }
    void  OnQueryCvarValueFinished(int, void *, int, const char *, const char *) override {}
    void  OnEdictAllocated(void *) override {}
    void  OnEdictFreed(const void *) override {}
};

static NVDAMenuPlugin g_Plugin;

// The engine (and Metamod's interception layer) call CreateInterface on this DLL.
// Return our VSP for the Source engine's ISERVERPLUGINCALLBACKS interface.
// Return null for Metamod's own interface query so it treats us as a plain VSP.
extern "C" __declspec(dllexport) void *CreateInterface(const char *name, int *ret)
{
    if (strcmp(name, "ISERVERPLUGINCALLBACKS003") == 0 ||
        strcmp(name, "ISERVERPLUGINCALLBACKS002") == 0)
    {
        if (ret) *ret = 0;
        return &g_Plugin;
    }
    if (ret) *ret = 1;
    return nullptr;
}
