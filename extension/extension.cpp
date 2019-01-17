/**
 * vim: set ts=4 :
 * =============================================================================
 * SendVar Proxy Manager
 * Copyright (C) 2011-2019 Afronanny & AlliedModders community.  All rights reserved.
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

#include "CDetour/detours.h"
#include "extension.h"
#include "interface.h"
#include "natives.h"

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

class CGameClient;
class CFrameSnapshot;
class CGlobalEntityList;

//we will use integer to store pointer lol
CGameClient * g_pCurrentGameClientPtr = 0;
int g_iCurrentClientIndexInLoop = -1; //used for optimization
int g_iEdictCount;
bool g_bCurrentGameClientCallFwd = false;
bool g_bCallingForNullClients = false;
bool g_bFirstTimeCalled = true;
bool g_bSVComputePacksDone = false;
IServer * g_pIServer = nullptr;

SendProxyManager g_SendProxyManager;
SendProxyManagerInterfaceImpl * g_pMyInterface = nullptr;
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
ConVar * sv_parallel_sendsnapshot = nullptr;

edict_t * g_pGameRulesProxyEdict = nullptr;
bool g_bShouldChangeGameRulesState = false;

CGlobalVars * g_pGlobals = nullptr;

static CBaseEntity * FindEntityByServerClassname(int, const char *);

const char * g_szGameRulesProxy;

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
		g_pCurrentGameClientPtr = (CGameClient *)((char *)pClient - 4);
		g_iCurrentClientIndexInLoop = iClients - 1;
		DETOUR_MEMBER_CALL(CGameServer_SendClientMessages)(true);
	}
	g_bCurrentGameClientCallFwd = false;
	g_iCurrentClientIndexInLoop = -1;
	g_bShouldChangeGameRulesState = false;
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
	if ((CGameClient *)this == g_pCurrentGameClientPtr)
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
			if (g_bShouldChangeGameRulesState && g_pGameRulesProxyEdict)
			{
				if (!(g_pGameRulesProxyEdict->m_fStateFlags & FL_EDICT_CHANGED))
					g_pGameRulesProxyEdict->m_fStateFlags |= FL_EDICT_CHANGED;
			}
			if (g_bCurrentGameClientCallFwd)
				g_bSVComputePacksDone = true;
		}
#endif
		return true;
	}
#if defined PLATFORM_x32
	else
	{
		int iTemp, iToSet = g_iCurrentClientIndexInLoop - 1;
		//just set the loop var to needed for us value, some optimization
#if SOURCE_ENGINE == SE_TF2
#ifdef _WIN32
		__asm mov iTemp, esi
		if (iTemp < iToSet)
			__asm mov esi, iToSet
#elif defined __linux__
		//I hate AT&T syntax
		asm("movl %%esi, %0" : "=r" (iTemp));
		if (iTemp < iToSet)
			asm("movl %0, %%esi" : : "r" (iToSet) : "%esi");
#endif
#else //CSGO
#ifdef _WIN32
		__asm mov iTemp, esi
		if (iTemp < iToSet)
			__asm mov esi, iToSet
#elif defined __linux__
		asm("movl %%edi, %0" : "=r" (iTemp));
		if (iTemp < iToSet)
			asm("movl %0, %%edi" : : "r" (iToSet) : "%edi");
#endif
#endif
	}
#endif
	return false;
}

#if SOURCE_ENGINE != SE_CSGO
DETOUR_DECL_STATIC3(SV_ComputeClientPacks, void, int, iClientCount, CGameClient **, pClients, CFrameSnapshot *, pSnapShot)
{
	g_bSVComputePacksDone = false;
	if (!iClientCount || pClients[0] != g_pCurrentGameClientPtr)
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
	if (g_bShouldChangeGameRulesState && g_pGameRulesProxyEdict)
	{
		if (!(g_pGameRulesProxyEdict->m_fStateFlags & FL_EDICT_CHANGED))
			g_pGameRulesProxyEdict->m_fStateFlags |= FL_EDICT_CHANGED;
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

void Hook_ClientDisconnect(edict_t * pEnt)
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
				case PropType::Prop_Int:
				{
					edict_t * pEnt = gamehelpers->EdictOfIndex(g_ChangeHooks[i].objectID);
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
				case PropType::Prop_Float:
				{
					edict_t * pEnt = gamehelpers->EdictOfIndex(g_ChangeHooks[i].objectID);
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
				case PropType::Prop_String:
				{
					edict_t * pEnt = gamehelpers->EdictOfIndex(g_ChangeHooks[i].objectID);
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
				case PropType::Prop_Int:
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
				case PropType::Prop_Float:
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
				case PropType::Prop_String:
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

	if (late) //if we loaded late, we need manually to call that
		OnCoreMapStart(nullptr, 0, 0);
	
	sharesys->AddDependency(myself, "sdktools.ext", true, true);
	sharesys->AddDependency(myself, "sdkhooks.ext", true, true);
	
	g_pMyInterface = new SendProxyManagerInterfaceImpl();
	sharesys->AddInterface(myself, g_pMyInterface);
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
	
	g_pGameRulesProxyEdict = nullptr;
}

void SendProxyManager::OnCoreMapStart(edict_t * pEdictList, int edictCount, int clientMax)
{
	g_iEdictCount = edictCount ? edictCount : g_pGlobals->maxEntities * 2;
	CBaseEntity * pGameRulesProxyEnt = FindEntityByServerClassname(0, g_szGameRulesProxy);
	if (!pGameRulesProxyEnt)
	{
		smutils->LogError(myself, "Unable to get gamerules proxy ent (1)!");
		return;
	}
	g_pGameRulesProxyEdict = gameents->BaseEntityToEdict(pGameRulesProxyEnt);
	if (!g_pGameRulesProxyEdict)
		smutils->LogError(myself, "Unable to get gamerules proxy ent (2)!");
}

bool SendProxyManager::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_ANY(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	
	g_pGlobals = ismm->GetCGlobals();
	
	SH_ADD_HOOK(IServerGameDLL, GameFrame, gamedll, SH_STATIC(Hook_GameFrame), false);
	SH_ADD_HOOK(IServerGameClients, ClientDisconnect, gameclients, SH_STATIC(Hook_ClientDisconnect), false);
	
	GET_CONVAR(sv_parallel_packentities);
	sv_parallel_packentities->SetValue(0); //If we don't do that the sendproxy extension will crash the server (Post ref: https://forums.alliedmods.net/showpost.php?p=2540106&postcount=324 )
	GET_CONVAR(sv_parallel_sendsnapshot);
	sv_parallel_sendsnapshot->SetValue(0); //If we don't do that, sendproxy will not work correctly and may crash server. This affects all versions of sendproxy manager!
	
	return true;
}

void SendProxyManager::OnPluginUnloaded(IPlugin * plugin)
{
	IPluginContext *pCtx = plugin->GetBaseContext();
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].iCallbackType == CallBackType::Callback_PluginFunction && ((IPluginFunction *)g_Hooks[i].pCallback)->GetParentContext() == pCtx)
		{
			UnhookProxy(i);
			i--;
		}
	}
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].iCallbackType == CallBackType::Callback_PluginFunction && ((IPluginFunction *)g_HooksGamerules[i].pCallback)->GetParentContext() == pCtx)
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

void SendProxyManager::UnhookProxy(int i)
{
	//if there are other hooks for this prop, don't change the proxy, just remove it from our list
	for (int j = 0; j < g_Hooks.Count(); j++)
	{
		if (g_Hooks[j].pVar == g_Hooks[i].pVar && i != j)
		{
			CallListenersForHookID(i);
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
	CallListenersForHookID(i);
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
			CallListenersForHookIDGamerules(i);
			g_HooksGamerules.Remove(i);
			return;
		}
	}
	CallListenersForHookIDGamerules(i);
	g_HooksGamerules[i].pVar->SetProxyFn(g_HooksGamerules[i].pRealProxy);
	g_HooksGamerules.Remove(i);
}

//callbacks

bool CallInt(SendPropHook hook, int *ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);

	switch (hook.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.pCallback;
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
			break;
		}
		case CallBackType::Callback_CPPFunction:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.pCallback;
			int iValue = *ret;
			bool bChange = pCallbacks->OnEntityPropProxyFunctionCalls(gameents->EdictToBaseEntity(hook.pEnt), hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&iValue, hook.PropType, hook.Element);
			if (bChange)
			{
				*ret = iValue;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallIntGamerules(SendPropHookGamerules hook, int *ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);

	switch (hook.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.pCallback;
			cell_t value = *ret;
			cell_t result = Pl_Continue;
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
			break;
		}
		case CallBackType::Callback_CPPFunction:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.pCallback;
			int iValue = *ret;
			bool bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&iValue, hook.PropType, hook.Element);
			if (bChange)
			{
				*ret = iValue;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallFloat(SendPropHook hook, float *ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);
	
	switch (hook.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.pCallback;
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
			break;
		}
		case CallBackType::Callback_CPPFunction:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.pCallback;
			float flValue = *ret;
			bool bChange = pCallbacks->OnEntityPropProxyFunctionCalls(gameents->EdictToBaseEntity(hook.pEnt), hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&flValue, hook.PropType, hook.Element);
			if (bChange)
			{
				*ret = flValue;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallFloatGamerules(SendPropHookGamerules hook, float *ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	switch (hook.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			AUTO_LOCK_FM(g_WorkMutex);

			IPluginFunction *callback = (IPluginFunction *)hook.pCallback;
			float value = *ret;
			cell_t result = Pl_Continue;
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
			break;
		}
		case CallBackType::Callback_CPPFunction:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.pCallback;
			float flValue = *ret;
			bool bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&flValue, hook.PropType, hook.Element);
			if (bChange)
			{
				*ret = flValue;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallString(SendPropHook hook, char **ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);

	switch (hook.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.pCallback;
			static char value[4096];
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
			break;
		}
		case CallBackType::Callback_CPPFunction:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.pCallback;
			static char cValue[4096];
			CBaseEntity * pEnt = gameents->EdictToBaseEntity(hook.pEnt);
			strncpy(cValue, (char *)pEnt + hook.Offset, sizeof(cValue));
			bool bChange = pCallbacks->OnEntityPropProxyFunctionCalls(pEnt, hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)cValue, hook.PropType, hook.Element);
			if (bChange)
			{
				*ret = cValue;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallStringGamerules(SendPropHookGamerules hook, char **ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);

	switch (hook.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			void *pGamerules = g_pSDKTools->GetGameRules();
			if(!pGamerules)
			{
				g_pSM->LogError(myself, "CRITICAL ERROR: Could not get gamerules pointer!");
			}
			
			IPluginFunction *callback = (IPluginFunction *)hook.pCallback;
			static char value[4096];
			const char *src;
			src = (char *)((unsigned char*)pGamerules + hook.Offset);
			strncpy(value, src, sizeof(value));
			cell_t result = Pl_Continue;
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
			break;
		}
		case CallBackType::Callback_CPPFunction:
		{
			void * pGamerules = g_pSDKTools->GetGameRules();
			if(!pGamerules)
				return false;
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.pCallback;
			static char cValue[4096];
			strncpy(cValue, (char *)pGamerules + hook.Offset, sizeof(cValue));
			bool bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)cValue, hook.PropType, hook.Element);
			if (bChange)
			{
				*ret = cValue;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallVector(SendPropHook hook, Vector &vec)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);

	switch (hook.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.pCallback;

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
			break;
		}
		case CallBackType::Callback_CPPFunction:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.pCallback;
			Vector vNewVec(vec.x, vec.y, vec.z);
			bool bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&vNewVec, hook.PropType, hook.Element);
			if (bChange)
			{
				vec.x = vNewVec.x;
				vec.y = vNewVec.y;
				vec.z = vNewVec.z;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallVectorGamerules(SendPropHookGamerules hook, Vector &vec)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);

	switch (hook.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.pCallback;

			cell_t vector[3];
			vector[0] = sp_ftoc(vec.x);
			vector[1] = sp_ftoc(vec.y);
			vector[2] = sp_ftoc(vec.z);

			cell_t result = Pl_Continue;
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
			break;
		}
		case CallBackType::Callback_CPPFunction:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.pCallback;
			Vector vNewVec(vec.x, vec.y, vec.z);
			bool bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&vNewVec, hook.PropType, hook.Element);
			if (bChange)
			{
				vec.x = vNewVec.x;
				vec.y = vNewVec.y;
				vec.z = vNewVec.z;
				return true;
			}
			break;
		}
	}
	return false;
}

void GlobalProxy(const SendProp *pProp, const void *pStructBase, const void * pData, DVariant *pOut, int iElement, int objectID)
{
	edict_t * pEnt = gamehelpers->EdictOfIndex(objectID);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].objectID == objectID && g_Hooks[i].pVar == pProp && pEnt == g_Hooks[i].pEnt)
		{
			switch (g_Hooks[i].PropType)
			{
				case PropType::Prop_Int:
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
				case PropType::Prop_Float:
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
				case PropType::Prop_String:
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
				case PropType::Prop_Vector:
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

void GlobalProxyGamerules(const SendProp *pProp, const void *pStructBase, const void * pData, DVariant *pOut, int iElement, int objectID)
{
	if (!g_bShouldChangeGameRulesState)
		g_bShouldChangeGameRulesState = true; //If this called once, so, the props wants to be sent at this time, and we should do this for all clients!
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].pVar == pProp)
		{
			switch (g_HooksGamerules[i].PropType)
			{
				case PropType::Prop_Int:
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
				case PropType::Prop_Float:
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
				case PropType::Prop_String:
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
				case PropType::Prop_Vector:
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

//help

CBaseEntity * FindEntityByServerClassname(int iStart, const char * pServerClassName)
{
	if (!g_iEdictCount)
		g_iEdictCount = g_pGlobals->maxEntities * 2;
	if (iStart >= g_iEdictCount)
		return nullptr;
	for (int i = iStart; i < g_iEdictCount; i++)
	{
		CBaseEntity * pEnt = gamehelpers->ReferenceToEntity(i);
		if (!pEnt)
			continue;
		IServerNetworkable * pNetworkable = ((IServerUnknown *)pEnt)->GetNetworkable();
		if (!pNetworkable)
			continue;
		const char * pName = pNetworkable->GetServerClass()->GetName();
		if (pName && !strcmp(pName, pServerClassName))
			return pEnt;
	}
	return nullptr;
}

bool IsPropValid(SendProp * pProp, PropType iType)
{
	switch (iType)
	{
		case PropType::Prop_Int: 
			if (pProp->GetType() != DPT_Int)
				return false;
			return true;
		case PropType::Prop_Float:
		{
			if (pProp->GetType() != DPT_Float)
				return false;
			return true;
		}
		case PropType::Prop_String:
		{
			if (pProp->GetType() != DPT_String)
				return false;
			return true;
		}
		case PropType::Prop_Vector:
		{
			if (pProp->GetType() != DPT_Vector)
				return false;
			return true;
		}
	}
	return false;
}