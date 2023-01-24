#include "luau_lib.h"
#include "luau_script.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "gd_luau.h"
#include "luau_cache.h"

namespace godot {
class Object;
};

// Much of the editor-specific implementations are based heavily on the GDScript implementation
// (especially placeholder and reloading functionality).
// See README.md for license information.

void *LuauScript::_placeholder_instance_create(Object *p_for_object) const {
    PlaceHolderScriptInstance *internal = memnew(PlaceHolderScriptInstance(Ref<LuauScript>(this), p_for_object));
    return internal::gde_interface->script_instance_create(&PlaceHolderScriptInstance::INSTANCE_INFO, internal);
}

void LuauScript::_update_exports() {
    if (_is_module)
        return;

    bool cyclic_error = false;
    update_exports_internal(&cyclic_error, nullptr);

    if (cyclic_error)
        return;

    // Update inheriting scripts
    HashSet<String> copy = dependents; // Gets modified by _update_exports

    for (const String &E : copy) {
        Error err;
        Ref<LuauScript> scr = LuauCache::get_singleton()->get_script(E, err);

        // Most likely means cyclic inheritance.
        // Skip silently as this method is called several times at once, and an error (or several) for cyclic dependency
        // should already have been printed.
        if (scr->has_dependent(get_path()))
            continue;

        scr->_update_exports();
    }
}

void LuauScript::update_exports_values(List<GDProperty> &properties, HashMap<StringName, Variant> &values) {
    if (base.is_valid() && base->_is_valid()) {
        base->update_exports_values(properties, values);
    }

    for (const GDClassProperty &prop : definition.properties) {
        properties.push_back(prop.property);
        values[prop.property.name] = prop.default_value;
    }
}

bool LuauScript::update_exports_internal(bool *r_err, PlaceHolderScriptInstance *p_instance_to_update) {
    // Step 1: Reload base class, properties, and signals from source
    bool changed = false;

    if (source_changed_cache) {
        source_changed_cache = false;
        changed = true;

        GDClassDefinition def;
        bool is_valid;
        compile(); // Always recompile.
        Error err = get_class_definition(this, nullptr, def, is_valid);

        if (err == OK) {
            // Update base class
            definition.extends = def.extends;

            // Update properties, signals
            definition.signals = def.signals;
            definition.properties = def.properties;
        } else {
            placeholder_fallback_enabled = true;
            return false;
        }
    } else if (placeholder_fallback_enabled) {
        return false;
    }

    placeholder_fallback_enabled = false;

    // Step 2: Update base and base scripts
    // This always happens (i.e. not only when source changed) to be certain that any cyclic inheritance is caught.
    Error err;
    update_base_script(err, true);

    if (err == ERR_CYCLIC_LINK) {
        if (r_err != nullptr)
            *r_err = true;

        return false;
    }

    if (base.is_valid() && base->update_exports_internal(r_err, nullptr)) {
        if (r_err != nullptr && *r_err) {
            return false;
        }

        changed = true;
    }

    // Step 3: Update placeholder instances
    if ((changed || p_instance_to_update != nullptr) && placeholders.size() > 0) {
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

void LuauScript::_placeholder_erased(void *p_placeholder) {
    placeholders.erase(((PlaceHolderScriptInstance *)p_placeholder)->get_owner()->get_instance_id());
}

bool LuauScript::placeholder_has(Object *p_object) const {
    return placeholders.has(p_object->get_instance_id());
}

PlaceHolderScriptInstance *LuauScript::placeholder_get(Object *p_object) {
    return placeholders.get(p_object->get_instance_id());
}

Ref<Script> LuauLanguage::_make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
    Ref<LuauScript> script;
    script.instantiate();

    // TODO: actual template stuff

    return script;
}

// Sort such that base scripts and modules come first.
struct LuauScriptDepSort {
    bool operator()(const Ref<LuauScript> &a, const Ref<LuauScript> &b) const {
        if (a == b) {
            return false;
        }

        const LuauScript *s = b.ptr();

        while (s != nullptr) {
            if (a->has_dependent(s->get_path()))
                return true;

            s = s->get_base().ptr();
        }

        return false;
    }
};

void LuauScript::unload_module() {
    luau_data.bytecode.clear(); // Forces recompile on next load.

    for (int i = 0; i < GDLuau::VM_MAX; i++) {
        lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VMType(i));

        luaL_findtable(L, LUA_REGISTRYINDEX, GDLuau::MODULE_TABLE, 1);
        lua_pushnil(L);
        lua_setfield(L, -2, get_path().utf8().get_data());

        lua_pop(L, 1); // MODULE_TABLE
    }
}

List<Ref<LuauScript>> LuauLanguage::get_scripts() const {
    List<Ref<LuauScript>> scripts;

    {
        MutexLock lock(this->lock);

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

void LuauLanguage::_reload_all_scripts() {
    List<Ref<LuauScript>> scripts = get_scripts();

    for (Ref<LuauScript> &script : scripts) {
        script->unload_module();
    }

    for (Ref<LuauScript> &script : scripts) {
        script->load_source_code(script->get_path());
        script->_reload(true);
    }
}

void LuauLanguage::_reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) {
    // Step 1: List all scripts
    List<Ref<LuauScript>> scripts = get_scripts();

    // Step 2: Save state (if soft) and remove scripts from instances
    HashMap<Ref<LuauScript>, ScriptInstanceState> to_reload;

    for (Ref<LuauScript> &script : scripts) {
        bool dependency_changed = false;

        for (const KeyValue<Ref<LuauScript>, ScriptInstanceState> &pair : to_reload) {
            if (pair.key->has_dependent(script->get_path())) {
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
            if (obj == nullptr)
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
}

bool LuauLanguage::_handles_global_class_type(const String &p_type) const {
    return p_type == _get_type();
}

Dictionary LuauLanguage::_get_global_class_name(const String &p_path) const {
    Error err;
    Ref<LuauScript> script = LuauCache::get_singleton()->get_script(p_path, err);
    if (err != OK)
        return Dictionary();

    const GDClassDefinition &def = script->get_definition();

    Dictionary ret;

    ret["name"] = def.name;

    if (script->get_base().is_valid()) {
        ret["base_type"] = script->get_base()->get_definition().name;
    } else {
        ret["base_type"] = def.extends;
    }

    ret["icon_path"] = def.icon_path;

    return ret;
}

//////////////////////////
// PLACEHOLDER INSTANCE //
//////////////////////////

// Implementation mostly mirrors Godot's implementation.
// See license information in README.md.

#define PLACEHOLDER_SELF ((PlaceHolderScriptInstance *)self)

static GDExtensionScriptInstanceInfo init_placeholder_instance_info() {
    // Methods which essentially have no utility (e.g. call) are implemented here instead of in the class.

    GDExtensionScriptInstanceInfo info;
    ScriptInstance::init_script_instance_info_common(info);

    info.property_can_revert_func = [](void *, GDExtensionConstStringNamePtr) -> GDExtensionBool {
        return false;
    };

    info.property_get_revert_func = [](void *, GDExtensionConstStringNamePtr, GDExtensionVariantPtr) -> GDExtensionBool {
        return false;
    };

    info.call_func = [](void *self, GDExtensionConstStringNamePtr p_method, const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count, GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
        r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
        *(Variant *)r_return = Variant();
    };

    info.is_placeholder_func = [](void *) -> GDExtensionBool {
        return true;
    };

    info.set_fallback_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) -> GDExtensionBool {
        return PLACEHOLDER_SELF->property_set_fallback(*(const StringName *)p_name, *(const Variant *)p_value);
    };

    info.get_fallback_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
        return PLACEHOLDER_SELF->property_get_fallback(*(const StringName *)p_name, *(Variant *)r_ret);
    };

    info.free_func = [](void *self) {
        memdelete(PLACEHOLDER_SELF);
    };

    return info;
}

const GDExtensionScriptInstanceInfo PlaceHolderScriptInstance::INSTANCE_INFO = init_placeholder_instance_info();

bool PlaceHolderScriptInstance::set(const StringName &p_name, const Variant &p_value, PropertySetGetError *r_err) {
    if (script->_is_placeholder_fallback_enabled()) {
        if (r_err != nullptr)
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

    if (r_err != nullptr)
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

    if (r_err != nullptr)
        *r_err = PROP_NOT_FOUND;

    return false;
}

GDExtensionPropertyInfo *PlaceHolderScriptInstance::get_property_list(uint32_t *r_count) const {
    Vector<GDExtensionPropertyInfo> props;

    int size = properties.size();
    props.resize(size);

    GDExtensionPropertyInfo *props_ptr = props.ptrw();

    if (script->_is_placeholder_fallback_enabled()) {
        for (int i = 0; i < size; i++) {
            GDExtensionPropertyInfo dst;
            copy_prop(properties[i], dst);

            props_ptr[i] = dst;
        }
    } else {
        for (int i = 0; i < size; i++) {
            GDExtensionPropertyInfo pinfo;
            copy_prop(properties[i], pinfo);

            if (!values.has(properties[i].name))
                pinfo.usage |= PROPERTY_USAGE_SCRIPT_DEFAULT_VALUE;

            props_ptr[i] = pinfo; // this is actually wrong in godot source (?)
        }
    }

    *r_count = size;

    GDExtensionPropertyInfo *list = alloc_with_len<GDExtensionPropertyInfo>(size);
    memcpy(list, props.ptr(), sizeof(GDExtensionPropertyInfo) * size);

    return list;
}

Variant::Type PlaceHolderScriptInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
    if (values.has(p_name)) {
        if (r_is_valid != nullptr)
            *r_is_valid = true;

        return values[p_name].get_type();
    }

    if (constants.has(p_name)) {
        if (r_is_valid != nullptr)
            *r_is_valid = true;

        return constants[p_name].get_type();
    }

    if (r_is_valid != nullptr)
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

    if (owner != nullptr && script.is_valid() && script->placeholder_has(owner) && script->placeholder_get(owner) == this) {
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

PlaceHolderScriptInstance::PlaceHolderScriptInstance(Ref<LuauScript> p_script, Object *p_owner) :
        script(p_script),
        owner(p_owner) {
    // Placeholder instance creation takes place in a const method.
    script->placeholders.insert(p_owner->get_instance_id(), this);
    script->update_exports_internal(nullptr, this);
}

PlaceHolderScriptInstance::~PlaceHolderScriptInstance() {
    if (script.is_valid()) {
        script->_placeholder_erased(this);
    }
}
