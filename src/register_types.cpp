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

#include "core/variant.h"
#include "scheduler/task_scheduler.h"
#include "scripting/luau_script.h"
#include "utils/wrapped_no_binding.h"

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
	Luau::assertHandler() = [](const char *p_expr, const char *p_file, int p_line, const char *p_function) -> int {
		UtilityFunctions::print("LUAU ASSERT FAILED: ", p_expr, " in file ", p_file, " at line ", p_line);
		return 1;
	};
#endif // DEBUG_ENABLED

	ClassDB::register_class<LuauScript>();
	ClassDB::register_class<LuauLanguage>();

	script_language_luau = memnew(LuauLanguage);
	CRASH_COND_MSG(nb::Engine::get_singleton_nb()->register_script_language(script_language_luau) != OK, "Failed to register LuauLanguage");

	ClassDB::register_class<ResourceFormatLoaderLuauScript>();
	resource_loader_luau.instantiate();
	nb::ResourceLoader::get_singleton_nb()->add_resource_format_loader(resource_loader_luau);

	ClassDB::register_class<ResourceFormatSaverLuauScript>();
	resource_saver_luau.instantiate();
	nb::ResourceSaver::get_singleton_nb()->add_resource_format_saver(resource_saver_luau);

	ClassDB::register_class<SignalWaiter>();
}

void uninitialize_luau_script_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
		return;

	UtilityFunctions::print_verbose("luau script: uninitializing...");

	nb::Engine::get_singleton_nb()->unregister_script_language(script_language_luau);

	if (script_language_luau)
		memdelete(script_language_luau);

	nb::ResourceLoader::get_singleton_nb()->remove_resource_format_loader(resource_loader_luau);
	resource_loader_luau.unref();

	nb::ResourceSaver::get_singleton_nb()->remove_resource_format_saver(resource_saver_luau);
	resource_saver_luau.unref();
}

extern "C" {
GDExtensionBool GDE_EXPORT luau_script_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_luau_script_module);
	init_obj.register_terminator(uninitialize_luau_script_module);

	// HACK: 4.1.1-stable: Godot uses this value to determine whether an editor
	// restart is required when extensions are loaded/unloaded. Due to the odd
	// initialization order when this extension is first detected, continuing
	// the import process when this extension is first loaded will cause the
	// engine to crash. Therefore, although we do not perform any initialization
	// before SCENE, it is necessary to set the minimum init level to SERVERS
	// to prompt a restart before the engine crashes.
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SERVERS);

	return init_obj.init();
}
}
