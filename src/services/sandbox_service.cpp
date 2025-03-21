#include "sandbox_service.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/method_ptrcall.hpp> // TODO: unused. required to prevent compile error when specializing PtrToArg.
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/lua_utils.h"
#include "core/permissions.h"
#include "scripting/luau_script.h"
#include "scripting/resource_format_luau_script.h"
#include "services/class_binder.h"
#include "services/luau_interface.h"

#define SANDBOX_SERVICE_NAME "SandboxService"
#define SANDBOX_SERVICE_MT_NAME "Luau." SANDBOX_SERVICE_NAME
SVC_STACK_OP_IMPL(SandboxService, SANDBOX_SERVICE_MT_NAME, UDATA_TAG_SANDBOX_SERVICE)

SandboxService *SandboxService::singleton = nullptr;

const LuaGDClass &SandboxService::get_lua_class() const {
	static LuaGDClass lua_class(UDATA_TAG_SANDBOX_SERVICE);
	static bool did_init = false;

	if (!did_init) {
		lua_class.set_name(SANDBOX_SERVICE_NAME, SANDBOX_SERVICE_MT_NAME);

		lua_class.bind_method("IsCoreScript", FID(&SandboxService::is_core_script), PERMISSION_INTERNAL);
		lua_class.bind_method("DiscoverCoreScripts", FID(&SandboxService::discover_core_scripts), PERMISSION_INTERNAL);
		lua_class.bind_method("CoreScriptIgnore", FID(&SandboxService::core_script_ignore), PERMISSION_INTERNAL);
		lua_class.bind_method("CoreScriptAdd", FID(&SandboxService::core_script_add), PERMISSION_INTERNAL);
		lua_class.bind_method("CoreScriptRemove", FID(&SandboxService::core_script_remove), PERMISSION_INTERNAL);
		lua_class.bind_method("CoreScriptList", FID(&SandboxService::core_script_list), PERMISSION_INTERNAL);

		lua_class.bind_method("ResourceAddPathRW", FID(&SandboxService::resource_add_path_rw), PERMISSION_INTERNAL);
		lua_class.bind_method("ResourceAddPathRO", FID(&SandboxService::resource_add_path_ro), PERMISSION_INTERNAL);
		lua_class.bind_method("ResourceRemovePath", FID(&SandboxService::resource_remove_path), PERMISSION_INTERNAL);

		lua_class.bind_method("ProtectedObjectAdd", FID(&SandboxService::protected_object_add), PERMISSION_INTERNAL);
		lua_class.bind_method("ProtectedObjectRemove", FID(&SandboxService::protected_object_remove), PERMISSION_INTERNAL);

		did_init = true;
	}

	return lua_class;
}

void SandboxService::lua_push(lua_State *L) {
	LuaStackOp<SandboxService *>::push(L, this);
}

/* CORE SCRIPTS */

bool SandboxService::is_core_script(const String &p_path) const {
	return core_scripts.has(p_path);
}

void SandboxService::discover_core_scripts_internal(const String &p_path) {
	// Will be scanned on Windows
	if (p_path == "res://.godot") {
		return;
	}

	UtilityFunctions::print_verbose("Searching for core scripts in ", p_path, "...");

	Ref<DirAccess> dir = DirAccess::open(p_path);
	ERR_FAIL_COND_MSG(!dir.is_valid(), "Failed to open directory at " + p_path);

	Error err = dir->list_dir_begin();
	ERR_FAIL_COND_MSG(err != OK, "Failed to list directory at " + p_path);

	String file_name = dir->get_next();

	while (!file_name.is_empty()) {
		String file_name_full = p_path.path_join(file_name);

		if (dir->current_is_dir()) {
			discover_core_scripts_internal(file_name_full);
		} else {
			String res_type = ResourceFormatLoaderLuauScript::get_resource_type(file_name_full);

			if (res_type == LuauLanguage::get_singleton()->_get_type()) {
				bool ignore = false;

				for (const String &ignore_path : ignore_paths) {
					if (file_name_full.begins_with(ignore_path)) {
						ignore = true;
						break;
					}
				}

				if (ignore) {
					UtilityFunctions::print_verbose("Ignoring script: ", file_name_full);
				} else {
					UtilityFunctions::print_verbose("Discovered core script: ", file_name_full);
					core_scripts.insert(file_name_full);
				}
			}
		}

		file_name = dir->get_next();
	}
}

void SandboxService::discover_core_scripts() {
	UtilityFunctions::print_verbose("======== Discovering core scripts ========");
	discover_core_scripts_internal();
	UtilityFunctions::print_verbose("Done! ", core_scripts.size(), " core scripts discovered.");
	UtilityFunctions::print_verbose("==========================================");
}

void SandboxService::core_script_ignore(const String &p_path) {
	ignore_paths.push_back(p_path.simplify_path());
}

void SandboxService::core_script_add(const String &p_path) {
	core_scripts.insert(p_path);
}

void SandboxService::core_script_remove(const String &p_path) {
	core_scripts.erase(p_path);
}

Array SandboxService::core_script_list() const {
	Array a;

	for (const String &E : core_scripts) {
		a.push_back(E);
	}

	return a;
}

/* RESOURCES */

void SandboxService::resource_add_path_rw(const String &p_path) {
	resource_paths.push_back({ p_path.simplify_path(), RESOURCE_READ_WRITE });
}

void SandboxService::resource_add_path_ro(const String &p_path) {
	resource_paths.push_back({ p_path.simplify_path(), RESOURCE_READ_ONLY });
}

void SandboxService::resource_remove_path(const String &p_path) {
	String path = p_path.simplify_path();

	for (int i = resource_paths.size() - 1; i >= 0; i--) {
		const ResourcePath &res_path = resource_paths[i];

		if (res_path.path == path) {
			resource_paths.remove_at(i);
		}
	}
}

bool SandboxService::resource_has_access(const String &p_path, ResourcePermissions p_permissions) const {
	String path = p_path.simplify_path();

	for (const ResourcePath &E : resource_paths) {
		if (p_permissions <= E.permissions && path.begins_with(E.path))
			return true;
	}

	return false;
}

void SandboxService::protected_object_add(GDExtensionObjectPtr p_obj, int p_permissions) {
	protected_objects.insert(p_obj, static_cast<ThreadPermissions>(p_permissions));
}

void SandboxService::protected_object_remove(GDExtensionObjectPtr p_obj) {
	protected_objects.erase(p_obj);
}

const BitField<ThreadPermissions> *SandboxService::get_object_permissions(GDExtensionObjectPtr p_obj) const {
	HashMap<GDExtensionObjectPtr, BitField<ThreadPermissions>>::ConstIterator E = protected_objects.find(p_obj);
	return E ? &E->value : nullptr;
}

SandboxService::SandboxService() {
	if (!singleton) {
		singleton = this;
	}
}

SandboxService::~SandboxService() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
