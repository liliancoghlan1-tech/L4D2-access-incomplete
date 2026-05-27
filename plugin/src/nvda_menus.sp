#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <sdktools>

#pragma dynamic 131072

// NVDA_GetBuffer() returns the address of a 2048-byte char buffer inside the
// extension DLL. We write the string there with StoreToAddress before calling
// NVDA_Speak(), which reads it directly — avoiding IPluginContext virtual calls
// that crash due to a SP 1.12/1.13 vtable mismatch in the installed SourceMod.
native int NVDA_GetBuffer();
native int NVDA_Speak();
native int NVDA_CancelSpeech();

public Extension __ext_nvda =
{
    name = "NVDA Accessibility",
    file = "nvda.ext",
    autoload = 1,
    required = 1,
};

Address g_NVDABuf;
int g_lastAimEnt[MAXPLAYERS + 1];
Handle g_aimTimer = null;

public void OnPluginStart()
{
    g_NVDABuf = Address_Null;

    for (int i = 0; i <= MAXPLAYERS; i++)
        g_lastAimEnt[i] = -1;

    RegConsoleCmd("sm_nvda_test",   Cmd_NVDATest,   "Test NVDA speech");
    RegConsoleCmd("sm_nvda_scan",   Cmd_NVDAScan,   "Dump nearby entities for debugging");
    RegConsoleCmd("sm_nvda_status", Cmd_NVDAStatus, "Announce current player status");
    RegConsoleCmd("sm_nvda_lockon", Cmd_NVDALockOn, "Lock on to nearest hostile NPC");

    HookEvent("player_say",            Event_PlayerSay);
    HookEvent("player_incapacitated",  Event_PlayerIncapped);
    HookEvent("player_death",          Event_PlayerDeath);
    HookEvent("tongue_grab",           Event_TongueGrab);
    HookEvent("lunge_pounce",          Event_LungePounce);
    HookEvent("jockey_ride",           Event_JockeyRide);
    HookEvent("charger_carry_start",   Event_ChargerCarry);
    HookEvent("item_pickup",           Event_ItemPickup);
    HookEvent("revive_success",        Event_ReviveSuccess);
    HookEvent("tongue_release",        Event_TongueRelease);
    HookEvent("charger_carry_end",     Event_ChargerCarryEnd);
    HookEvent("charger_pummel_end",    Event_ChargerPummelEnd);
    HookEventEx("jockey_ride_end",     Event_JockeyRideEnd);
    HookEventEx("pounce_stopped",      Event_HunterPounceEnd);
}

public void OnMapStart()
{
    for (int i = 0; i <= MAXPLAYERS; i++)
        g_lastAimEnt[i] = -1;

    delete g_aimTimer;
    g_aimTimer = CreateTimer(0.2, Timer_CheckAimTargets, _, TIMER_REPEAT);
}

public void OnClientPostAdminCheck(int client)
{
    if (!IsFakeClient(client))
    {
        ClientCommand(client, "bind h sm_nvda_status");
        ClientCommand(client, "bind x sm_nvda_lockon");
    }
}

public void OnClientDisconnect(int client)
{
    g_lastAimEnt[client] = -1;
}

// ── Helpers ──────────────────────────────────────────────────────────────────

void NVDASpeak(const char[] text)
{
    if (g_NVDABuf == Address_Null)
        g_NVDABuf = view_as<Address>(NVDA_GetBuffer());

    int len = strlen(text);
    if (len > 2047) len = 2047;

    for (int i = 0; i < len; i++)
        StoreToAddress(g_NVDABuf + view_as<Address>(i), text[i], NumberType_Int8);
    StoreToAddress(g_NVDABuf + view_as<Address>(len), 0, NumberType_Int8);

    NVDA_Speak();
}

void AnnounceGrab(Event event, const char[] infectedName)
{
    int victim = GetClientOfUserId(event.GetInt("victim"));
    if (victim < 1 || !IsClientInGame(victim))
        return;

    char playerName[64];
    char msg[96];
    GetClientName(victim, playerName, sizeof(playerName));
    Format(msg, sizeof(msg), "%s grabbed by %s.", playerName, infectedName);
    NVDASpeak(msg);
}

// ── Item name lookups ─────────────────────────────────────────────────────────

bool GetFriendlyItemNameByKey(const char[] key, char[] output, int maxlen)
{
    // Tier 1
    if (StrEqual(key, "pistol"))                 { strcopy(output, maxlen, "Pistol");                return true; }
    if (StrEqual(key, "pistol_magnum"))          { strcopy(output, maxlen, "Magnum Pistol");         return true; }
    if (StrEqual(key, "smg"))                    { strcopy(output, maxlen, "SMG");                   return true; }
    if (StrEqual(key, "smg_silenced"))           { strcopy(output, maxlen, "Silenced SMG");          return true; }
    if (StrEqual(key, "smg_mp5"))                { strcopy(output, maxlen, "MP5");                   return true; }
    if (StrEqual(key, "pumpshotgun"))            { strcopy(output, maxlen, "Pump Shotgun");          return true; }
    if (StrEqual(key, "shotgun_chrome"))         { strcopy(output, maxlen, "Chrome Shotgun");        return true; }
    // Tier 2
    if (StrEqual(key, "rifle"))                  { strcopy(output, maxlen, "Assault Rifle");         return true; }
    if (StrEqual(key, "rifle_ak47"))             { strcopy(output, maxlen, "AK-47");                 return true; }
    if (StrEqual(key, "rifle_desert"))           { strcopy(output, maxlen, "Desert Rifle");          return true; }
    if (StrEqual(key, "rifle_m60"))              { strcopy(output, maxlen, "M60");                   return true; }
    if (StrEqual(key, "rifle_sg552"))            { strcopy(output, maxlen, "SG 552");                return true; }
    if (StrEqual(key, "autoshotgun"))            { strcopy(output, maxlen, "Auto Shotgun");          return true; }
    if (StrEqual(key, "shotgun_spas"))           { strcopy(output, maxlen, "SPAS-12");               return true; }
    if (StrEqual(key, "hunting_rifle"))          { strcopy(output, maxlen, "Hunting Rifle");         return true; }
    if (StrEqual(key, "sniper_military"))        { strcopy(output, maxlen, "Military Sniper");       return true; }
    if (StrEqual(key, "sniper_scout"))           { strcopy(output, maxlen, "Scout Sniper");          return true; }
    if (StrEqual(key, "sniper_awp"))             { strcopy(output, maxlen, "AWP");                   return true; }
    // Special
    if (StrEqual(key, "grenade_launcher"))       { strcopy(output, maxlen, "Grenade Launcher");      return true; }
    if (StrEqual(key, "chainsaw"))               { strcopy(output, maxlen, "Chainsaw");              return true; }
    if (StrEqual(key, "melee"))                  { strcopy(output, maxlen, "Melee Weapon");          return true; }
    // Healing
    if (StrEqual(key, "first_aid_kit"))          { strcopy(output, maxlen, "First Aid Kit");         return true; }
    if (StrEqual(key, "defibrillator"))          { strcopy(output, maxlen, "Defibrillator");         return true; }
    if (StrEqual(key, "adrenaline"))             { strcopy(output, maxlen, "Adrenaline");            return true; }
    if (StrEqual(key, "pain_pills"))             { strcopy(output, maxlen, "Pain Pills");            return true; }
    // Throwables
    if (StrEqual(key, "molotov"))                { strcopy(output, maxlen, "Molotov");               return true; }
    if (StrEqual(key, "pipe_bomb"))              { strcopy(output, maxlen, "Pipe Bomb");             return true; }
    if (StrEqual(key, "vomitjar"))               { strcopy(output, maxlen, "Bile Bomb");             return true; }
    // Upgrades
    if (StrEqual(key, "upgradepack_explosive"))  { strcopy(output, maxlen, "Explosive Ammo Pack");   return true; }
    if (StrEqual(key, "upgradepack_incendiary")) { strcopy(output, maxlen, "Incendiary Ammo Pack");  return true; }
    // Ammo pile
    if (StrEqual(key, "ammo"))                   { strcopy(output, maxlen, "Ammo Pile");             return true; }

    return false;
}

bool GetFriendlyItemNameForEnt(const char[] classname, int ent, char[] output, int maxlen)
{
    char key[64];
    if (strncmp(classname, "weapon_", 7) == 0)
        strcopy(key, sizeof(key), classname[7]);
    else
        strcopy(key, sizeof(key), classname);

    int keyLen = strlen(key);
    if (keyLen > 6)
    {
        char tail[7];
        strcopy(tail, sizeof(tail), key[keyLen - 6]);
        if (StrEqual(tail, "_spawn"))
            key[keyLen - 6] = '\0';
    }

    if (StrEqual(key, "melee"))
    {
        if (HasEntProp(ent, Prop_Data, "m_iszScriptName"))
        {
            char script[64];
            GetEntPropString(ent, Prop_Data, "m_iszScriptName", script, sizeof(script));
            if (strlen(script) > 0)
                return GetFriendlyMeleeName(script, output, maxlen);
        }
        strcopy(output, maxlen, "Melee Weapon");
        return true;
    }

    return GetFriendlyItemNameByKey(key, output, maxlen);
}

bool GetFriendlyMeleeName(const char[] script, char[] output, int maxlen)
{
    if (StrEqual(script, "baseball_bat"))    { strcopy(output, maxlen, "Baseball Bat");   return true; }
    if (StrEqual(script, "crowbar"))         { strcopy(output, maxlen, "Crowbar");        return true; }
    if (StrEqual(script, "fireaxe"))         { strcopy(output, maxlen, "Fire Axe");       return true; }
    if (StrEqual(script, "frying_pan"))      { strcopy(output, maxlen, "Frying Pan");     return true; }
    if (StrEqual(script, "electric_guitar")) { strcopy(output, maxlen, "Guitar");         return true; }
    if (StrEqual(script, "machete"))         { strcopy(output, maxlen, "Machete");        return true; }
    if (StrEqual(script, "cricket_bat"))     { strcopy(output, maxlen, "Cricket Bat");    return true; }
    if (StrEqual(script, "tonfa"))           { strcopy(output, maxlen, "Nightstick");     return true; }
    if (StrEqual(script, "katana"))          { strcopy(output, maxlen, "Katana");         return true; }
    if (StrEqual(script, "pitchfork"))       { strcopy(output, maxlen, "Pitchfork");      return true; }
    if (StrEqual(script, "shovel"))          { strcopy(output, maxlen, "Shovel");         return true; }
    if (StrEqual(script, "knife"))           { strcopy(output, maxlen, "Knife");          return true; }
    if (StrEqual(script, "gnome"))           { strcopy(output, maxlen, "Garden Gnome");   return true; }
    strcopy(output, maxlen, "Melee Weapon");
    return true;
}

// ── Aim-target polling ────────────────────────────────────────────────────────

int FindAimedItem(int client)
{
    float eyePos[3], eyeAng[3];
    GetClientEyePosition(client, eyePos);
    GetClientEyeAngles(client, eyeAng);

    float fwd[3];
    GetAngleVectors(eyeAng, fwd, NULL_VECTOR, NULL_VECTOR);

    int   best    = -1;
    float bestDot = 0.90;

    int maxEnts = GetMaxEntities();
    for (int ent = MaxClients + 1; ent < maxEnts; ent++)
    {
        if (!IsValidEdict(ent) || !IsValidEntity(ent))
            continue;

        char cls[64];
        GetEntityClassname(ent, cls, sizeof(cls));
        if (strncmp(cls, "weapon_", 7) != 0)
            continue;

        int owner = GetEntPropEnt(ent, Prop_Send, "m_hOwnerEntity");
        if (owner >= 1 && owner <= MaxClients && IsClientInGame(owner))
            continue;

        if (!HasEntProp(ent, Prop_Send, "m_vecOrigin"))
            continue;

        float entPos[3];
        GetEntPropVector(ent, Prop_Send, "m_vecOrigin", entPos);
        entPos[2] += 20.0;

        float dir[3];
        SubtractVectors(entPos, eyePos, dir);
        float dist = GetVectorLength(dir);
        if (dist > 180.0)
            continue;

        NormalizeVector(dir, dir);
        float dot = GetVectorDotProduct(fwd, dir);
        if (dot > bestDot)
        {
            bestDot = dot;
            best = ent;
        }
    }

    return best;
}

public Action Timer_CheckAimTargets(Handle timer)
{
    if (IsDedicatedServer())
        return Plugin_Continue;

    int client = 1;
    if (!IsClientInGame(client) || !IsPlayerAlive(client) || GetClientTeam(client) != 2)
        return Plugin_Continue;

    int target = FindAimedItem(client);

    if (target == g_lastAimEnt[client])
        return Plugin_Continue;

    g_lastAimEnt[client] = target;

    if (target < 1)
        return Plugin_Continue;

    char classname[64];
    GetEntityClassname(target, classname, sizeof(classname));

    char friendlyName[64];
    if (GetFriendlyItemNameForEnt(classname, target, friendlyName, sizeof(friendlyName)))
        NVDASpeak(friendlyName);

    return Plugin_Continue;
}

// ── Item pickup ───────────────────────────────────────────────────────────────

public Action Event_ItemPickup(Event event, const char[] name, bool dontBroadcast)
{
    if (IsDedicatedServer())
        return Plugin_Continue;

    int client = GetClientOfUserId(event.GetInt("userid"));
    if (client != 1)
        return Plugin_Continue;

    char item[64];
    event.GetString("item", item, sizeof(item));

    char key[64];
    if (strncmp(item, "weapon_", 7) == 0)
        strcopy(key, sizeof(key), item[7]);
    else
        strcopy(key, sizeof(key), item);

    char friendlyName[64];
    if (GetFriendlyItemNameByKey(key, friendlyName, sizeof(friendlyName)))
    {
        char msg[80];
        Format(msg, sizeof(msg), "Picked up %s.", friendlyName);
        NVDASpeak(msg);
    }

    return Plugin_Continue;
}

// ── Rescue / revive ───────────────────────────────────────────────────────────

public Action Event_ReviveSuccess(Event event, const char[] name, bool dontBroadcast)
{
    int reviver = GetClientOfUserId(event.GetInt("userid"));
    int revived  = GetClientOfUserId(event.GetInt("subject"));

    if (revived < 1 || !IsClientInGame(revived))
        return Plugin_Continue;

    char revivedName[64];
    GetClientName(revived, revivedName, sizeof(revivedName));
    char msg[128];

    if (reviver >= 1 && IsClientInGame(reviver) && reviver != revived)
    {
        char reviverName[64];
        GetClientName(reviver, reviverName, sizeof(reviverName));
        Format(msg, sizeof(msg), "%s helped up %s.", reviverName, revivedName);
    }
    else
    {
        Format(msg, sizeof(msg), "%s got up.", revivedName);
    }

    NVDASpeak(msg);
    return Plugin_Continue;
}

public Action Event_TongueRelease(Event event, const char[] name, bool dontBroadcast)
{
    int victim = GetClientOfUserId(event.GetInt("victim"));
    if (victim < 1 || !IsClientInGame(victim))
        return Plugin_Continue;

    char playerName[64];
    char msg[96];
    GetClientName(victim, playerName, sizeof(playerName));
    Format(msg, sizeof(msg), "%s freed from Smoker.", playerName);
    NVDASpeak(msg);
    return Plugin_Continue;
}

public Action Event_ChargerCarryEnd(Event event, const char[] name, bool dontBroadcast)
{
    int victim = GetClientOfUserId(event.GetInt("victim"));
    if (victim < 1 || !IsClientInGame(victim))
        return Plugin_Continue;

    char playerName[64];
    char msg[96];
    GetClientName(victim, playerName, sizeof(playerName));
    Format(msg, sizeof(msg), "%s freed from Charger.", playerName);
    NVDASpeak(msg);
    return Plugin_Continue;
}

public Action Event_ChargerPummelEnd(Event event, const char[] name, bool dontBroadcast)
{
    int victim = GetClientOfUserId(event.GetInt("victim"));
    if (victim < 1 || !IsClientInGame(victim))
        return Plugin_Continue;

    char playerName[64];
    char msg[96];
    GetClientName(victim, playerName, sizeof(playerName));
    Format(msg, sizeof(msg), "%s rescued from Charger pummel.", playerName);
    NVDASpeak(msg);
    return Plugin_Continue;
}

public Action Event_JockeyRideEnd(Event event, const char[] name, bool dontBroadcast)
{
    int victim = GetClientOfUserId(event.GetInt("victim"));
    if (victim < 1 || !IsClientInGame(victim))
        return Plugin_Continue;

    char playerName[64];
    char msg[96];
    GetClientName(victim, playerName, sizeof(playerName));
    Format(msg, sizeof(msg), "%s freed from Jockey.", playerName);
    NVDASpeak(msg);
    return Plugin_Continue;
}

public Action Event_HunterPounceEnd(Event event, const char[] name, bool dontBroadcast)
{
    int victim = GetClientOfUserId(event.GetInt("victim"));
    if (victim < 1 || !IsClientInGame(victim))
        return Plugin_Continue;

    char playerName[64];
    char msg[96];
    GetClientName(victim, playerName, sizeof(playerName));
    Format(msg, sizeof(msg), "%s freed from Hunter.", playerName);
    NVDASpeak(msg);
    return Plugin_Continue;
}

// ── Game events ───────────────────────────────────────────────────────────────

public Action Event_PlayerSay(Event event, const char[] name, bool dontBroadcast)
{
    int client = GetClientOfUserId(event.GetInt("userid"));

    char playerName[64];
    char message[256];
    char fullMsg[320];

    GetClientName(client, playerName, sizeof(playerName));
    event.GetString("text", message, sizeof(message));
    Format(fullMsg, sizeof(fullMsg), "%s: %s", playerName, message);

    NVDASpeak(fullMsg);
    return Plugin_Continue;
}

public Action Event_PlayerIncapped(Event event, const char[] name, bool dontBroadcast)
{
    int client = GetClientOfUserId(event.GetInt("userid"));
    char playerName[64];
    char msg[80];
    GetClientName(client, playerName, sizeof(playerName));
    Format(msg, sizeof(msg), "%s is down.", playerName);
    NVDASpeak(msg);
    return Plugin_Continue;
}

public Action Event_PlayerDeath(Event event, const char[] name, bool dontBroadcast)
{
    int client = GetClientOfUserId(event.GetInt("userid"));
    if (client < 1 || !IsClientInGame(client) || GetClientTeam(client) != 2)
        return Plugin_Continue;

    char playerName[64];
    char msg[80];
    GetClientName(client, playerName, sizeof(playerName));
    Format(msg, sizeof(msg), "%s died.", playerName);
    NVDASpeak(msg);
    return Plugin_Continue;
}

public Action Event_TongueGrab(Event event, const char[] name, bool dontBroadcast)
{
    AnnounceGrab(event, "Smoker");
    return Plugin_Continue;
}

public Action Event_LungePounce(Event event, const char[] name, bool dontBroadcast)
{
    AnnounceGrab(event, "Hunter");
    return Plugin_Continue;
}

public Action Event_JockeyRide(Event event, const char[] name, bool dontBroadcast)
{
    AnnounceGrab(event, "Jockey");
    return Plugin_Continue;
}

public Action Event_ChargerCarry(Event event, const char[] name, bool dontBroadcast)
{
    AnnounceGrab(event, "Charger");
    return Plugin_Continue;
}

// ── Lock-on ───────────────────────────────────────────────────────────────────

bool IsHostileClass(const char[] cls)
{
    return StrEqual(cls, "infected") || StrEqual(cls, "witch")   ||
           StrEqual(cls, "boomer")   || StrEqual(cls, "smoker")  ||
           StrEqual(cls, "hunter")   || StrEqual(cls, "spitter") ||
           StrEqual(cls, "jockey")   || StrEqual(cls, "charger") ||
           StrEqual(cls, "tank");
}

// Tank > specials > witch > common infected
int GetHostilePriority(const char[] cls)
{
    if (StrEqual(cls, "tank"))                                    return 3;
    if (StrEqual(cls, "boomer")  || StrEqual(cls, "smoker")  ||
        StrEqual(cls, "hunter")  || StrEqual(cls, "spitter") ||
        StrEqual(cls, "jockey")  || StrEqual(cls, "charger"))     return 2;
    if (StrEqual(cls, "witch"))                                    return 1;
    return 0;
}

void GetHostileFriendlyName(const char[] cls, char[] output, int maxlen)
{
    if      (StrEqual(cls, "infected")) strcopy(output, maxlen, "Common Infected");
    else if (StrEqual(cls, "witch"))    strcopy(output, maxlen, "Witch");
    else if (StrEqual(cls, "boomer"))   strcopy(output, maxlen, "Boomer");
    else if (StrEqual(cls, "smoker"))   strcopy(output, maxlen, "Smoker");
    else if (StrEqual(cls, "hunter"))   strcopy(output, maxlen, "Hunter");
    else if (StrEqual(cls, "spitter"))  strcopy(output, maxlen, "Spitter");
    else if (StrEqual(cls, "jockey"))   strcopy(output, maxlen, "Jockey");
    else if (StrEqual(cls, "charger"))  strcopy(output, maxlen, "Charger");
    else if (StrEqual(cls, "tank"))     strcopy(output, maxlen, "Tank");
    else                                strcopy(output, maxlen, cls);
}

public Action Cmd_NVDALockOn(int client, int args)
{
    if (client == 0) client = 1;

    if (!IsClientInGame(client) || !IsPlayerAlive(client))
    {
        NVDASpeak("Not in game.");
        return Plugin_Handled;
    }

    // PVE only — block in versus and scavenge
    ConVar cvMode = FindConVar("mp_gamemode");
    if (cvMode != null)
    {
        char gamemode[32];
        cvMode.GetString(gamemode, sizeof(gamemode));
        if (StrContains(gamemode, "versus", false) != -1 ||
            StrEqual(gamemode, "scavenge", false))
        {
            NVDASpeak("Lock-on not available in this mode.");
            return Plugin_Handled;
        }
    }

    float clientPos[3];
    GetClientAbsOrigin(client, clientPos);

    int   bestEnt      = -1;
    int   bestPriority = -1;
    float bestDist     = 99999.0;
    char  bestCls[32];

    // ── Pass 1: client slots — special infected are bots on team 3 ───────────
    // They have classname "player"; use m_zombieClass to identify type.
    for (int i = 1; i <= MaxClients; i++)
    {
        if (i == client || !IsClientInGame(i) || !IsPlayerAlive(i)) continue;
        if (GetClientTeam(i) != 3) continue;

        char cls[32];
        int  priority;
        int  zc = GetEntProp(i, Prop_Send, "m_zombieClass");
        if      (zc == 1) { strcopy(cls, sizeof(cls), "smoker");  priority = 2; }
        else if (zc == 2) { strcopy(cls, sizeof(cls), "boomer");  priority = 2; }
        else if (zc == 3) { strcopy(cls, sizeof(cls), "hunter");  priority = 2; }
        else if (zc == 4) { strcopy(cls, sizeof(cls), "spitter"); priority = 2; }
        else if (zc == 5) { strcopy(cls, sizeof(cls), "jockey");  priority = 2; }
        else if (zc == 6) { strcopy(cls, sizeof(cls), "charger"); priority = 2; }
        else if (zc == 8) { strcopy(cls, sizeof(cls), "tank");    priority = 3; }
        else continue;

        float entPos[3];
        GetClientAbsOrigin(i, entPos);
        float dist = GetVectorDistance(clientPos, entPos);
        if (dist > 2000.0) continue;

        // Boost anything within 300 units to at least special priority
        if (dist <= 300.0 && priority < 2) priority = 2;

        if (priority > bestPriority || (priority == bestPriority && dist < bestDist))
        {
            bestPriority = priority;
            bestDist     = dist;
            bestEnt      = i;
            strcopy(bestCls, sizeof(bestCls), cls);
        }
    }

    // ── Pass 2: world entities — common infected and witch ────────────────────
    int maxEnts = GetMaxEntities();
    for (int ent = MaxClients + 1; ent < maxEnts; ent++)
    {
        if (!IsValidEntity(ent) || !IsValidEdict(ent))
            continue;

        char cls[64];
        GetEntityClassname(ent, cls, sizeof(cls));
        if (!IsHostileClass(cls))
            continue;

        if (!HasEntProp(ent, Prop_Data, "m_lifeState") ||
            GetEntProp(ent, Prop_Data, "m_lifeState") != 0)
            continue;

        if (!HasEntProp(ent, Prop_Send, "m_vecOrigin"))
            continue;

        float entPos[3];
        GetEntPropVector(ent, Prop_Send, "m_vecOrigin", entPos);
        float dist = GetVectorDistance(clientPos, entPos);
        if (dist > 2000.0) continue;

        int priority = GetHostilePriority(cls);
        // Boost anything within 300 units to at least special priority
        if (dist <= 300.0 && priority < 2) priority = 2;

        if (priority > bestPriority || (priority == bestPriority && dist < bestDist))
        {
            bestPriority = priority;
            bestDist     = dist;
            bestEnt      = ent;
            strcopy(bestCls, sizeof(bestCls), cls);
        }
    }

    if (bestEnt == -1)
    {
        NVDASpeak("No hostiles nearby.");
        return Plugin_Handled;
    }

    // Save weapon attack timers — TeleportEntity resets them in L4D2
    int wep = GetEntPropEnt(client, Prop_Send, "m_hActiveWeapon");
    float nextPrimary   = -1.0;
    float nextSecondary = -1.0;
    if (wep > 0 && IsValidEntity(wep))
    {
        nextPrimary   = GetEntPropFloat(wep, Prop_Send, "m_flNextPrimaryAttack");
        nextSecondary = GetEntPropFloat(wep, Prop_Send, "m_flNextSecondaryAttack");
    }

    // Snap view to face the target
    float eyePos[3], entPos[3], dir[3], angles[3];
    GetClientEyePosition(client, eyePos);

    if (bestEnt >= 1 && bestEnt <= MaxClients)
        GetClientAbsOrigin(bestEnt, entPos);
    else
        GetEntPropVector(bestEnt, Prop_Send, "m_vecOrigin", entPos);

    entPos[2] += 40.0;
    SubtractVectors(entPos, eyePos, dir);
    GetVectorAngles(dir, angles);
    angles[2] = 0.0;
    TeleportEntity(client, NULL_VECTOR, angles, NULL_VECTOR);

    // Restore attack timers
    if (wep > 0 && IsValidEntity(wep) && nextPrimary >= 0.0)
    {
        SetEntPropFloat(wep, Prop_Send, "m_flNextPrimaryAttack",   nextPrimary);
        SetEntPropFloat(wep, Prop_Send, "m_flNextSecondaryAttack", nextSecondary);
    }

    // Announce
    char hostileName[64];
    GetHostileFriendlyName(bestCls, hostileName, sizeof(hostileName));
    NVDASpeak(hostileName);

    return Plugin_Handled;
}

public Action Cmd_NVDAStatus(int client, int args)
{
    if (client == 0) client = 1;

    if (!IsClientInGame(client) || !IsPlayerAlive(client))
    {
        NVDASpeak("Not in game.");
        return Plugin_Handled;
    }

    char status[384];

    // Health
    int hp = GetClientHealth(client);
    float tempHpF = GetEntPropFloat(client, Prop_Send, "m_healthBuffer");
    int tempHp = RoundToFloor(tempHpF);

    if (GetEntProp(client, Prop_Send, "m_isIncapacitated"))
        Format(status, sizeof(status), "Incapacitated. Health %d.", hp);
    else if (tempHp > 0)
        Format(status, sizeof(status), "Health %d, plus %d temporary.", hp, tempHp);
    else
        Format(status, sizeof(status), "Health %d.", hp);

    // Active weapon
    int activeWep = GetEntPropEnt(client, Prop_Send, "m_hActiveWeapon");
    if (activeWep > 0 && IsValidEntity(activeWep))
    {
        char cls[64], wepName[64];
        GetEntityClassname(activeWep, cls, sizeof(cls));
        if (GetFriendlyItemNameForEnt(cls, activeWep, wepName, sizeof(wepName)))
            Format(status, sizeof(status), "%s Holding %s.", status, wepName);
    }

    // Throwable (slot 2)
    int throwable = GetPlayerWeaponSlot(client, 2);
    if (throwable > 0 && IsValidEntity(throwable))
    {
        char cls[64], itemName[64];
        GetEntityClassname(throwable, cls, sizeof(cls));
        if (GetFriendlyItemNameForEnt(cls, throwable, itemName, sizeof(itemName)))
            Format(status, sizeof(status), "%s Has %s.", status, itemName);
    }

    // Medkit / defibrillator (slot 3)
    int medical = GetPlayerWeaponSlot(client, 3);
    if (medical > 0 && IsValidEntity(medical))
    {
        char cls[64], itemName[64];
        GetEntityClassname(medical, cls, sizeof(cls));
        if (GetFriendlyItemNameForEnt(cls, medical, itemName, sizeof(itemName)))
            Format(status, sizeof(status), "%s Has %s.", status, itemName);
    }

    // Pills / adrenaline (slot 4)
    int pills = GetPlayerWeaponSlot(client, 4);
    if (pills > 0 && IsValidEntity(pills))
    {
        char cls[64], itemName[64];
        GetEntityClassname(pills, cls, sizeof(cls));
        if (GetFriendlyItemNameForEnt(cls, pills, itemName, sizeof(itemName)))
            Format(status, sizeof(status), "%s Has %s.", status, itemName);
    }

    NVDASpeak(status);
    return Plugin_Handled;
}

public Action Cmd_NVDATest(int client, int args)
{
    NVDASpeak("NVDA test from Left 4 Dead 2.");
    return Plugin_Handled;
}

public Action Cmd_NVDAScan(int client, int args)
{
    int scanner = (client == 0) ? 1 : client;
    if (!IsClientInGame(scanner))
    {
        PrintToConsole(client, "[NVDA] No valid player to scan from.");
        return Plugin_Handled;
    }

    float myPos[3];
    GetClientAbsOrigin(scanner, myPos);

    PrintToConsole(client, "=== NVDA entity scan (within 300 units of client %d) ===", scanner);

    int maxEnts = GetMaxEntities();
    int found = 0;
    for (int ent = 1; ent < maxEnts; ent++)
    {
        if (!IsValidEntity(ent))
            continue;

        if (!HasEntProp(ent, Prop_Send, "m_vecOrigin"))
            continue;

        float entPos[3];
        GetEntPropVector(ent, Prop_Send, "m_vecOrigin", entPos);
        float dist = GetVectorDistance(myPos, entPos);
        if (dist > 300.0)
            continue;

        char cls[64];
        GetEntityClassname(ent, cls, sizeof(cls));

        int owner = -1;
        if (HasEntProp(ent, Prop_Send, "m_hOwnerEntity"))
            owner = GetEntPropEnt(ent, Prop_Send, "m_hOwnerEntity");

        PrintToConsole(client, "ent=%d cls=%s dist=%.1f owner=%d", ent, cls, dist, owner);
        found++;
    }

    PrintToConsole(client, "=== %d entities found ===", found);
    return Plugin_Handled;
}
