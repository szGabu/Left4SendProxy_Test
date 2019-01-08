/**
 * vim: set ts=4 :
 * =============================================================================
 * SendVar Proxy Manager
 * Copyright (C) 2011 Afronanny.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */
 
 /*
	TODO:
		gamerules props should also sends individually for each client
		more optimizations! =D
 */

#ifdef _WIN32
#undef GetProp
#ifdef _WIN64
	#define PLATFORM_x64
#else
	#define PLATFORM_x32
#endif
#elif defined __linux__
	#if defined __x86_64__
		#define PLATFORM_x64
	#else
		#define PLATFORM_x32
	#endif
#endif

#ifdef _WIN32
	#define FAKECLIENT_KEY "CreateFakeClient_Windows"
#elif defined __linux__
	#ifdef PLATFORM_x64
		#define FAKECLIENT_KEY "CreateFakeClient_Linux64"
	#else
		#define FAKECLIENT_KEY "CreateFakeClient_Linux"
	#endif
#elif defined PLATFORM_APPLE
	#define FAKECLIENT_KEY "CreateFakeClient_Mac"
#else
	#error "Unsupported platform"
#endif

#include "CDetour/detours.h"
#include "extension.h"

#include <ISDKTools.h>
//path: hl2sdk-<your sdk here>/public/<include>.h, "../public/" included to prevent compile errors due wrong directory scanning by compiler on my computer, and I'm too lazy to find where I can change that =D
#include <../public/eiface.h>
#include <../public/iserver.h>
#include <../public/iclient.h>

SH_DECL_HOOK1_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, false, edict_t *);
SH_DECL_HOOK1_void(IServerGameDLL, GameFrame, SH_NOATTRIB, false, bool);
SH_DECL_HOOK0(IServer, GetClientCount, const, false, int);

DECL_DETOUR(CGameServer_SendClientMessages);
DECL_DETOUR(CGameClient_ShouldSendMessages);
#if SOURCE_ENGINE != SE_CSGO
DECL_DETOUR(SV_ComputeClientPacks);
#endif

#ifdef PLATFORM_x64
	#include <stdint.h>
	#define int_for_clptr int64_t
#else
	#define int_for_clptr int
#endif

class CGameClient;
class CFrameSnapshot;

//we will use integer to store pointer lol
int_for_clptr g_iCurrentGameClientPtr = 0;
int g_iCurrentClientIndexInLoop = -1; //used for optimization
bool g_bCurrentGameClientCallFwd = false;
bool g_bCallingForNullClients = false;
bool g_bFirstTimeCalled = true;
bool g_bSVComputePacksDone = false;
IServer * g_pIServer = nullptr;

SendProxyManager g_SendProxyManager;
SMEXT_LINK(&g_SendProxyManager);

CThreadFastMutex g_WorkMutex;

CUtlVector<SendPropHook> g_Hooks;
CUtlVector<SendPropHookGamerules> g_HooksGamerules;
CUtlVector<PropChangeHook> g_ChangeHooks;
CUtlVector<PropChangeHookGamerules> g_ChangeHooksGamerules;
CUtlVector<edict_t *> g_vHookedEdicts;

IServerGameEnts * gameents = nullptr;
IServerGameClients * gameclients = nullptr;
ISDKTools * g_pSDKTools = nullptr;
ISDKHooks * g_pSDKHooks = nullptr;
IGameConfig * g_pGameConf = nullptr;
IGameConfig * g_pGameConfSDKTools = nullptr;

ConVar * sv_parallel_packentities = nullptr;

static cell_t Native_Hook(IPluginContext * pContext, const cell_t  * params);
static cell_t Native_HookGameRules(IPluginContext * pContext, const cell_t * params);
static cell_t Native_Unhook(IPluginContext * pContext, const cell_t * params);
static cell_t Native_UnhookGameRules(IPluginContext * pContext, const cell_t * params);
static cell_t Native_IsHooked(IPluginContext * pContext, const cell_t * params);
static cell_t Native_IsHookedGameRules(IPluginContext * pContext, const cell_t * params);
static cell_t Native_HookArrayProp(IPluginContext * pContext, const cell_t * params);
static cell_t Native_UnhookArrayProp(IPluginContext * pContext, const cell_t * params);
static cell_t Native_HookPropChange(IPluginContext * pContext, const cell_t * params);
static cell_t Native_HookPropChangeGameRules(IPluginContext * pContext, const cell_t * params);
static cell_t Native_UnhookPropChange(IPluginContext * pContext, const cell_t * params);
static cell_t Native_UnhookPropChangeGameRules(IPluginContext * pContext, const cell_t * params);

static IServer * GetIServer();

const char * g_szGameRulesProxy;

const sp_nativeinfo_t g_MyNatives[] = {
	{"SendProxy_Hook", Native_Hook},
	{"SendProxy_HookGameRules", Native_HookGameRules},
	{"SendProxy_HookArrayProp", Native_HookArrayProp},
	{"SendProxy_UnhookArrayProp", Native_UnhookArrayProp},
	{"SendProxy_Unhook", Native_Unhook},
	{"SendProxy_UnhookGameRules", Native_UnhookGameRules},
	{"SendProxy_IsHooked", Native_IsHooked},
	{"SendProxy_IsHookedGameRules", Native_IsHookedGameRules},
	{"SendProxy_HookPropChange", Native_HookPropChange},
	{"SendProxy_HookPropChangeGameRules", Native_HookPropChangeGameRules},
	{"SendProxy_UnhookPropChange", Native_UnhookPropChange},
	{"SendProxy_UnhookPropChangeGameRules", Native_UnhookPropChangeGameRules},
	{NULL,	NULL},
};

//detours

/*Call stack:
	...
	1. CGameServer::SendClientMessages //function we hooking to send props individually for each client
	2. SV_ComputeClientPacks //function we hooking to set edicts state and to know, need we call callbacks or not, but not in csgo
	3. PackEntities_Normal //if we in multiplayer
	4. SV_PackEntity //also we can hook this instead hooking ProxyFn, but there no reason to do that
	5. SendTable_Encode
	6. SendTable_EncodeProp //here the ProxyFn will be called
	7. ProxyFn //here our callbacks is called
*/

DETOUR_DECL_MEMBER1(CGameServer_SendClientMessages, void, bool, bSendSnapshots)
{
	if (!bSendSnapshots)
		return DETOUR_MEMBER_CALL(CGameServer_SendClientMessages)(false); //if so, we do not interested in this call
	if (!g_pIServer && g_pSDKTools)
		g_pIServer = g_pSDKTools->GetIServer();
	if (!g_pIServer)
		return DETOUR_MEMBER_CALL(CGameServer_SendClientMessages)(true); //if so, we should stop to process this function! See below
	if (g_bFirstTimeCalled)
	{
#ifdef _WIN32
		//HACK, don't delete this, or server will be crashed on start!
		g_pIServer->GetClientCount();
#endif
		SH_ADD_HOOK(IServer, GetClientCount, g_pIServer, SH_MEMBER(&g_SendProxyManager, &SendProxyManager::GetClientCount), false);
		g_bFirstTimeCalled = false;
	}
	bool bCalledForNullIClientsThisTime = false;
	for (int iClients = 1; iClients <= playerhelpers->GetMaxClients(); iClients++)
	{
		IGamePlayer * pPlayer = playerhelpers->GetGamePlayer(iClients);
		bool bFake = (pPlayer->IsFakeClient() && !(pPlayer->IsSourceTV()
#if SOURCE_ENGINE == SE_TF2
		|| pPlayer->IsReplay()
#endif
		));
		volatile IClient * pClient = nullptr; //volatile used to prevent optimizations here for some reason
		if (!pPlayer->IsConnected() || bFake || (pClient = g_pIServer->GetClient(iClients - 1)) == nullptr)
		{
			if (!bCalledForNullIClientsThisTime && !g_bCallingForNullClients)
			{
				g_bCurrentGameClientCallFwd = false;
				g_bCallingForNullClients = true;
				DETOUR_MEMBER_CALL(CGameServer_SendClientMessages)(true);
				g_bCallingForNullClients = false;
			}
			bCalledForNullIClientsThisTime = true;
			continue;
		}
		if (!pPlayer->IsInGame() || bFake) //We should call SV_ComputeClientPacks, but shouldn't call forwards!
			g_bCurrentGameClientCallFwd = false;
		else
			g_bCurrentGameClientCallFwd = true;
		g_iCurrentGameClientPtr = reinterpret_cast<int_for_clptr>(pClient) - 4;
		g_iCurrentClientIndexInLoop = iClients - 1;
		DETOUR_MEMBER_CALL(CGameServer_SendClientMessages)(true);
	}
	g_bCurrentGameClientCallFwd = false;
	g_iCurrentClientIndexInLoop = -1;
}

DETOUR_DECL_MEMBER0(CGameClient_ShouldSendMessages, bool)
{
#if SOURCE_ENGINE == SE_CSGO
	g_bSVComputePacksDone = false;
#endif
	if (g_bCallingForNullClients)
	{
		IClient * pClient = (IClient *)((char *)this + 4);
#if SOURCE_ENGINE == SE_TF2
		//don't remove this code
		int iUserID = pClient->GetUserID();
		IGamePlayer * pPlayer = playerhelpers->GetGamePlayer(pClient->GetPlayerSlot() + 1);
		if (pPlayer->GetUserId() != iUserID) //if so, there something went wrong, check this now!
#endif
		{
			if (pClient->IsHLTV()
#if SOURCE_ENGINE == SE_TF2
			|| pClient->IsReplay()
#endif
			|| (pClient->IsConnected() && !pClient->IsActive()))
				return true; //Also we need to allow connect for inactivated clients, sourcetv & replay
		}
		return false;
	}
	bool bOriginalResult = DETOUR_MEMBER_CALL(CGameClient_ShouldSendMessages)();
	if (!bOriginalResult)
		return false;
	if (reinterpret_cast<int_for_clptr>(this) == g_iCurrentGameClientPtr)
	{
#if SOURCE_ENGINE == SE_CSGO
		//if we in csgo, we should do stuff from SV_ComputeClientPacks here, or server will crash when SV_ComputeClientPacks is called
		IClient * pClient = (IClient *)((char *)this + 4);
		int iClient = pClient->GetPlayerSlot();
		if (g_iCurrentClientIndexInLoop == iClient)
		{
			for (int i = 0; i < g_vHookedEdicts.Count(); i++)
			{
				edict_t * pEdict = g_vHookedEdicts[i];
				if (pEdict && !(pEdict->m_fStateFlags & FL_EDICT_CHANGED))
					pEdict->m_fStateFlags |= FL_EDICT_CHANGED;
			}
			if (g_bCurrentGameClientCallFwd)
				g_bSVComputePacksDone = true;
		}
#endif
		return true;
	}
#if defined PLATFORM_x32 && SOURCE_ENGINE == SE_TF2 //I'm too lazy to do same optimization for csgo, somebody else can do this if he want
	else
	{
		int iTemp = 0, iToSet = g_iCurrentClientIndexInLoop - 1;
		//just set the loop var to needed for us value, some optimization
#ifdef _WIN32
		__asm mov iTemp, ebx
		if (iTemp < iToSet)
			__asm mov ebx, iToSet
#elif defined __linux__
		asm("movl %%esi, %0" : "=r" (iTemp));
		if (iTemp < iToSet)
			asm("movl %0, %%esi" : "=r" (iToSet));
#endif
	}
#endif
	return false;
}

#if SOURCE_ENGINE != SE_CSGO
DETOUR_DECL_STATIC3(SV_ComputeClientPacks, void, int, iClientCount, CGameClient **, pClients, CFrameSnapshot *, pSnapShot)
{
	g_bSVComputePacksDone = false;
	if (!iClientCount || reinterpret_cast<int_for_clptr>(pClients[0]) != g_iCurrentGameClientPtr)
		return DETOUR_STATIC_CALL(SV_ComputeClientPacks)(iClientCount, pClients, pSnapShot);
	IClient * pClient = (IClient *)((char *)pClients[0] + 4);
	int iClient = pClient->GetPlayerSlot();
	if (g_iCurrentClientIndexInLoop != iClient)
		return DETOUR_STATIC_CALL(SV_ComputeClientPacks)(iClientCount, pClients, pSnapShot);
	//Also here we can change actual values for each client! But for what?
	//Just mark all hooked edicts as changed to bypass check in SV_PackEntity!
	for (int i = 0; i < g_vHookedEdicts.Count(); i++)
	{
		edict_t * pEdict = g_vHookedEdicts[i];
		if (pEdict && !(pEdict->m_fStateFlags & FL_EDICT_CHANGED))
			pEdict->m_fStateFlags |= FL_EDICT_CHANGED;
	}
	if (g_bCurrentGameClientCallFwd)
		g_bSVComputePacksDone = true;
	return DETOUR_STATIC_CALL(SV_ComputeClientPacks)(iClientCount, pClients, pSnapShot);
}
#endif

//hooks

void SendProxyManager::OnEntityDestroyed(CBaseEntity* pEnt)
{
	int idx = gamehelpers->EntityToBCompatRef(pEnt);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].objectID == idx)
		{
			g_SendProxyManager.UnhookProxy(i);
		}
	}

	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].objectID == idx)
			g_ChangeHooks.Remove(i--);
	}
}

void Hook_ClientDisconnect(edict_t* pEnt)
{
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].objectID == gamehelpers->IndexOfEdict(pEnt))
			g_SendProxyManager.UnhookProxy(i);
	}

	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].objectID == gamehelpers->IndexOfEdict(pEnt))
			g_ChangeHooks.Remove(i--);
	}
	RETURN_META(MRES_IGNORED);
}

void Hook_GameFrame(bool simulating)
{
	if (simulating)
	{
		for (int i = 0; i < g_ChangeHooks.Count(); i++)
		{
			switch(g_ChangeHooks[i].PropType)
			{
				case Prop_Int:
				{
					edict_t* pEnt = gamehelpers->EdictOfIndex(g_ChangeHooks[i].objectID);
					CBaseEntity* pEntity = gameents->EdictToBaseEntity(pEnt);
					int iCurrent = *(int*)((unsigned char*)pEntity + g_ChangeHooks[i].Offset);
					if (iCurrent != g_ChangeHooks[i].iLastValue)
						{
						g_ChangeHooks[i].pCallback->PushCell(g_ChangeHooks[i].objectID);
						g_ChangeHooks[i].pCallback->PushString(g_ChangeHooks[i].pVar->GetName());
						char oldValue[64];
						snprintf(oldValue, 64, "%d", g_ChangeHooks[i].iLastValue);
						char newValue[64];
						snprintf(newValue, 64, "%d", iCurrent);
						g_ChangeHooks[i].pCallback->PushString(oldValue);
						g_ChangeHooks[i].pCallback->PushString(newValue);
						g_ChangeHooks[i].pCallback->Execute(0);
						g_ChangeHooks[i].iLastValue = iCurrent;
					}
					break;
				}
				case Prop_Float:
				{
					edict_t* pEnt = gamehelpers->EdictOfIndex(g_ChangeHooks[i].objectID);
					CBaseEntity* pEntity = gameents->EdictToBaseEntity(pEnt);
					float flCurrent = *(float*)((unsigned char*)pEntity + g_ChangeHooks[i].Offset);
					if (flCurrent != g_ChangeHooks[i].flLastValue)
					{
						g_ChangeHooks[i].pCallback->PushCell(g_ChangeHooks[i].objectID);
						g_ChangeHooks[i].pCallback->PushString(g_ChangeHooks[i].pVar->GetName());
						char oldValue[64];
						snprintf(oldValue, 64, "%f", g_ChangeHooks[i].flLastValue);
						char newValue[64];
						snprintf(newValue, 64, "%f", flCurrent);
						g_ChangeHooks[i].pCallback->PushString(oldValue);
						g_ChangeHooks[i].pCallback->PushString(newValue);
						g_ChangeHooks[i].pCallback->Execute(0);
						g_ChangeHooks[i].flLastValue = flCurrent;
					}
					break;
				}
				case Prop_String:
				{
					edict_t* pEnt = gamehelpers->EdictOfIndex(g_ChangeHooks[i].objectID);
					CBaseEntity* pEntity = gameents->EdictToBaseEntity(pEnt);
					const char* szCurrent = (const char*)((unsigned char*)pEntity + g_ChangeHooks[i].Offset);
					if (strcmp(szCurrent, g_ChangeHooks[i].cLastValue) != 0)
					{
						g_ChangeHooks[i].pCallback->PushCell(g_ChangeHooks[i].objectID);
						g_ChangeHooks[i].pCallback->PushString(g_ChangeHooks[i].pVar->GetName());
						g_ChangeHooks[i].pCallback->PushString(g_ChangeHooks[i].cLastValue);
						g_ChangeHooks[i].pCallback->PushString(szCurrent);
						g_ChangeHooks[i].pCallback->Execute(0);
						memset(g_ChangeHooks[i].cLastValue, 0, sizeof(g_ChangeHooks[i].cLastValue));
						strncpy(g_ChangeHooks[i].cLastValue, szCurrent, sizeof(g_ChangeHooks[i].cLastValue));
					}
					break;
				}
				default:
				{
					//earlier typechecks failed
				}
			}
		}
		static void *pGamerules = nullptr;
		if (!pGamerules && g_pSDKTools)
		{
			pGamerules = g_pSDKTools->GetGameRules();
			if(!pGamerules)
			{
				g_pSM->LogError(myself, "CRITICAL ERROR: Could not get gamerules pointer!");
				return;
			}
		}
		//Gamerules hooks
		for (int i = 0; i < g_ChangeHooksGamerules.Count(); i++)
		{
			switch(g_ChangeHooksGamerules[i].PropType)
			{
				case Prop_Int:
				{
					int iCurrent = *(int*)((unsigned char*)pGamerules + g_ChangeHooksGamerules[i].Offset);
					if (iCurrent != g_ChangeHooksGamerules[i].iLastValue)
					{
						g_ChangeHooksGamerules[i].pCallback->PushString(g_ChangeHooksGamerules[i].pVar->GetName());
						char oldValue[64];
						snprintf(oldValue, 64, "%d", g_ChangeHooksGamerules[i].iLastValue);
						char newValue[64];
						snprintf(newValue, 64, "%d", iCurrent);
						g_ChangeHooksGamerules[i].pCallback->PushString(oldValue);
						g_ChangeHooksGamerules[i].pCallback->PushString(newValue);
						g_ChangeHooksGamerules[i].pCallback->Execute(0);
						g_ChangeHooksGamerules[i].iLastValue = iCurrent;
					}
					break;
				}
				case Prop_Float:
				{
					float flCurrent = *(float*)((unsigned char*)pGamerules + g_ChangeHooksGamerules[i].Offset);
					if (flCurrent != g_ChangeHooksGamerules[i].flLastValue)
					{
						g_ChangeHooksGamerules[i].pCallback->PushString(g_ChangeHooksGamerules[i].pVar->GetName());
						char oldValue[64];
						snprintf(oldValue, 64, "%f", g_ChangeHooksGamerules[i].flLastValue);
						char newValue[64];
						snprintf(newValue, 64, "%f", flCurrent);
						g_ChangeHooksGamerules[i].pCallback->PushString(oldValue);
						g_ChangeHooksGamerules[i].pCallback->PushString(newValue);
						g_ChangeHooksGamerules[i].pCallback->Execute(0);
						g_ChangeHooksGamerules[i].flLastValue = flCurrent;
					}
					break;
				}
				case Prop_String:
				{
					const char* szCurrent = (const char*)((unsigned char*)pGamerules + g_ChangeHooksGamerules[i].Offset);
					if (strcmp(szCurrent, g_ChangeHooksGamerules[i].cLastValue) != 0)
					{
						g_ChangeHooksGamerules[i].pCallback->PushString(g_ChangeHooksGamerules[i].pVar->GetName());
						g_ChangeHooksGamerules[i].pCallback->PushString(g_ChangeHooksGamerules[i].cLastValue);
						g_ChangeHooksGamerules[i].pCallback->PushString(szCurrent);
						g_ChangeHooksGamerules[i].pCallback->Execute(0);
						memset(g_ChangeHooks[i].cLastValue, 0, sizeof(g_ChangeHooks[i].cLastValue));
						strncpy(g_ChangeHooks[i].cLastValue, szCurrent, sizeof(g_ChangeHooks[i].cLastValue));
					}
					break;
				}
				default:
				{
					//earlier typechecks failed
				}
			}
		}
	}
	RETURN_META(MRES_IGNORED);
}

int SendProxyManager::GetClientCount() const
{
	if (g_iCurrentClientIndexInLoop != -1)
		RETURN_META_VALUE(MRES_SUPERCEDE, g_iCurrentClientIndexInLoop + 1);
	RETURN_META_VALUE(MRES_IGNORED, 0/*META_RESULT_ORIG_RET(int)*/);
}

//main sm class implementation

bool SendProxyManager::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	char conf_error[255];
	if (!gameconfs->LoadGameConfigFile("sdktools.games", &g_pGameConfSDKTools, conf_error, sizeof(conf_error)))
	{
		if (conf_error[0])
			snprintf(error, maxlength, "Could not read config file sdktools.games.txt: %s", conf_error);
		return false;
	}
	
	g_szGameRulesProxy = g_pGameConfSDKTools->GetKeyValue("GameRulesProxy");
	
	if (!gameconfs->LoadGameConfigFile("sendproxy", &g_pGameConf, conf_error, sizeof(conf_error)))
	{
		if (conf_error[0])
			snprintf(error, maxlength, "Could not read config file sendproxy.txt: %s", conf_error);
		return false;
	}
	
	CDetourManager::Init(smutils->GetScriptingEngine(), g_pGameConf);
	
	bool bDetoursInited = false;
	CREATE_DETOUR(CGameServer_SendClientMessages, "CGameServer::SendClientMessages", bDetoursInited);
	CREATE_DETOUR(CGameClient_ShouldSendMessages, "CGameClient::ShouldSendMessages", bDetoursInited);
#if SOURCE_ENGINE != SE_CSGO
	CREATE_DETOUR_STATIC(SV_ComputeClientPacks, "SV_ComputeClientPacks", bDetoursInited);
#endif
	
	if (!bDetoursInited)
	{
		snprintf(error, maxlength, "Could not create detours, see error log!");
		return false;
	}

	sharesys->RegisterLibrary(myself, "sendproxy");
	plsys->AddPluginsListener(&g_SendProxyManager);

	return true;
}

void SendProxyManager::SDK_OnAllLoaded()
{
	sharesys->AddNatives(myself, g_MyNatives);
	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);
	SM_GET_LATE_IFACE(SDKHOOKS, g_pSDKHooks);

	if (g_pSDKHooks)
	{
		g_pSDKHooks->AddEntityListener(this);
	}
}

void SendProxyManager::SDK_OnUnload()
{
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		g_Hooks[i].pVar->SetProxyFn(g_Hooks[i].pRealProxy);
	}
	
	SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, gameclients, SH_STATIC(Hook_ClientDisconnect), false);
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, gamedll, SH_STATIC(Hook_GameFrame), false);
	if (!g_bFirstTimeCalled)
		SH_REMOVE_HOOK(IServer, GetClientCount, g_pIServer, SH_MEMBER(this, &SendProxyManager::GetClientCount), false);

	DESTROY_DETOUR(CGameServer_SendClientMessages);
	DESTROY_DETOUR(CGameClient_ShouldSendMessages);
#if SOURCE_ENGINE != SE_CSGO
	DESTROY_DETOUR(SV_ComputeClientPacks);
#endif
	
	gameconfs->CloseGameConfigFile(g_pGameConf);
	gameconfs->CloseGameConfigFile(g_pGameConfSDKTools);
	
	plsys->RemovePluginsListener(&g_SendProxyManager);
	if( g_pSDKHooks )
	{
		g_pSDKHooks->RemoveEntityListener(this);
	}
}

void SendProxyManager::OnCoreMapEnd()
{
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		UnhookProxyGamerules(i);
		i--;
	}
}

bool SendProxyManager::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_ANY(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	SH_ADD_HOOK(IServerGameDLL, GameFrame, gamedll, SH_STATIC(Hook_GameFrame), false);
	SH_ADD_HOOK(IServerGameClients, ClientDisconnect, gameclients, SH_STATIC(Hook_ClientDisconnect), false);
	
	GET_CONVAR(sv_parallel_packentities);
	sv_parallel_packentities->SetValue(0); //If we don't do that the sendproxy extension will crash the server (Post ref: https://forums.alliedmods.net/showpost.php?p=2540106&postcount=324 )
	
	return true;
}

void SendProxyManager::OnPluginUnloaded(IPlugin *plugin)
{
	IPluginContext *pCtx = plugin->GetBaseContext();
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pCallback->GetParentContext() == pCtx)
		{
			UnhookProxy(i);
			i--;
		}
	}
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].pCallback->GetParentContext() == pCtx)
		{
			UnhookProxyGamerules(i);
			i--;
		}
	}
}


//functions

bool SendProxyManager::AddHookToList(SendPropHook hook)
{
	//Need to make sure this prop isn't already hooked for this entity
	bool bEdictHooked = false;
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].objectID == hook.objectID)
		{
			if (g_Hooks[i].pVar == hook.pVar)
				return false;
			else
				bEdictHooked = true;
		}
	}
	//CBaseEntity *pbe = gameents->EdictToBaseEntity(hook.pEnt);
	g_Hooks.AddToTail(hook);
	if (!bEdictHooked)
		g_vHookedEdicts.AddToTail(hook.pEnt);
	return true;
}

bool SendProxyManager::AddHookToListGamerules(SendPropHookGamerules hook)
{
	//Need to make sure this prop isn't already hooked for this entity
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].pVar == hook.pVar)
			return false;
	}
	g_HooksGamerules.AddToTail(hook);
	return true;
}

bool SendProxyManager::HookProxy(SendProp* pProp, int objectID, IPluginFunction *pCallback)
{
	edict_t* pEdict = gamehelpers->EdictOfIndex(objectID);
	if (!pEdict || pEdict->IsFree())
		return false;

	SendPropHook hook;
	hook.objectID = objectID;
	hook.pCallback = pCallback;
	hook.PropType = Prop_Int;
	hook.pEnt = pEdict;
	hook.pVar = pProp;
	hook.pRealProxy = pProp->GetProxyFn();
	if (AddHookToList(hook))
		pProp->SetProxyFn(GlobalProxy);
	else
		return false;

	return true;

}

bool SendProxyManager::HookProxyGamerules(SendProp* pProp, IPluginFunction *pCallback)
{
	void *pGamerules = g_pSDKTools->GetGameRules();
	if(!pGamerules)
	{
		g_pSM->LogError(myself, "CRITICAL ERROR: Could not get gamerules pointer!");
	}
	
	SendPropHookGamerules hook;
	hook.pCallback = pCallback;
	hook.PropType = Prop_Int;
	hook.pVar = pProp;
	hook.pRealProxy = pProp->GetProxyFn();
	if (AddHookToListGamerules(hook))
		pProp->SetProxyFn(GlobalProxyGamerules);
	else
		return false;

	return true;

}

void SendProxyManager::UnhookProxy(int i)
{
	//if there are other hooks for this prop, don't change the proxy, just remove it from our list
	for (int j = 0; j < g_Hooks.Count(); j++)
	{
		if (g_Hooks[j].pVar == g_Hooks[i].pVar && i != j)
		{
			g_Hooks.Remove(i); //for others: this not a mistake
			return;
		}
	}
	for (int j = 0; j < g_vHookedEdicts.Count(); j++)
		if (g_vHookedEdicts[j] == g_Hooks[i].pEnt)
		{
			g_vHookedEdicts.Remove(j);
			break;
		}
	g_Hooks[i].pVar->SetProxyFn(g_Hooks[i].pRealProxy);
	g_Hooks.Remove(i);
}

void SendProxyManager::UnhookProxyGamerules(int i)
{
	//if there are other hooks for this prop, don't change the proxy, just remove it from our list
	for (int j = 0; j < g_HooksGamerules.Count(); j++)
	{
		if (g_HooksGamerules[j].pVar == g_HooksGamerules[i].pVar && i != j)
		{
			g_HooksGamerules.Remove(i);
			return;
		}
	}
	g_HooksGamerules[i].pVar->SetProxyFn(g_HooksGamerules[i].pRealProxy);
	g_HooksGamerules.Remove(i);
}

//callbacks

bool CallInt(SendPropHook hook, int *ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	AUTO_LOCK_FM(g_WorkMutex);

	IPluginFunction *callback = hook.pCallback;
	//CBaseEntity *pbe = gameents->EdictToBaseEntity(hook.pEnt);
	cell_t value = *ret;
	cell_t result = Pl_Continue;
	callback->PushCell(hook.objectID);
	callback->PushString(hook.pVar->GetName());
	callback->PushCellByRef(&value);
	callback->PushCell(hook.Element);
	callback->PushCell(g_iCurrentClientIndexInLoop + 1);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		*ret = value;
		return true;
	}
	return false;
}

bool CallIntGamerules(SendPropHookGamerules hook, int *ret)
{
	AUTO_LOCK_FM(g_WorkMutex);

	IPluginFunction *callback = hook.pCallback;
	cell_t value = *ret;
	cell_t result = Pl_Continue;
	callback->PushString(hook.pVar->GetName());
	callback->PushCellByRef(&value);
	callback->PushCell(hook.Element);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		*ret = value;
		return true;
	}
	return false;
}

bool CallFloat(SendPropHook hook, float *ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	AUTO_LOCK_FM(g_WorkMutex);

	IPluginFunction *callback = hook.pCallback;
	//CBaseEntity *pbe = gameents->EdictToBaseEntity(hook.pEnt);
	float value = *ret;
	cell_t result = Pl_Continue;
	callback->PushCell(hook.objectID);
	callback->PushString(hook.pVar->GetName());
	callback->PushFloatByRef(&value);
	callback->PushCell(hook.Element);
	callback->PushCell(g_iCurrentClientIndexInLoop + 1);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		*ret = value;
		return true;
	}
	return false;
}

bool CallFloatGamerules(SendPropHookGamerules hook, float *ret)
{
	AUTO_LOCK_FM(g_WorkMutex);

	IPluginFunction *callback = hook.pCallback;
	float value = *ret;
	cell_t result = Pl_Continue;
	callback->PushString(hook.pVar->GetName());
	callback->PushFloatByRef(&value);
	callback->PushCell(hook.Element);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		*ret = value;
		return true;
	}
	return false;
}

bool CallString(SendPropHook hook, char **ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	AUTO_LOCK_FM(g_WorkMutex);

	IPluginFunction *callback = hook.pCallback;
	char value[4096];
	const char *src;
	CBaseEntity *pbe = gameents->EdictToBaseEntity(hook.pEnt);
	src = (char *)((unsigned char*)pbe + hook.Offset);
	strncpy(value, src, sizeof(value));
	cell_t result = Pl_Continue;
	callback->PushCell(hook.objectID);
	callback->PushString(hook.pVar->GetName());
	callback->PushStringEx(value, 4096, SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
	callback->PushCell(hook.Element);
	callback->PushCell(g_iCurrentClientIndexInLoop + 1);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		*ret = value;
		return true;
	}
	return false;
}

bool CallStringGamerules(SendPropHookGamerules hook, char **ret)
{
	AUTO_LOCK_FM(g_WorkMutex);

	void *pGamerules = g_pSDKTools->GetGameRules();
	if(!pGamerules)
	{
		g_pSM->LogError(myself, "CRITICAL ERROR: Could not get gamerules pointer!");
	}
	
	IPluginFunction *callback = hook.pCallback;
	char value[4096];
	const char *src;
	src = (char *)((unsigned char*)pGamerules + hook.Offset);
	strncpy(value, src, sizeof(value));
	cell_t result = Pl_Continue;
	callback->PushString(hook.pVar->GetName());
	callback->PushStringEx(value, 4096, SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
	callback->PushCell(hook.Element);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		*ret = value;
		return true;
	}
	return false;
}

bool CallVector(SendPropHook hook, Vector &vec)
{
	if (!g_bSVComputePacksDone)
		return false;
	AUTO_LOCK_FM(g_WorkMutex);

	IPluginFunction *callback = hook.pCallback;

	cell_t vector[3];
	vector[0] = sp_ftoc(vec.x);
	vector[1] = sp_ftoc(vec.y);
	vector[2] = sp_ftoc(vec.z);

	cell_t result = Pl_Continue;
	callback->PushCell(hook.objectID);
	callback->PushString(hook.pVar->GetName());
	callback->PushArray(vector, 3, SM_PARAM_COPYBACK);
	callback->PushCell(hook.Element);
	callback->PushCell(g_iCurrentClientIndexInLoop + 1);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		vec.x = sp_ctof(vector[0]);
		vec.y = sp_ctof(vector[1]);
		vec.z = sp_ctof(vector[2]);
		return true;
	}
	return false;
}

bool CallVectorGamerules(SendPropHookGamerules hook, Vector &vec)
{
	AUTO_LOCK_FM(g_WorkMutex);

	IPluginFunction *callback = hook.pCallback;

	cell_t vector[3];
	vector[0] = sp_ftoc(vec.x);
	vector[1] = sp_ftoc(vec.y);
	vector[2] = sp_ftoc(vec.z);

	cell_t result = Pl_Continue;
	callback->PushString(hook.pVar->GetName());
	callback->PushArray(vector, 3, SM_PARAM_COPYBACK);
	callback->PushCell(hook.Element);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		vec.x = sp_ctof(vector[0]);
		vec.y = sp_ctof(vector[1]);
		vec.z = sp_ctof(vector[2]);
		return true;
	}
	return false;
}

void GlobalProxy(const SendProp *pProp, const void *pStructBase, const void* pData, DVariant *pOut, int iElement, int objectID)
{
	edict_t* pEnt = gamehelpers->EdictOfIndex(objectID);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].objectID == objectID && g_Hooks[i].pVar == pProp && pEnt == g_Hooks[i].pEnt)
		{
			switch (g_Hooks[i].PropType)
			{
				case Prop_Int:
				{
					int result = *(int *)pData;

					if (CallInt(g_Hooks[i], &result))
					{
						long data = result;

						g_Hooks[i].pRealProxy(pProp, pStructBase, &data, pOut, iElement, objectID);
						return;
					} else {
						g_Hooks[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
				case Prop_Float:
				{
					float result = *(float *)pData;

					if (CallFloat(g_Hooks[i], &result))
					{
						g_Hooks[i].pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return;
					} else {
						g_Hooks[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
				case Prop_String:
				{
					char* result = (char *)pData;

					if (CallString(g_Hooks[i], &result))
					{
						g_Hooks[i].pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return;
					} else {
						g_Hooks[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
				case Prop_Vector:
				{
					Vector result = *(Vector *)pData;

					if (CallVector(g_Hooks[i], result))
					{
						g_Hooks[i].pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return;
					} else {
						g_Hooks[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
				default: rootconsole->ConsolePrint("SendProxy report: Unknown prop type.");
			}
		}
	}
	//perhaps we aren't hooked, but we can still find the real proxy for this prop
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			g_Hooks[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
			return;
		}
	}
	g_pSM->LogError(myself, "CRITICAL: Proxy for unmanaged entity %d called for prop %s", objectID, pProp->GetName());
}

void GlobalProxyGamerules(const SendProp *pProp, const void *pStructBase, const void* pData, DVariant *pOut, int iElement, int objectID)
{
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].pVar == pProp)
		{
			switch (g_HooksGamerules[i].PropType)
			{
				case Prop_Int:
				{
					int result = *(int *)pData;

					if (CallIntGamerules(g_HooksGamerules[i], &result))
					{
						long data = result;

						g_HooksGamerules[i].pRealProxy(pProp, pStructBase, &data, pOut, iElement, objectID);
						return;
					}
					else
					{
						g_HooksGamerules[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
				case Prop_Float:
				{
					float result = *(float *)pData;

					if (CallFloatGamerules(g_HooksGamerules[i], &result))
					{
						g_HooksGamerules[i].pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return;
					} 
					else
					{
						g_HooksGamerules[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
				case Prop_String:
				{
					char* result = (char *)pData;

					if (CallStringGamerules(g_HooksGamerules[i], &result))
					{
						g_HooksGamerules[i].pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return;
					}
					else
					{
						g_HooksGamerules[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
				case Prop_Vector:
				{
					Vector result = *(Vector *)pData;

					if (CallVectorGamerules(g_HooksGamerules[i], result))
					{
						g_HooksGamerules[i].pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return;
					}
					else
					{
						g_HooksGamerules[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
				default: rootconsole->ConsolePrint("SendProxy report: Unknown prop type.");
			}
		}
	}
	//perhaps we aren't hooked, but we can still find the real proxy for this prop
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].pVar == pProp)
		{
			g_Hooks[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
			return;
		}
	}
	g_pSM->LogError(myself, "CRITICAL: Proxy for unmanaged gamerules called for prop %s", pProp->GetName());
	
}
//PropChanged(entity, const String:propname[], const String:oldValue[], const String:newValue[])
//SendProxy_HookPropChange(entity, const String:name[], PropChanged:callback)

//Natives

static cell_t Native_UnhookPropChange(IPluginContext* pContext, const cell_t* params)
{
	if (params[1] < 0 || params[1] >= 2048)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char* name;
	edict_t* pEnt = gamehelpers->EdictOfIndex(entity);
	pContext->LocalToString(params[2], &name);
	IPluginFunction *callback = pContext->GetFunctionById(params[3]);
	sm_sendprop_info_t info;
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	gamehelpers->FindSendPropInfo(sc->GetName(), name, &info);
	
	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].pCallback == callback && g_ChangeHooks[i].objectID == entity && g_ChangeHooks[i].pVar == info.prop)
			g_ChangeHooks.Remove(i--);
	}
	return 1;
}

static cell_t Native_UnhookPropChangeGameRules(IPluginContext* pContext, const cell_t* params)
{
	char* name;
	pContext->LocalToString(params[1], &name);
	IPluginFunction *callback = pContext->GetFunctionById(params[2]);
	sm_sendprop_info_t info;
	gamehelpers->FindSendPropInfo(g_szGameRulesProxy, name, &info);
	
	for (int i = 0; i < g_ChangeHooksGamerules.Count(); i++)
	{
		if (g_ChangeHooksGamerules[i].pCallback == callback && g_ChangeHooksGamerules[i].pVar == info.prop)
			g_ChangeHooksGamerules.Remove(i--);
	}
	return 1;
}
	
static cell_t Native_HookPropChange(IPluginContext* pContext, const cell_t* params)
{
	if (params[1] < 0 || params[1] >= 2048)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char* name;
	edict_t* pEnt = gamehelpers->EdictOfIndex(entity);
	pContext->LocalToString(params[2], &name);
	IPluginFunction *callback = pContext->GetFunctionById(params[3]);
	SendProp *pProp = nullptr;
	PropChangeHook hook;
	sm_sendprop_info_t info;
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	gamehelpers->FindSendPropInfo(sc->GetName(), name, &info);

	pProp = info.prop;
	int offset = info.actual_offset;
	SendPropType type = pProp->GetType();
	CBaseEntity* pEntity = gameents->EdictToBaseEntity(pEnt);

	switch (type)
	{
	case DPT_Int: hook.PropType = Prop_Int; hook.iLastValue = *(int*)((unsigned char*)pEntity + offset); break;
	case DPT_Float: hook.PropType = Prop_Float; hook.flLastValue = *(float*)((unsigned char*)pEntity + offset); break;
	case DPT_String: hook.PropType = Prop_String; strncpy(hook.cLastValue, (const char*)((unsigned char*)pEntity + offset), sizeof(hook.cLastValue)); break;
	default: return pContext->ThrowNativeError("Prop type %d is not yet supported", type);
	}

	hook.objectID = entity;
	hook.Offset = offset;
	hook.pVar = pProp;
	hook.pCallback = callback;

	g_ChangeHooks.AddToTail(hook);
	return 1;
}

static cell_t Native_HookPropChangeGameRules(IPluginContext* pContext, const cell_t* params)
{
	char* name;
	pContext->LocalToString(params[1], &name);
	IPluginFunction *callback = pContext->GetFunctionById(params[2]);
	SendProp *pProp = nullptr;
	PropChangeHookGamerules hook;
	sm_sendprop_info_t info;
	gamehelpers->FindSendPropInfo(g_szGameRulesProxy, name, &info);

	pProp = info.prop;
	int offset = info.actual_offset;
	SendPropType type = pProp->GetType();

	static void *pGamerules = nullptr;
	if (!pGamerules)
	{
		pGamerules = g_pSDKTools->GetGameRules();
		if (!pGamerules)
		{
			g_pSM->LogError(myself, "CRITICAL ERROR: Could not get gamerules pointer!");
			return 0;
		}
	}

	switch (type)
	{
	case DPT_Int: hook.PropType = Prop_Int; hook.iLastValue = *(int*)((unsigned char*)pGamerules + offset); break;
	case DPT_Float: hook.PropType = Prop_Float; hook.flLastValue = *(float*)((unsigned char*)pGamerules + offset); break;
	case DPT_String: hook.PropType = Prop_String; strncpy(hook.cLastValue, (const char*)((unsigned char*)pGamerules + offset), sizeof(hook.cLastValue)); break;
	default: return pContext->ThrowNativeError("Prop type %d is not yet supported", type);
	}

	hook.Offset = offset;
	hook.pVar = pProp;
	hook.pCallback = callback;

	g_ChangeHooksGamerules.AddToTail(hook);
	return 1;
}

static cell_t Native_Hook(IPluginContext* pContext, const cell_t* params)
{
	if (params[1] < 0 || params[1] >= 2048)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char* name;
	pContext->LocalToString(params[2], &name);
	edict_t* pEnt = gamehelpers->EdictOfIndex(entity);
	int propType = params[3];
	IPluginFunction *callback = pContext->GetFunctionById(params[4]);
	SendProp *pProp = nullptr;
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	sm_sendprop_info_t info;
	
	if (!sc)
	{
		pContext->ThrowNativeError("Cannot find ServerClass for entity %d", params[1]);
		return 0;
	}

	gamehelpers->FindSendPropInfo(sc->GetName(), name, &info);
	pProp = info.prop;
	if (!pProp)
	{
		pContext->ThrowNativeError("Could not find prop %s", name);
		return 0;
	}
	switch (propType)
	{
	case Prop_Int: 
		{
			if (pProp->GetType() != DPT_Int)
				return pContext->ThrowNativeError("Prop %s is not an int!", pProp->GetName());
			break;
		}
	case Prop_Float:
		{
			if (pProp->GetType() != DPT_Float)
				return pContext->ThrowNativeError("Prop %s is not a float!", pProp->GetName());
			break;
		}
	case Prop_String:
		{
			if (pProp->GetType() != DPT_String)
				return pContext->ThrowNativeError("Prop %s is not a string!", pProp->GetName());
			break;
		}
	case Prop_Vector:
		{
			if (pProp->GetType() != DPT_Vector)
				return pContext->ThrowNativeError("Prop %s is not a vector!", pProp->GetName());
			break;
		}
	default: return pContext->ThrowNativeError("Unsupported prop type %d", propType);
	}
	
	
	SendPropHook hook;
	hook.objectID = entity;
	hook.pCallback = callback;
	hook.pEnt = pEnt;
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			hook.pRealProxy = g_Hooks[i].pRealProxy;
			goto after;
		}
	}
	hook.pRealProxy = pProp->GetProxyFn();
after:
	hook.PropType = propType;
	hook.pVar = pProp;
	hook.Offset = info.actual_offset;
	
	//if this prop has been hooked already, don't set the proxy again
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			hook.pRealProxy = g_Hooks[i].pRealProxy;
			g_SendProxyManager.AddHookToList(hook);
			return 1;
		}
	}
	g_SendProxyManager.AddHookToList(hook);
	pProp->SetProxyFn(GlobalProxy);
	return 1;
}

static cell_t Native_HookGameRules(IPluginContext* pContext, const cell_t* params)
{
	char* name;
	pContext->LocalToString(params[1], &name);
	int propType = params[2];
	IPluginFunction *callback = pContext->GetFunctionById(params[3]);
	SendProp *pProp = nullptr;
	sm_sendprop_info_t info;

	gamehelpers->FindSendPropInfo(g_szGameRulesProxy, name, &info);
	pProp = info.prop;
	if (!pProp)
	{
		pContext->ThrowNativeError("Could not find prop %s", name);
		return 0;
	}
	switch (propType)
	{
		case Prop_Int: 
		{
			if (pProp->GetType() != DPT_Int)
				return pContext->ThrowNativeError("Prop %s is not an int!", pProp->GetName());
			break;
		}
		case Prop_Float:
		{
			if (pProp->GetType() != DPT_Float)
				return pContext->ThrowNativeError("Prop %s is not a float!", pProp->GetName());
			break;
		}
		case Prop_String:
		{
			if (pProp->GetType() != DPT_String)
				return pContext->ThrowNativeError("Prop %s is not a string!", pProp->GetName());
			break;
		}
		case Prop_Vector:
		{
			if (pProp->GetType() != DPT_Vector)
				return pContext->ThrowNativeError("Prop %s is not a vector!", pProp->GetName());
			break;
		}
		default: return pContext->ThrowNativeError("Unsupported prop type %d", propType);
	}
	SendPropHookGamerules hook;
	hook.pCallback = callback;
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].pVar == pProp)
		{
			hook.pRealProxy = g_HooksGamerules[i].pRealProxy;
			goto after;
		}
	}
	hook.pRealProxy = pProp->GetProxyFn();
after:
	hook.PropType = propType;
	hook.pVar = pProp;
	hook.Offset = info.actual_offset;
	
	//if this prop has been hooked already, don't set the proxy again
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].pVar == pProp)
		{
			hook.pRealProxy = g_HooksGamerules[i].pRealProxy;
			g_SendProxyManager.AddHookToListGamerules(hook);
			return 1;
		}
	}
	g_SendProxyManager.AddHookToListGamerules(hook);
	pProp->SetProxyFn(GlobalProxyGamerules);
	return 1;
}

//native SendProxy_HookArrayProp(entity, const String:name[], element, SendPropType:type, SendProxyCallback:callback);

static cell_t Native_HookArrayProp(IPluginContext* pContext, const cell_t* params)
{
	if (params[1] < 0 || params[1] >= 2048)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char *propName;
	pContext->LocalToString(params[2], &propName);
	int element = params[3];
	int propType = params[4];
	IPluginFunction *callback = pContext->GetFunctionById(params[5]);
	
	edict_t* pEnt = gamehelpers->EdictOfIndex(entity);
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	sm_sendprop_info_t info;
	gamehelpers->FindSendPropInfo(sc->GetName(), propName, &info);
	if (!info.prop)
	{
		return pContext->ThrowNativeError("Could not find prop %s", propName);
	}
	SendTable *st = info.prop->GetDataTable();
	if (!st)
	{
		return pContext->ThrowNativeError("Prop %s does not contain any elements", propName);
	}

	SendProp *pProp = st->GetProp(element);
	if (!pProp)
	{
		return pContext->ThrowNativeError("Could not find element %d in %s", element, info.prop->GetName());
	}
	
	SendPropHook hook;
	hook.objectID = entity;
	hook.pCallback = callback;
	hook.pEnt = pEnt;
	hook.Element = element;
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			hook.pRealProxy = g_Hooks[i].pRealProxy;
			goto after1;
		}
	}
	hook.pRealProxy = pProp->GetProxyFn();
after1:
	hook.PropType = propType;
	hook.pVar = pProp;
	
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			hook.pRealProxy = g_Hooks[i].pRealProxy;
			g_SendProxyManager.AddHookToList(hook);
			return 1;
		}
	}
	g_SendProxyManager.AddHookToList(hook);
	pProp->SetProxyFn(GlobalProxy);
	return 1;
}

//native SendProxy_UnhookArrayProp(entity, const String:name[], element, SendPropType:type, SendProxyCallback:callback);

static cell_t Native_UnhookArrayProp(IPluginContext* pContext, const cell_t* params)
{
	if (params[1] < 0 || params[1] >= 2048)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char *propName;
	pContext->LocalToString(params[2], &propName);
	int element = params[3];
	int propType = params[4];
	IPluginFunction *callback = pContext->GetFunctionById(params[5]);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].Element == element && g_Hooks[i].PropType == propType && g_Hooks[i].pCallback == callback && !strcmp(g_Hooks[i].pVar->GetName(), propName) && g_Hooks[i].objectID == entity)
		{
			g_SendProxyManager.UnhookProxy(i);
		}
	}
	return 0;
}

static cell_t Native_Unhook(IPluginContext* pContext, const cell_t* params)
{
	char *propName;
	pContext->LocalToString(params[2], &propName);
	IPluginFunction *pFunction = pContext->GetFunctionById(params[3]);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (params[1] == g_Hooks[i].objectID && strcmp(g_Hooks[i].pVar->GetName(), propName) == 0 && pFunction == g_Hooks[i].pCallback)
		{
			g_SendProxyManager.UnhookProxy(i);
			return 1;
		}
	}
	return 0;
}

static cell_t Native_UnhookGameRules(IPluginContext* pContext, const cell_t* params)
{
	char *propName;
	pContext->LocalToString(params[1], &propName);
	IPluginFunction *pFunction = pContext->GetFunctionById(params[2]);
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (strcmp(g_HooksGamerules[i].pVar->GetName(), propName) == 0 && pFunction == g_HooksGamerules[i].pCallback)
		{
			g_SendProxyManager.UnhookProxyGamerules(i);
			return 1;
		}
	}
	return 0;
}

static cell_t Native_IsHooked(IPluginContext* pContext, const cell_t* params)
{
	int objectID = params[1];
	char *propName;
	pContext->LocalToString(params[2], &propName);

	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].objectID == objectID && strcmp(propName, g_Hooks[i].pVar->GetName()) == 0)
			return 1;
	}
	return 0;
}

static cell_t Native_IsHookedGameRules(IPluginContext* pContext, const cell_t* params)
{
	char *propName;
	pContext->LocalToString(params[1], &propName);

	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (strcmp(propName, g_HooksGamerules[i].pVar->GetName()) == 0)
			return 1;
	}
	return 0;
}

//helpers

//code below copied from sdktools!
static size_t UTIL_StringToSignature(const char *str, char buffer[], size_t maxlength)
{
	size_t real_bytes = 0;
	size_t length = strlen(str);

	for (size_t i=0; i<length; i++)
	{
		if (real_bytes >= maxlength)
		{
			break;
		}
		buffer[real_bytes++] = (unsigned char)str[i];
		if (str[i] == '\\'
			&& str[i+1] == 'x')
		{
			if (i + 3 >= length)
			{
				continue;
			}
			/* Get the hex part */
			char s_byte[3];
			int r_byte;
			s_byte[0] = str[i+2];
			s_byte[1] = str[i+3];
			s_byte[2] = '\n';
			/* Read it as an integer */
			sscanf(s_byte, "%x", &r_byte);
			/* Save the value */
			buffer[real_bytes-1] = (unsigned char)r_byte;
			/* Adjust index */
			i += 3;
		}
	}

	return real_bytes;
}

static bool UTIL_VerifySignature(const void *addr, const char *sig, size_t len)
{
	unsigned char *addr1 = (unsigned char *) addr;
	unsigned char *addr2 = (unsigned char *) sig;

	for (size_t i = 0; i < len; i++)
	{
		if (addr2[i] == '*')
			continue;
		if (addr1[i] != addr2[i])
			return false;
	}

	return true;
}

static IServer * GetIServer()
{
#if SOURCE_ENGINE == SE_TF2        \
	|| SOURCE_ENGINE == SE_DODS    \
	|| SOURCE_ENGINE == SE_HL2DM   \
	|| SOURCE_ENGINE == SE_CSS     \
	|| SOURCE_ENGINE == SE_SDK2013 \
	|| SOURCE_ENGINE == SE_BMS     \
	|| SOURCE_ENGINE == SE_DOI     \
	|| SOURCE_ENGINE == SE_INSURGENCY

#if SOURCE_ENGINE != SE_INSURGENCY && SOURCE_ENGINE != SE_DOI
	if (g_SMAPI->GetEngineFactory(false)("VEngineServer022", nullptr))
#endif // !SE_INSURGENCY
		return engine->GetIServer();
#endif

	void *addr;
	const char *sigstr;
	char sig[32];
	size_t siglen;
	int offset;
	void *vfunc = NULL;

	/* Use the symbol if it exists */
	if (g_pGameConfSDKTools->GetMemSig("sv", &addr) && addr)
		return reinterpret_cast<IServer *>(addr);

	/* Get the CreateFakeClient function pointer */
	if (!(vfunc=SH_GET_ORIG_VFNPTR_ENTRY(engine, &IVEngineServer::CreateFakeClient)))
		return nullptr;

	/* Get signature string for IVEngineServer::CreateFakeClient() */
	sigstr = g_pGameConfSDKTools->GetKeyValue(FAKECLIENT_KEY);

	if (!sigstr)
		return nullptr;

	/* Convert signature string to signature bytes */
	siglen = UTIL_StringToSignature(sigstr, sig, sizeof(sig));

	/* Check if we're on the expected function */
	if (!UTIL_VerifySignature(vfunc, sig, siglen))
		return nullptr;

	/* Get the offset into CreateFakeClient */
	if (!g_pGameConfSDKTools->GetOffset("sv", &offset))
		return nullptr;

	/* Finally we have the interface we were looking for */
	return *reinterpret_cast<IServer **>(reinterpret_cast<unsigned char *>(vfunc) + offset);
}