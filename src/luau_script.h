#pragma once

#include <godot/gdnative_interface.h>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/resource_format_saver.hpp>
#include <godot_cpp/classes/global_constants.hpp>

#include "luau_lib.h"
#include "gd_luau.h"

namespace godot
{
    class Object;
    class ScriptLanguage;
}

using namespace godot;

class LuauScriptInstance;

class LuauScript : public ScriptExtension
{
    GDCLASS(LuauScript, ScriptExtension);

    friend class LuauScriptInstance;

private:
    String source;
    HashMap<Object *, LuauScriptInstance *> instances;

    bool valid;
    GDClassDefinition definition;
    HashMap<GDLuau::VMType, GDClassMethods> methods;

    Error load_methods(GDLuau::VMType p_vm_type, bool force = false);

protected:
    static void _bind_methods() {}

public:
    virtual bool _has_source_code() const override;
    virtual String _get_source_code() const override;
    virtual void _set_source_code(const String &p_code) override;
    Error load_source_code(const String &p_path);
    virtual Error _reload(bool p_keep_state) override;

    virtual ScriptLanguage *_get_language() const override;

    virtual bool _is_valid() const override;
    virtual bool _can_instantiate() const override;

    /* SCRIPT INFO */
    virtual bool _is_tool() const override;

    virtual TypedArray<Dictionary> _get_script_method_list() const override;
    virtual bool _has_method(const StringName &p_method) const override;
    virtual Dictionary _get_method_info(const StringName &p_method) const override;

    virtual TypedArray<Dictionary> _get_script_property_list() const override;
    virtual TypedArray<StringName> _get_members() const override;
    virtual bool _has_property_default_value(const StringName &p_property) const override;
    virtual Variant _get_property_default_value(const StringName &p_property) const override;

    /* INSTANCE */
    virtual void *_instance_create(Object *p_for_object) const override;
    virtual bool _instance_has(Object *p_object) const override;
    LuauScriptInstance *instance_get(Object *p_object) const;

    /*
    virtual void _placeholder_erased(void *placeholder);
    virtual Ref<Script> _get_base_script() const;
    virtual bool _inherits_script(const Ref<Script> &script) const;
    virtual StringName _get_instance_base_type() const;
    virtual void *_placeholder_instance_create(Object *for_object) const;
    virtual bool _has_script_signal(const StringName &signal) const;
    virtual TypedArray<Dictionary> _get_script_signal_list() const;
    virtual void _update_exports();
    virtual int64_t _get_member_line(const StringName &member) const;
    virtual Dictionary _get_constants() const;
    virtual bool _is_placeholder_fallback_enabled() const;
    virtual Variant _get_rpc_config() const;

    // To implement later (or never)
    virtual TypedArray<Dictionary> _get_documentation() const;
    */
};

class LuauScriptInstance
{
private:
    Ref<LuauScript> script;
    Object *owner;
    GDLuau::VMType vm_type;

    int table_ref;
    int thread_ref;
    lua_State *T;

public:
    static const GDNativeExtensionScriptInstanceInfo INSTANCE_INFO;

    /*
    bool set(const StringName &p_name, const Variant &p_value);
    bool get(const StringName &p_name, Variant &r_ret) const;
    */

    GDNativePropertyInfo *get_property_list(uint32_t *r_count) const;
    void free_property_list(const GDNativePropertyInfo *p_list) const;

    Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid) const;

    Object *get_owner() const;

    /*
    void get_property_state(GDNativeExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) const;
    */

    GDNativeMethodInfo *get_method_list(uint32_t *r_count) const;
    void free_method_list(const GDNativeMethodInfo *p_list) const;

    bool has_method(const StringName &p_name) const;

    void call(const StringName &p_method, const Variant *p_args, const GDNativeInt p_argument_count, Variant *r_return, GDNativeCallError *r_error);

    /*
    void notification(int32_t p_what);

    const char *to_string(GDNativeBool *r_is_valid, String *r_out);
    */

    Ref<Script> get_script() const;
    ScriptLanguage *get_language() const;

    LuauScriptInstance(Ref<LuauScript> p_script, Object *p_owner, GDLuau::VMType p_vm_type);
    ~LuauScriptInstance();
};

class LuauLanguage : public ScriptLanguageExtension
{
    GDCLASS(LuauLanguage, ScriptLanguageExtension);

    friend class LuauScript;
    friend class LuauScriptInstance;

private:
    // TODO: idk why these are needed, but all the other implementations have them
    Mutex lock;

    static LuauLanguage *singleton;
    GDLuau *luau;

    bool finalized = false;

    void finalize();

protected:
    static void _bind_methods() {}

public:
    static LuauLanguage *get_singleton() { return singleton; }

    virtual void _init() override;
    virtual void _finish() override;

    /* LANGUAGE INFO */
    virtual String _get_name() const override;
    virtual String _get_type() const override;

    virtual String _get_extension() const override;
    virtual PackedStringArray _get_recognized_extensions() const override;

    virtual PackedStringArray _get_reserved_words() const override;
    virtual bool _is_control_flow_keyword(const String &p_keyword) const override;

    virtual PackedStringArray _get_comment_delimiters() const override;
    virtual PackedStringArray _get_string_delimiters() const override;

    virtual bool _supports_builtin_mode() const override;

    /* ... */
    virtual Object *_create_script() const override;
    virtual Ref<Script> _make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const override;

    /* ???: pure virtual functions which have no clear purpose */
    virtual Error _execute_file(const String &p_path) override;
    virtual bool _has_named_classes() const override;

    /*
    virtual Dictionary _validate(const String &script, const String &path, bool validate_functions, bool validate_errors, bool validate_warnings, bool validate_safe_lines) const;
    virtual bool _can_inherit_from_file() const;
    virtual int64_t _find_function(const String &class_name, const String &function_name) const;
    virtual String _make_function(const String &class_name, const String &function_name, const PackedStringArray &function_args) const;
    virtual void _reload_all_scripts();
    virtual void _reload_tool_script(const Ref<Script> &script, bool soft_reload);
    virtual void _frame();

    // To implement later (or never)
    virtual void _add_global_constant(const StringName &name, const Variant &value);
    virtual void _add_named_global_constant(const StringName &name, const Variant &value);
    virtual void _remove_named_global_constant(const StringName &name);

    virtual TypedArray<Dictionary> _get_built_in_templates(const StringName &object) const;
    virtual bool _is_using_templates();

    virtual Error _open_in_external_editor(const Ref<Script> &script, int64_t line, int64_t column);
    virtual bool _overrides_external_editor();

    virtual Dictionary _complete_code(const String &code, const String &path, Object *owner) const;
    virtual Dictionary _lookup_code(const String &code, const String &symbol, const String &path, Object *owner) const;
    virtual String _auto_indent_code(const String &code, int64_t from_line, int64_t to_line) const;

    virtual void _thread_enter();
    virtual void _thread_exit();

    virtual String _validate_path(const String &path) const; // used by C# only to prevent naming class to keyword. probably not super necessary

    // Non-essential. For class icons?
    virtual bool _handles_global_class_type(const String &type) const;
    virtual Dictionary _get_global_class_name(const String &path) const;

    // Debugger
    virtual String _debug_get_error() const;
    virtual int64_t _debug_get_stack_level_count() const;
    virtual int64_t _debug_get_stack_level_line(int64_t level) const;
    virtual String _debug_get_stack_level_function(int64_t level) const;
    virtual Dictionary _debug_get_stack_level_locals(int64_t level, int64_t max_subitems, int64_t max_depth);
    virtual Dictionary _debug_get_stack_level_members(int64_t level, int64_t max_subitems, int64_t max_depth);
    virtual void *_debug_get_stack_level_instance(int64_t level);
    virtual Dictionary _debug_get_globals(int64_t max_subitems, int64_t max_depth);
    virtual String _debug_parse_stack_level_expression(int64_t level, const String &expression, int64_t max_subitems, int64_t max_depth);
    virtual TypedArray<Dictionary> _debug_get_current_stack_info();

    // Profiler
    virtual void _profiling_start();
    virtual void _profiling_stop();
    virtual int64_t _profiling_get_accumulated_data(ScriptLanguageExtensionProfilingInfo *info_array, int64_t info_max);
    virtual int64_t _profiling_get_frame_data(ScriptLanguageExtensionProfilingInfo *info_array, int64_t info_max);

    // For docs
    virtual bool _supports_documentation() const;
    virtual Array _get_public_functions() const;
    virtual Dictionary _get_public_constants() const;
    virtual TypedArray<Dictionary> _get_public_annotations() const;

    // Seemingly unused by Godot
    virtual void *_alloc_instance_binding_data(Object *object);
    virtual void _free_instance_binding_data(void *data);
    virtual void _refcount_incremented_instance_binding(Object *object);
    virtual bool _refcount_decremented_instance_binding(Object *object);
    */

    LuauLanguage();
    ~LuauLanguage();
};

class ResourceFormatLoaderLuauScript : public ResourceFormatLoader
{
    GDCLASS(ResourceFormatLoaderLuauScript, ResourceFormatLoader);

protected:
    static void _bind_methods() {}

public:
    virtual PackedStringArray _get_recognized_extensions() const override;
    virtual bool _handles_type(const StringName &p_type) const override;
    virtual String _get_resource_type(const String &p_path) const override;
    virtual Variant _load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int64_t p_cache_mode) const override;
};

class ResourceFormatSaverLuauScript : public ResourceFormatSaver
{
    GDCLASS(ResourceFormatSaverLuauScript, ResourceFormatSaver);

protected:
    static void _bind_methods() {}

public:
    virtual PackedStringArray _get_recognized_extensions(const Ref<Resource> &p_resource) const override;
    virtual bool _recognize(const Ref<Resource> &p_resource) const override;
    virtual int64_t _save(const Ref<Resource> &p_resource, const String &p_path, int64_t p_flags) override;
};
