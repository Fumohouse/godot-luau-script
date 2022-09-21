#include "luau_script.h"

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/file.hpp>

#include "luau.h"

using namespace godot;

////////////
// SCRIPT //
////////////

void LuauScript::_bind_methods()
{
}

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

ScriptLanguage *LuauScript::_get_language() const
{
    return LuauLanguage::get_singleton();
}

Error LuauScript::load_source_code(const String &p_path)
{
    Ref<File> file = memnew(File);
    Error err = file->open(p_path, File::ModeFlags::READ);

    ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to read file: '" + p_path + "'.");

    PackedByteArray bytes = file->get_buffer(file->get_length());

    String src;
    src.parse_utf8(reinterpret_cast<const char *>(bytes.ptr()));

    set_source_code(src);

    return OK;
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
    luau = memnew(Luau);
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
    extensions.push_back("luau");

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

// TODO: PtrToArg compile error.
/*
Error LuauLanguage::_execute_file(const String &p_path)
{
    // Unused by Godot; purpose unclear
    return OK;
}
*/

bool LuauLanguage::_has_named_classes() const
{
    // not true for any of Godot's built in languages. why
    return false;
}

void LuauLanguage::_bind_methods()
{
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

    if (Object::cast_to<LuauScript>(const_cast<Resource *>(p_resource.ptr())))
        extensions.push_back("lua");

    return extensions;
}

bool ResourceFormatSaverLuauScript::_recognize(const Ref<Resource> &p_resource) const
{
    return Object::cast_to<LuauScript>(const_cast<Resource *>(p_resource.ptr())) != nullptr;
}

int64_t ResourceFormatSaverLuauScript::_save(const Ref<Resource> &p_resource, const String &p_path, int64_t p_flags)
{
    Ref<LuauScript> script = p_resource;
    ERR_FAIL_COND_V(script.is_null(), ERR_INVALID_PARAMETER);

    String source = script->get_source_code();

    {
        Ref<File> file = memnew(File);
        Error err = file->open(p_path, File::ModeFlags::WRITE);
        ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot save Luau script file '" + p_path + "'.");

        file->store_string(source);

        if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF)
            return ERR_CANT_CREATE;
    }

    // TODO: Godot's default language implementations have a check here. It isn't possible in extensions (yet).
    // if (ScriptServer::is_reload_scripts_on_save_enabled())
    LuauLanguage::get_singleton()->_reload_tool_script(p_resource, false);

    return OK;
}
