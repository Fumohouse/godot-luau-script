#include "register_types.h"

#include <godot/gdnative_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>

#include "luau_script.h"

#ifdef TESTS_ENABLED
#include <vector>

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/char_string.hpp>

#include <catch_amalgamated.hpp>
#endif // TESTS_ENABLED

using namespace godot;

/*
    Luau scripting design

    Requirements:
    - [x] Binding of built in Godot APIs to Luau
    - [ ] Manual binding of GDExtension Object classes
        - Push extension initialization earlier (SERVERS)
        - Interface between extensions with a singleton and named method calls
        - Create an interface source file/header which can be pulled into a GDExtension and used for convenience
    - [ ] Separated VMs
        - [x] VMs for loading script resources, running core scripts, running map scripts
        - [ ] Ability to reset VMs (unloading scripts?)
        - [x] Thread data containing flag enum of permissions (OS, File, etc.) + inheritance
            - https://github.com/Roblox/luau/pull/167
        - [ ] Resource for setting core script permissions
        - [x] Bound API checks for permissions
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

    - Each ScriptInstance should have its own table stored in the registry
        - Must take into consideration the VM on which the ScriptInstance was made
        - External access to this is locked behind get(), set(), call()
        - Registered exports will query some Variant-compatible property in this table
        - The metatable assigned to the ScriptInstance Object should consider this table and the parent
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
    Engine::get_singleton()->register_script_language(script_language_luau);

    ClassDB::register_class<ResourceFormatLoaderLuauScript>();
    resource_loader_luau.instantiate();
    ResourceLoader::get_singleton()->add_resource_format_loader(resource_loader_luau);

    ClassDB::register_class<ResourceFormatSaverLuauScript>();
    resource_saver_luau.instantiate();
    ResourceSaver::get_singleton()->add_resource_format_saver(resource_saver_luau);

#ifdef TESTS_ENABLED
    if (!OS::get_singleton()->get_cmdline_args().has("--luau-tests"))
        return;

    UtilityFunctions::print("Catch2: Running tests...");

    Catch::Session session;

    // Fetch args
    PackedStringArray args = OS::get_singleton()->get_cmdline_user_args();
    int argc = args.size();

    // CharString does not work with godot::Vector
    std::vector<CharString> charstr_vec(argc);

    std::vector<const char *> argv_vec(argc + 1);
    argv_vec[0] = "luau-script"; // executable name

    for (int i = 0; i < argc; i++)
    {
        charstr_vec[i] = args[i].utf8();
        argv_vec[i + 1] = charstr_vec[i].get_data();
    }

    session.applyCommandLine(argc + 1, argv_vec.data());

    // Run
    session.run();
#endif // TESTS_ENABLED
}

void uninitialize_luau_script_module(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;

    UtilityFunctions::print_verbose("luau script: uninitializing...");

    // TODO: unregister script language (not currently possible)

    // 2022-09-21: it does break. (segfault when ScriptServer cleans up)
    // should be ok if/when script languages can be properly unregistered
    // if (script_language_luau)
        // memdelete(script_language_luau); // will this break? maybe

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
