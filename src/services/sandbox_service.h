#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/core/method_ptrcall.hpp> // TODO: unused. required to prevent compile error when specializing PtrToArg.
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "services/luau_interface.h"

class SandboxService : public Service {
public:
	enum ResourcePermissions {
		RESOURCE_READ_ONLY,
		RESOURCE_READ_WRITE
	};

private:
	struct ResourcePath {
		String path;
		ResourcePermissions permissions;
	};

	static SandboxService *singleton;

	const LuaGDClass &get_lua_class() const override;

	HashSet<String> core_scripts;
	Vector<String> ignore_paths;

	Vector<ResourcePath> resource_paths;

	HashMap<GDExtensionObjectPtr, BitField<ThreadPermissions>> protected_objects;

	void discover_core_scripts_internal(const String &p_path = "res://");

public:
	static SandboxService *get_singleton() { return singleton; }

	void lua_push(lua_State *L) override;

	bool is_core_script(const String &p_path) const;
	void discover_core_scripts();
	void core_script_ignore(const String &p_path);
	void core_script_add(const String &p_path);
	void core_script_remove(const String &p_path);
	Array core_script_list() const;

	void resource_add_path_rw(const String &p_path);
	void resource_add_path_ro(const String &p_path);
	void resource_remove_path(const String &p_path);
	bool resource_has_access(const String &p_path, ResourcePermissions p_permissions) const;

	void protected_object_add(GDExtensionObjectPtr p_obj, int p_permissions);
	void protected_object_remove(GDExtensionObjectPtr p_obj);
	const BitField<ThreadPermissions> *get_object_permissions(GDExtensionObjectPtr p_obj) const;

	SandboxService();
	~SandboxService();
};

STACK_OP_SVC_DEF(SandboxService)
