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

#ifndef _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_

 /*
	TODO:
		Implement interface for prop change hooks & array props
		Add Native_HookArrayPropGamerules & Native_UnhookArrayPropSendProxy
		Split extension.cpp into modules: natives.cpp, interface.cpp & extension.cpp
		Try to fix and use sv_parallel_sendsnapshot & sv_parallel_packentities if possible
		Allow multiple hooks for prop on same enity (probably use delegate for this?)
		Prop hooks also should removes automatically for extensions
		More optimizations! =D
 */

#include "smsdk_ext.h"
#include "convar.h"
#include <string>
#include <stdint.h>
#include <ISDKHooks.h>
#include "ISendProxy.h"

#define GET_CONVAR(name) \
	name = g_pCVar->FindVar(#name); \
	if (name == nullptr) { \
		if (error != nullptr && maxlen != 0) { \
			ismm->Format(error, maxlen, "Could not find ConVar: " #name); \
		} \
		return false; \
	}

void GlobalProxy(const SendProp *pProp, const void *pStructBase, const void* pData, DVariant *pOut, int iElement, int objectID);
void GlobalProxyGamerules(const SendProp *pProp, const void *pStructBase, const void* pData, DVariant *pOut, int iElement, int objectID);
/**
 * @brief Sample implementation of the SDK Extension.
 * Note: Uncomment one of the pre-defined virtual functions in order to use it.
 */
class SendProxyManager :
	public SDKExtension,
	public IPluginsListener,
	public ISMEntityListener
{
public: //sm
	virtual bool SDK_OnLoad(char *error, size_t maxlength, bool late);
	virtual void SDK_OnUnload();
	virtual void SDK_OnAllLoaded();
	
	virtual void OnCoreMapEnd();
	virtual void OnCoreMapStart(edict_t *, int, int);
	//virtual void SDK_OnPauseChange(bool paused);

	//virtual bool QueryRunning(char *error, size_t maxlength);
public: //other
	void OnPluginUnloaded(IPlugin *plugin);
	//Returns true upon success
	//Returns false if hook exists for that object and prop
	//Returns false if the prop does not exist or the edict does not exist/is free
	bool AddHookToList(SendPropHook hook);
	bool AddHookToListGamerules(SendPropHookGamerules hook);

	void UnhookProxy(int i);
	void UnhookProxyGamerules(int i);
	virtual int GetClientCount() const;
public: // ISMEntityListener
	virtual void OnEntityDestroyed(CBaseEntity *pEntity);
public:
#if defined SMEXT_CONF_METAMOD
	virtual bool SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late);
	//virtual bool SDK_OnMetamodUnload(char *error, size_t maxlength);
	//virtual bool SDK_OnMetamodPauseChange(bool paused, char *error, size_t maxlength);
#endif
};

class SendProxyManagerInterfaceImpl : public ISendProxyManager
{
public: //SMInterface
	virtual const char * GetInterfaceName();
	virtual unsigned int GetInterfaceVersion();
public: //interface impl:
	virtual bool HookProxy(SendProp *, CBaseEntity *, PropType, CallBackType, void *);
	virtual bool HookProxy(const char *, CBaseEntity *, PropType, CallBackType, void *);
	virtual bool HookProxyGamerules(SendProp *, PropType, CallBackType, void *);
	virtual bool HookProxyGamerules(const char *, PropType, CallBackType, void *);
	virtual bool UnhookProxy(SendProp *, CBaseEntity *, CallBackType, void *);
	virtual bool UnhookProxy(const char *, CBaseEntity *, CallBackType, void *);
	virtual bool UnhookProxyGamerules(SendProp *, CallBackType, void *);
	virtual bool UnhookProxyGamerules(const char *, CallBackType, void *);
	virtual bool AddUnhookListener(IExtension *, SendProp *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool AddUnhookListener(IExtension *, const char *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool AddUnhookListenerGamerules(IExtension *, SendProp *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool AddUnhookListenerGamerules(IExtension *, const char *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListener(IExtension *, SendProp *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListener(IExtension *, const char *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListenerGamerules(IExtension *, SendProp *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListenerGamerules(IExtension *, const char *, CallBackType, void *, ISendProxyUnhookListener *);
	
	/*TODO:
		HookProxyArray
		HookProxyArrayGamerules
		UnhookProxyArray
		UnhookProxyArrayGamerules
		AddUnhookListenerArray
		RemoveUnhookListenerArray
		AddUnhookListenerArrayGamerules
		RemoveUnhookListenerArrayGamerules
		IsProxyHooked
		IsGamerulesProxyHooked
		IsArrayProxyHooked
		IsArrayGamerulesProxyHooked
	*/
};

#endif // _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
