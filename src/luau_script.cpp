#include "luau_script.h"

#include <lua.h>
#include <Luau/Compiler.h>
#include <string>
#include <string.h>

#include <godot/gdnative_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/templates/pair.hpp>

#include "luagd_stack.h"
#include "gd_luau.h"
#include "luau_lib.h"

namespace godot
{
    class Object;
}

using namespace godot;

////////////
// SCRIPT //
////////////

bool LuauScript::_has_source_code() const
{
    return !source.is_empty();
}

String LuauScript::_get_source_code() const
{
    return source;
}

void LuauScript::_set_source_code(const String &p_code)
{
    source = p_code;
}

Error LuauScript::load_source_code(const String &p_path)
{
    Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::ModeFlags::READ);
    ERR_FAIL_COND_V_MSG(file.is_null(), FileAccess::get_open_error(), "Failed to read file: '" + p_path + "'.");

    PackedByteArray bytes = file->get_buffer(file->get_length());

    String src;
    src.parse_utf8(reinterpret_cast<const char *>(bytes.ptr()));

    set_source_code(src);

    return OK;
}

#define LUAU_LOAD_ERR(line, msg) _err_print_error("LuauScript::_reload", get_path().is_empty() ? "built-in" : get_path().utf8().get_data(), line, msg);
#define LUAU_LOAD_YIELD_MSG "Luau Error: Script yielded when loading definition."
#define LUAU_LOAD_NO_DEF_MSG "Luau Error: Script did not return a class definition."

#define LUAU_LOAD_RESUME                             \
    int status = lua_resume(T, nullptr, 0);          \
                                                     \
    if (status == LUA_YIELD)                         \
    {                                                \
        LUAU_LOAD_ERR(1, LUAU_LOAD_YIELD_MSG);       \
        return ERR_COMPILATION_FAILED;               \
    }                                                \
    else if (status != LUA_OK)                       \
    {                                                \
        String err = LuaStackOp<String>::get(T, -1); \
        LUAU_LOAD_ERR(1, "Luau Error: " + err);      \
                                                     \
        return ERR_COMPILATION_FAILED;               \
    }

static Error try_load(lua_State *L, const char *src)
{
    Luau::CompileOptions opts;
    std::string bytecode = Luau::compile(src, opts);

    return luau_load(L, "=load", bytecode.data(), bytecode.size(), 0) == 0 ? OK : ERR_COMPILATION_FAILED;
}

Error LuauScript::_reload(bool p_keep_state)
{
    bool has_instances;

    {
        MutexLock lock(LuauLanguage::singleton->lock);
        has_instances = instances.size();
    }

    ERR_FAIL_COND_V(!p_keep_state && has_instances, ERR_ALREADY_IN_USE);

    valid = false;

    // TODO: error line numbers?

    lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_SCRIPT_LOAD);
    lua_State *T = lua_newthread(L);
    luaL_sandboxthread(T);

    if (try_load(T, source.utf8().get_data()) == OK)
    {
        LUAU_LOAD_RESUME

        GDClassDefinition *def = LuaStackOp<GDClassDefinition>::get_ptr(T, 1);
        if (def == nullptr)
        {
            lua_pop(L, 1); // thread
            LUAU_LOAD_ERR(1, LUAU_LOAD_NO_DEF_MSG);

            return ERR_COMPILATION_FAILED;
        }

        definition = *def;
        valid = true;

        lua_pop(L, 1); // thread

        for (const KeyValue<GDLuau::VMType, GDClassMethods> &pair : methods)
        {
            if (load_methods(pair.key, true) == OK)
                continue;

            valid = false;
            return ERR_COMPILATION_FAILED;
        }

        return OK;
    }

    String err = LuaStackOp<String>::get(T, -1);
    LUAU_LOAD_ERR(1, "Luau Error: " + err);

    lua_pop(L, 1); // thread

    return ERR_COMPILATION_FAILED;
}

Error LuauScript::load_methods(GDLuau::VMType p_vm_type, bool force)
{
    if (!force && methods.has(p_vm_type))
        return ERR_SKIP;

    lua_State *L = GDLuau::get_singleton()->get_vm(p_vm_type);
    lua_State *T = lua_newthread(L);
    luaL_sandboxthread(T);
    luascript_openclasslib(T, true);

    if (try_load(T, source.utf8().get_data()) == OK)
    {
        LUAU_LOAD_RESUME

        GDClassMethods *method_def = LuaStackOp<GDClassMethods>::get_ptr(T, -1);
        if (method_def == nullptr)
        {
            LUAU_LOAD_ERR(1, LUAU_LOAD_NO_DEF_MSG);
            return ERR_COMPILATION_FAILED;
        }

        methods[p_vm_type] = *method_def;

        lua_pop(L, 1); // thread

        return OK;
    }

    String err = LuaStackOp<String>::get(T, -1);
    LUAU_LOAD_ERR(1, "Luau Error: " + err);

    lua_pop(L, 1); // thread

    return ERR_COMPILATION_FAILED;
}

ScriptLanguage *LuauScript::_get_language() const
{
    return LuauLanguage::get_singleton();
}

bool LuauScript::_is_valid() const
{
    return valid;
}

bool LuauScript::_can_instantiate() const
{
    // TODO: built-in scripting languages check if scripting is enabled OR if this is a tool script
    return valid;
}

bool LuauScript::_is_tool() const
{
    return definition.is_tool;
}

TypedArray<Dictionary> LuauScript::_get_script_method_list() const
{
    TypedArray<Dictionary> methods;

    for (const KeyValue<StringName, GDMethod> &pair : definition.methods)
        methods.push_back(pair.value);

    return methods;
}

bool LuauScript::_has_method(const StringName &p_method) const
{
    return definition.methods.has(p_method);
}

Dictionary LuauScript::_get_method_info(const StringName &p_method) const
{
    return definition.methods.get(p_method);
}

TypedArray<Dictionary> LuauScript::_get_script_property_list() const
{
    TypedArray<Dictionary> properties;

    for (const KeyValue<StringName, GDClassProperty> &pair : definition.properties)
        properties.push_back(pair.value.property.operator Dictionary());

    return properties;
}

TypedArray<StringName> LuauScript::_get_members() const
{
    // TODO: 2022-10-08: evil witchery garbage
    // segfault occurs when initializing with TypedArray<StringName> and relying on copy.
    // conversion works fine. for some reason.
    Array members;

    for (const KeyValue<StringName, GDClassProperty> &pair : definition.properties)
        members.push_back(pair.value.property.name);

    return members;
}

bool LuauScript::_has_property_default_value(const StringName &p_property) const
{
    return definition.properties.get(p_property).default_value != Variant();
}

Variant LuauScript::_get_property_default_value(const StringName &p_property) const
{
    return definition.properties.get(p_property).default_value;
}

void *LuauScript::_instance_create(Object *p_for_object) const
{
    // TODO: decide which vm to use
    LuauScriptInstance *internal = memnew(LuauScriptInstance(Ref<Script>(this), p_for_object, GDLuau::VMType::VM_CORE));

    return internal::gdn_interface->script_instance_create(&LuauScriptInstance::INSTANCE_INFO, internal);
}

bool LuauScript::_instance_has(Object *p_object) const
{
    MutexLock lock(LuauLanguage::singleton->lock);
    return instances.has(p_object);
}

LuauScriptInstance *LuauScript::instance_get(Object *p_object) const
{
    MutexLock lock(LuauLanguage::singleton->lock);
    return instances.get(p_object);
}

/////////////////////
// SCRIPT INSTANCE //
/////////////////////

#define INSTANCE_SELF ((LuauScriptInstance *)self)

static GDNativeExtensionScriptInstanceInfo init_script_instance_info()
{
    GDNativeExtensionScriptInstanceInfo info;

    // GDNativeExtensionScriptInstanceSet set_func;
    // GDNativeExtensionScriptInstanceGet get_func;

    info.get_property_list_func = [](void *self, uint32_t *r_count) -> const GDNativePropertyInfo *
    {
        return INSTANCE_SELF->get_property_list(r_count);
    };

    info.free_property_list_func = [](void *self, const GDNativePropertyInfo *p_list)
    {
        INSTANCE_SELF->free_property_list(p_list);
    };

    info.get_property_type_func = [](void *self, const GDNativeStringNamePtr p_name, GDNativeBool *r_is_valid) -> GDNativeVariantType
    {
        return static_cast<GDNativeVariantType>(INSTANCE_SELF->get_property_type(*((const StringName *)p_name), (bool *)r_is_valid));
    };

    // GDNativeExtensionScriptInstancePropertyCanRevert property_can_revert_func;
    // GDNativeExtensionScriptInstancePropertyGetRevert property_get_revert_func;

    info.get_owner_func = [](void *self)
    {
        return INSTANCE_SELF->get_owner()->_owner;
    };

    // GDNativeExtensionScriptInstanceGetPropertyState get_property_state_func;

    info.get_method_list_func = [](void *self, uint32_t *r_count) -> const GDNativeMethodInfo *
    {
        return INSTANCE_SELF->get_method_list(r_count);
    };

    info.free_method_list_func = [](void *self, const GDNativeMethodInfo *p_list)
    {
        INSTANCE_SELF->free_method_list(p_list);
    };

    info.has_method_func = [](void *self, const GDNativeStringNamePtr p_name) -> GDNativeBool
    {
        return INSTANCE_SELF->has_method(*((const StringName *)p_name));
    };

    info.call_func = [](void *self, const GDNativeStringNamePtr p_method, const GDNativeVariantPtr *p_args, const GDNativeInt p_argument_count, GDNativeVariantPtr r_return, GDNativeCallError *r_error)
    {
        return INSTANCE_SELF->call(*((StringName *)p_method), (const Variant *)p_args, p_argument_count, (Variant *)r_return, r_error);
    };

    // GDNativeExtensionScriptInstanceNotification notification_func;

    // GDNativeExtensionScriptInstanceToString to_string_func;

    // GDNativeExtensionScriptInstanceRefCountIncremented refcount_incremented_func;
    // GDNativeExtensionScriptInstanceRefCountDecremented refcount_decremented_func;

    info.get_script_func = [](void *self)
    {
        return INSTANCE_SELF->get_script().ptr()->_owner;
    };

    /* Overriden for PlaceholderScriptInstance only */
    // GDNativeExtensionScriptInstanceIsPlaceholder is_placeholder_func;
    // GDNativeExtensionScriptInstanceSet set_fallback_func;
    // GDNativeExtensionScriptInstanceGet get_fallback_func;

    info.get_language_func = [](void *self)
    {
        return INSTANCE_SELF->get_language()->_owner;
    };

    info.free_func = [](void *self)
    {
        memdelete(INSTANCE_SELF);
    };

    return info;
}

const GDNativeExtensionScriptInstanceInfo LuauScriptInstance::INSTANCE_INFO = init_script_instance_info();

static String *string_alloc(const String &p_str)
{
    String *ptr = memnew(String);
    *ptr = p_str;

    return ptr;
}

static StringName *stringname_alloc(const String &p_str)
{
    StringName *ptr = memnew(StringName);
    *ptr = p_str;

    return ptr;
}

static void copy_prop(const GDProperty &src, GDNativePropertyInfo &dst)
{
    dst.type = src.type;
    dst.name = stringname_alloc(src.name);
    dst.class_name = stringname_alloc(src.class_name);
    dst.hint = src.hint;
    dst.hint_string = string_alloc(src.hint_string);
    dst.usage = src.usage;
}

static void free_prop(const GDNativePropertyInfo &prop)
{
    // smelly
    memdelete((StringName *)prop.name);
    memdelete((StringName *)prop.class_name);
    memdelete((String *)prop.hint_string);
}

GDNativePropertyInfo *LuauScriptInstance::get_property_list(uint32_t *r_count) const
{
    *r_count = script->definition.properties.size();
    GDNativePropertyInfo *list = memnew_arr(GDNativePropertyInfo, *r_count);

    int i = 0;

    for (const KeyValue<StringName, GDClassProperty> &pair : script->definition.properties)
    {
        copy_prop(pair.value.property, list[i]);
        i++;
    }

    return list;
}

void LuauScriptInstance::free_property_list(const GDNativePropertyInfo *p_list) const
{
    // smelly
    int size = script->definition.properties.size();

    for (int i = 0; i < size; i++)
        free_prop(p_list[i]);

    memdelete((GDNativePropertyInfo *)p_list);
}

Variant::Type LuauScriptInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const
{
    if (script->definition.properties.has(p_name))
    {
        if (r_is_valid != nullptr)
            *r_is_valid = true;

        return (Variant::Type)script->definition.properties[p_name].property.type;
    }

    if (r_is_valid != nullptr)
        *r_is_valid = false;

    return Variant::NIL;
}

Object *LuauScriptInstance::get_owner() const
{
    return owner;
}

GDNativeMethodInfo *LuauScriptInstance::get_method_list(uint32_t *r_count) const
{
    *r_count = script->definition.methods.size();
    GDNativeMethodInfo *list = memnew_arr(GDNativeMethodInfo, *r_count);

    int i = 0;

    for (const KeyValue<StringName, GDMethod> pair : script->definition.methods)
    {
        const GDMethod &src = pair.value;
        GDNativeMethodInfo &dst = list[i];

        dst.name = stringname_alloc(src.name);

        copy_prop(src.return_val, dst.return_value);

        dst.flags = src.flags;

        dst.argument_count = src.arguments.size();

        if (dst.argument_count > 0)
        {
            GDNativePropertyInfo *arg_list = memnew_arr(GDNativePropertyInfo, dst.argument_count);

            for (int j = 0; j < dst.argument_count; j++)
                copy_prop(src.arguments[j], arg_list[j]);

            dst.arguments = arg_list;
        }

        dst.default_argument_count = src.default_arguments.size();

        if (dst.default_argument_count > 0)
        {
            Variant *defargs = memnew_arr(Variant, dst.default_argument_count);

            for (int j = 0; j < dst.default_argument_count; j++)
                defargs[j] = src.default_arguments[j];

            dst.default_arguments = (GDNativeVariantPtr *)defargs;
        }

        i++;
    }

    return list;
}

void LuauScriptInstance::free_method_list(const GDNativeMethodInfo *p_list) const
{
    // smelly
    int size = script->definition.methods.size();

    for (int i = 0; i < size; i++)
    {
        const GDNativeMethodInfo &method = p_list[i];

        memdelete((StringName *)method.name);

        free_prop(method.return_value);

        if (method.argument_count > 0)
        {
            for (int i = 0; i < method.argument_count; i++)
                free_prop(method.arguments[i]);

            memdelete(method.arguments);
        }

        if (method.default_argument_count > 0)
            memdelete((Variant *)method.default_arguments);
    }

    memdelete((GDNativeMethodInfo *)p_list);
}

bool LuauScriptInstance::has_method(const StringName &p_name) const
{
    return script->_has_method(p_name);
}

void LuauScriptInstance::call(
    const StringName &p_method,
    const Variant *p_args, const GDNativeInt p_argument_count,
    Variant *r_return, GDNativeCallError *r_error)
{
    if (!has_method(p_method))
    {
        r_error->error = GDNATIVE_CALL_ERROR_INVALID_METHOD;
        return;
    }

    const GDMethod &method = script->definition.methods[p_method];
    lua_State *ET = lua_newthread(T); // execution thread

    // Check argument count
    int args_allowed = method.arguments.size();
    int args_default = method.default_arguments.size();
    int args_required = args_allowed - args_default;

    if (p_argument_count < args_required)
    {
        r_error->error = GDNATIVE_CALL_ERROR_TOO_FEW_ARGUMENTS;
        r_error->argument = args_required;

        lua_pop(T, 1); // thread
        return;
    }

    if (p_argument_count > args_allowed)
    {
        r_error->error = GDNATIVE_CALL_ERROR_TOO_MANY_ARGUMENTS;
        r_error->argument = args_allowed;

        lua_pop(T, 1); // thread
        return;
    }

    // Push method
    int ref = script->methods.get(vm_type).methods[p_method];
    lua_getref(ET, ref);

    // Push arguments
    LuaStackOp<Object *>::push(ET, owner); // self

    for (int i = 0; i < p_argument_count; i++)
    {
        const Variant &arg = p_args[i];

        if ((GDNativeVariantType)arg.get_type() != method.arguments[i].type)
        {
            r_error->error = GDNATIVE_CALL_ERROR_INVALID_ARGUMENT;
            r_error->argument = i;
            r_error->expected = method.arguments[i].type;

            lua_pop(T, 1); // thread
            return;
        }

        LuaStackOp<Variant>::push(ET, arg);
    }

    for (int i = p_argument_count - args_required; i < args_default; i++)
        LuaStackOp<Variant>::push(ET, method.default_arguments[i]);

    // Call
    r_error->error = GDNATIVE_CALL_OK;

    int err = lua_pcall(ET, args_allowed + 1, 1, 0); // args: self + passed + default

    ERR_FAIL_COND_MSG(err != LUA_OK, "Lua Error: " + LuaStackOp<String>::get(ET, -1));

    *r_return = LuaStackOp<Variant>::get(ET, -1);

    lua_pop(T, 1); // thread
}

Ref<Script> LuauScriptInstance::get_script() const
{
    return script;
}

ScriptLanguage *LuauScriptInstance::get_language() const
{
    return LuauLanguage::get_singleton();
}

LuauScriptInstance::LuauScriptInstance(Ref<LuauScript> p_script, Object *p_owner, GDLuau::VMType p_vm_type)
    : script(p_script), owner(p_owner), vm_type(p_vm_type)
{
    // this usually occurs in _instance_create, but that is marked const for ScriptExtension
    {
        MutexLock lock(LuauLanguage::singleton->lock);
        p_script->instances.insert(p_owner, this);
    }

    lua_State *L = GDLuau::get_singleton()->get_vm(p_vm_type);
    T = lua_newthread(L);
    luaL_sandboxthread(T);
    thread_ref = lua_ref(L, -1);

    lua_newtable(T);
    lua_pushvalue(T, -1);
    table_ref = lua_ref(T, -1);

    Error method_err = p_script->load_methods(p_vm_type);
    ERR_FAIL_COND_MSG(method_err != OK && method_err != ERR_SKIP, "Couldn't load script methods for " + p_script->definition.name);

    int init_ref = p_script->methods[p_vm_type].initialize;

    if (init_ref != -1)
    {
        LuaStackOp<Object *>::push(T, p_owner);
        lua_pushvalue(T, -2);
        lua_getref(T, init_ref);

        int status = lua_pcall(T, 2, 0, 0);

        if (status != LUA_OK)
        {
            ERR_PRINT(p_script->definition.name + ":Initialize failed: " + LuaStackOp<String>::get(T, -1));
            lua_pop(T, 1);
        }
    }
}

LuauScriptInstance::~LuauScriptInstance()
{
    if (script.is_valid() && owner != nullptr)
    {
        MutexLock lock(LuauLanguage::singleton->lock);
        script->instances.erase(owner);
    }

    lua_State *L = GDLuau::get_singleton()->get_vm(vm_type);

    lua_unref(L, table_ref);
    table_ref = 0;

    lua_unref(L, thread_ref);
    thread_ref = 0;
}

//////////////
// LANGUAGE //
//////////////

LuauLanguage *LuauLanguage::singleton = nullptr;

LuauLanguage::LuauLanguage()
{
    singleton = this;
}

LuauLanguage::~LuauLanguage()
{
    finalize();
    singleton = nullptr;
}

void LuauLanguage::_init()
{
    luau = memnew(GDLuau);
}

void LuauLanguage::finalize()
{
    if (finalized)
        return;

    if (luau != nullptr)
    {
        memdelete(luau);
        luau = nullptr;
    }

    finalized = true;
}

void LuauLanguage::_finish()
{
    finalize();
}

String LuauLanguage::_get_name() const
{
    return "Luau";
}

String LuauLanguage::_get_type() const
{
    return "LuauScript";
}

String LuauLanguage::_get_extension() const
{
    return "lua";
}

PackedStringArray LuauLanguage::_get_recognized_extensions() const
{
    PackedStringArray extensions;
    extensions.push_back("lua");

    return extensions;
}

PackedStringArray LuauLanguage::_get_reserved_words() const
{
    static const char *_reserved_words[] = {
        "and",
        "break",
        "do",
        "else",
        "elseif",
        "end",
        "false",
        "for",
        "function",
        "if",
        "in",
        "local",
        "nil",
        "not",
        "or",
        "repeat",
        "return",
        "then",
        "true",
        "until",
        "while",
        "continue", // not technically a keyword, but ...
        nullptr};

    PackedStringArray keywords;

    const char **w = _reserved_words;

    while (*w)
    {
        keywords.push_back(*w);
        w++;
    }

    return keywords;
}

bool LuauLanguage::_is_control_flow_keyword(const String &p_keyword) const
{
    return p_keyword == "break" ||
           p_keyword == "else" ||
           p_keyword == "elseif" ||
           p_keyword == "for" ||
           p_keyword == "if" ||
           p_keyword == "repeat" ||
           p_keyword == "return" ||
           p_keyword == "until" ||
           p_keyword == "while";
}

PackedStringArray LuauLanguage::_get_comment_delimiters() const
{
    PackedStringArray delimiters;
    delimiters.push_back("--");
    delimiters.push_back("--[[ ]]");

    return delimiters;
}

PackedStringArray LuauLanguage::_get_string_delimiters() const
{
    PackedStringArray delimiters;
    delimiters.push_back("\" \"");
    delimiters.push_back("' '");
    delimiters.push_back("[[ ]]");

    // TODO: does not include the [=======[ style strings

    return delimiters;
}

bool LuauLanguage::_supports_builtin_mode() const
{
    // don't currently wish to deal with overhead (if any) of supporting this
    // and honestly I don't care for builtin scripts anyway
    return false;
}

Object *LuauLanguage::_create_script() const
{
    return memnew(LuauScript);
}

Ref<Script> LuauLanguage::_make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const
{
    Ref<LuauScript> script;
    script.instantiate();

    // TODO: this should not be necessary. it prevents a segfault when this ref is exchanged to Godot.
    //       tracking https://github.com/godotengine/godot-cpp/issues/652
    //       it will create a memory leak if/when it is time to remove this. lol.
    script->reference();

    // TODO: actual template stuff

    return script;
}

Error LuauLanguage::_execute_file(const String &p_path)
{
    // Unused by Godot; purpose unclear
    return OK;
}

bool LuauLanguage::_has_named_classes() const
{
    // not true for any of Godot's built in languages. why
    return false;
}

//////////////
// RESOURCE //
//////////////

// Loader

PackedStringArray ResourceFormatLoaderLuauScript::_get_recognized_extensions() const
{
    PackedStringArray extensions;
    extensions.push_back("lua");

    return extensions;
}

bool ResourceFormatLoaderLuauScript::_handles_type(const StringName &p_type) const
{
    return p_type == StringName("Script") || p_type == LuauLanguage::get_singleton()->_get_type();
}

String ResourceFormatLoaderLuauScript::_get_resource_type(const String &p_path) const
{
    return p_path.get_extension().to_lower() == "lua" ? LuauLanguage::get_singleton()->_get_type() : "";
}

Variant ResourceFormatLoaderLuauScript::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int64_t p_cache_mode) const
{
    Ref<LuauScript> script = memnew(LuauScript);
    Error err = script->load_source_code(p_path);

    ERR_FAIL_COND_V_MSG(err != OK, Ref<LuauScript>(), "Cannot load Luau script file '" + p_path + "'.");

    script->set_path(p_original_path);
    script->reload();

    return script;
}

// Saver

PackedStringArray ResourceFormatSaverLuauScript::_get_recognized_extensions(const Ref<Resource> &p_resource) const
{
    PackedStringArray extensions;

    Ref<LuauScript> ref = p_resource;
    if (ref.is_valid())
        extensions.push_back("lua");

    return extensions;
}

bool ResourceFormatSaverLuauScript::_recognize(const Ref<Resource> &p_resource) const
{
    Ref<LuauScript> ref = p_resource;
    return ref.is_valid();
}

int64_t ResourceFormatSaverLuauScript::_save(const Ref<Resource> &p_resource, const String &p_path, int64_t p_flags)
{
    Ref<LuauScript> script = p_resource;
    ERR_FAIL_COND_V(script.is_null(), ERR_INVALID_PARAMETER);

    String source = script->get_source_code();

    {
        Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::ModeFlags::WRITE);
        ERR_FAIL_COND_V_MSG(file.is_null(), FileAccess::get_open_error(), "Cannot save Luau script file '" + p_path + "'.");

        file->store_string(source);

        if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF)
            return ERR_CANT_CREATE;
    }

    // TODO: Godot's default language implementations have a check here. It isn't possible in extensions (yet).
    // if (ScriptServer::is_reload_scripts_on_save_enabled())
    LuauLanguage::get_singleton()->_reload_tool_script(p_resource, false);

    return OK;
}
