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

#ifndef _INCLUDE_ISENDPROXY_
#define _INCLUDE_ISENDPROXY_
 
#include <IShareSys.h>
#include <IExtensionSys.h>
#include "dt_send.h"
#include "server_class.h"

#define SMINTERFACE_SENDPROXY_NAME		"ISendProxyInterface132"
#define SMINTERFACE_SENDPROXY_VERSION	0x132

class CBaseEntity;
class CBasePlayer;
class ISendProxyUnhookListener;

using namespace SourceMod;

template <class T, class A = CUtlMemory<T>>
class CModifiedUtlVector : public CUtlVector<T, A>
{
public:
	CModifiedUtlVector(int growSize = 0, int initSize = 0) : CUtlVector<T, A>(growSize, initSize) {}
	CModifiedUtlVector(T * pMemory, int allocationCount, int numElements = 0) : CUtlVector<T, A>(pMemory, allocationCount, numElements) {}
	
	//allow copy constructor
	CModifiedUtlVector(CModifiedUtlVector const& vec) { *this = vec; } //= is overriden
};

enum class PropType : uint8_t
{
	Prop_Int = 0,
	Prop_Float = 1, 
	Prop_String = 2,
	Prop_Array = 3,
	Prop_Vector = 4,
	Prop_Max
};

enum class CallBackType : uint8_t
{
	Callback_PluginFunction = 1,
	Callback_CPPFunction //see ISendProxyCallbacks
};

struct ListenerCallbackInfo
{
	IExtension *				m_pExt;
	IExtensionInterface *		m_pExtAPI;
	ISendProxyUnhookListener *	m_pCallBack;
	int 						m_iUnloadHook{0};
};

struct SendPropHook
{
	void *									pCallback;
	CallBackType							iCallbackType;
	SendProp *								pVar;
	edict_t *								pEnt;
	SendVarProxyFn							pRealProxy;
	int										objectID;
	PropType								PropType;
	int										Offset;
	int										Element{0};
	CModifiedUtlVector<ListenerCallbackInfo>	vListeners;
};

struct SendPropHookGamerules
{
	void *									pCallback;
	CallBackType							iCallbackType;
	SendProp *								pVar;
	SendVarProxyFn							pRealProxy;
	PropType								PropType;
	int										Offset;
	int										Element{0};
	CModifiedUtlVector<ListenerCallbackInfo>	vListeners;
};

struct PropChangeHook
{
	IPluginFunction *						pCallback;
	int										iLastValue;
	float									flLastValue;
	char									cLastValue[4096];
	SendProp *								pVar;
	PropType								PropType;
	unsigned int							Offset;
	int										objectID;
};

struct PropChangeHookGamerules
{
	IPluginFunction *						pCallback;
	int										iLastValue;
	float									flLastValue;
	char									cLastValue[4096];
	SendProp *								pVar;
	PropType								PropType;
	unsigned int							Offset;
};
 
class ISendProxyUnhookListener
{
public:
	/*
	 * Calls when hook of the entity prop is removed
	 *
	 * @param pEntity		Pointer to CBaseEntity object that was hooked
	 * @param pProp			Pointer to SendProp that was hooked
	 * @param iType			PropType of the prop
	 * @param iCallbackType	Type of callback
	 * @param pCallback		Pointer to callback function / class
	 *
	 * @noreturn
	 */
	virtual void OnEntityPropHookRemoved(CBaseEntity * pEntity, SendProp * pProp, PropType iType, CallBackType iCallbackType, void * pCallback) = 0;
	/*
	 * Calls when hook of the gamerules prop is removed
	 *
	 * @param pProp			Pointer to SendProp that was hooked
	 * @param iType			PropType of the prop
	 * @param iCallbackType	Type of callback
	 * @param pCallback		Pointer to callback function / class
	 *
	 * @noreturn
	 */
	virtual void OnGamerulesPropHookRemoved(SendProp * pProp, PropType iType, CallBackType iCallbackType, void * pCallback) = 0;
};

class ISendProxyCallbacks
{
public:
	/*
	 * Calls when proxy function of entity prop is called
	 *
	 * @param pEntity		Pointer to CBaseEntity object that hooked
	 * @param pProp			Pointer to SendProp that hooked
	 * @param pPlayer		Pointer to CBasePlayer object of the client that should receive the changed value
	 * @param pValue		Pointer to value of prop
	 * @param iType			PropType of the prop
	 * @param iElement		Element number
	 *
	 * @return				true, to use changed value, false, to use original
	 */
	virtual bool OnEntityPropProxyFunctionCalls(CBaseEntity * pEntity, SendProp * pProp, CBasePlayer * pPlayer, void * pValue, PropType iType, int iElement) = 0;
	/*
	 * Calls when proxy function of gamerules prop is called
	 *
	 * @param pProp			Pointer to SendProp that hooked
	 * @param pPlayer		Pointer to CBasePlayer object of the client that should receive the changed value
	 * @param pValue		Pointer to value of prop
	 * @param iType			PropType of the prop
	 * @param iElement		Element number
	 *
	 * @return				true, to use changed value, false, to use original
	 */
	virtual bool OnGamerulesPropProxyFunctionCalls(SendProp * pProp, CBasePlayer * pPlayer, void * pValue, PropType iType, int iElement) = 0;
};
 
class ISendProxyManager : public SMInterface
{
public: //SMInterface
	virtual const char * GetInterfaceName() = 0;
	virtual unsigned int GetInterfaceVersion() = 0;
	
public: //ISendProxyManager
	/*
	 * Hooks SendProp of entity, extension MUST remove ALL his hooks with no plugin callbacks on unload!!!
	 *
	 * @param pProp			Pointer to SendProp / name of the prop that should be hooked
	 * @param pEntity		Pointer to CBaseEntity object that should be hooked
	 * @param iType			PropType of the prop
	 * @param iCallbackType	Type of callback
	 * @param pCallback		Pointer to callback function / class
	 *
	 * @return				true, if prop hooked, false otherwise
	 */
	virtual bool HookProxy(SendProp * pProp, CBaseEntity * pEntity, PropType iType, CallBackType iCallbackType, void * pCallback) = 0;
	virtual bool HookProxy(const char * pProp, CBaseEntity * pEntity, PropType iType, CallBackType iCallbackType, void * pCallback) = 0;
	/*
	 * Hooks gamerules SendProp, extension MUST remove ALL his hooks with no plugin callbacks on unload!!!
	 *
	 * @param pProp			Pointer to SendProp / name of the prop that should be hooked
	 * @param iType			PropType of the prop
	 * @param iCallbackType	Type of callback
	 * @param pCallback		Pointer to callback function / class
	 *
	 * @return				true, if prop hooked, false otherwise
	 */
	virtual bool HookProxyGamerules(SendProp * pProp, PropType iType, CallBackType iCallbackType, void * pCallback) = 0;
	virtual bool HookProxyGamerules(const char * pProp, PropType iType, CallBackType iCallbackType, void * pCallback) = 0;
	/*
	 * Unhooks SendProp of entity
	 *
	 * @param pProp			Pointer to SendProp / name of the prop that should be unhooked
	 * @param pEntity		Pointer to CBaseEntity object that should be unhooked
	 * @param iCallbackType	Type of callback
	 * @param pCallback		Pointer to callback function / class
	 *
	 * @return				true, if prop unhooked, false otherwise
	 *						P.S. This function will trigger unhook listeners
	 */
	virtual bool UnhookProxy(SendProp * pProp, CBaseEntity * pEntity, CallBackType iCallbackType, void * pCallback) = 0;
	virtual bool UnhookProxy(const char * pProp, CBaseEntity * pEntity, CallBackType iCallbackType, void * pCallback) = 0;
	/*
	 * Unhooks gamerules SendProp
	 *
	 * @param pProp			Pointer to SendProp / name of the prop that should be unhooked
	 * @param iCallbackType	Type of callback
	 * @param pCallback		Pointer to callback function / class
	 *
	 * @return				true, if prop unhooked, false otherwise
	 *						P.S. This function will trigger unhook listeners
	 */
	virtual bool UnhookProxyGamerules(SendProp * pProp, CallBackType iCallbackType, void * pCallback) = 0;
	virtual bool UnhookProxyGamerules(const char * pProp, CallBackType iCallbackType, void * pCallback) = 0;
	/*
	 * Adds unhook listener to entity hook, so, when hook will be removed listener callback is called
	 *
	 * @param pProp			Pointer to SendProp / name of the prop that should be listen
	 * @param pEntity		Pointer to CBaseEntity object that should be listen
	 * @param iCallbackType	Type of callback of entity hook
	 * @param pCallback		Pointer to callback function / class of entity hook
	 * @param pListener		Pointer to listener callback
	 *
	 * @return				true, if listener installed, false otherwise
	 */
	virtual bool AddUnhookListener(IExtension * pMyself, SendProp * pProp, CBaseEntity * pEntity, CallBackType iCallbackType, void * pCallback, ISendProxyUnhookListener * pListener) = 0;
	virtual bool AddUnhookListener(IExtension * pMyself, const char * pProp, CBaseEntity * pEntity, CallBackType iCallbackType, void * pCallback, ISendProxyUnhookListener * pListener) = 0;
	 /*
	 * Adds unhook listener to gamerules hook, so, when hook will removed listener callback is called
	 *
	 * @param pProp			Pointer to SendProp / name of the prop that should be listen
	 * @param iCallbackType	Type of callback of gamerules hook
	 * @param pCallback		Pointer to callback function / class of gamerules hook
	 * @param pListener		Pointer to listener callback
	 *
	 * @return				true, if listener installed, false otherwise
	 */
	virtual bool AddUnhookListenerGamerules(IExtension * pMyself, SendProp * pProp, CallBackType iCallbackType, void * pCallback, ISendProxyUnhookListener * pListener) = 0;
	virtual bool AddUnhookListenerGamerules(IExtension * pMyself, const char * pProp, CallBackType iCallbackType, void * pCallback, ISendProxyUnhookListener * pListener) = 0;
	 /*
	 * Removes unhook listener from entity hook
	 *
	 * @param pMyself		Pointer to IExtension interface of current extension
	 * @param pProp			Pointer to SendProp / name of the prop that is listening
	 * @param pEntity		Pointer to CBaseEntity object that is listening
	 * @param iCallbackType	Type of callback of entity hook
	 * @param pCallback		Pointer to callback function / class of entity hook
	 * @param pListener		Pointer to listener callback
	 *
	 * @return				true, if listener removed, false otherwise
	 */
	virtual bool RemoveUnhookListener(IExtension * pMyself, SendProp * pProp, CBaseEntity * pEntity, CallBackType iCallbackType, void * pCallback, ISendProxyUnhookListener * pListener) = 0;
	virtual bool RemoveUnhookListener(IExtension * pMyself, const char * pProp, CBaseEntity * pEntity, CallBackType iCallbackType, void * pCallback, ISendProxyUnhookListener * pListener) = 0;
	  /*
	 * Removes unhook listener from gamerules hook
	 *
	 * @param pMyself		Pointer to IExtension interface of current extension
	 * @param pProp			Pointer to SendProp / name of the prop that is listening
	 * @param iCallbackType	Type of callback of gamerules hook
	 * @param pCallback		Pointer to callback function / class of gamerules hook
	 * @param pListener		Pointer to listener callback
	 *
	 * @return				true, if listener removed, false otherwise
	 */
	virtual bool RemoveUnhookListenerGamerules(IExtension * pMyself, SendProp * pProp, CallBackType iCallbackType, void * pCallback, ISendProxyUnhookListener * pListener) = 0;
	virtual bool RemoveUnhookListenerGamerules(IExtension * pMyself, const char * pProp, CallBackType iCallbackType, void * pCallback, ISendProxyUnhookListener * pListener) = 0;
};

#endif