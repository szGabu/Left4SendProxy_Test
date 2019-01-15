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

/**
 * @file extension.h
 * @brief Sample extension code header.
 */
#include "smsdk_ext.h"
#include "dt_send.h"
#include "server_class.h"
#include "convar.h"
#include <string>
#include <ISDKHooks.h>

#define GET_CONVAR(name) \
	name = g_pCVar->FindVar(#name); \
	if (name == nullptr) { \
		if (error != nullptr && maxlen != 0) { \
			ismm->Format(error, maxlen, "Could not find ConVar: " #name); \
		} \
		return false; \
	}

//#define STRING( offset )	( ( offset ) ? reinterpret_cast<const char *>( offset ) : "" )
	
enum {
	Prop_Int = 0,
	Prop_Float = 1, 
	Prop_String = 2,
	Prop_Array = 3,
	Prop_Vector = 4,
	Prop_Max
};

class SendPropHook
{
public:
	IPluginFunction*	pCallback;
	SendProp*			pVar;
	edict_t*			pEnt;
	SendVarProxyFn		pRealProxy;
	int					objectID;
	int					PropType;
	int					Offset;
	int					Element;
};

class SendPropHookGamerules
{
public:
	IPluginFunction*	pCallback;
	SendProp*			pVar;
	SendVarProxyFn		pRealProxy;
	int					PropType;
	int					Offset;
	int					Element;
};

class PropChangeHook
{
public:
	IPluginFunction*	pCallback;
	int					iLastValue;
	float				flLastValue;
	char				cLastValue[4096];
	SendProp*			pVar;
	int					PropType;
	unsigned int		Offset;
	int					objectID;
};

class PropChangeHookGamerules
{
public:
	IPluginFunction*	pCallback;
	int					iLastValue;
	float				flLastValue;
	char				cLastValue[4096];
	SendProp*			pVar;
	int					PropType;
	unsigned int		Offset;
};

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
public:
	virtual bool SDK_OnLoad(char *error, size_t maxlength, bool late);
	virtual void SDK_OnUnload();
	virtual void SDK_OnAllLoaded();
	
	virtual void OnCoreMapEnd();
	virtual void OnCoreMapStart(edict_t *, int, int);
	//virtual void SDK_OnPauseChange(bool paused);

	//virtual bool QueryRunning(char *error, size_t maxlength);
	void OnPluginUnloaded(IPlugin *plugin);
	//Returns true upon success
	//Returns false if hook exists for that object and prop
	//Returns false if the prop does not exist or the edict does not exist/is free
	bool AddHookToList(SendPropHook hook);
	bool AddHookToListGamerules(SendPropHookGamerules hook);
	bool HookProxy(SendProp* pProp, int objectID, IPluginFunction *pCallback);
	bool HookProxyGamerules(SendProp* pProp, IPluginFunction *pCallback);

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

#endif // _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
