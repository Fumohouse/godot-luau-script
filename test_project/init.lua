print("init.lua ran!")

LuauInterface.SandboxService:CoreScriptIgnore("res://test_scripts/")
LuauInterface.SandboxService:DiscoverCoreScripts()

print("Core scripts: ", LuauInterface.SandboxService:CoreScriptList())
