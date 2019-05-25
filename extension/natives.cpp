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

#include "natives.h"

static cell_t Native_UnhookPropChange(IPluginContext * pContext, const cell_t * params)
{
	if (params[1] < 0 || params[1] >= g_iEdictCount)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char* name;
	edict_t * pEnt = gamehelpers->EdictOfIndex(entity);
	pContext->LocalToString(params[2], &name);
	IPluginFunction * callback = pContext->GetFunctionById(params[3]);
	sm_sendprop_info_t info;
	ServerClass * sc = pEnt->GetNetworkable()->GetServerClass();
	if (!sc)
	{
		return pContext->ThrowNativeError("Cannot find ServerClass for entity %d", params[1]);
	}
	gamehelpers->FindSendPropInfo(sc->GetName(), name, &info);
	
	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].objectID == entity && g_ChangeHooks[i].pVar == info.prop)
		{
			auto pCallbacks = g_ChangeHooks[i].vCallbacksInfo;
			if (pCallbacks->Count())
			{
				for (int j = 0; j < pCallbacks->Count(); j++)
					if ((*pCallbacks)[j].iCallbackType == CallBackType::Callback_PluginFunction && (*pCallbacks)[j].pCallback == (void *)callback)
					{
						pCallbacks->Remove(j--);
					}
			}
			if (!pCallbacks->Count())
				g_ChangeHooks.Remove(i--);
		}
	}
	return 1;
}

static cell_t Native_UnhookPropChangeGameRules(IPluginContext * pContext, const cell_t * params)
{
	char* name;
	pContext->LocalToString(params[1], &name);
	IPluginFunction *callback = pContext->GetFunctionById(params[2]);
	sm_sendprop_info_t info;
	gamehelpers->FindSendPropInfo(g_szGameRulesProxy, name, &info);
	
	for (int i = 0; i < g_ChangeHooksGamerules.Count(); i++)
	{
		if (g_ChangeHooksGamerules[i].pVar == info.prop)
		{
			auto pCallbacks = g_ChangeHooksGamerules[i].vCallbacksInfo;
			if (pCallbacks->Count())
			{
				for (int j = 0; j < pCallbacks->Count(); j++)
					if ((*pCallbacks)[j].iCallbackType == CallBackType::Callback_PluginFunction && (*pCallbacks)[j].pCallback == (void *)callback)
					{
						pCallbacks->Remove(j--);
					}
			}
			if (!pCallbacks->Count())
				g_ChangeHooksGamerules.Remove(i--);
		}
	}
	return 1;
}
	
static cell_t Native_HookPropChange(IPluginContext * pContext, const cell_t * params)
{
	bool bSafeCheck = params[0] >= 4;
	if (params[1] < 0 || params[1] >= g_iEdictCount)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char* name;
	edict_t * pEnt = gamehelpers->EdictOfIndex(entity);
	pContext->LocalToString(params[2], &name);
	IPluginFunction * callback = nullptr;
	PropType propType = PropType::Prop_Max;
	if (bSafeCheck)
	{
		propType = static_cast<PropType>(params[3]);
		callback = pContext->GetFunctionById(params[4]);
	}
	else
	{
		callback = pContext->GetFunctionById(params[3]);
	}
	SendProp * pProp = nullptr;
	sm_sendprop_info_t info;
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	if (!sc)
	{
		return pContext->ThrowNativeError("Cannot find ServerClass for entity %d", params[1]);
	}
	gamehelpers->FindSendPropInfo(sc->GetName(), name, &info);
	pProp = info.prop;
	if (!pProp)
	{
		return pContext->ThrowNativeError("Could not find prop %s", name);
	}
	if (bSafeCheck && !IsPropValid(pProp, propType))
		switch (propType)
		{
		case PropType::Prop_Int:
			return pContext->ThrowNativeError("Prop %s is not an int!", pProp->GetName());
		case PropType::Prop_Float:
			return pContext->ThrowNativeError("Prop %s is not a float!", pProp->GetName());
		case PropType::Prop_String:
			return pContext->ThrowNativeError("Prop %s is not a string!", pProp->GetName());
		case PropType::Prop_Vector:
			return pContext->ThrowNativeError("Prop %s is not a vector!", pProp->GetName());
		default:
			return pContext->ThrowNativeError("Unsupported prop type %d", propType);
		}
	int offset = info.actual_offset;
	SendPropType type = pProp->GetType();
	CBaseEntity* pEntity = gameents->EdictToBaseEntity(pEnt);
	decltype(&g_ChangeHooks[0]) pHookInfo = nullptr;
	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].pVar == pProp)
		{
			pHookInfo = &g_ChangeHooks[i];
			break;
		}
	}
	if (pHookInfo)
	{
		//just validate it
		switch (type)
		{
		case DPT_Int:
		case DPT_Float:
		case DPT_String:
		case DPT_Vector:
			break;
		default: return pContext->ThrowNativeError("Prop type %d is not yet supported", type);
		}
		CallBackInfo sCallInfo;
		sCallInfo.iCallbackType = CallBackType::Callback_PluginFunction;
		sCallInfo.pCallback = (void *)callback;
		sCallInfo.pOwner = (void *)pContext;
		pHookInfo->vCallbacksInfo->AddToTail(sCallInfo);
	}
	else
	{
		PropChangeHook hook;
		switch (type)
		{
		case DPT_Int: hook.PropType = PropType::Prop_Int; hook.iLastValue = *(int *)((unsigned char *)pEntity + offset); break;
		case DPT_Float: hook.PropType = PropType::Prop_Float; hook.flLastValue = *(float *)((unsigned char*)pEntity + offset); break;
		case DPT_String: hook.PropType = PropType::Prop_String; strncpy(hook.cLastValue, (const char *)((unsigned char *)pEntity + offset), sizeof(hook.cLastValue)); break;
		case DPT_Vector: hook.PropType = PropType::Prop_Vector; hook.vecLastValue = *(Vector *)((unsigned char *)pEntity + offset); break;
		default: return pContext->ThrowNativeError("Prop type %d is not yet supported", type);
		}

		hook.objectID = entity;
		hook.Offset = offset;
		hook.pVar = pProp;
		CallBackInfo sCallInfo;
		sCallInfo.iCallbackType = CallBackType::Callback_PluginFunction;
		sCallInfo.pCallback = (void *)callback;
		sCallInfo.pOwner = (void *)pContext;
		hook.vCallbacksInfo->AddToTail(sCallInfo);
		g_ChangeHooks.AddToTail(hook);
	}
	return 1;
}

static cell_t Native_HookPropChangeGameRules(IPluginContext * pContext, const cell_t * params)
{
	bool bSafeCheck = params[0] >= 3;
	char* name;
	pContext->LocalToString(params[1], &name);
	IPluginFunction * callback = nullptr;
	PropType propType = PropType::Prop_Max;
	if (bSafeCheck)
	{
		propType = static_cast<PropType>(params[2]);
		callback = pContext->GetFunctionById(params[3]);
	}
	else
	{
		callback = pContext->GetFunctionById(params[2]);
	}
	SendProp *pProp = nullptr;
	PropChangeHookGamerules hook;
	sm_sendprop_info_t info;
	gamehelpers->FindSendPropInfo(g_szGameRulesProxy, name, &info);
	pProp = info.prop;
	if (!pProp)
	{
		return pContext->ThrowNativeError("Could not find prop %s", name);
	}
	if (bSafeCheck && !IsPropValid(pProp, propType))
		switch (propType)
		{
		case PropType::Prop_Int:
			return pContext->ThrowNativeError("Prop %s is not an int!", pProp->GetName());
		case PropType::Prop_Float:
			return pContext->ThrowNativeError("Prop %s is not a float!", pProp->GetName());
		case PropType::Prop_String:
			return pContext->ThrowNativeError("Prop %s is not a string!", pProp->GetName());
		case PropType::Prop_Vector:
			return pContext->ThrowNativeError("Prop %s is not a vector!", pProp->GetName());
		default:
			return pContext->ThrowNativeError("Unsupported prop type %d", propType);
		}
	int offset = info.actual_offset;
	SendPropType type = pProp->GetType();

	if (!g_pGameRules)
	{
		g_pGameRules = g_pSDKTools->GetGameRules();
		if (!g_pGameRules)
		{
			return pContext->ThrowNativeError("CRITICAL ERROR: Could not get gamerules pointer!");
		}
	}
	decltype(&g_ChangeHooksGamerules[0]) pHookInfo = nullptr;
	for (int i = 0; i < g_ChangeHooksGamerules.Count(); i++)
	{
		if (g_ChangeHooksGamerules[i].pVar == pProp)
		{
			pHookInfo = &g_ChangeHooksGamerules[i];
			break;
		}
	}
	
	if (pHookInfo)
	{
		//just validate it
		switch (type)
		{
		case DPT_Int:
		case DPT_Float:
		case DPT_String:
		case DPT_Vector:
			break;
		default: return pContext->ThrowNativeError("Prop type %d is not yet supported", type);
		}
		CallBackInfo sCallInfo;
		sCallInfo.iCallbackType = CallBackType::Callback_PluginFunction;
		sCallInfo.pCallback = (void *)callback;
		sCallInfo.pOwner = (void *)pContext;
		pHookInfo->vCallbacksInfo->AddToTail(sCallInfo);
	}
	else
	{
		switch (type)
		{
		case DPT_Int: hook.PropType = PropType::Prop_Int; hook.iLastValue = *(int *)((unsigned char *)g_pGameRules + offset); break;
		case DPT_Float: hook.PropType = PropType::Prop_Float; hook.flLastValue = *(float *)((unsigned char*)g_pGameRules + offset); break;
		case DPT_String: hook.PropType = PropType::Prop_String; strncpy(hook.cLastValue, (const char *)((unsigned char *)g_pGameRules + offset), sizeof(hook.cLastValue)); break;
		case DPT_Vector: hook.PropType = PropType::Prop_Vector; hook.vecLastValue = *(Vector *)((unsigned char *)g_pGameRules + offset); break;
		default: return pContext->ThrowNativeError("Prop type %d is not yet supported", type);
		}

		hook.Offset = offset;
		hook.pVar = pProp;
		CallBackInfo sCallInfo;
		sCallInfo.iCallbackType = CallBackType::Callback_PluginFunction;
		sCallInfo.pCallback = (void *)callback;
		sCallInfo.pOwner = (void *)pContext;
		hook.vCallbacksInfo->AddToTail(sCallInfo);

		g_ChangeHooksGamerules.AddToTail(hook);
	}
	return 1;
}

static cell_t Native_Hook(IPluginContext * pContext, const cell_t * params)
{
	if (params[1] < 0 || params[1] >= g_iEdictCount)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char* name;
	pContext->LocalToString(params[2], &name);
	edict_t * pEnt = gamehelpers->EdictOfIndex(entity);
	PropType propType = static_cast<PropType>(params[3]);
	IPluginFunction *callback = pContext->GetFunctionById(params[4]);
	SendProp *pProp = nullptr;
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	sm_sendprop_info_t info;
	
	if (!sc)
	{
		return pContext->ThrowNativeError("Cannot find ServerClass for entity %d", params[1]);
	}

	gamehelpers->FindSendPropInfo(sc->GetName(), name, &info);
	pProp = info.prop;
	if (!pProp)
	{
		return pContext->ThrowNativeError("Could not find prop %s", name);
	}
	if (!IsPropValid(pProp, propType))
		switch (propType)
		{
			case PropType::Prop_Int: 
				return pContext->ThrowNativeError("Prop %s is not an int!", pProp->GetName());
			case PropType::Prop_Float:
				return pContext->ThrowNativeError("Prop %s is not a float!", pProp->GetName());
			case PropType::Prop_String:
				return pContext->ThrowNativeError("Prop %s is not a string!", pProp->GetName());
			case PropType::Prop_Vector:
				return pContext->ThrowNativeError("Prop %s is not a vector!", pProp->GetName());
			default:
				return pContext->ThrowNativeError("Unsupported prop type %d", propType);
		}
	
	SendPropHook hook;
	hook.objectID = entity;
	hook.sCallbackInfo.pCallback = (void *)callback;
	hook.sCallbackInfo.iCallbackType = CallBackType::Callback_PluginFunction;
	hook.pEnt = pEnt;
	bool bHookedAlready = false;
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			hook.pRealProxy = g_Hooks[i].pRealProxy;
			bHookedAlready = true;
			break;
		}
	}
	if (!bHookedAlready)
		hook.pRealProxy = pProp->GetProxyFn();
	hook.PropType = propType;
	hook.pVar = pProp;
	
	//if this prop has been hooked already, don't set the proxy again
	if (bHookedAlready)
	{
		if (g_SendProxyManager.AddHookToList(hook))
			return 1;
		return 0;
	}
	if (g_SendProxyManager.AddHookToList(hook))
	{
		pProp->SetProxyFn(GlobalProxy);
		return 1;
	}
	return 0;
}

static cell_t Native_HookGameRules(IPluginContext * pContext, const cell_t * params)
{
	char* name;
	pContext->LocalToString(params[1], &name);
	PropType propType = static_cast<PropType>(params[2]);
	IPluginFunction *callback = pContext->GetFunctionById(params[3]);
	SendProp *pProp = nullptr;
	sm_sendprop_info_t info;

	gamehelpers->FindSendPropInfo(g_szGameRulesProxy, name, &info);
	pProp = info.prop;
	if (!pProp)
	{
		return pContext->ThrowNativeError("Could not find prop %s", name);
	}
	if (!IsPropValid(pProp, propType))
		switch (propType)
		{
			case PropType::Prop_Int: 
				return pContext->ThrowNativeError("Prop %s is not an int!", pProp->GetName());
			case PropType::Prop_Float:
				return pContext->ThrowNativeError("Prop %s is not a float!", pProp->GetName());
			case PropType::Prop_String:
				return pContext->ThrowNativeError("Prop %s is not a string!", pProp->GetName());
			case PropType::Prop_Vector:
				return pContext->ThrowNativeError("Prop %s is not a vector!", pProp->GetName());
			default:
				return pContext->ThrowNativeError("Unsupported prop type %d", propType);
		}
	SendPropHookGamerules hook;
	hook.sCallbackInfo.pCallback = (void *)callback;
	hook.sCallbackInfo.iCallbackType = CallBackType::Callback_PluginFunction;
	bool bHookedAlready = false;
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].pVar == pProp)
		{
			hook.pRealProxy = g_HooksGamerules[i].pRealProxy;
			bHookedAlready = true;
			break;
		}
	}
	if (!bHookedAlready)
		hook.pRealProxy = pProp->GetProxyFn();
	hook.PropType = propType;
	hook.pVar = pProp;
	
	//if this prop has been hooked already, don't set the proxy again
	if (bHookedAlready)
	{
		if (g_SendProxyManager.AddHookToListGamerules(hook))
			return 1;
		return 0;
	}
	if (g_SendProxyManager.AddHookToListGamerules(hook))
	{
		pProp->SetProxyFn(GlobalProxyGamerules);
		return 1;
	}
	return 0;
}

static cell_t Native_HookArrayProp(IPluginContext * pContext, const cell_t * params)
{
	if (params[1] < 0 || params[1] >= g_iEdictCount)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char *propName;
	pContext->LocalToString(params[2], &propName);
	int element = params[3];
	PropType propType = static_cast<PropType>(params[4]);
	IPluginFunction *callback = pContext->GetFunctionById(params[5]);
	
	edict_t * pEnt = gamehelpers->EdictOfIndex(entity);
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	if (!sc)
	{
		return pContext->ThrowNativeError("Cannot find ServerClass for entity %d", params[1]);
	}
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
	
	if (!IsPropValid(pProp, propType))
		switch (propType)
		{
			case PropType::Prop_Int: 
				return pContext->ThrowNativeError("Prop %s is not an int!", pProp->GetName());
			case PropType::Prop_Float:
				return pContext->ThrowNativeError("Prop %s is not a float!", pProp->GetName());
			case PropType::Prop_String:
				return pContext->ThrowNativeError("Prop %s is not a string!", pProp->GetName());
			case PropType::Prop_Vector:
				return pContext->ThrowNativeError("Prop %s is not a vector!", pProp->GetName());
			default:
				return pContext->ThrowNativeError("Unsupported prop type %d", propType);
		}
	
	SendPropHook hook;
	hook.objectID = entity;
	hook.sCallbackInfo.pCallback = (void *)callback;
	hook.sCallbackInfo.iCallbackType = CallBackType::Callback_PluginFunction;
	hook.pEnt = pEnt;
	hook.Element = element;
	bool bHookedAlready = false;
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			hook.pRealProxy = g_Hooks[i].pRealProxy;
			bHookedAlready = true;
			break;
		}
	}
	if (!bHookedAlready)
		hook.pRealProxy = pProp->GetProxyFn();
	hook.PropType = propType;
	hook.pVar = pProp;
	
	if (bHookedAlready)
	{
		if (g_SendProxyManager.AddHookToList(hook))
			return 1;
		return 0;
	}
	if (g_SendProxyManager.AddHookToList(hook))
	{
		pProp->SetProxyFn(GlobalProxy);
		return 1;
	}
	return 0;
}

static cell_t Native_UnhookArrayProp(IPluginContext * pContext, const cell_t * params)
{
	if (params[1] < 0 || params[1] >= g_iEdictCount)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char *propName;
	pContext->LocalToString(params[2], &propName);
	int element = params[3];
	PropType propType = static_cast<PropType>(params[4]);
	IPluginFunction *callback = pContext->GetFunctionById(params[5]);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].Element == element && g_Hooks[i].sCallbackInfo.iCallbackType == CallBackType::Callback_PluginFunction && g_Hooks[i].PropType == propType && g_Hooks[i].sCallbackInfo.pCallback == (void *)callback && !strcmp(g_Hooks[i].pVar->GetName(), propName) && g_Hooks[i].objectID == entity)
		{
			g_SendProxyManager.UnhookProxy(i);
			return 1;
		}
	}
	return 0;
}

static cell_t Native_Unhook(IPluginContext * pContext, const cell_t * params)
{
	char *propName;
	pContext->LocalToString(params[2], &propName);
	IPluginFunction *pFunction = pContext->GetFunctionById(params[3]);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (params[1] == g_Hooks[i].objectID && g_Hooks[i].sCallbackInfo.iCallbackType == CallBackType::Callback_PluginFunction && strcmp(g_Hooks[i].pVar->GetName(), propName) == 0 && (void *)pFunction == g_Hooks[i].sCallbackInfo.pCallback)
		{
			g_SendProxyManager.UnhookProxy(i);
			return 1;
		}
	}
	return 0;
}

static cell_t Native_UnhookGameRules(IPluginContext * pContext, const cell_t * params)
{
	char *propName;
	pContext->LocalToString(params[1], &propName);
	IPluginFunction *pFunction = pContext->GetFunctionById(params[2]);
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].sCallbackInfo.iCallbackType == CallBackType::Callback_PluginFunction && strcmp(g_HooksGamerules[i].pVar->GetName(), propName) == 0 && (void *)pFunction == g_HooksGamerules[i].sCallbackInfo.pCallback)
		{
			g_SendProxyManager.UnhookProxyGamerules(i);
			return 1;
		}
	}
	return 0;
}

static cell_t Native_IsHooked(IPluginContext * pContext, const cell_t * params)
{
	int objectID = params[1];
	char * propName;
	pContext->LocalToString(params[2], &propName);

	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].objectID == objectID && g_Hooks[i].sCallbackInfo.pOwner == (void *)pContext && strcmp(propName, g_Hooks[i].pVar->GetName()) == 0)
			return 1;
	}
	return 0;
}

static cell_t Native_IsHookedGameRules(IPluginContext * pContext, const cell_t * params)
{
	char *propName;
	pContext->LocalToString(params[1], &propName);

	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].sCallbackInfo.pOwner == (void *)pContext && strcmp(propName, g_HooksGamerules[i].pVar->GetName()) == 0)
			return 1;
	}
	return 0;
}

static cell_t Native_HookArrayPropGamerules(IPluginContext * pContext, const cell_t * params)
{
	char *propName;
	pContext->LocalToString(params[1], &propName);
	int element = params[2];
	PropType propType = static_cast<PropType>(params[3]);
	IPluginFunction * callback = pContext->GetFunctionById(params[4]);
	
	SendProp * pProp = nullptr;
	sm_sendprop_info_t info;

	gamehelpers->FindSendPropInfo(g_szGameRulesProxy, propName, &info);
	pProp = info.prop;
	if (!info.prop)
	{
		return pContext->ThrowNativeError("Could not find prop %s", propName);
	}
	SendTable *st = info.prop->GetDataTable();
	if (!st)
	{
		return pContext->ThrowNativeError("Prop %s does not contain any elements", propName);
	}

	pProp = st->GetProp(element);
	if (!pProp)
	{
		return pContext->ThrowNativeError("Could not find element %d in %s", element, info.prop->GetName());
	}
	
	if (!IsPropValid(pProp, propType))
		switch (propType)
		{
			case PropType::Prop_Int: 
				return pContext->ThrowNativeError("Prop %s is not an int!", pProp->GetName());
			case PropType::Prop_Float:
				return pContext->ThrowNativeError("Prop %s is not a float!", pProp->GetName());
			case PropType::Prop_String:
				return pContext->ThrowNativeError("Prop %s is not a string!", pProp->GetName());
			case PropType::Prop_Vector:
				return pContext->ThrowNativeError("Prop %s is not a vector!", pProp->GetName());
			default:
				return pContext->ThrowNativeError("Unsupported prop type %d", propType);
		}
	
	SendPropHookGamerules hook;
	hook.sCallbackInfo.pCallback = (void *)callback;
	hook.sCallbackInfo.iCallbackType = CallBackType::Callback_PluginFunction;
	hook.Element = element;
	bool bHookedAlready = false;
	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_HooksGamerules[i].pVar == pProp)
		{
			hook.pRealProxy = g_HooksGamerules[i].pRealProxy;
			bHookedAlready = true;
			break;
		}
	}
	if (!bHookedAlready)
		hook.pRealProxy = pProp->GetProxyFn();
	hook.PropType = propType;
	hook.pVar = pProp;
	
	if (bHookedAlready)
	{
		if (g_SendProxyManager.AddHookToListGamerules(hook))
			return 1;
		return 0;
	}
	if (g_SendProxyManager.AddHookToListGamerules(hook))
	{
		pProp->SetProxyFn(GlobalProxy);
		return 1;
	}
	return 0;
}

static cell_t Native_UnhookArrayPropGamerules(IPluginContext * pContext, const cell_t * params)
{
	char *propName;
	pContext->LocalToString(params[1], &propName);
	int iElement = params[2];
	PropType iPropType = static_cast<PropType>(params[3]);
	IPluginFunction *pFunction = pContext->GetFunctionById(params[4]);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_HooksGamerules[i].Element == iElement && g_HooksGamerules[i].sCallbackInfo.iCallbackType == CallBackType::Callback_PluginFunction && g_HooksGamerules[i].PropType == iPropType && g_HooksGamerules[i].sCallbackInfo.pCallback == (void *)pFunction && !strcmp(g_HooksGamerules[i].pVar->GetName(), propName))
		{
			g_SendProxyManager.UnhookProxy(i);
			return 1;
		}
	}
	return 0;
}

static cell_t Native_IsHookedArray(IPluginContext * pContext, const cell_t * params)
{
	int objectID = params[1];
	char *propName;
	pContext->LocalToString(params[2], &propName);
	int iElement = params[3];

	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].sCallbackInfo.pOwner == (void *)pContext && g_Hooks[i].objectID == objectID && g_Hooks[i].Element == iElement && strcmp(propName, g_Hooks[i].pVar->GetName()) == 0)
			return 1;
	}
	return 0;
}

static cell_t Native_IsHookedArrayGameRules(IPluginContext * pContext, const cell_t * params)
{
	char *propName;
	pContext->LocalToString(params[1], &propName);
	int iElement = params[2];

	for (int i = 0; i < g_HooksGamerules.Count(); i++)
	{
		if (g_Hooks[i].sCallbackInfo.pOwner == (void *)pContext && g_HooksGamerules[i].Element == iElement && strcmp(propName, g_HooksGamerules[i].pVar->GetName()) == 0)
			return 1;
	}
	return 0;
}

static cell_t Native_HookPropChangeArray(IPluginContext * pContext, const cell_t * params)
{
	if (params[1] < 0 || params[1] >= g_iEdictCount)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char* name;
	edict_t * pEnt = gamehelpers->EdictOfIndex(entity);
	pContext->LocalToString(params[2], &name);
	int element = params[3];
	PropType propType = static_cast<PropType>(params[4]);
	IPluginFunction *callback = pContext->GetFunctionById(params[5]);
	PropChangeHook hook;
	sm_sendprop_info_t info;
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	if (!sc)
	{
		return pContext->ThrowNativeError("Cannot find ServerClass for entity %d", params[1]);
	}
	gamehelpers->FindSendPropInfo(sc->GetName(), name, &info);
	if (!info.prop)
	{
		return pContext->ThrowNativeError("Could not find prop %s", name);
	}
	SendTable * st = info.prop->GetDataTable();
	if (!st)
	{
		return pContext->ThrowNativeError("Prop %s does not contain any elements", name);
	}
	SendProp * pProp = st->GetProp(element);
	if (!pProp)
	{
		return pContext->ThrowNativeError("Could not find element %d in %s", element, info.prop->GetName());
	}
	if (!IsPropValid(pProp, propType))
		switch (propType)
		{
		case PropType::Prop_Int:
			return pContext->ThrowNativeError("Prop %s is not an int!", pProp->GetName());
		case PropType::Prop_Float:
			return pContext->ThrowNativeError("Prop %s is not a float!", pProp->GetName());
		case PropType::Prop_String:
			return pContext->ThrowNativeError("Prop %s is not a string!", pProp->GetName());
		case PropType::Prop_Vector:
			return pContext->ThrowNativeError("Prop %s is not a vector!", pProp->GetName());
		default:
			return pContext->ThrowNativeError("Unsupported prop type %d", propType);
		}
	int offset = info.actual_offset + pProp->GetOffset();
	SendPropType type = pProp->GetType();
	CBaseEntity * pEntity = gameents->EdictToBaseEntity(pEnt);

	decltype(&g_ChangeHooks[0]) pHookInfo = nullptr;
	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].pVar == pProp)
		{
			pHookInfo = &g_ChangeHooks[i];
			break;
		}
	}
	if (pHookInfo)
	{
		//just validate it
		switch (type)
		{
		case DPT_Int:
		case DPT_Float:
		case DPT_String:
		case DPT_Vector:
			break;
		default: return pContext->ThrowNativeError("Prop type %d is not yet supported", type);
		}
		CallBackInfo sCallInfo;
		sCallInfo.iCallbackType = CallBackType::Callback_PluginFunction;
		sCallInfo.pCallback = (void *)callback;
		sCallInfo.pOwner = (void *)pContext;
		pHookInfo->vCallbacksInfo->AddToTail(sCallInfo);
	}
	else
	{
		PropChangeHook hook;
		switch (type)
		{
		case DPT_Int: hook.PropType = PropType::Prop_Int; hook.iLastValue = *(int *)((unsigned char *)pEntity + offset); break;
		case DPT_Float: hook.PropType = PropType::Prop_Float; hook.flLastValue = *(float *)((unsigned char*)pEntity + offset); break;
		case DPT_String: hook.PropType = PropType::Prop_String; strncpy(hook.cLastValue, (const char *)((unsigned char *)pEntity + offset), sizeof(hook.cLastValue)); break;
		case DPT_Vector: hook.PropType = PropType::Prop_Vector; hook.vecLastValue = *(Vector *)((unsigned char *)pEntity + offset); break;
		default: return pContext->ThrowNativeError("Prop type %d is not yet supported", type);
		}

		hook.objectID = entity;
		hook.Offset = offset;
		hook.pVar = pProp;
		CallBackInfo sCallInfo;
		sCallInfo.iCallbackType = CallBackType::Callback_PluginFunction;
		sCallInfo.pCallback = (void *)callback;
		sCallInfo.pOwner = (void *)pContext;
		hook.vCallbacksInfo->AddToTail(sCallInfo);
		g_ChangeHooks.AddToTail(hook);
	}
	return 1;
}

static cell_t Native_UnhookPropChangeArray(IPluginContext * pContext, const cell_t * params)
{
	if (params[1] < 0 || params[1] >= g_iEdictCount)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char * name;
	edict_t * pEnt = gamehelpers->EdictOfIndex(entity);
	pContext->LocalToString(params[2], &name);
	int element = params[3];
	IPluginFunction * callback = pContext->GetFunctionById(params[4]);
	sm_sendprop_info_t info;
	ServerClass * sc = pEnt->GetNetworkable()->GetServerClass();
	if (!sc)
	{
		return pContext->ThrowNativeError("Cannot find ServerClass for entity %d", params[1]);
	}
	gamehelpers->FindSendPropInfo(sc->GetName(), name, &info);
	SendTable * st = info.prop->GetDataTable();
	if (!st)
	{
		return pContext->ThrowNativeError("Prop %s does not contain any elements", name);
	}
	SendProp * pProp = st->GetProp(element);
	if (!pProp)
	{
		return pContext->ThrowNativeError("Could not find element %d in %s", element, info.prop->GetName());
	}
	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].objectID == entity && g_ChangeHooks[i].pVar == info.prop)
		{
			auto pCallbacks = g_ChangeHooks[i].vCallbacksInfo;
			if (pCallbacks->Count())
			{
				for (int j = 0; j < pCallbacks->Count(); j++)
					if ((*pCallbacks)[j].iCallbackType == CallBackType::Callback_PluginFunction && (*pCallbacks)[j].pCallback == (void *)callback)
					{
						pCallbacks->Remove(j--);
					}
			}
			if (!pCallbacks->Count())
				g_ChangeHooks.Remove(i--);
		}
	}
	return 1;
}

static cell_t Native_HookPropChangeArrayGameRules(IPluginContext * pContext, const cell_t * params)
{
	char* name;
	pContext->LocalToString(params[1], &name);
	int element = params[2];
	PropType propType = static_cast<PropType>(params[3]);
	IPluginFunction * callback = pContext->GetFunctionById(params[4]);
	sm_sendprop_info_t info;

	gamehelpers->FindSendPropInfo(g_szGameRulesProxy, name, &info);
	if (!info.prop)
	{
		return pContext->ThrowNativeError("Could not find prop %s", name);
	}
	SendTable * st = info.prop->GetDataTable();
	if (!st)
	{
		return pContext->ThrowNativeError("Prop %s does not contain any elements", name);
	}
	SendProp * pProp = st->GetProp(element);
	if (!pProp)
	{
		return pContext->ThrowNativeError("Could not find element %d in %s", element, info.prop->GetName());
	}
	if (!IsPropValid(pProp, propType))
		switch (propType)
		{
		case PropType::Prop_Int:
			return pContext->ThrowNativeError("Prop %s is not an int!", pProp->GetName());
		case PropType::Prop_Float:
			return pContext->ThrowNativeError("Prop %s is not a float!", pProp->GetName());
		case PropType::Prop_String:
			return pContext->ThrowNativeError("Prop %s is not a string!", pProp->GetName());
		case PropType::Prop_Vector:
			return pContext->ThrowNativeError("Prop %s is not a vector!", pProp->GetName());
		default:
			return pContext->ThrowNativeError("Unsupported prop type %d", propType);
		}
	int offset = info.actual_offset + pProp->GetOffset();
	SendPropType type = pProp->GetType();
	PropChangeHookGamerules hook;

	decltype(&g_ChangeHooksGamerules[0]) pHookInfo = nullptr;
	for (int i = 0; i < g_ChangeHooksGamerules.Count(); i++)
	{
		if (g_ChangeHooksGamerules[i].pVar == pProp)
		{
			pHookInfo = &g_ChangeHooksGamerules[i];
			break;
		}
	}

	if (pHookInfo)
	{
		//just validate it
		switch (type)
		{
		case DPT_Int:
		case DPT_Float:
		case DPT_String:
		case DPT_Vector:
			break;
		default: return pContext->ThrowNativeError("Prop type %d is not yet supported", type);
		}
		CallBackInfo sCallInfo;
		sCallInfo.iCallbackType = CallBackType::Callback_PluginFunction;
		sCallInfo.pCallback = (void *)callback;
		sCallInfo.pOwner = (void *)pContext;
		pHookInfo->vCallbacksInfo->AddToTail(sCallInfo);
	}
	else
	{
		switch (type)
		{
		case DPT_Int: hook.PropType = PropType::Prop_Int; hook.iLastValue = *(int *)((unsigned char *)g_pGameRules + offset); break;
		case DPT_Float: hook.PropType = PropType::Prop_Float; hook.flLastValue = *(float *)((unsigned char*)g_pGameRules + offset); break;
		case DPT_String: hook.PropType = PropType::Prop_String; strncpy(hook.cLastValue, (const char *)((unsigned char *)g_pGameRules + offset), sizeof(hook.cLastValue)); break;
		case DPT_Vector: hook.PropType = PropType::Prop_Vector; hook.vecLastValue = *(Vector *)((unsigned char *)g_pGameRules + offset); break;
		default: return pContext->ThrowNativeError("Prop type %d is not yet supported", type);
		}

		hook.Offset = offset;
		hook.pVar = pProp;
		CallBackInfo sCallInfo;
		sCallInfo.iCallbackType = CallBackType::Callback_PluginFunction;
		sCallInfo.pCallback = (void *)callback;
		sCallInfo.pOwner = (void *)pContext;
		hook.vCallbacksInfo->AddToTail(sCallInfo);

		g_ChangeHooksGamerules.AddToTail(hook);
	}
	return 1;
}

static cell_t Native_UnhookPropChangeArrayGameRules(IPluginContext * pContext, const cell_t * params)
{
	char * name;
	pContext->LocalToString(params[1], &name);
	int element = params[2];
	IPluginFunction * callback = pContext->GetFunctionById(params[3]);
	sm_sendprop_info_t info;
	gamehelpers->FindSendPropInfo(g_szGameRulesProxy, name, &info);
	if (!info.prop)
	{
		return pContext->ThrowNativeError("Could not find prop %s", name);
	}
	SendTable * st = info.prop->GetDataTable();
	if (!st)
	{
		return pContext->ThrowNativeError("Prop %s does not contain any elements", name);
	}
	SendProp * pProp = st->GetProp(element);
	if (!pProp)
	{
		return pContext->ThrowNativeError("Could not find element %d in %s", element, info.prop->GetName());
	}
	for (int i = 0; i < g_ChangeHooksGamerules.Count(); i++)
	{
		if (g_ChangeHooksGamerules[i].pVar == info.prop)
		{
			auto pCallbacks = g_ChangeHooksGamerules[i].vCallbacksInfo;
			if (pCallbacks->Count())
			{
				for (int j = 0; j < pCallbacks->Count(); j++)
					if ((*pCallbacks)[j].iCallbackType == CallBackType::Callback_PluginFunction && (*pCallbacks)[j].pCallback == (void *)callback)
					{
						pCallbacks->Remove(j--);
					}
			}
			if (!pCallbacks->Count())
				g_ChangeHooksGamerules.Remove(i--);
		}
	}
	return 1;
}

static cell_t Native_IsPropChangeHooked(IPluginContext * pContext, const cell_t * params)
{
	int objectID = params[1];
	char * propName;
	pContext->LocalToString(params[2], &propName);

	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].objectID == objectID && strcmp(propName, g_ChangeHooks[i].pVar->GetName()) == 0)
		{
			auto pCallbacks = g_ChangeHooks[i].vCallbacksInfo;
			if (pCallbacks->Count())
			{
				for (int j = 0; j < pCallbacks->Count(); j++)
					if ((*pCallbacks)[j].iCallbackType == CallBackType::Callback_PluginFunction && (*pCallbacks)[j].pOwner == (void *)pContext)
					{
						return 1;
					}
			}
			break;
		}
	}
	return 0;
}

static cell_t Native_IsPropChangeHookedGameRules(IPluginContext * pContext, const cell_t * params)
{
	char * propName;
	pContext->LocalToString(params[1], &propName);

	for (int i = 0; i < g_ChangeHooksGamerules.Count(); i++)
	{
		if (strcmp(propName, g_ChangeHooksGamerules[i].pVar->GetName()) == 0)
		{
			auto pCallbacks = g_ChangeHooksGamerules[i].vCallbacksInfo;
			if (pCallbacks->Count())
			{
				for (int j = 0; j < pCallbacks->Count(); j++)
					if ((*pCallbacks)[j].iCallbackType == CallBackType::Callback_PluginFunction && (*pCallbacks)[j].pOwner == (void *)pContext)
					{
						return 1;
					}
			}
			break;
		}
	}
	return 0;
}

static cell_t Native_IsPropChangeArrayHooked(IPluginContext * pContext, const cell_t * params)
{
	int objectID = params[1];
	char * propName;
	pContext->LocalToString(params[2], &propName);
	int element = params[3];

	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].Element == element && g_ChangeHooks[i].objectID == objectID && strcmp(propName, g_ChangeHooks[i].pVar->GetName()) == 0)
		{
			auto pCallbacks = g_ChangeHooks[i].vCallbacksInfo;
			if (pCallbacks->Count())
			{
				for (int j = 0; j < pCallbacks->Count(); j++)
					if ((*pCallbacks)[j].iCallbackType == CallBackType::Callback_PluginFunction && (*pCallbacks)[j].pOwner == (void *)pContext)
					{
						return 1;
					}
			}
			break;
		}
	}
	return 0;
}

static cell_t Native_IsPropChangeArrayHookedGameRules(IPluginContext * pContext, const cell_t * params)
{
	char * propName;
	pContext->LocalToString(params[1], &propName);
	int element = params[2];

	for (int i = 0; i < g_ChangeHooksGamerules.Count(); i++)
	{
		if (g_ChangeHooksGamerules[i].Element == element && strcmp(propName, g_ChangeHooksGamerules[i].pVar->GetName()) == 0)
		{
			auto pCallbacks = g_ChangeHooksGamerules[i].vCallbacksInfo;
			if (pCallbacks->Count())
			{
				for (int j = 0; j < pCallbacks->Count(); j++)
					if ((*pCallbacks)[j].iCallbackType == CallBackType::Callback_PluginFunction && (*pCallbacks)[j].pOwner == (void *)pContext)
					{
						return 1;
					}
			}
			break;
		}
	}
	return 0;
}

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
	{"SendProxy_HookArrayPropGamerules", Native_HookArrayPropGamerules},
	{"SendProxy_UnhookArrayPropGamerules", Native_UnhookArrayPropGamerules},
	{"SendProxy_IsHookedArrayProp", Native_IsHookedArray},
	{"SendProxy_IsHookedArrayPropGamerules", Native_IsHookedArrayGameRules},
	{"SendProxy_HookPropChangeArray", Native_HookPropChangeArray},
	{"SendProxy_UnhookPropChangeArray", Native_UnhookPropChangeArray},
	{"SendProxy_HookPropChangeArrayGameRules", Native_HookPropChangeArrayGameRules},
	{"SendProxy_UnhookPropChangeArrayGameRules", Native_UnhookPropChangeArrayGameRules},
	{"SendProxy_IsPropChangeHooked", Native_IsPropChangeHooked},
	{"SendProxy_IsPropChangeHookedGameRules", Native_IsPropChangeHookedGameRules},
	{"SendProxy_IsPropChangeArrayHooked", Native_IsPropChangeArrayHooked},
	{"SendProxy_IsPropChangeArrayHookedGameRules", Native_IsPropChangeArrayHookedGameRules},
	{"SendProxy_HookPropChangeSafe", Native_HookPropChange},
	{"SendProxy_HookPropChangeGameRulesSafe", Native_HookPropChangeGameRules},
	//Probably add listeners for plugins?
	{NULL,	NULL},
};