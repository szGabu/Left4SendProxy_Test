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

#ifndef SENDPROXY_IFACE_IMPL_INC
#define SENDPROXY_IFACE_IMPL_INC

#include "extension.h"
#include "ISendProxy.h"

void CallListenersForHookID(int iID);
void CallListenersForHookIDGamerules(int iID);

class SendProxyManagerInterfaceImpl : public ISendProxyManager
{
public: //SMInterface
	virtual const char * GetInterfaceName();
	virtual unsigned int GetInterfaceVersion();
public: //interface impl:
	virtual bool HookProxy(IExtension *, SendProp *, CBaseEntity *, PropType, CallBackType, void *);
	virtual bool HookProxy(IExtension *, const char *, CBaseEntity *, PropType, CallBackType, void *);
	virtual bool HookProxyGamerules(IExtension *, SendProp *, PropType, CallBackType, void *);
	virtual bool HookProxyGamerules(IExtension *, const char *, PropType, CallBackType, void *);
	virtual bool UnhookProxy(IExtension *, SendProp *, CBaseEntity *, CallBackType, void *);
	virtual bool UnhookProxy(IExtension *, const char *, CBaseEntity *, CallBackType, void *);
	virtual bool UnhookProxyGamerules(IExtension *, SendProp *, CallBackType, void *);
	virtual bool UnhookProxyGamerules(IExtension *, const char *, CallBackType, void *);
	virtual bool AddUnhookListener(IExtension *, SendProp *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool AddUnhookListener(IExtension *, const char *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool AddUnhookListenerGamerules(IExtension *, SendProp *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool AddUnhookListenerGamerules(IExtension *, const char *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListener(IExtension *, SendProp *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListener(IExtension *, const char *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListenerGamerules(IExtension *, SendProp *, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListenerGamerules(IExtension *, const char *, CallBackType, void *, ISendProxyUnhookListener *);
	//same for the arrays =|
	virtual bool HookProxyArray(IExtension *, SendProp *, CBaseEntity *, PropType, int, CallBackType, void *);
	virtual bool HookProxyArray(IExtension *, const char *, CBaseEntity *, PropType, int, CallBackType, void *);
	virtual bool UnhookProxyArray(IExtension *, SendProp *, CBaseEntity *, int, CallBackType, void *);
	virtual bool UnhookProxyArray(IExtension *, const char *, CBaseEntity *, int, CallBackType, void *);
	virtual bool HookProxyArrayGamerules(IExtension *, SendProp *, PropType, int, CallBackType, void *);
	virtual bool HookProxyArrayGamerules(IExtension *, const char *, PropType, int, CallBackType, void *);
	virtual bool UnhookProxyArrayGamerules(IExtension *, SendProp *, int, CallBackType, void *);
	virtual bool UnhookProxyArrayGamerules(IExtension *, const char *, int, CallBackType, void *);
	virtual bool AddUnhookListenerArray(IExtension *, SendProp *, CBaseEntity *, int, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool AddUnhookListenerArray(IExtension *, const char *, CBaseEntity *, int, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool AddUnhookListenerArrayGamerules(IExtension *, SendProp *, int, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool AddUnhookListenerArrayGamerules(IExtension *, const char *, int, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListenerArray(IExtension *, SendProp *, CBaseEntity *, int, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListenerArray(IExtension *, const char *, CBaseEntity *, int, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListenerArrayGamerules(IExtension *, SendProp *, int, CallBackType, void *, ISendProxyUnhookListener *);
	virtual bool RemoveUnhookListenerArrayGamerules(IExtension *, const char *, int, CallBackType, void *, ISendProxyUnhookListener *);
	//checkers
	virtual bool IsProxyHooked(SendProp *, CBaseEntity *);
	virtual bool IsProxyHooked(const char *, CBaseEntity *);
	virtual bool IsProxyHookedGamerules(SendProp *);
	virtual bool IsProxyHookedGamerules(const char *);
	virtual bool IsProxyHookedArray(SendProp *, CBaseEntity *, int);
	virtual bool IsProxyHookedArray(const char *, CBaseEntity *, int);
	virtual bool IsProxyHookedArrayGamerules(SendProp *, int);
	virtual bool IsProxyHookedArrayGamerules(const char *, int);
	//TODO: same for the change hooks wtf
};

#endif