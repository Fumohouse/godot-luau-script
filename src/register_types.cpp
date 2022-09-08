#include "register_types.h"

#include <godot/gdnative_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>

#include "luau_script.h"

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
    - [x] ResourceFormatLoaderLuauScript, ResourceFormatSaverLuauScript - saving/loading
    - [ ] Luau - runtime (manages actual states, etc.)

    Requirements:
    - [x] Binding of built in Godot APIs to Luau
    - [ ] Manual binding of GDExtension Object classes
        - Push extension initialization earlier (SERVERS)
        - Interface between extensions with a singleton and named method calls
        - Create an interface source file/header which can be pulled into a GDExtension and used for convenience
    - [ ] Separated VMs
        - [ ] VMs for loading script resources, running core scripts, running map scripts
        - [ ] Thread data containing flag enum of permissions (OS, File, etc.) + inheritance
            - https://github.com/Roblox/luau/pull/167
        - [ ] Resource for setting core script permissions
        - [ ] Bound API checks for permissions
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

    - For custom objects, any custom script instance data/methods are hidden behind get(), set(), call()
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
*/

LuauLanguage *script_language_luau = nullptr;
Ref<ResourceFormatLoaderLuauScript> resource_loader_luau;
Ref<ResourceFormatSaverLuauScript> resource_saver_luau;

void initialize_luau_script_module(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;

    UtilityFunctions::print_verbose("luau script: initializing...");

    ClassDB::register_class<LuauScript>();
    ClassDB::register_class<LuauLanguage>();

    script_language_luau = memnew(LuauLanguage);
    // TODO: Not enough is done to allow this without crashes
    // Engine::get_singleton()->register_script_language(script_language_luau);

    resource_loader_luau.instantiate();
    ResourceLoader::get_singleton()->add_resource_format_loader(resource_loader_luau);

    resource_saver_luau.instantiate();
    ResourceSaver::get_singleton()->add_resource_format_saver(resource_saver_luau);

#ifdef DEBUG_ENABLED
    ClassDB::register_class<LuauTest>();
#endif // DEBUG_ENABLED
}

void uninitialize_luau_script_module(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;

    UtilityFunctions::print_verbose("luau script: uninitializing...");

    // TODO: unregister script language (not currently possible)

    if (script_language_luau)
        memdelete(script_language_luau); // will this break? maybe

    ResourceLoader::get_singleton()->remove_resource_format_loader(resource_loader_luau);
    resource_loader_luau.unref();

    ResourceSaver::get_singleton()->remove_resource_format_saver(resource_saver_luau);
    resource_saver_luau.unref();
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
