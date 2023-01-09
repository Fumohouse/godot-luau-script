#include "luau_script.h"

void *LuauScript::_placeholder_instance_create(Object *p_for_object) const {
    PlaceHolderScriptInstance *internal = memnew(PlaceHolderScriptInstance(Ref<LuauScript>(this), p_for_object));
    return internal::gde_interface->script_instance_create(&PlaceHolderScriptInstance::INSTANCE_INFO, internal);
}

// Based on GDScript implementation. See license information in README.md.
void LuauScript::_update_exports() {
    bool cyclic_error = false;
    update_exports_internal(&cyclic_error, false, nullptr);

    if (cyclic_error)
        return;

    // Update inheriting scripts
    HashSet<uint64_t> copy = inheriters_cache; // Gets modified by _update_exports

    for (const uint64_t &E : copy) {
        Object *ptr = ObjectDB::get_instance(E);
        LuauScript *s = Object::cast_to<LuauScript>(ptr);

        if (s == nullptr)
            continue;

        s->_update_exports();
    }
}

void LuauScript::update_exports_values(List<GDProperty> &properties, HashMap<StringName, Variant> &values) {
    for (const KeyValue<StringName, GDClassProperty> &pair : definition.properties) {
        const GDClassProperty &prop = pair.value;

        properties.push_back(prop.property);
        values[pair.key] = prop.default_value;
    }

    if (base.is_valid()) {
        base->update_exports_values(properties, values);
    }
}

// `p_recursive_call` indicates whether this method was called from within this method.
bool LuauScript::update_exports_internal(bool *r_err, bool p_recursive_call, PlaceHolderScriptInstance *p_instance_to_update) {
    // Step 1: Base cache
    static Vector<LuauScript *> base_caches;
    if (!p_recursive_call) {
        base_caches.clear();
    }

    base_caches.append(this);

    // Step 2: Reload base class, properties, and signals from source
    bool changed = false;

    if (source_changed_cache) {
        source_changed_cache = false;
        changed = true;

        GDClassDefinition def;
        bool is_valid;
        Error err = get_class_definition(this, source, def, is_valid);

        if (err == OK) {
            // Update base class, inheriter cache
            if (base.is_valid()) {
                base->inheriters_cache.erase(get_instance_id());
                base = Ref<LuauScript>();
            }

            definition.extends = def.extends;

            Error err;
            update_base_script(err);

            if (base.is_valid()) {
                base->inheriters_cache.insert(get_instance_id());
            }

            // Update properties, signals
            // TODO: Signals
            definition.properties = def.properties;
        } else {
            placeholder_fallback_enabled = true;
            return false;
        }
    } else if (placeholder_fallback_enabled) {
        return false;
    }

    placeholder_fallback_enabled = false;

    // Step 3: Check base cache for cyclic inheritance
    // See the GDScript implementation.
    if (base.is_valid() && base->_is_valid()) {
        for (int i = 0; i < base_caches.size(); i++) {
            if (base_caches[i] == base.ptr()) {
                if (r_err != nullptr) {
                    *r_err = true;
                }

                valid = false; // Show error in editor
                base->valid = false;
                base->inheriters_cache.clear(); // Prevents stack overflows
                base = Ref<LuauScript>();
                ERR_FAIL_V_MSG(false, "Cyclic inheritance in script class.");
            }
        }

        if (base->update_exports_internal(r_err, true, nullptr)) {
            if (r_err != nullptr && *r_err) {
                return false;
            }

            changed = true;
        }
    }

    // Step 4: Update placeholder instances
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
    script->update_exports_internal(nullptr, false, this);
}

PlaceHolderScriptInstance::~PlaceHolderScriptInstance() {
    if (script.is_valid()) {
        script->_placeholder_erased(this);
    }
}
