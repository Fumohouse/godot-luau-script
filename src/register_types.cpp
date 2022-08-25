#include "register_types.h"

#include <godot/gdnative_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/core/class_db.hpp>

#ifdef DEBUG_ENABLED
#include "luau_test.h"
#endif // DEBUG_ENABLED

using namespace godot;

/*
    Luau scripting design

    Classes:
    - [~] LuauScript - script resource
    - [~] LuauScriptInstance - script instance
    - [~] LuauLanguage - language definition, manages runtime
    - [~] ResourceFormatLoaderLuauScript, ResourceFormatSaverLuauScript - saving/loading
    - [ ] Luau - runtime (manages actual states, etc.)

    Requirements:
    - [ ] Binding of Godot APIs to Luau
        - [x] Binding of Variant builtins
        - [x] Binding of builtin Object classes
        - [ ] Binding / replacement of certain Godot global functions
        - [ ] Binding of global enums
        - [ ] Manual binding of GDExtension Object classes
            - Push extension initialization earlier (SERVERS)
            - Interface between extensions with a singleton and named method calls
            - Create an interface source file/header which can be pulled into a GDExtension and used for convenience
    - [ ] Creation of custom classes in Luau, extending native classes
    - [ ] Extending/referencing custom Luau classes
    - Godot feature support:
        - [ ] Signals
        - [ ] RPCs
        - [ ] Tool scripts
        - [ ] Exports
        - [ ] Properties (get/set)
        - [ ] Global classes
        - [ ] AutoLoad
        - [ ] Expose methods to other Godot APIs
        - [ ] Templates
    - [ ] is keyword alternative - using is_class_ptr and get_class_ptr_static or metatables (or both idk)

    Necessary but not now:
    - [ ] Debugger
    - [ ] Profiler
    - [ ] Typechecking & analysis
        - Reference https://github.com/Roblox/luau/pull/578

    Nice-to-have:
    - [ ] Callable support for function objects (needs CallableCustom support in GDExtension)
    - [ ] Documentation support
    - [ ] Autocomplete within the editor
*/

/*
    Design considerations

    - Sandboxing:
        - Unsafe Godot APIs (OS, File, etc.) MUST be restricted to certain privileged, trusted scripts (that is the whole point!!!)

        - Core scripts should ideally be run in an entirely separate VM to map scripts
            - Within the core VM, could further restrict script privileges using thread states and lua_Callbacks
            - Scripts which do not need unsafe APIs should not have access to them, even if they are run in a separate core VM

        - A resource could be used to store this info:
            - Button to auto-detect ingame scripts (i.e. ignore those defined in maps) and add them to a list
            - Add a dropdown to mark scripts as a particular privilege level (unsafe, safe) or add specific permission model (like Android)

    - Userdata:
        - Variants can be copied into userdata and can be checked for vailidity (if they represent an object)
            - For RefCounted objects, should increment the refcount and decrement it in the dtor
        - For objects, we will always push a Variant wrapping only the object, meaning that any custom script instance data/methods are hidden behind get(), set(), call()
            - We could (should?) cache userdata, per object, per VM
            - Scripted objects will have 2 separate states - defined as a part of the underlying object and in Luau
                - It is easiest (probably desirable) to store this state in C++ rather than in Lua
                    - Any tables would need to be converted to reference and stored that way
                    - However, the state itself would not need to be referenced in any way, which is pretty nice
                    - Also, we would not need to deal with somehow merging both a userdata and a table into "self"
            - With this model, referencing a method even cross-VM should be non-problematic (hopefully)
              since no cross-VM "marshalling"/somehow wrapping calls in get/set/call is needed
                - The metatable would need to be copied though

    - Custom classes:
        - Definition table and metatable will be slightly different
        - Create a class definition table that defines exports, properties, etc. as special userdata types

    - Threading:
        - Threading makes no sense for Luau. Use coroutines. (implications? idk)
*/

void initialize_luau_script_module(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;

    UtilityFunctions::print_verbose("luau script: initializing...");

#ifdef DEBUG_ENABLED
    ClassDB::register_class<LuauTest>();
#endif // DEBUG_ENABLED
}

void uninitialize_luau_script_module(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;

    UtilityFunctions::print_verbose("luau script: uninitializing...");
}

#define GD_LIB_EXPORT __attribute__((visibility("default")))

extern "C"
{
    GD_LIB_EXPORT GDNativeBool luau_script_init(const GDNativeInterface *p_interface, const GDNativeExtensionClassLibraryPtr p_library, GDNativeInitialization *r_initialization)
    {
        godot::GDExtensionBinding::InitObject init_obj(p_interface, p_library, r_initialization);

        init_obj.register_initializer(initialize_luau_script_module);
        init_obj.register_terminator(uninitialize_luau_script_module);

        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
}
