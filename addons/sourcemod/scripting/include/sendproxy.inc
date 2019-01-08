#if !defined _SENDPROXYMANAGER_INC_
#define _SENDPROXYMANAGER_INC_

enum SendPropType {
	Prop_Int = 0,
	Prop_Float = 1,
	Prop_String = 2,
	//Prop_Array = 3,
	Prop_Vector = 4,
	Prop_Max
};

typeset SendProxyCallback
{
	function Action (int iEntity, const char[] cPropName, int &iValue, int iElement, int iClient); //Prop_Int
	function Action (int iEntity, const char[] cPropName, float &flValue, int iElement, int iClient); //Prop_Float
	function Action (int iEntity, const char[] cPropName, char cModifiedValue[4096], int iElement, int iClient); //Prop_String
	function Action (int iEntity, const char[] cPropName, float vecValues[3], int iElement, int iClient); //Prop_Vector
};

typeset SendProxyCallbackGamerules
{
	function Action (const char[] cPropName, int &iValue, int iElement); //Prop_Int
	function Action (const char[] cPropName, float &flValue, int iElement); //Prop_Float
	function Action (const char[] cPropName, char cModifiedValue[4096], int iElement); //Prop_String
	function Action (const char[] cPropName, float vecValues[3], int iElement); //Prop_Vector
};

typedef PropChangedCallback = function void(int iEntity, const char[] cPropName, const char[] oldValue, const char[] newValue);
typedef GameRulesPropChangedCallback = function void(const char[] PropName, const char[] oldValue, const char[] newValue);

//Returns true upon success, false upon failure
native bool SendProxy_Hook(int entity, char[] propname, SendPropType proptype, SendProxyCallback callback);
native bool SendProxy_HookGameRules(char[] propname, SendPropType proptype, SendProxyCallbackGamerules callback);
native bool SendProxy_HookArrayProp(int entity, const char[] name, int element, SendPropType type, SendProxyCallback callback);
native bool SendProxy_UnhookArrayProp(int entity, const char[] name, int element, SendPropType type, SendProxyCallback callback);
native bool SendProxy_Unhook(int entity, char[] propname, SendProxyCallback callback);
native bool SendProxy_UnhookGameRules(char[] propname, SendProxyCallbackGamerules callback);
native bool SendProxy_IsHooked(int entity, char[] propname);
native bool SendProxy_IsHookedGameRules(char[] propname);

native bool SendProxy_HookPropChange(int entity, const char[] name, PropChangedCallback callback);
native bool SendProxy_HookPropChangeGameRules(const char[] name, GameRulesPropChangedCallback callback);
native void SendProxy_UnhookPropChange(int entity, const char[] name, PropChangedCallback callback);
native void SendProxy_UnhookPropChangeGameRules(const char[] name, GameRulesPropChangedCallback callback);

#if !defined REQUIRE_EXTENSIONS
public __ext_sendproxymanager_SetNTVOptional()
{
    MarkNativeAsOptional("SendProxy_Hook");
    MarkNativeAsOptional("SendProxy_HookGameRules");
    MarkNativeAsOptional("SendProxy_HookArrayProp");
    MarkNativeAsOptional("SendProxy_UnhookArrayProp");
    MarkNativeAsOptional("SendProxy_Unhook");
    MarkNativeAsOptional("SendProxy_UnhookGameRules");
    MarkNativeAsOptional("SendProxy_IsHooked");
    MarkNativeAsOptional("SendProxy_IsHookedGameRules");
    MarkNativeAsOptional("SendProxy_HookPropChange");
    MarkNativeAsOptional("SendProxy_HookPropChangeGameRules");
    MarkNativeAsOptional("SendProxy_UnhookPropChange");
    MarkNativeAsOptional("SendProxy_UnhookPropChangeGameRules");
}
#endif  

public Extension __ext_sendproxymanager = 
{
	name = "SendProxy Manager",
	file = "sendproxy.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#endif