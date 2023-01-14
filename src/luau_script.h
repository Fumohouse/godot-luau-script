#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/resource_format_saver.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/self_list.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "gd_luau.h"
#include "luau_lib.h"

namespace godot {
class Object;
class ScriptLanguage;
} //namespace godot

class PlaceHolderScriptInstance;

using namespace godot;

typedef HashMap<uint64_t, List<Pair<StringName, Variant>>> ScriptInstanceState;

class LuauCache;
class LuauScriptInstance;

class LuauScript : public ScriptExtension {
    GDCLASS(LuauScript, ScriptExtension);

    friend class LuauLanguage;
    friend class LuauCache;
    friend class GDLuau;
    friend class LuauScriptInstance;
    friend class PlaceHolderScriptInstance;

private:
    SelfList<LuauScript> script_list;

    bool _is_module = false;
    HashSet<String> dependents;

    String base_dir;
    String source;
    bool source_changed_cache;

    HashMap<uint64_t, LuauScriptInstance *> instances;
    HashMap<uint64_t, PlaceHolderScriptInstance *> placeholders;

    bool _is_reloading = false;
    bool valid;
    GDClassDefinition definition;
    HashMap<GDLuau::VMType, GDClassDefinition> vm_defs;

    bool placeholder_fallback_enabled;

    Error load_methods(GDLuau::VMType p_vm_type, bool p_force = false);

    void update_base_script(Error &r_error);

    void update_exports_values(List<GDProperty> &properties, HashMap<StringName, Variant> &values);
    bool update_exports_internal(bool *r_err, bool p_recursive_call, PlaceHolderScriptInstance *p_instance_to_update);

    static Error get_class_definition(Ref<LuauScript> p_script, lua_State *L, GDClassDefinition &r_def, bool &r_is_valid);

#ifdef TESTS_ENABLED
public:
#endif // TESTS_ENABLED
    Ref<LuauScript> base;
    HashSet<uint64_t> inheriters_cache;

    ScriptInstanceState pending_reload_state;

protected:
    static void _bind_methods() {}

public:
    bool _has_source_code() const override;
    String _get_source_code() const override;
    void _set_source_code(const String &p_code) override;
    Error load_source_code(const String &p_path);
    Error _reload(bool p_keep_state) override;

    ScriptLanguage *_get_language() const override;

    bool _is_valid() const override;
    bool _can_instantiate() const override;

    /* SCRIPT INFO */
    bool _is_tool() const override;
    StringName _get_instance_base_type() const override;
    Ref<Script> _get_base_script() const override;
    bool _inherits_script(const Ref<Script> &p_script) const override;

    TypedArray<Dictionary> _get_script_method_list() const override;
    bool _has_method(const StringName &p_method) const override;
    bool has_method(const StringName &p_method, StringName *r_actual_name = nullptr) const;
    Dictionary _get_method_info(const StringName &p_method) const override;

    TypedArray<Dictionary> _get_script_property_list() const override;
    TypedArray<StringName> _get_members() const override;
    bool _has_property_default_value(const StringName &p_property) const override;
    Variant _get_property_default_value(const StringName &p_property) const override;

    bool has_property(const StringName &p_name) const;
    const GDClassProperty &get_property(const StringName &p_name) const;

    bool _has_script_signal(const StringName &signal) const override;
    TypedArray<Dictionary> _get_script_signal_list() const override;

    Variant _get_rpc_config() const override;

    Dictionary _get_constants() const override;

    /* INSTANCE */
    void *_instance_create(Object *p_for_object) const override;
    bool _instance_has(Object *p_object) const override;
    LuauScriptInstance *instance_get(Object *p_object) const;

    /* PLACEHOLDER INSTANCE */
    bool _is_placeholder_fallback_enabled() const override { return placeholder_fallback_enabled; }
    void *_placeholder_instance_create(Object *p_for_object) const override;
    void _update_exports() override;
    void _placeholder_erased(void *p_placeholder) override;

    bool placeholder_has(Object *p_object) const;
    PlaceHolderScriptInstance *placeholder_get(Object *p_object);

    /*
    int64_t _get_member_line(const StringName &member) const;

    // To implement later (or never)
    TypedArray<Dictionary> _get_documentation() const;
    */

    void def_table_get(GDLuau::VMType p_vm_type, lua_State *T) const;
    const GDClassDefinition &get_definition() const { return definition; }

    bool is_reloading() const { return _is_reloading; }

    bool is_module() const { return _is_module; }
    bool has_dependent(const String &p_path) const;

    Error load_module(lua_State *L) const;
    void reload_module();

    LuauScript();
    ~LuauScript();
};

class ScriptInstance {
protected:
    // allocates list with an int at the front saying how long it is
    template <typename T>
    static T *alloc_with_len(int size) {
        uint64_t list_size = sizeof(T) * size;
        void *ptr = memalloc(list_size + sizeof(int));

        *((int *)ptr) = size;

        return (T *)((int *)ptr + 1);
    }

    static int get_len_from_ptr(const void *ptr);
    static void free_with_len(void *ptr);

    static void copy_prop(const GDProperty &src, GDExtensionPropertyInfo &dst);
    static void free_prop(const GDExtensionPropertyInfo &prop);

public:
    static void init_script_instance_info_common(GDExtensionScriptInstanceInfo &p_info);

    enum PropertySetGetError {
        PROP_OK,
        PROP_NOT_FOUND,
        PROP_WRONG_TYPE,
        PROP_READ_ONLY,
        PROP_WRITE_ONLY,
        PROP_GET_FAILED,
        PROP_SET_FAILED
    };

    virtual bool set(const StringName &p_name, const Variant &p_value, PropertySetGetError *r_err = nullptr) = 0;
    virtual bool get(const StringName &p_name, Variant &r_ret, PropertySetGetError *r_err = nullptr) = 0;

    void get_property_state(GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata);
    void get_property_state(List<Pair<StringName, Variant>> &p_list);

    virtual GDExtensionPropertyInfo *get_property_list(uint32_t *r_count) const = 0;
    void free_property_list(const GDExtensionPropertyInfo *p_list) const;

    virtual Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid) const = 0;

    virtual GDExtensionMethodInfo *get_method_list(uint32_t *r_count) const;
    void free_method_list(const GDExtensionMethodInfo *p_list) const;

    virtual bool has_method(const StringName &p_name) const = 0;

    virtual Object *get_owner() const = 0;
    virtual Ref<LuauScript> get_script() const = 0;
    ScriptLanguage *get_language() const;
};

class LuauScriptInstance : public ScriptInstance {
private:
    lua_State *L;

    Ref<LuauScript> script;
    Object *owner;
    GDLuau::VMType vm_type;

    int table_ref;
    int thread_ref;
    lua_State *T;

    int call_internal(const StringName &p_method, lua_State *T, int nargs, int nret);

    int protected_table_set(lua_State *L, const Variant &p_key, const Variant &p_value);
    int protected_table_get(lua_State *L, const Variant &p_key);

public:
    static const GDExtensionScriptInstanceInfo INSTANCE_INFO;

    bool set(const StringName &p_name, const Variant &p_value, PropertySetGetError *r_err = nullptr) override;
    bool get(const StringName &p_name, Variant &r_ret, PropertySetGetError *r_err = nullptr) override;

    GDExtensionPropertyInfo *get_property_list(uint32_t *r_count) const override;

    Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid) const override;

    bool property_can_revert(const StringName &p_name) const;
    bool property_get_revert(const StringName &p_name, Variant *r_ret) const;

    bool has_method(const StringName &p_name) const override;

    void call(const StringName &p_method, const Variant *const *p_args, const GDExtensionInt p_argument_count, Variant *r_return, GDExtensionCallError *r_error);
    void notification(int32_t p_what);
    void to_string(GDExtensionBool *r_is_valid, String *r_out);

    Object *get_owner() const override { return owner; }
    Ref<LuauScript> get_script() const override { return script; }

    bool table_set(lua_State *T) const;
    bool table_get(lua_State *T) const;

    const GDClassProperty *get_property(const StringName &p_name) const;
    const GDMethod *get_signal(const StringName &p_name) const;
    const Variant *get_constant(const StringName &p_name) const;

    LuauScriptInstance(Ref<LuauScript> p_script, Object *p_owner, GDLuau::VMType p_vm_type);
    ~LuauScriptInstance();
};

// ! sync with core/object/script_language
// need to reimplement here because Godot does not expose placeholders to GDExtension.
// doing this is okay because all Godot functions which request a placeholder instance assign it to a ScriptInstance *
class PlaceHolderScriptInstance : public ScriptInstance {
private:
    Object *owner = nullptr;
    Ref<LuauScript> script;

    List<GDProperty> properties;
    HashMap<StringName, Variant> values;
    HashMap<StringName, Variant> constants;

public:
    static const GDExtensionScriptInstanceInfo INSTANCE_INFO;

    bool set(const StringName &p_name, const Variant &p_value, PropertySetGetError *r_err = nullptr) override;
    bool get(const StringName &p_name, Variant &r_ret, PropertySetGetError *r_err = nullptr) override;

    GDExtensionPropertyInfo *get_property_list(uint32_t *r_count) const override;

    Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid) const override;

    GDExtensionMethodInfo *get_method_list(uint32_t *r_count) const override;

    bool has_method(const StringName &p_name) const override;

    bool property_set_fallback(const StringName &p_name, const Variant &p_value);
    bool property_get_fallback(const StringName &p_name, Variant &r_ret);

    void update(const List<GDProperty> &p_properties, const HashMap<StringName, Variant> &p_values);

    Object *get_owner() const override { return owner; }
    Ref<LuauScript> get_script() const override { return script; }

    PlaceHolderScriptInstance(Ref<LuauScript> p_script, Object *p_owner);
    ~PlaceHolderScriptInstance();
};

class LuauLanguage : public ScriptLanguageExtension {
    GDCLASS(LuauLanguage, ScriptLanguageExtension);

    friend class LuauScript;
    friend class LuauScriptInstance;

private:
    // TODO: idk why these are needed, but all the other implementations have them
    Mutex lock;

    static LuauLanguage *singleton;
    GDLuau *luau = nullptr;
    LuauCache *cache = nullptr;

    SelfList<LuauScript>::List script_list;
    HashSet<String> core_scripts;

    HashMap<StringName, Variant> global_constants;

    bool finalized = false;
    void finalize();

    void discover_core_scripts(const String &path = "res://");

    List<Ref<LuauScript>> get_scripts() const;

protected:
    static void _bind_methods() {}

public:
    static LuauLanguage *get_singleton() { return singleton; }

    void _init() override;
    void _finish() override;

    /* LANGUAGE INFO */
    String _get_name() const override;
    String _get_type() const override;

    String _get_extension() const override;
    PackedStringArray _get_recognized_extensions() const override;

    PackedStringArray _get_reserved_words() const override;
    bool _is_control_flow_keyword(const String &p_keyword) const override;

    PackedStringArray _get_comment_delimiters() const override;
    PackedStringArray _get_string_delimiters() const override;

    bool _supports_builtin_mode() const override;

    /* ... */
    Object *_create_script() const override;
    Ref<Script> _make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const override;

    void _reload_all_scripts() override;
    void _reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) override;

    void _add_global_constant(const StringName &p_name, const Variant &p_value) override;
    void _add_named_global_constant(const StringName &p_name, const Variant &p_value) override;
    void _remove_named_global_constant(const StringName &p_name) override;

    /* ???: pure virtual functions which have no clear purpose */
    Error _execute_file(const String &p_path) override;
    bool _has_named_classes() const override;

    /* UNNEEDED */
    void _thread_enter() override {}
    void _thread_exit() override {}

    bool _overrides_external_editor() override { return false; }
    // Error _open_in_external_editor(const Ref<Script> &script, int64_t line, int64_t column);

    /* TO IMPLEMENT */
    void _frame() override {}

    Dictionary _validate(const String &script, const String &path, bool validate_functions, bool validate_errors, bool validate_warnings, bool validate_safe_lines) const override {
        Dictionary output;

        output["valid"] = true;

        return output;
    }

    // Debugger
    // String _debug_get_error() const;
    // int64_t _debug_get_stack_level_count() const;
    // int64_t _debug_get_stack_level_line(int64_t level) const;
    // String _debug_get_stack_level_function(int64_t level) const;
    // Dictionary _debug_get_stack_level_locals(int64_t level, int64_t max_subitems, int64_t max_depth);
    // Dictionary _debug_get_stack_level_members(int64_t level, int64_t max_subitems, int64_t max_depth);
    // void *_debug_get_stack_level_instance(int64_t level);
    // Dictionary _debug_get_globals(int64_t max_subitems, int64_t max_depth);
    // String _debug_parse_stack_level_expression(int64_t level, const String &expression, int64_t max_subitems, int64_t max_depth);
    TypedArray<Dictionary> _debug_get_current_stack_info() override { return TypedArray<Dictionary>(); }

    /*
    bool _can_inherit_from_file() const;
    int64_t _find_function(const String &class_name, const String &function_name) const;
    String _make_function(const String &class_name, const String &function_name, const PackedStringArray &function_args) const;

    // To implement later (or never)
    TypedArray<Dictionary> _get_built_in_templates(const StringName &object) const;
    bool _is_using_templates();

    Dictionary _complete_code(const String &code, const String &path, Object *owner) const;
    Dictionary _lookup_code(const String &code, const String &symbol, const String &path, Object *owner) const;
    String _auto_indent_code(const String &code, int64_t from_line, int64_t to_line) const;

    String _validate_path(const String &path) const; // used by C# only to prevent naming class to keyword. probably not super necessary

    // Non-essential. For class icons?
    bool _handles_global_class_type(const String &type) const;
    Dictionary _get_global_class_name(const String &path) const;

    // Profiler
    void _profiling_start();
    void _profiling_stop();
    int64_t _profiling_get_accumulated_data(ScriptLanguageExtensionProfilingInfo *info_array, int64_t info_max);
    int64_t _profiling_get_frame_data(ScriptLanguageExtensionProfilingInfo *info_array, int64_t info_max);

    // For docs
    bool _supports_documentation() const;
    Array _get_public_functions() const;
    Dictionary _get_public_constants() const;
    TypedArray<Dictionary> _get_public_annotations() const;

    // Seemingly unused by Godot
    void *_alloc_instance_binding_data(Object *object);
    void _free_instance_binding_data(void *data);
    void _refcount_incremented_instance_binding(Object *object);
    bool _refcount_decremented_instance_binding(Object *object);
    */

    bool is_core_script(const String &p_path) const;
    const HashMap<StringName, Variant> &get_global_constants() const { return global_constants; }

    LuauLanguage();
    ~LuauLanguage();
};

class ResourceFormatLoaderLuauScript : public ResourceFormatLoader {
    GDCLASS(ResourceFormatLoaderLuauScript, ResourceFormatLoader);

protected:
    static void _bind_methods() {}

public:
    PackedStringArray _get_recognized_extensions() const override;
    bool _handles_type(const StringName &p_type) const override;
    static String get_resource_type(const String &p_path);
    String _get_resource_type(const String &p_path) const override;
    Variant _load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int64_t p_cache_mode) const override;
};

class ResourceFormatSaverLuauScript : public ResourceFormatSaver {
    GDCLASS(ResourceFormatSaverLuauScript, ResourceFormatSaver);

protected:
    static void _bind_methods() {}

public:
    PackedStringArray _get_recognized_extensions(const Ref<Resource> &p_resource) const override;
    bool _recognize(const Ref<Resource> &p_resource) const override;
    int64_t _save(const Ref<Resource> &p_resource, const String &p_path, int64_t p_flags) override;
};
