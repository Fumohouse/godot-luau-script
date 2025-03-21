#pragma once

#include <Luau/Lexer.h>
#include <Luau/ParseResult.h>
#include <gdextension_interface.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/self_list.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <string>
#include <vector>

#include "analysis/analysis.h"
#include "core/permissions.h"
#include "core/runtime.h"
#include "scheduler/task_scheduler.h"
#include "scripting/luau_lib.h"

namespace godot {
class Object;
class ScriptLanguage;
} //namespace godot

using namespace godot;

#ifdef TOOLS_ENABLED
class PlaceHolderScriptInstance;
#endif // TOOLS_ENABLED

typedef HashMap<uint64_t, List<Pair<StringName, Variant>>> ScriptInstanceState;

class LuauCache;
class LuauScriptInstance;
class LuauInterface;

#define INIT_LUA_PATH "res://init.lua"

struct LuauData {
	Luau::Allocator allocator;
	Luau::ParseResult parse_result;
	std::string bytecode;
	LuauScriptAnalysisResult analysis_result;
};

class LuauScript : public ScriptExtension {
	GDCLASS(LuauScript, ScriptExtension);

	friend class LuauLanguage;
	friend class LuauCache;
	friend class LuauScriptInstance;
#ifdef TOOLS_ENABLED
	friend class PlaceHolderScriptInstance;
#endif // TOOLS_ENABLED

public:
	enum LoadStage {
		LOAD_NONE,
		LOAD_COMPILE,
		LOAD_ANALYZE,
		LOAD_FULL
	};

private:
	SelfList<LuauScript> script_list;

	Ref<LuauScript> base;

	bool _is_module = false;
	HashSet<Ref<LuauScript>> dependencies; // Load-time dependencies only.

	String source;
	LuauData luau_data;
	bool source_changed_cache;

	HashMap<uint64_t, LuauScriptInstance *> instances;
#ifdef TOOLS_ENABLED
	HashMap<uint64_t, PlaceHolderScriptInstance *> placeholders;
	bool placeholder_fallback_enabled = false;
#endif // TOOLS_ENABLED

	bool _is_loading = false;
	bool valid = false;
	GDClassDefinition definition;
	int table_refs[LuauRuntime::VM_MAX] = { 0 };

#ifdef TOOLS_ENABLED
	struct {
		int function_refs[LuauRuntime::VM_MAX] = { 0 };
		HashSet<int> breakpoints;
	} debug;

	void ref_thread(lua_State *L);
	void set_breakpoint(int p_line, bool p_enabled);
#endif // TOOLS_ENABLED

	HashSet<String> methods;
	HashMap<StringName, Variant> constants;

	LoadStage load_stage = LOAD_NONE;
	Error compile();
	Error analyze();
	Error finish_load();
	Error try_load(lua_State *L, String *r_err = nullptr);

#ifdef TOOLS_ENABLED
	void update_exports_values(List<GDProperty> &r_properties, HashMap<StringName, Variant> &r_values);
	bool update_exports_internal(PlaceHolderScriptInstance *p_instance_to_update);
#endif // TOOLS_ENABLED

	Error reload_tables();

#ifdef TESTS_ENABLED
public:
#endif // TESTS_ENABLED
	ScriptInstanceState pending_reload_state;

protected:
	static void _bind_methods() {}

public:
	bool _has_source_code() const override;
	String _get_source_code() const override;
	void _set_source_code(const String &p_code) override;
	Error load_source_code(const String &p_path);

	Error load(LoadStage p_load_stage, bool p_force = false);
	Error _reload(bool p_keep_state) override;

	ScriptLanguage *_get_language() const override;

	bool _is_valid() const override;
	bool _can_instantiate() const override;

	/* SCRIPT INFO */
	bool _is_tool() const override;
	StringName _get_instance_base_type() const override;
	Ref<Script> _get_base_script() const override;
	bool _inherits_script(const Ref<Script> &p_script) const override;
	StringName _get_global_name() const override;

	TypedArray<Dictionary> _get_script_method_list() const override;
	bool _has_method(const StringName &p_method) const override;
	bool has_method(const StringName &p_method, StringName *r_actual_name = nullptr) const;
	bool _has_static_method(const StringName &p_method) const override { return false; }
	Dictionary _get_method_info(const StringName &p_method) const override;

	TypedArray<Dictionary> _get_script_property_list() const override;
	TypedArray<StringName> _get_members() const override;
	bool _has_property_default_value(const StringName &p_property) const override;
	Variant _get_property_default_value(const StringName &p_property) const override;

	bool has_property(const StringName &p_name) const;
	const GDClassProperty &get_property(const StringName &p_name) const;

	bool _has_script_signal(const StringName &p_signal) const override;
	TypedArray<Dictionary> _get_script_signal_list() const override;

	Variant _get_rpc_config() const override;
	Dictionary _get_constants() const override;

	bool _editor_can_reload_from_file() override { return true; }

	/* INSTANCE */
	void *_instance_create(Object *p_for_object) const override;
	bool instance_has(uint64_t p_obj_id) const;
	bool _instance_has(Object *p_object) const override;
	LuauScriptInstance *instance_get(uint64_t p_obj_id) const;

	/* PLACEHOLDER INSTANCE */
	bool _is_placeholder_fallback_enabled() const override;
	void *_placeholder_instance_create(Object *p_for_object) const override;
	void _update_exports() override;
	void _placeholder_erased(void *p_placeholder) override;

#ifdef TOOLS_ENABLED
	bool placeholder_has(Object *p_object) const;
	PlaceHolderScriptInstance *placeholder_get(Object *p_object);
#endif // TOOLS_ENABLED

	/* TO IMPLEMENT */
	int32_t _get_member_line(const StringName &p_member) const override { return -1; }
	TypedArray<Dictionary> _get_documentation() const override { return TypedArray<Dictionary>(); }

	/* MISC (NON OVERRIDE) */
	String resolve_path(const String &p_relative_path, String &r_error) const;
	Error load_table(LuauRuntime::VMType p_vm_type, bool p_force = false);
	void unref_table(LuauRuntime::VMType p_vm);
	int get_table_ref(LuauRuntime::VMType p_vm_type) const { return table_refs[p_vm_type]; };

	const LuauData &get_luau_data() const { return luau_data; }
	Ref<LuauScript> get_base() const { return base; }

	void def_table_get(const ThreadHandle &T) const;
	const GDClassDefinition &get_definition() const { return definition; }

	bool is_loading() const { return _is_loading; }
	bool is_module() const { return _is_module; }

	bool has_dependency(const Ref<LuauScript> &p_script) const;
	bool add_dependency(const Ref<LuauScript> &p_script);

#if TOOLS_ENABLED
	void insert_breakpoint(int p_line);
	void remove_breakpoint(int p_line);
#endif // TOOLS_ENABLED

	void load_module(lua_State *L);
#if TOOLS_ENABLED
	void unload_module();
#endif // TOOLS_ENABLED

	void error(const char *p_method, const String &p_msg, int p_line = 0) const;

	LuauScript();
	~LuauScript();
};

class ScriptInstance {
protected:
	static void copy_prop(const GDProperty &p_src, GDExtensionPropertyInfo &p_dst);
	static void free_prop(const GDExtensionPropertyInfo &p_prop);

public:
	static void init_script_instance_info_common(GDExtensionScriptInstanceInfo3 &p_info);

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

	virtual GDExtensionPropertyInfo *get_property_list(uint32_t *r_count) = 0;
	void free_property_list(const GDExtensionPropertyInfo *p_list, uint32_t p_count) const;
	virtual bool validate_property(GDExtensionPropertyInfo *p_property) const { return false; }

	virtual Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid) const = 0;

	virtual GDExtensionMethodInfo *get_method_list(uint32_t *r_count) const;
	void free_method_list(const GDExtensionMethodInfo *p_list, uint32_t p_count) const;

	virtual bool has_method(const StringName &p_name) const = 0;

	virtual Object *get_owner() const = 0;
	virtual Ref<LuauScript> get_script() const = 0;
	ScriptLanguage *get_language() const;
};

class LuauScriptInstance : public ScriptInstance {
	Ref<LuauScript> script;
	Object *owner;
	LuauRuntime::VMType vm_type;
	BitField<ThreadPermissions> permissions = PERMISSION_BASE;

	int table_ref;
	int thread_ref;
	lua_State *T;

	int call_internal(const StringName &p_method, const ThreadHandle &ET, int p_nargs, int p_nret);

public:
	static const GDExtensionScriptInstanceInfo3 INSTANCE_INFO;

	bool set(const StringName &p_name, const Variant &p_value, PropertySetGetError *r_err = nullptr) override;
	bool get(const StringName &p_name, Variant &r_ret, PropertySetGetError *r_err = nullptr) override;

	GDExtensionPropertyInfo *get_property_list(uint32_t *r_count) override;

	Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid) const override;

	bool property_can_revert(const StringName &p_name);
	bool property_get_revert(const StringName &p_name, Variant *r_ret);

	bool has_method(const StringName &p_name) const override;

	void call(const StringName &p_method, const Variant *const *p_args, const GDExtensionInt p_argument_count, Variant *r_return, GDExtensionCallError *r_error);

	void notification(int32_t p_what);
	void to_string(GDExtensionBool *r_is_valid, String *r_out);

	Object *get_owner() const override { return owner; }
	Ref<LuauScript> get_script() const override { return script; }

	bool get_table(const ThreadHandle &T) const;
	bool table_set(const ThreadHandle &T) const;
	bool table_get(const ThreadHandle &T) const;

	LuauRuntime::VMType get_vm_type() const { return vm_type; }

	const GDMethod *get_method(const StringName &p_name) const;
	const GDClassProperty *get_property(const StringName &p_name) const;
	const GDMethod *get_signal(const StringName &p_name) const;
	const Variant *get_constant(const StringName &p_name) const;

	static LuauScriptInstance *from_object(GDExtensionObjectPtr p_object);

	LuauScriptInstance(const Ref<LuauScript> &p_script, Object *p_owner, LuauRuntime::VMType p_vm_type);
	~LuauScriptInstance();
};

// ! SYNC WITH core/object/script_language
// need to reimplement here because Godot does not expose placeholders to GDExtension.
// doing this is okay because all Godot functions which request a placeholder instance assign it to a ScriptInstance *
#ifdef TOOLS_ENABLED
class PlaceHolderScriptInstance final : public ScriptInstance {
	Object *owner = nullptr;
	Ref<LuauScript> script;

	List<GDProperty> properties;
	HashMap<StringName, Variant> values;
	HashMap<StringName, Variant> constants;

public:
	static const GDExtensionScriptInstanceInfo3 INSTANCE_INFO;

	bool set(const StringName &p_name, const Variant &p_value, PropertySetGetError *r_err = nullptr) override;
	bool get(const StringName &p_name, Variant &r_ret, PropertySetGetError *r_err = nullptr) override;

	GDExtensionPropertyInfo *get_property_list(uint32_t *r_count) override;

	Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid) const override;

	GDExtensionMethodInfo *get_method_list(uint32_t *r_count) const override;

	bool has_method(const StringName &p_name) const override;

	bool property_set_fallback(const StringName &p_name, const Variant &p_value);
	bool property_get_fallback(const StringName &p_name, Variant &r_ret);

	void update(const List<GDProperty> &p_properties, const HashMap<StringName, Variant> &p_values);

	Object *get_owner() const override { return owner; }
	Ref<LuauScript> get_script() const override { return script; }

	PlaceHolderScriptInstance(const Ref<LuauScript> &p_script, Object *p_owner);
	~PlaceHolderScriptInstance();
};
#endif // TOOLS_ENABLED

#define THREAD_EXECUTION_TIMEOUT 10

class LuauLanguage : public ScriptLanguageExtension {
	GDCLASS(LuauLanguage, ScriptLanguageExtension);

	friend class LuauScript;
	friend class LuauScriptInstance;

	// TODO: idk why these are needed, but all the other implementations have them
	Ref<Mutex> lock;

	static LuauLanguage *singleton;
	LuauRuntime *luau = nullptr;
	LuauCache *cache = nullptr;
	LuauInterface *interface = nullptr;
	TaskScheduler task_scheduler;

	uint64_t ticks_usec = 0;

	SelfList<LuauScript>::List script_list;

	HashMap<StringName, Variant> global_constants;

#ifdef TOOLS_ENABLED
	HashMap<LuauScriptInstance *, void *> instance_to_godot;

	struct DebugInfo {
		struct StackInfo {
			// These are presumably unstable pointers, but they should be read
			// by Godot by the time they are collected/changed.
			const char *source;
			const char *name;
			int line = 0;

			operator Dictionary() const;
		};

		struct BreakStackInfo : public StackInfo {
			HashMap<String, Variant> members;
			HashMap<String, Variant> locals;

			void *instance = nullptr;
		};

		Ref<Mutex> call_lock;
		// STL is faster. The call_stack path especially is extremely hot (made
		// on every engine call in the editor).
		std::vector<StackInfo> call_stack;
		std::vector<BreakStackInfo> break_call_stack;

		int thread_ref = 0;
		lua_State *L = nullptr;
		String error;
		int break_depth = -1;

		String base_break_source;
		int base_break_line = -1;

		int breakhits[LuauRuntime::VM_MAX] = { 0 };

		uint64_t interrupt_reset = 0;
	} debug;

	void debug_init();
	void debug_reset();

	static void lua_interrupt(lua_State *L, int p_gc);
	static void lua_debuginterrupt(lua_State *L, lua_Debug *ar);
	static void lua_debugbreak(lua_State *L, lua_Debug *ar);
	static void lua_debugstep(lua_State *L, lua_Debug *ar);
	static void lua_debugprotectederror(lua_State *L);

	static bool ar_to_si(lua_Debug &p_ar, DebugInfo::StackInfo &p_si);
#endif // TOOLS_ENABLED

	bool finalized = false;
	void finalize();

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
	String _validate_path(const String &p_path) const override { return ""; }

	PackedStringArray _get_comment_delimiters() const override;
	PackedStringArray _get_string_delimiters() const override;

	bool _supports_builtin_mode() const override;
	bool _can_inherit_from_file() const override;

	/* ... */
	Object *_create_script() const override;

	bool _is_using_templates() override { return true; }
	TypedArray<Dictionary> _get_built_in_templates(const StringName &p_object) const override;
	Ref<Script> _make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const override;

	void _add_global_constant(const StringName &p_name, const Variant &p_value) override;
	void _add_named_global_constant(const StringName &p_name, const Variant &p_value) override;
	void _remove_named_global_constant(const StringName &p_name) override;

	void _frame() override;

	/* EDITOR */
	void _reload_all_scripts() override;
	void _reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) override;

	bool _handles_global_class_type(const String &p_type) const override;
	Dictionary _get_global_class_name(const String &p_path) const override;

	/* DEBUGGER */
#ifdef TOOLS_ENABLED
	void debug_break(const ThreadHandle &L, bool p_is_step = false);
	void set_call_stack(lua_State *L);
	void clear_call_stack();
#endif // TOOLS_ENABLED

	String _debug_get_error() const override;
	int32_t _debug_get_stack_level_count() const override;
	int32_t _debug_get_stack_level_line(int32_t p_level) const override;
	String _debug_get_stack_level_function(int32_t p_level) const override;
	String _debug_get_stack_level_source(int32_t p_level) const override;
	Dictionary _debug_get_stack_level_locals(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) override;
	Dictionary _debug_get_stack_level_members(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) override;
	void *_debug_get_stack_level_instance(int32_t p_level) override;
	Dictionary _debug_get_globals(int32_t p_max_subitems, int32_t p_max_depth) override { return Dictionary(); }
	String _debug_parse_stack_level_expression(int32_t p_level, const String &p_expression, int32_t p_max_subitems, int32_t p_max_depth) override;
	TypedArray<Dictionary> _debug_get_current_stack_info() override;

	/* UNNEEDED */
	void _thread_enter() override {}
	void _thread_exit() override {}

	bool _overrides_external_editor() override { return false; }
	// Error _open_in_external_editor(const Ref<Script> &script, int64_t line, int64_t column);

	/* TO IMPLEMENT */
	Dictionary _validate(const String &p_script, const String &p_path, bool p_validate_functions, bool p_validate_errors, bool p_validate_warnings, bool p_validate_safe_lines) const override {
		Dictionary output;

		output["valid"] = true;

		return output;
	}

	Dictionary _complete_code(const String &p_code, const String &p_path, Object *p_owner) const override { return Dictionary(); }
	Dictionary _lookup_code(const String &p_code, const String &p_symbol, const String &p_path, Object *p_owner) const override { return Dictionary(); }
	String _auto_indent_code(const String &p_code, int32_t p_from_line, int32_t p_to_line) const override { return p_code; }

	int32_t _find_function(const String &p_class_name, const String &p_function_name) const override { return -1; }
	String _make_function(const String &p_class_name, const String &p_function_name, const PackedStringArray &p_function_args) const override { return String(); }

	bool _supports_documentation() const override { return false; }
	TypedArray<Dictionary> _get_public_functions() const override { return TypedArray<Dictionary>(); }
	Dictionary _get_public_constants() const override { return Dictionary(); }
	TypedArray<Dictionary> _get_public_annotations() const override { return TypedArray<Dictionary>(); }

	/*
	// Profiler
	void _profiling_start();
	void _profiling_stop();
	int64_t _profiling_get_accumulated_data(ScriptLanguageExtensionProfilingInfo *info_array, int64_t info_max);
	int64_t _profiling_get_frame_data(ScriptLanguageExtensionProfilingInfo *info_array, int64_t info_max);
	*/

	const HashMap<StringName, Variant> &get_global_constants() const { return global_constants; }
	TaskScheduler &get_task_scheduler() { return task_scheduler; }

#ifdef TOOLS_ENABLED
	List<Ref<LuauScript>> get_scripts() const;
#endif // TOOLS_ENABLED

	LuauLanguage();
	~LuauLanguage();
};
