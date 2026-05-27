#include "extension.h"
#include <windows.h>

NVDAExtension g_NVDAExt;
SMEXT_LINK(&g_NVDAExt);

typedef int (__stdcall *nvdaController_speakText_t)(const wchar_t *);
typedef int (__stdcall *nvdaController_cancelSpeech_t)();
typedef int (__stdcall *nvdaController_testIfRunning_t)();

static HMODULE g_nvdaLib = nullptr;
static nvdaController_speakText_t    g_speakText    = nullptr;
static nvdaController_cancelSpeech_t g_cancelSpeech = nullptr;
static nvdaController_testIfRunning_t g_testIfRunning = nullptr;

// The plugin writes the text here via StoreToAddress before calling NVDA_Speak().
// This avoids all IPluginContext virtual calls (which crash due to SP 1.12 vs 1.13
// vtable mismatch between the installed SourceMod and our SDK headers).
static char g_speak_buf[2048] = {};

// ── Minimal VGUI stubs ────────────────────────────────────────────────────────
// Vtable offsets confirmed from hl2sdk-l4d2 public/vgui/IInput.h and IPanel.h.
// Only pure virtual slots needed to reach the methods we call; all others are
// placeholders so the vtable index stays correct.

typedef unsigned int VPANEL;

// IInput — "VGUI_Input005"
// The shipped vgui2.dll has one virtual destructor slot before SetMouseFocus
// that the hl2sdk header doesn't declare. Confirmed by disassembling vtable[3]
// in-game: it has 3 explicit parameters (GetKeyCodeText), not 0 (GetFocus).
// Adding one placeholder shifts GetFocus to slot 4, which is correct.
class IVGuiInput
{
public:
    virtual void _vi_dtor() = 0;     // 0: destructor (undeclared in header)
    virtual void _vi0(VPANEL) = 0;   // 1: SetMouseFocus
    virtual void _vi1(VPANEL) = 0;   // 2: SetMouseCapture
    virtual void _vi2() = 0;         // 3: GetKeyCodeText
    virtual VPANEL GetFocus() = 0;   // 4
};

// IPanel — "VGUI_Panel009"
// Same undeclared destructor slot at position 0. GetName() shifts from 34 → 35.
class IVGuiPanel
{
public:
    virtual void _vp_dtor() = 0; // 0: destructor (undeclared in header)
    virtual void _vp00() = 0; // Init
    virtual void _vp01() = 0; // SetPos
    virtual void _vp02() = 0; // GetPos
    virtual void _vp03() = 0; // SetSize
    virtual void _vp04() = 0; // GetSize
    virtual void _vp05() = 0; // SetMinimumSize
    virtual void _vp06() = 0; // GetMinimumSize
    virtual void _vp07() = 0; // SetZPos
    virtual void _vp08() = 0; // GetZPos
    virtual void _vp09() = 0; // GetAbsPos
    virtual void _vp10() = 0; // GetClipRect
    virtual void _vp11() = 0; // SetInset
    virtual void _vp12() = 0; // GetInset
    virtual void _vp13() = 0; // SetVisible
    virtual void _vp14() = 0; // IsVisible
    virtual void _vp15() = 0; // SetParent
    virtual int   GetChildCount(VPANEL vguiPanel) = 0;          // 17
    virtual VPANEL GetChild(VPANEL vguiPanel, int index) = 0;  // 18
    virtual VPANEL GetParent(VPANEL vguiPanel) = 0;            // 19
    virtual void _vp19() = 0; // MoveToFront
    virtual void _vp20() = 0; // MoveToBack
    virtual void _vp21() = 0; // HasParent
    virtual void _vp22() = 0; // IsPopup
    virtual void _vp23() = 0; // SetPopup
    virtual void _vp24() = 0; // IsFullyVisible
    virtual void _vp25() = 0; // GetScheme
    virtual void _vp26() = 0; // IsProportional
    virtual void _vp27() = 0; // IsAutoDeleteSet
    virtual void _vp28() = 0; // DeletePanel
    virtual void _vp29() = 0; // SetKeyBoardInputEnabled
    virtual void _vp30() = 0; // SetMouseInputEnabled
    virtual void _vp31() = 0; // IsKeyBoardInputEnabled
    virtual void _vp32() = 0; // IsMouseInputEnabled
    virtual void _vp33() = 0; // Solve
    virtual void _vp34() = 0; // GetName placeholder → actual GetName shifted to 35
    virtual const char *GetName(VPANEL vguiPanel) = 0; // 35
};

static IVGuiInput *g_pVGuiInput = nullptr;
static IVGuiPanel *g_pVGuiPanel = nullptr;

// Tracks the panel that was last announced so we don't repeat on every poll.
// Reset to "" when focus goes null so the next focus event always fires.
static char g_last_panel_name[64] = {};

// After this many consecutive SEH failures in GetFocus(), stop polling.
static int g_focusFails = 0;
static const int k_maxFocusFails = 5;

// Returns the address of g_speak_buf so the plugin can write to it with StoreToAddress.
static cell_t Native_GetBuffer(IPluginContext *pCtx, const cell_t *params)
{
    return (cell_t)(uintptr_t)g_speak_buf;
}

// Reads from g_speak_buf (written by plugin via StoreToAddress) and speaks it.
// Takes no string parameter — zero IPluginContext virtual calls needed.
static cell_t Native_Speak(IPluginContext *pCtx, const cell_t *params)
{
    if (!g_speakText) return 0;

    g_speak_buf[sizeof(g_speak_buf) - 1] = '\0';
    g_pSM->LogMessage(myself, "Native_Speak: buf=\"%s\"", g_speak_buf);

    if (g_speak_buf[0] == '\0') return 0;

    wchar_t wtext[2048] = {};
    MultiByteToWideChar(CP_UTF8, 0, g_speak_buf, -1, wtext, 2048);

    int result = 0;
    __try
    {
        result = g_speakText(wtext);
        g_pSM->LogMessage(myself, "Native_Speak: speakText returned %d", result);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_pSM->LogMessage(myself, "Native_Speak: SEH exception 0x%lX", GetExceptionCode());
    }

    g_speak_buf[0] = '\0';
    return result;
}

static cell_t Native_CancelSpeech(IPluginContext *pCtx, const cell_t *params)
{
    if (!g_cancelSpeech) return 0;
    int result = 0;
    __try { result = g_cancelSpeech(); }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_pSM->LogMessage(myself, "Native_CancelSpeech: SEH exception 0x%lX", GetExceptionCode());
    }
    return result;
}

// NVDA_DumpInputVtable()
// Logs the first 12 vtable function-pointer addresses for g_pVGuiInput.
// Use sm_nvda_dumptable in-game to identify the correct GetFocus() slot.
static cell_t Native_DumpInputVtable(IPluginContext * /*pCtx*/, const cell_t * /*params*/)
{
    if (!g_pVGuiInput)
    {
        g_pSM->LogMessage(myself, "[NVDA] DumpVtable: g_pVGuiInput is null");
        return 0;
    }

    HMODULE vgui2 = GetModuleHandleA("vgui2.dll");
    uintptr_t vgui2Base = vgui2 ? (uintptr_t)vgui2 : 0;

    void **vtable = *(void ***)g_pVGuiInput;
    g_pSM->LogMessage(myself, "[NVDA] IVGuiInput=%p vtable=%p vgui2base=%p",
        g_pVGuiInput, vtable, (void *)vgui2Base);

    for (int i = 0; i < 12; i++)
    {
        uintptr_t fn = (uintptr_t)vtable[i];
        uintptr_t offset = vgui2Base ? fn - vgui2Base : 0;
        g_pSM->LogMessage(myself, "[NVDA]   vtable[%2d] = %p  (+0x%lX from vgui2)",
            i, (void *)fn, (unsigned long)offset);
    }
    return 1;
}

// NVDA_DumpPanel()
// Logs the focused panel, its parent, and both levels' children.
// Run sm_nvda_dumpfocus while a dropdown is highlighted to discover the tree.
static void DumpPanelChildren(VPANEL panel, const char *label, int maxChildren)
{
    int count = 0;
    __try { count = g_pVGuiPanel->GetChildCount(panel); }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_pSM->LogMessage(myself, "[NVDA]   %s GetChildCount SEH 0x%lX", label, GetExceptionCode());
        return;
    }
    g_pSM->LogMessage(myself, "[NVDA] %s has %d children:", label, count);
    if (count > maxChildren) count = maxChildren;
    for (int i = 0; i < count; i++)
    {
        VPANEL child = 0;
        __try { child = g_pVGuiPanel->GetChild(panel, i); }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            g_pSM->LogMessage(myself, "[NVDA]   [%d] GetChild SEH 0x%lX", i, GetExceptionCode());
            continue;
        }
        const char *cn = nullptr;
        __try { cn = g_pVGuiPanel->GetName(child); }
        __except(EXCEPTION_EXECUTE_HANDLER) { cn = "[SEH]"; }
        g_pSM->LogMessage(myself, "[NVDA]   [%d] vpanel=%u name=%s", i, (unsigned)child, cn ? cn : "(null)");
    }
}

static cell_t Native_DumpPanel(IPluginContext * /*pCtx*/, const cell_t * /*params*/)
{
    if (!g_pVGuiInput || !g_pVGuiPanel)
    {
        g_pSM->LogMessage(myself, "[NVDA] DumpPanel: VGUI not initialised");
        return 0;
    }

    VPANEL focused = 0;
    __try { focused = g_pVGuiInput->GetFocus(); }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_pSM->LogMessage(myself, "[NVDA] DumpPanel: GetFocus SEH 0x%lX", GetExceptionCode());
        return 0;
    }
    if (!focused)
    {
        g_pSM->LogMessage(myself, "[NVDA] DumpPanel: no focused panel");
        return 0;
    }

    const char *focusName = nullptr;
    __try { focusName = g_pVGuiPanel->GetName(focused); }
    __except(EXCEPTION_EXECUTE_HANDLER) { focusName = "[SEH]"; }
    g_pSM->LogMessage(myself, "[NVDA] FOCUSED: vpanel=%u name=%s", (unsigned)focused, focusName ? focusName : "(null)");
    DumpPanelChildren(focused, "FOCUSED", 16);

    VPANEL parent = 0;
    __try { parent = g_pVGuiPanel->GetParent(focused); }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_pSM->LogMessage(myself, "[NVDA] PARENT: GetParent SEH 0x%lX", GetExceptionCode());
        return 1;
    }
    if (!parent) { g_pSM->LogMessage(myself, "[NVDA] PARENT: none"); return 1; }

    const char *parentName = nullptr;
    __try { parentName = g_pVGuiPanel->GetName(parent); }
    __except(EXCEPTION_EXECUTE_HANDLER) { parentName = "[SEH]"; }
    g_pSM->LogMessage(myself, "[NVDA] PARENT: vpanel=%u name=%s", (unsigned)parent, parentName ? parentName : "(null)");
    DumpPanelChildren(parent, "PARENT", 16);

    VPANEL grandparent = 0;
    __try { grandparent = g_pVGuiPanel->GetParent(parent); }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_pSM->LogMessage(myself, "[NVDA] GRANDPARENT: GetParent SEH 0x%lX", GetExceptionCode());
        return 1;
    }
    if (!grandparent) { g_pSM->LogMessage(myself, "[NVDA] GRANDPARENT: none"); return 1; }

    const char *gpName = nullptr;
    __try { gpName = g_pVGuiPanel->GetName(grandparent); }
    __except(EXCEPTION_EXECUTE_HANDLER) { gpName = "[SEH]"; }
    g_pSM->LogMessage(myself, "[NVDA] GRANDPARENT: vpanel=%u name=%s", (unsigned)grandparent, gpName ? gpName : "(null)");
    DumpPanelChildren(grandparent, "GRANDPARENT", 32);

    VPANEL great = 0;
    __try { great = g_pVGuiPanel->GetParent(grandparent); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 1; }
    if (!great) return 1;

    const char *greatName = nullptr;
    __try { greatName = g_pVGuiPanel->GetName(great); }
    __except(EXCEPTION_EXECUTE_HANDLER) { greatName = "[SEH]"; }
    g_pSM->LogMessage(myself, "[NVDA] GREAT-GRANDPARENT: vpanel=%u name=%s", (unsigned)great, greatName ? greatName : "(null)");
    DumpPanelChildren(great, "GREAT-GRANDPARENT", 32);

    return 1;
}

// NVDA_GetFocusedPanelName(bool force)
// Queries VGUI keyboard focus. If the focused panel differs from the last call
// (or force==true), writes its programmatic name into g_speak_buf and returns 1.
// Returns 0 if focus is unchanged or there is no focused panel.
// When focus is null the last-known name is reset so the next focus event fires.
static cell_t Native_GetFocusedPanelName(IPluginContext * /*pCtx*/, const cell_t *params)
{
    if (!g_pVGuiInput || !g_pVGuiPanel) return 0;
    if (g_focusFails >= k_maxFocusFails) return 0;

    bool force = (params[0] >= 1) && (params[1] != 0);

    VPANEL focused = 0;
    __try { focused = g_pVGuiInput->GetFocus(); }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_focusFails++;
        g_pSM->LogMessage(myself, "[NVDA] GetFocus SEH 0x%lX (fail %d/%d)",
            GetExceptionCode(), g_focusFails, k_maxFocusFails);
        if (g_focusFails >= k_maxFocusFails)
            g_pSM->LogMessage(myself, "[NVDA] GetFocus disabled — vtable index likely wrong. Run sm_nvda_dumptable.");
        return 0;
    }
    g_focusFails = 0;

    if (!focused)
    {
        g_last_panel_name[0] = '\0';
        return 0;
    }

    const char *name = nullptr;
    __try { name = g_pVGuiPanel->GetName(focused); }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_pSM->LogMessage(myself, "[NVDA] GetName SEH 0x%lX", GetExceptionCode());
        return 0;
    }

    if (!name || name[0] == '\0') return 0;

    // For generic control names, walk up to the parent panel and use its name
    // instead — it is usually named after the setting (e.g. "ResolutionRow").
    // This lets us tell the user which option they are on, not just "Dropdown".
    bool isGeneric = (strcmp(name, "BtnDropButton") == 0 ||
                      strcmp(name, "BtnCancel")     == 0);
    if (isGeneric)
    {
        VPANEL parent = 0;
        __try { parent = g_pVGuiPanel->GetParent(focused); }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            g_pSM->LogMessage(myself, "[NVDA] GetParent SEH 0x%lX", GetExceptionCode());
        }

        if (parent)
        {
            const char *parentName = nullptr;
            __try { parentName = g_pVGuiPanel->GetName(parent); }
            __except(EXCEPTION_EXECUTE_HANDLER) { /* expected until GetParent slot is fixed */ }

            if (parentName && parentName[0] != '\0' &&
                strcmp(parentName, "Panel") != 0)   // skip truly-unnamed panels
            {
                name = parentName;
            }
        }
    }

    if (!force && strcmp(name, g_last_panel_name) == 0) return 0;

    strncpy(g_last_panel_name, name, sizeof(g_last_panel_name) - 1);
    g_last_panel_name[sizeof(g_last_panel_name) - 1] = '\0';
    strncpy(g_speak_buf, name, sizeof(g_speak_buf) - 1);
    g_speak_buf[sizeof(g_speak_buf) - 1] = '\0';

    return 1;
}

// NVDA_DumpPanelVtable()
// Logs the first 40 vtable function-pointer addresses for g_pVGuiPanel with
// offsets from vgui2.dll — use sm_nvda_dumppanelvtable to identify the
// correct GetParent() slot.
static cell_t Native_DumpPanelVtable(IPluginContext * /*pCtx*/, const cell_t * /*params*/)
{
    if (!g_pVGuiPanel)
    {
        g_pSM->LogMessage(myself, "[NVDA] DumpPanelVtable: g_pVGuiPanel is null");
        return 0;
    }

    HMODULE vgui2 = GetModuleHandleA("vgui2.dll");
    uintptr_t vgui2Base = vgui2 ? (uintptr_t)vgui2 : 0;

    void **vtable = *(void ***)g_pVGuiPanel;
    g_pSM->LogMessage(myself, "[NVDA] IVGuiPanel=%p vtable=%p vgui2base=%p",
        g_pVGuiPanel, vtable, (void *)vgui2Base);

    for (int i = 0; i < 40; i++)
    {
        uintptr_t fn = (uintptr_t)vtable[i];
        uintptr_t offset = vgui2Base ? fn - vgui2Base : 0;
        g_pSM->LogMessage(myself, "[NVDA]   vtable[%2d] = %p  (+0x%lX from vgui2)",
            i, (void *)fn, (unsigned long)offset);
    }
    return 1;
}

// NVDA_ProbeGetParent()
// With a panel currently focused, tries calling vtable slots 17-25 as
// GetParent(focused) and logs the returned VPANEL plus its name. The slot
// that returns a non-null, non-crashing handle with a meaningful name is
// the real GetParent slot. All calls are wrapped in SEH.
typedef VPANEL (__thiscall *ProbeGetParent_t)(void *, VPANEL);

static cell_t Native_ProbeGetParent(IPluginContext * /*pCtx*/, const cell_t * /*params*/)
{
    if (!g_pVGuiInput || !g_pVGuiPanel)
    {
        g_pSM->LogMessage(myself, "[NVDA] ProbeGetParent: VGUI not initialised");
        return 0;
    }

    VPANEL focused = 0;
    __try { focused = g_pVGuiInput->GetFocus(); }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_pSM->LogMessage(myself, "[NVDA] ProbeGetParent: GetFocus SEH 0x%lX", GetExceptionCode());
        return 0;
    }
    if (!focused)
    {
        g_pSM->LogMessage(myself, "[NVDA] ProbeGetParent: no focused panel");
        return 0;
    }

    const char *focusName = nullptr;
    __try { focusName = g_pVGuiPanel->GetName(focused); }
    __except(EXCEPTION_EXECUTE_HANDLER) { focusName = "[SEH]"; }
    g_pSM->LogMessage(myself, "[NVDA] ProbeGetParent: focused vpanel=%u name=%s",
        (unsigned)focused, focusName ? focusName : "(null)");

    void **vtable = *(void ***)g_pVGuiPanel;

    for (int slot = 17; slot <= 25; slot++)
    {
        ProbeGetParent_t fn = (ProbeGetParent_t)vtable[slot];
        VPANEL result = 0;
        __try { result = fn(g_pVGuiPanel, focused); }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            g_pSM->LogMessage(myself, "[NVDA]   slot[%d]: SEH 0x%lX", slot, GetExceptionCode());
            continue;
        }

        if (!result)
        {
            g_pSM->LogMessage(myself, "[NVDA]   slot[%d]: returned 0", slot);
            continue;
        }

        const char *name = nullptr;
        __try { name = g_pVGuiPanel->GetName(result); }
        __except(EXCEPTION_EXECUTE_HANDLER) { name = "[SEH on GetName]"; }

        g_pSM->LogMessage(myself, "[NVDA]   slot[%d]: vpanel=%u name=%s",
            slot, (unsigned)result, name ? name : "(null)");
    }

    return 1;
}

const sp_nativeinfo_t g_Natives[] = {
    {"NVDA_GetBuffer",           Native_GetBuffer},
    {"NVDA_Speak",               Native_Speak},
    {"NVDA_CancelSpeech",        Native_CancelSpeech},
    {"NVDA_GetFocusedPanelName", Native_GetFocusedPanelName},
    {"NVDA_DumpInputVtable",     Native_DumpInputVtable},
    {"NVDA_DumpPanel",           Native_DumpPanel},
    {"NVDA_DumpPanelVtable",     Native_DumpPanelVtable},
    {"NVDA_ProbeGetParent",      Native_ProbeGetParent},
    {NULL, NULL}
};

bool NVDAExtension::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
    char dllPath[512];
    g_pSM->BuildPath(Path_SM, dllPath, sizeof(dllPath), "extensions/nvdaControllerClient32.dll");
    g_pSM->LogMessage(myself, "Trying to load NVDA client from: %s", dllPath);

    g_nvdaLib = LoadLibraryA(dllPath);
    if (!g_nvdaLib)
    {
        g_pSM->LogMessage(myself, "Not found there, trying CWD (error %lu)", GetLastError());
        g_nvdaLib = LoadLibraryA("nvdaControllerClient32.dll");
    }

    if (!g_nvdaLib)
    {
        snprintf(error, maxlength, "Could not load nvdaControllerClient32.dll (Win32 error %lu)", GetLastError());
        g_pSM->LogMessage(myself, "FAILED to load nvdaControllerClient32.dll: %s", error);
        return false;
    }

    g_speakText    = (nvdaController_speakText_t)    GetProcAddress(g_nvdaLib, "nvdaController_speakText");
    g_cancelSpeech = (nvdaController_cancelSpeech_t) GetProcAddress(g_nvdaLib, "nvdaController_cancelSpeech");
    g_testIfRunning = (nvdaController_testIfRunning_t) GetProcAddress(g_nvdaLib, "nvdaController_testIfRunning");

    g_pSM->LogMessage(myself, "nvdaControllerClient32.dll loaded OK. speakText=%p cancelSpeech=%p testIfRunning=%p",
        g_speakText, g_cancelSpeech, g_testIfRunning);

    if (g_testIfRunning)
    {
        int running = 0;
        __try { running = g_testIfRunning(); }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            g_pSM->LogMessage(myself, "testIfRunning: SEH exception 0x%lX", GetExceptionCode());
        }
        g_pSM->LogMessage(myself, "nvdaController_testIfRunning returned %d (0=running)", running);
    }

    // Get VGUI interfaces for pause-menu focus detection.
    // vgui2.dll is already loaded by the game; we borrow its factory without
    // incrementing the ref-count (GetModuleHandleA, not LoadLibraryA).
    HMODULE vgui2 = GetModuleHandleA("vgui2.dll");
    if (vgui2)
    {
        typedef void *(*CreateInterface_t)(const char *, int *);
        CreateInterface_t vguiFactory =
            (CreateInterface_t)GetProcAddress(vgui2, "CreateInterface");
        if (vguiFactory)
        {
            g_pVGuiInput = (IVGuiInput *)vguiFactory("VGUI_Input005", nullptr);
            g_pVGuiPanel = (IVGuiPanel *)vguiFactory("VGUI_Panel009", nullptr);
        }
    }
    g_focusFails = 0;
    g_pSM->LogMessage(myself, "[NVDA] VGUI input=%p panel=%p", g_pVGuiInput, g_pVGuiPanel);

    sharesys->AddNatives(myself, g_Natives);
    sharesys->RegisterLibrary(myself, "NVDA");

    return true;
}

void NVDAExtension::SDK_OnUnload()
{
    if (g_nvdaLib)
    {
        FreeLibrary(g_nvdaLib);
        g_nvdaLib = nullptr;
    }
}
