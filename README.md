sendproxy
===========================

Fork of Afronanny's SendProxy Manager [extension](https://forums.alliedmods.net/showthread.php?t=169795).

### 1.2 Changes:
* Removed the broken auto-updater.
* Added AMBuild scripts.
* Includes TF2 build.
* Added GameRules hook.

### 1.3 Changes:
* Fixed some errors
* Now sendproxy hooks calls for each client individually
* Removed std::string usage to prevent libstdc++ linking in linux build
* Added interface for extensions and some missed natives
