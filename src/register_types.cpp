#include "register_types.h"

#include <Luau/Common.h>
#include <gdextension_interface.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "luagd_variant.h"
#include "luau_script.h"
#include "task_scheduler.h"

#ifdef TESTS_ENABLED
#include <vector>

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <catch_amalgamated.hpp>
#endif // TESTS_ENABLED

using namespace godot;

LuauLanguage *script_language_luau = nullptr;
Ref<ResourceFormatLoaderLuauScript> resource_loader_luau;
Ref<ResourceFormatSaverLuauScript> resource_saver_luau;

void initialize_luau_script_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;

    UtilityFunctions::print_verbose("luau script: initializing...");

    LuauVariant::_register_types();

#ifdef DEBUG_ENABLED
    Luau::assertHandler() = [](const char *expr, const char *file, int line, const char *function) -> int {
        UtilityFunctions::print("LUAU ASSERT FAILED: ", expr, " in file ", file, " at line ", line);
        return 1;
    };
#endif // DEBUG_ENABLED

    ClassDB::register_class<LuauScript>();
    ClassDB::register_class<LuauLanguage>();

    script_language_luau = memnew(LuauLanguage);
    CRASH_COND_MSG(Engine::get_singleton()->register_script_language(script_language_luau) != OK, "failed to register LuauLanguage");

    ClassDB::register_class<ResourceFormatLoaderLuauScript>();
    resource_loader_luau.instantiate();
    ResourceLoader::get_singleton()->add_resource_format_loader(resource_loader_luau);

    ClassDB::register_class<ResourceFormatSaverLuauScript>();
    resource_saver_luau.instantiate();
    ResourceSaver::get_singleton()->add_resource_format_saver(resource_saver_luau);

    ClassDB::register_class<SignalWaiter>();

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

    for (int i = 0; i < argc; i++) {
        charstr_vec[i] = args[i].utf8();
        argv_vec[i + 1] = charstr_vec[i].get_data();
    }

    session.applyCommandLine(argc + 1, argv_vec.data());

    // Run
    session.run();
#endif // TESTS_ENABLED
}

void uninitialize_luau_script_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;

    UtilityFunctions::print_verbose("luau script: uninitializing...");

    Engine::get_singleton()->unregister_script_language(script_language_luau);

    if (script_language_luau)
        memdelete(script_language_luau);

    ResourceLoader::get_singleton()->remove_resource_format_loader(resource_loader_luau);
    resource_loader_luau.unref();

    ResourceSaver::get_singleton()->remove_resource_format_saver(resource_saver_luau);
    resource_saver_luau.unref();
}

extern "C" {
GDExtensionBool GDE_EXPORT luau_script_init(const GDExtensionInterface *p_interface, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_interface, p_library, r_initialization);

    init_obj.register_initializer(initialize_luau_script_module);
    init_obj.register_terminator(uninitialize_luau_script_module);

    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
