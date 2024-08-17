#include "scripting/luau_script.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/editor_settings.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "core/lua_utils.h"
#include "core/runtime.h"
#include "scripting/luau_cache.h"
#include "scripting/luau_lib.h"
#include "scripting/resource_format_luau_script.h"
#include "utils/wrapped_no_binding.h"

using namespace godot;

// Much of the editor-specific implementations are based heavily on the GDScript implementation
// (especially placeholder and reloading functionality).
// See COPYRIGHT.txt for license information.

bool LuauScript::_is_placeholder_fallback_enabled() const {
#ifdef TOOLS_ENABLED
	return placeholder_fallback_enabled;
#else
	return false;
#endif // TOOLS_ENABLED
}

void *LuauScript::_placeholder_instance_create(Object *p_for_object) const {
#ifdef TOOLS_ENABLED
	PlaceHolderScriptInstance *internal = memnew(PlaceHolderScriptInstance(Ref<LuauScript>(this), p_for_object));
	return internal::gdextension_interface_script_instance_create3(&PlaceHolderScriptInstance::INSTANCE_INFO, internal);
#else
	return nullptr;
#endif // TOOLS_ENABLED
}

void LuauScript::_update_exports() {
#ifdef TOOLS_ENABLED
	if (_is_module)
		return;

	update_exports_internal(nullptr);

	// Update old dependent scripts.
	List<Ref<LuauScript>> scripts = LuauLanguage::get_singleton()->get_scripts();

	for (Ref<LuauScript> &scr : scripts) {
		// Check dependent to avoid endless loop.
		if (scr->has_dependency(this) && !this->has_dependency(scr))
			scr->_update_exports();
	}
#endif // TOOLS_ENABLED
}

#ifdef TOOLS_ENABLED
void LuauScript::update_exports_values(List<GDProperty> &r_properties, HashMap<StringName, Variant> &r_values) {
	if (base.is_valid() && base->_is_valid()) {
		base->update_exports_values(r_properties, r_values);
	}

	for (const GDClassProperty &prop : definition.properties) {
		r_properties.push_back(prop.property);
		r_values[prop.property.name] = prop.default_value;
	}
}

bool LuauScript::update_exports_internal(PlaceHolderScriptInstance *p_instance_to_update) {
	// Step 1: Reload base class, properties, and signals from source
	bool changed = false;

	if (source_changed_cache) {
		source_changed_cache = false;
		changed = true;

		dependencies.clear();
		unload_module();

		Error err = compile();
		if (err != OK) {
			placeholder_fallback_enabled = true;
			return false;
		}

		err = analyze();
		if (err != OK) {
			placeholder_fallback_enabled = true;
			return false;
		}

		err = load_table(LuauRuntime::VM_SCRIPT_LOAD, true);
		if (err == OK) {
			// Update base class
			base = Ref<LuauScript>(definition.base_script);
		} else {
			placeholder_fallback_enabled = true;
			return false;
		}
	} else if (placeholder_fallback_enabled) {
		return false;
	}

	placeholder_fallback_enabled = false;

	// Step 2: Update base scripts
	// This always happens (i.e. not only when source changed) to be certain that any cyclic inheritance is caught.
	if (base.is_valid() && base->update_exports_internal(nullptr)) {
		changed = true;
	}

	// Step 3: Update placeholder instances
	if ((changed || p_instance_to_update) && placeholders.size() > 0) {
		List<GDProperty> properties;
		HashMap<StringName, Variant> values;

		update_exports_values(properties, values);

		if (changed) {
			for (const KeyValue<uint64_t, PlaceHolderScriptInstance *> &pair : placeholders) {
				pair.value->update(properties, values);
			}
		} else {
			p_instance_to_update->update(properties, values);
		}
	}

	return changed;
}
#endif // TOOLS_ENABLED

void LuauScript::_placeholder_erased(void *p_placeholder) {
#ifdef TOOLS_ENABLED
	placeholders.erase(((PlaceHolderScriptInstance *)p_placeholder)->get_owner()->get_instance_id());
#endif // TOOLS_ENABLED
}

#ifdef TOOLS_ENABLED
bool LuauScript::placeholder_has(Object *p_object) const {
	return placeholders.has(p_object->get_instance_id());
}

PlaceHolderScriptInstance *LuauScript::placeholder_get(Object *p_object) {
	return placeholders.get(p_object->get_instance_id());
}
#endif // TOOLS_ENABLED

TypedArray<Dictionary> LuauLanguage::_get_built_in_templates(const StringName &p_object) const {
#ifdef TOOLS_ENABLED
	TypedArray<Dictionary> templates;

	if (p_object == StringName("Object")) {
		Dictionary t;
		t["inherit"] = "Object";
		t["name"] = "Default";
		t["description"] = "Default template for Objects";
		t["content"] = R"TEMPLATE(--- @class
--- @extends _BASE_CLASS_
local _CLASS_NAME_ = {}
local _CLASS_NAME_C = gdclass(_CLASS_NAME_)

export type _CLASS_NAME_ = _BASE_CLASS_ & typeof(_CLASS_NAME_) & {
_I_-- Put properties, signals, and non-registered table fields here
}

return _CLASS_NAME_C
)TEMPLATE";

		t["id"] = 0;
		t["origin"] = 0; // TEMPLATE_BUILT_IN

		templates.push_back(t);
	}

	if (p_object == StringName("Node")) {
		Dictionary t;
		t["inherit"] = "Node";
		t["name"] = "Default";
		t["description"] = "Default template for Nodes with _Ready and _Process callbacks";
		t["content"] = R"TEMPLATE(--- @class
--- @extends _BASE_CLASS_
local _CLASS_NAME_ = {}
local _CLASS_NAME_C = gdclass(_CLASS_NAME_)

export type _CLASS_NAME_ = _BASE_CLASS_ & typeof(_CLASS_NAME_) & {
_I_-- Put properties, signals, and non-registered table fields here
}

--- @registerMethod
function _CLASS_NAME_._Ready(self: _CLASS_NAME_)
_I_-- Called when the node enters the scene tree
end

--- @registerMethod
function _CLASS_NAME_._Process(self: _CLASS_NAME_, delta: number)
_I_-- Called every frame
end

return _CLASS_NAME_C
)TEMPLATE";

		t["id"] = 0;
		t["origin"] = 0; // TEMPLATE_BUILT_IN

		templates.push_back(t);
	}

	return templates;
#else
	return TypedArray<Dictionary>();
#endif // TOOLS_ENABLED
}

Ref<Script> LuauLanguage::_make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
#ifdef TOOLS_ENABLED
	Ref<LuauScript> script;
	script.instantiate();

	// https://github.com/godotengine/godot/blob/13a0d6e9b253654f5cc2a44f3d0b3cae10440443/modules/gdscript/gdscript_editor.cpp#L3249-L3261
	Ref<EditorSettings> settings = nb::EditorInterface::get_singleton_nb()->get_editor_settings();
	bool indent_spaces = settings->get_setting("text_editor/behavior/indent/type");
	int indent_size = settings->get_setting("text_editor/behavior/indent/size");
	String indent = indent_spaces ? String(" ").repeat(indent_size) : "\t";

	String contents = p_template
							  .replace("_CLASS_NAME_", p_class_name)
							  .replace("_BASE_CLASS_", p_base_class_name)
							  .replace("_I_", indent);

	script->_set_source_code(contents);

	return script;
#else
	return Ref<Script>();
#endif // TOOLS_ENABLED
}

#ifdef TOOLS_ENABLED
// Sort such that base scripts and modules come first.
struct LuauScriptDepSort {
	bool operator()(const Ref<LuauScript> &p_a, const Ref<LuauScript> &p_b) const {
		if (p_a == p_b) {
			return false;
		}

		const LuauScript *s = p_b.ptr();

		while (s) {
			if (s->has_dependency(p_a))
				return true;

			s = s->get_base().ptr();
		}

		return false;
	}
};
#endif // TOOLS_ENABLED

#ifdef TOOLS_ENABLED
void LuauScript::unload_module() {
	luau_data.bytecode.clear(); // Forces recompile on next load.

	CharString path_utf8 = get_path().utf8();

	for (int i = 0; i < LuauRuntime::VM_MAX; i++) {
		lua_State *L = LuauRuntime::get_singleton()->get_vm(LuauRuntime::VMType(i));
		LUAU_LOCK(L);

		luaL_findtable(L, LUA_REGISTRYINDEX, LUASCRIPT_MODULE_TABLE, 1);
		lua_pushnil(L);
		lua_setfield(L, -2, path_utf8.get_data());

		lua_pop(L, 1); // MODULE_TABLE
	}
}
#endif // TOOLS_ENABLED

#ifdef TOOLS_ENABLED
List<Ref<LuauScript>> LuauLanguage::get_scripts() const {
	List<Ref<LuauScript>> scripts;

	{
		MutexLock lock(*this->lock.ptr());

		const SelfList<LuauScript> *elem = script_list.first();

		while (elem) {
			String path = elem->self()->get_path();

			if (ResourceFormatLoaderLuauScript::get_resource_type(path) == _get_type()) {
				// Ref prevents accidental deallocation of scripts (?)
				scripts.push_back(Ref<LuauScript>(elem->self()));
			}

			elem = elem->next();
		}
	}

	scripts.sort_custom<LuauScriptDepSort>();

	return scripts;
}
#endif // TOOLS_ENABLED

void LuauLanguage::_reload_all_scripts() {
#ifdef TOOLS_ENABLED
	List<Ref<LuauScript>> scripts = get_scripts();

	for (Ref<LuauScript> &script : scripts) {
		script->unload_module();
	}

	for (Ref<LuauScript> &script : scripts) {
		script->load_source_code(script->get_path());
		script->_reload(true);
	}
#endif // TOOLS_ENABLED
}

void LuauLanguage::_reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) {
#ifdef TOOLS_ENABLED
	// Step 1: List all scripts
	List<Ref<LuauScript>> scripts = get_scripts();

	// Step 2: Save state (if soft) and remove scripts from instances
	HashMap<Ref<LuauScript>, ScriptInstanceState> to_reload;

	for (Ref<LuauScript> &script : scripts) {
		bool dependency_changed = false;

		for (const KeyValue<Ref<LuauScript>, ScriptInstanceState> &pair : to_reload) {
			if (script->has_dependency(pair.key)) {
				dependency_changed = true;
				break;
			}
		}

		if (script != p_script && !dependency_changed && !to_reload.has(script->_get_base_script()))
			continue;

		to_reload.insert(script, ScriptInstanceState());
		script->unload_module();

		if (!p_soft_reload && !script->is_module()) {
			ScriptInstanceState &map = to_reload[script];

			HashMap<uint64_t, LuauScriptInstance *> instances = script->instances;

			for (const KeyValue<uint64_t, LuauScriptInstance *> &pair : instances) {
				Object *obj = pair.value->get_owner();

				List<Pair<StringName, Variant>> state;
				pair.value->get_property_state(state);
				map[obj->get_instance_id()] = state;

				obj->set_script(Variant());
			}

			HashMap<uint64_t, PlaceHolderScriptInstance *> placeholder_instances = script->placeholders;

			for (const KeyValue<uint64_t, PlaceHolderScriptInstance *> &pair : placeholder_instances) {
				Object *obj = pair.value->get_owner();

				List<Pair<StringName, Variant>> state;
				pair.value->get_property_state(state);
				map[obj->get_instance_id()] = state;

				obj->set_script(Variant());
			}

			// Restore state from failed reload instead
			for (const KeyValue<uint64_t, List<Pair<StringName, Variant>>> &pair : script->pending_reload_state) {
				map[pair.key] = pair.value;
			}
		}
	}

	// Step 3: Reload scripts and load saved state if any
	for (const KeyValue<Ref<LuauScript>, ScriptInstanceState> &E : to_reload) {
		Ref<LuauScript> scr = E.key;
		UtilityFunctions::print_verbose("Reloading script: ", scr->get_path());

		scr->_reload(p_soft_reload);

		for (const KeyValue<uint64_t, List<Pair<StringName, Variant>>> &F : E.value) {
			const List<Pair<StringName, Variant>> &saved_state = F.value;

			Object *obj = ObjectDB::get_instance(F.key);
			if (!obj)
				return;

			if (!p_soft_reload) {
				// Object::set_script returns early if the scripts are the same,
				// which they may be if the reload previously failed (pending).
				obj->set_script(Variant());
			}

			obj->set_script(scr);

			HashMap<uint64_t, LuauScriptInstance *>::ConstIterator G = scr->instances.find(F.key);

			if (G) {
				for (const Pair<StringName, Variant> &I : saved_state) {
					G->value->set(I.first, I.second);
				}
			}

			HashMap<uint64_t, PlaceHolderScriptInstance *>::ConstIterator H = scr->placeholders.find(F.key);

			if (H) {
				if (scr->_is_placeholder_fallback_enabled()) {
					for (const Pair<StringName, Variant> &I : saved_state) {
						H->value->property_set_fallback(I.first, I.second);
					}
				} else {
					for (const Pair<StringName, Variant> &I : saved_state) {
						H->value->set(I.first, I.second);
					}
				}
			}

			if (!G && !H) {
				// Script load failed; save state to reload later
				if (!scr->pending_reload_state.has(F.key)) {
					scr->pending_reload_state[F.key] = saved_state;
				}
			} else {
				// State is loaded; clear it now
				scr->pending_reload_state.erase(F.key);
			}
		}
	}
#endif // TOOLS_ENABLED
}

bool LuauLanguage::_handles_global_class_type(const String &p_type) const {
#ifdef TOOLS_ENABLED
	return p_type == _get_type();
#else
	return false;
#endif // TOOLS_ENABLED
}

Dictionary LuauLanguage::_get_global_class_name(const String &p_path) const {
#ifdef TOOLS_ENABLED
	Error err = OK;
	Ref<LuauScript> script = LuauCache::get_singleton()->get_script(p_path, err);
	if (err != OK)
		return Dictionary();

	const GDClassDefinition &def = script->get_definition();

	Dictionary ret;

	ret["name"] = def.name;

	if (script->get_base().is_valid()) {
		// C# implementation used as reference
		bool global_base_found = false;
		const LuauScript *s = script->get_base().ptr();

		while (s) {
			const String &name = s->get_definition().name;
			if (!name.is_empty()) {
				ret["base_type"] = name;
				global_base_found = true;
				break;
			}

			s = s->get_base().ptr();
		}

		if (!global_base_found) {
			ret["base_type"] = script->_get_instance_base_type();
		}
	} else {
		ret["base_type"] = def.extends;
	}

	ret["icon_path"] = def.icon_path;

	return ret;
#else
	return Dictionary();
#endif // TOOLS_ENABLED
}

//////////////////////////
// PLACEHOLDER INSTANCE //
//////////////////////////

// Implementation mostly mirrors Godot's implementation.
// See license information in COPYRIGHT.txt.

#ifdef TOOLS_ENABLED
#define PLACEHOLDER_SELF ((PlaceHolderScriptInstance *)p_self)

static GDExtensionScriptInstanceInfo3 init_placeholder_instance_info() {
	// Methods which essentially have no utility (e.g. call) are implemented here instead of in the class.

	GDExtensionScriptInstanceInfo3 info;
	ScriptInstance::init_script_instance_info_common(info);

	info.property_can_revert_func = [](void *, GDExtensionConstStringNamePtr) -> GDExtensionBool {
		return false;
	};

	info.property_get_revert_func = [](void *, GDExtensionConstStringNamePtr, GDExtensionVariantPtr) -> GDExtensionBool {
		return false;
	};

	info.call_func = [](void *p_self, GDExtensionConstStringNamePtr p_method, const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count, GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
		r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		*(Variant *)r_return = Variant();
	};

	info.is_placeholder_func = [](void *) -> GDExtensionBool {
		return true;
	};

	info.set_fallback_func = [](void *p_self, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) -> GDExtensionBool {
		return PLACEHOLDER_SELF->property_set_fallback(*(const StringName *)p_name, *(const Variant *)p_value);
	};

	info.get_fallback_func = [](void *p_self, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
		return PLACEHOLDER_SELF->property_get_fallback(*(const StringName *)p_name, *(Variant *)r_ret);
	};

	info.free_func = [](void *p_self) {
		memdelete(PLACEHOLDER_SELF);
	};

	return info;
}

const GDExtensionScriptInstanceInfo3 PlaceHolderScriptInstance::INSTANCE_INFO = init_placeholder_instance_info();

bool PlaceHolderScriptInstance::set(const StringName &p_name, const Variant &p_value, PropertySetGetError *r_err) {
	if (script->_is_placeholder_fallback_enabled()) {
		if (r_err)
			*r_err = PROP_NOT_FOUND;

		return false;
	}

	if (values.has(p_name)) {
		if (script->_has_property_default_value(p_name)) {
			Variant defval = script->_get_property_default_value(p_name);

			Variant op_result;
			bool op_valid = false;
			Variant::evaluate(Variant::OP_EQUAL, defval, p_value, op_result, op_valid);

			if (op_valid && op_result.operator bool()) {
				values.erase(p_name);
				return true;
			}
		}

		values[p_name] = p_value;
		return true;
	} else {
		if (script->_has_property_default_value(p_name)) {
			Variant defval = script->get_property_default_value(p_name);

			Variant op_result;
			bool op_valid = false;
			Variant::evaluate(Variant::OP_NOT_EQUAL, defval, p_value, op_result, op_valid);

			if (op_valid && op_result.operator bool())
				values[p_name] = p_value;

			return true;
		}
	}

	if (r_err)
		*r_err = PROP_NOT_FOUND;

	return false;
}

bool PlaceHolderScriptInstance::get(const StringName &p_name, Variant &r_ret, PropertySetGetError *r_err) {
	if (values.has(p_name)) {
		r_ret = values[p_name];
		return true;
	}

	if (constants.has(p_name)) {
		r_ret = constants[p_name];
		return true;
	}

	if (!script->_is_placeholder_fallback_enabled() && script->_has_property_default_value(p_name)) {
		r_ret = script->_get_property_default_value(p_name);
		return true;
	}

	if (r_err)
		*r_err = PROP_NOT_FOUND;

	return false;
}

GDExtensionPropertyInfo *PlaceHolderScriptInstance::get_property_list(uint32_t *r_count) {
	LocalVector<GDExtensionPropertyInfo> props;

	int size = properties.size();
	props.resize(size);

	if (script->_is_placeholder_fallback_enabled()) {
		for (int i = 0; i < size; i++) {
			GDExtensionPropertyInfo dst;
			copy_prop(properties[i], dst);

			props[i] = dst;
		}
	} else {
		for (int i = 0; i < size; i++) {
			GDExtensionPropertyInfo &pinfo = props[i];
			copy_prop(properties[i], pinfo);

			if (!values.has(properties[i].name))
				pinfo.usage |= PROPERTY_USAGE_SCRIPT_DEFAULT_VALUE;
		}
	}

	*r_count = size;

	GDExtensionPropertyInfo *list = (GDExtensionPropertyInfo *)memalloc(sizeof(GDExtensionPropertyInfo) * size);
	memcpy(list, props.ptr(), sizeof(GDExtensionPropertyInfo) * size);

	return list;
}

Variant::Type PlaceHolderScriptInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
	if (values.has(p_name)) {
		if (r_is_valid)
			*r_is_valid = true;

		return values[p_name].get_type();
	}

	if (constants.has(p_name)) {
		if (r_is_valid)
			*r_is_valid = true;

		return constants[p_name].get_type();
	}

	if (r_is_valid)
		*r_is_valid = false;

	return Variant::NIL;
}

GDExtensionMethodInfo *PlaceHolderScriptInstance::get_method_list(uint32_t *r_count) const {
	if (!script->_is_placeholder_fallback_enabled() && script.is_valid())
		return ScriptInstance::get_method_list(r_count);

	*r_count = 0;
	return nullptr;
}

bool PlaceHolderScriptInstance::has_method(const StringName &p_name) const {
	if (script->_is_placeholder_fallback_enabled())
		return false;

	if (script.is_valid())
		return script->_has_method(p_name);

	return false;
}

bool PlaceHolderScriptInstance::property_set_fallback(const StringName &p_name, const Variant &p_value) {
	if (script->_is_placeholder_fallback_enabled()) {
		HashMap<StringName, Variant>::Iterator E = values.find(p_name);

		if (E) {
			E->value = p_value;
		} else {
			values.insert(p_name, p_value);
		}

		bool found = false;
		for (const GDProperty &F : properties) {
			if (F.name == p_name) {
				found = true;
				break;
			}
		}

		if (!found) {
			GDProperty pinfo;

			pinfo.type = (GDExtensionVariantType)p_value.get_type();
			pinfo.name = p_name;
			pinfo.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_SCRIPT_VARIABLE;

			properties.push_back(pinfo);
		}
	}

	return false;
}

bool PlaceHolderScriptInstance::property_get_fallback(const StringName &p_name, Variant &r_ret) {
	if (script->_is_placeholder_fallback_enabled()) {
		HashMap<StringName, Variant>::ConstIterator E = values.find(p_name);

		if (E) {
			r_ret = E->value;
			return true;
		}

		E = constants.find(p_name);

		if (E) {
			r_ret = E->value;
			return true;
		}
	}

	r_ret = Variant();
	return false;
}

void PlaceHolderScriptInstance::update(const List<GDProperty> &p_properties, const HashMap<StringName, Variant> &p_values) {
	HashSet<StringName> new_values;

	for (const GDProperty &E : p_properties) {
		StringName n = E.name;
		new_values.insert(n);

		if (!values.has(n) || values[n].get_type() != (Variant::Type)E.type) {
			if (p_values.has(n))
				values[n] = p_values[n];
		}
	}

	properties = p_properties;

	List<StringName> to_remove;

	for (KeyValue<StringName, Variant> &E : values) {
		if (!new_values.has(E.key))
			to_remove.push_back(E.key);

		if (script->_has_property_default_value(E.key)) {
			Variant defval = script->_get_property_default_value(E.key);

			if (defval == E.value)
				to_remove.push_back(E.key);
		}
	}

	while (to_remove.size()) {
		values.erase(to_remove.front()->get());
		to_remove.pop_front();
	}

	if (owner && script.is_valid() && script->placeholder_has(owner) && script->placeholder_get(owner) == this) {
		owner->notify_property_list_changed();
	}

	constants.clear();

	Dictionary new_constants = script->_get_constants();
	Array keys = new_constants.keys();

	for (int i = 0; i < keys.size(); i++) {
		const Variant &key = keys[i];
		constants[key] = new_constants[key];
	}
}

PlaceHolderScriptInstance::PlaceHolderScriptInstance(const Ref<LuauScript> &p_script, Object *p_owner) :
		script(p_script),
		owner(p_owner) {
	// Placeholder instance creation takes place in a const method.
	script->placeholders.insert(p_owner->get_instance_id(), this);
	script->update_exports_internal(this);
}

PlaceHolderScriptInstance::~PlaceHolderScriptInstance() {
	if (script.is_valid()) {
		script->_placeholder_erased(this);
	}
}
#endif // TOOLS_ENABLED
