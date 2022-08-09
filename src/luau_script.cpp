#include "luau_script.h"

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

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

void LuauLanguage::finalize()
{
    if (finalized)
        return;

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
    return "luau";
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

Object *LuauLanguage::_create_script() const
{
    return memnew(LuauScript);
}
