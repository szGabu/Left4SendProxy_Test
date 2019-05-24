#pragma semicolon 1

#define PLUGIN_VERSION "0.1"

#include <sourcemod>
#include <sdktools>
#include "sendproxy"

#pragma newdecls required

public Plugin myinfo = 
{
	name = "SendProxy test",
	author = "CurT",
	description = "",
	version = PLUGIN_VERSION,
	url = "https://SourceGames.RU"
};

public void OnPluginStart()
{
	RegConsoleCmd("sm_testsendproxy", Cmd_TestSendProxy);
}

public Action Cmd_TestSendProxy(int iClient, int iArgs)
{
	for (int iClients = 1; iClients <= MaxClients; iClients++)
		if (IsClientInGame(iClients))
			SendProxy_Hook(iClients, "m_iHealth", Prop_Int, SendProxyCallBack_Client);
	SendProxy_HookGameRules("m_nRoundsPlayed", Prop_Int, SendProxyCallBack_GameRulesTest);
	PrintToConsole(iClient, "Okay");
	return Plugin_Handled;
}

public Action SendProxyCallBack_Client(int entity, const char[] PropName, int &iValue, int element, int iClient)
{
	if (entity != iClient)
	{
		iValue = 0;
		return Plugin_Changed;
	}
	return Plugin_Continue;
}

public Action SendProxyCallBack_GameRulesTest(const char[] cPropName, int &iValue, int iElement, int iClient)
{
	LogMessage("%i: %i", iClient, iValue);
	return Plugin_Continue;
}