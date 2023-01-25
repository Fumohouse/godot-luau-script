#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/builtin_types.hpp>

using namespace godot;

struct lua_State;

// mirrors Variant + VariantInternal.
// see README.md for Godot Engine license

#define DATA_SIZE sizeof(real_t) * 4

class LuauVariant
{
private:
    int32_t type;

    // forces the value to refer to a Luau userdata pointer
    // limits lifetime based on Luau GC (this type should be temporary anyway)
    bool from_luau;

public:
    // ! buffer is public for reducing jank. don't touch.
    union {
        void *_ptr;
        uint8_t _opaque[DATA_SIZE];
    } _data;

    static void _register_types();

    /* Pointer getter */
    template <typename T>
    const T *get_ptr() const {
        return (const T *)get_opaque_pointer();
    }

    template <typename T>
    T *get_ptr() {
        return (T *)get_opaque_pointer();
    }

    void *get_opaque_pointer_arg();
    void *get_opaque_pointer();
    const void *get_opaque_pointer() const;

    /* Member getter */
    _FORCE_INLINE_ int32_t get_type() const { return type; }
    _FORCE_INLINE_ bool is_from_luau() const { return from_luau; }

    /* (De)Initialization */
    void initialize(GDExtensionVariantType init_type);
    void clear();

    /* Stack */
    static bool lua_is(
            lua_State *L, int idx,
            GDExtensionVariantType required_type,
            const String &type_name = "");
    void lua_check(
            lua_State *L, int idx,
            GDExtensionVariantType required_type,
            const String &type_name = "");
    void lua_push(lua_State *L) const;

    /* To/from Variant */
    void assign_variant(const Variant &val);
    Variant to_variant();

    /* Constructor */
    _FORCE_INLINE_ LuauVariant() :
            type(-1), from_luau(false) {}
    LuauVariant(const LuauVariant &from);
    LuauVariant &operator=(const LuauVariant &from);
    ~LuauVariant();

private:
    static void copy_variant(LuauVariant &to, const LuauVariant &from);
};
