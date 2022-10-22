#include "luau_script.h"

#include <lua.h>
#include <Luau/Compiler.h>
#include <string>

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
#include <godot_cpp/templates/pair.hpp>

#include "luagd_stack.h"
#include "gd_luau.h"
#include "luau_lib.h"

namespace godot
{
    class Object;
    class ScriptLanguage;
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

    lua_State *T = lua_newthread(GDLuau::get_singleton()->get_vm(GDLuau::VM_SCRIPT_LOAD));

    Luau::CompileOptions opts;
    std::string bytecode = Luau::compile(source.utf8().get_data(), opts);

    if (luau_load(T, "=load", bytecode.data(), bytecode.size(), 0) == 0)
    {
        int status = lua_resume(T, nullptr, 0);

        if (status == LUA_YIELD)
        {
            LUAU_LOAD_ERR(1, "Luau Error: Script yielded when loading definition.");

            return ERR_COMPILATION_FAILED;
        }
        else if (status != LUA_OK)
        {
            String err = LuaStackOp<String>::get(T, -1);
            LUAU_LOAD_ERR(1, "Luau Error: " + err);

            return ERR_COMPILATION_FAILED;
        }

        GDClassDefinition *def = LuaStackOp<GDClassDefinition>::get_ptr(T, 1);
        if (def == nullptr)
        {
            LUAU_LOAD_ERR(1, "Luau Error: Script did not return a class definition.");

            return ERR_COMPILATION_FAILED;
        }

        definition = *def;
        valid = true;
        return OK;
    }

    String err = LuaStackOp<String>::get(T, -1);
    LUAU_LOAD_ERR(1, "Luau Error: " + err);

    return ERR_COMPILATION_FAILED;
}

ScriptLanguage *LuauScript::_get_language() const
{
    return LuauLanguage::get_singleton();
}

bool LuauScript::_instance_has(Object *p_object) const
{
    MutexLock lock(LuauLanguage::singleton->lock);
    return instances.has(p_object);
}

bool LuauScript::_is_valid() const
{
    return valid;
}

bool LuauScript::_is_tool() const
{
    return definition.is_tool;
}

TypedArray<Dictionary> LuauScript::_get_script_method_list() const
{
    TypedArray<Dictionary> methods;

    for (const KeyValue<StringName, Dictionary> &pair : definition.methods)
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
        properties.push_back(pair.value.property.internal);

    return properties;
}

TypedArray<StringName> LuauScript::_get_members() const
{
    // TODO: 2022-10-08: evil witchery garbage
    // segfault occurs when initializing with TypedArray<StringName> and relying on copy.
    // conversion works fine. for some reason.
    Array members;

    for (const KeyValue<StringName, GDClassProperty> &pair : definition.properties)
        members.push_back(pair.value.property.internal["name"]);

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

/////////////////////
// SCRIPT INSTANCE //
/////////////////////

const GDNativeExtensionScriptInstanceInfo LuauScriptInstance::INSTANCE_INFO = {
    nullptr, // set
    nullptr, // get

    nullptr, // get_property_list
    nullptr, // free_property_list
    nullptr, // get_property_type

    nullptr, // get_owner

    nullptr, // get_property_state

    nullptr, // get_method_list
    nullptr, // free_method_list
    nullptr, // has_method

    nullptr, // call
    nullptr, // notification

    nullptr, // to_string

    nullptr, // refcount_incremented
    nullptr, // refcount_decremented

    nullptr, // get_script

    /* Overriden for PlaceHolderScriptInstance only */
    nullptr, // is_placeholder
    nullptr, // set_fallback
    nullptr, // get_fallback

    nullptr, // get_language
    nullptr  // free
};

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
