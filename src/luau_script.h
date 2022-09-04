#pragma once

#include <godot/gdnative_interface.h>

#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/resource_format_saver.hpp>
#include <godot_cpp/classes/global_constants.hpp>

using namespace godot;

class LuauScript : public ScriptExtension
{
    GDCLASS(LuauScript, ScriptExtension);

private:
    String source;

protected:
    static void _bind_methods();

public:
    virtual bool _has_source_code() const override;
    virtual String _get_source_code() const override;
    virtual void _set_source_code(const String &p_code) override;
    Error load_source_code(const String &p_path);

    virtual ScriptLanguage *_get_language() const override;

    /*
    virtual void _placeholder_erased(void *placeholder);
    virtual bool _can_instantiate() const;
    virtual Ref<Script> _get_base_script() const;
    virtual bool _inherits_script(const Ref<Script> &script) const;
    virtual StringName _get_instance_base_type() const;
    virtual void *_instance_create(Object *for_object) const;
    virtual void *_placeholder_instance_create(Object *for_object) const;
    virtual bool _instance_has(Object *object) const;
    virtual Error _reload(bool keep_state);
    virtual bool _has_method(const StringName &method) const;
    virtual Dictionary _get_method_info(const StringName &method) const;
    virtual bool _is_tool() const;
    virtual bool _is_valid() const;
    virtual bool _has_script_signal(const StringName &signal) const;
    virtual Array _get_script_signal_list() const;
    virtual Variant _get_property_default_value(const StringName &property) const;
    virtual void _update_exports();
    virtual Array _get_script_method_list() const;
    virtual Array _get_script_property_list() const;
    virtual int64_t _get_member_line(const StringName &member) const;
    virtual Dictionary _get_constants() const;
    virtual Array _get_members() const;
    virtual bool _is_placeholder_fallback_enabled() const;
    virtual Array _get_rpc_methods() const;

    // To implement later (or never)
    virtual Array _get_documentation() const;
    */
};

class LuauScriptInstance
{
public:
    static const GDNativeExtensionScriptInstanceInfo INSTANCE_INFO;

    /*
    bool set(const StringName &p_name, const Variant &p_value);
    bool get(const StringName &p_name, Variant &r_ret) const;

    void get_property_list(List<GDNativePropertyInfo> *p_list) const;
    GDNativeVariantType get_property_type(const StringName &p_name, GDNativeBool *r_is_valid) const;

    Object *get_owner();

    void get_property_state(List<Pair<StringName, Variant>> &p_state);

    void get_method_list(List<GDNativeMethodInfo> *p_list) const;
    GDNativeBool has_method(const StringName &p_name) const;

    void call(const StringName &p_method, const Variant *p_args, const GDNativeInt p_argument_count, Variant *r_return, GDNativeCallError *r_error);
    void notification(int32_t p_what);

    const char *to_string(GDNativeBool *r_is_valid);

    Script *get_script() const;
    ScriptLanguage *get_language() const;

    void free();
    */
};

class LuauLanguage : public ScriptLanguageExtension
{
    GDCLASS(LuauLanguage, ScriptLanguageExtension);

private:
    static LuauLanguage *singleton;
    bool finalized = false;

    void finalize();

protected:
    static void _bind_methods();

public:
    static LuauLanguage *get_singleton() { return singleton; }

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

    /* ... */
    virtual Object *_create_script() const override;

    /*
    virtual void _init();
    virtual Dictionary _validate(const String &script, const String &path, bool validate_functions, bool validate_errors, bool validate_warnings, bool validate_safe_lines) const;
    virtual bool _can_inherit_from_file() const;
    virtual int64_t _find_function(const String &class_name, const String &function_name) const;
    virtual String _make_function(const String &class_name, const String &function_name, const PackedStringArray &function_args) const;
    virtual void _add_global_constant(const StringName &name, const Variant &value);
    virtual void _add_named_global_constant(const StringName &name, const Variant &value);
    virtual void _remove_named_global_constant(const StringName &name);
    virtual void _reload_all_scripts();
    virtual void _reload_tool_script(const Ref<Script> &script, bool soft_reload);
    virtual void _frame();

    // To implement later (or never)
    virtual Ref<Script> _make_template(const String &_template, const String &class_name, const String &base_class_name) const;
    virtual Array _get_built_in_templates(const StringName &object) const;
    virtual bool _is_using_templates();

    virtual Error _open_in_external_editor(const Ref<Script> &script, int64_t line, int64_t column);
    virtual bool _overrides_external_editor();

    virtual Dictionary _complete_code(const String &code, const String &path, Object *owner) const;
    virtual Dictionary _lookup_code(const String &code, const String &symbol, const String &path, Object *owner) const;
    virtual String _auto_indent_code(const String &code, int64_t from_line, int64_t to_line) const;

    virtual void _thread_enter();
    virtual void _thread_exit();

    virtual bool _supports_builtin_mode() const;
    virtual bool _has_named_classes() const; // not true for any of Godot's built in languages. why

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
    virtual Array _debug_get_current_stack_info();

    // Profiler
    virtual void _profiling_start();
    virtual void _profiling_stop();
    virtual int64_t _profiling_get_accumulated_data(ScriptLanguageExtensionProfilingInfo *info_array, int64_t info_max);
    virtual int64_t _profiling_get_frame_data(ScriptLanguageExtensionProfilingInfo *info_array, int64_t info_max);

    // For docs
    virtual bool _supports_documentation() const;
    virtual Array _get_public_functions() const;
    virtual Dictionary _get_public_constants() const;
    virtual Array _get_public_annotations() const;

    // Seemingly unused by Godot
    virtual Error _execute_file(const String &path);

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
public:
    virtual PackedStringArray _get_recognized_extensions() const override;
    virtual bool _handles_type(const StringName &p_type) const override;
    virtual String _get_resource_type(const String &p_path) const override;
    virtual Variant _load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int64_t p_cache_mode) const override;
};

class ResourceFormatSaverLuauScript : public ResourceFormatSaver
{
public:
    virtual PackedStringArray _get_recognized_extensions(const Ref<Resource> &p_resource) const override;
    virtual bool _recognize(const Ref<Resource> &p_resource) const override;
    virtual int64_t _save(const Ref<Resource> &p_resource, const String &p_path, int64_t p_flags) override;
};
