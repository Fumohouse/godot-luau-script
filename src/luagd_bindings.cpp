#include "luagd_bindings.h"

#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/string.hpp>
#include <lua.h>
#include <lualib.h>

#include "extension_api.h"
#include "luagd_variant.h"
#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

namespace godot
{
    class Object;
}

/////////////
// Generic //
/////////////

static bool variant_types_compatible(Variant::Type t1, Variant::Type t2)
{
    return t1 == t2 ||
           (t1 == Variant::FLOAT && t2 == Variant::INT) ||
           (t1 == Variant::INT && t2 == Variant::FLOAT);
}

static void check_variant(lua_State *L, int idx, const Variant &val, GDExtensionVariantType p_expected_type)
{
    Variant::Type expected_type = (Variant::Type)p_expected_type;

    if (expected_type != Variant::NIL && !variant_types_compatible(val.get_type(), expected_type))
        luaL_typeerrorL(L, idx, Variant::get_type_name(expected_type).utf8().get_data());
}

static void push_enum(lua_State *L, const ApiEnum &p_enum) // notation cause reserved keyword
{
    lua_createtable(L, 0, p_enum.values.size());

    for (const Pair<String, int32_t> &value : p_enum.values)
    {
        lua_pushinteger(L, value.second);
        lua_setfield(L, -2, value.first.utf8().get_data());
    }

    lua_setreadonly(L, -1, true);
}

//////////////
// Builtins //
//////////////

static int luaGD_builtin_ctor(lua_State *L)
{
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);
    const char *error_string = lua_tostring(L, lua_upvalueindex(2));

    // remove first argument to __call (global table)
    lua_remove(L, 1);

    int nargs = lua_gettop(L);

    Vector<LuauVariant> args;
    args.resize(nargs);

    Vector<const void *> pargs;
    pargs.resize(nargs);

    for (const ApiVariantConstructor &ctor : builtin_class->constructors)
    {
        if (nargs != ctor.arguments.size())
            continue;

        bool valid = true;

        for (int i = 0; i < nargs; i++)
        {
            GDExtensionVariantType type = ctor.arguments[i].type;

            if (!LuaStackOp<Variant>::is(L, i + 1) ||
                !variant_types_compatible(LuaStackOp<Variant>::get(L, i + 1).get_type(), (Variant::Type)type))
            {
                valid = false;
                break;
            }

            LuauVariant arg;
            arg.lua_check(L, i + 1, type);

            args.set(i, arg);
            pargs.set(i, args[i].get_opaque_pointer());
        }

        if (!valid)
            continue;

        LuauVariant ret;
        ret.initialize(builtin_class->type);

        ctor.func(ret.get_opaque_pointer(), pargs.ptr());

        ret.lua_push(L);
        return 1;
    }

    luaL_error(L, "%s", error_string);
}

static int luaGD_builtin_newindex(lua_State *L)
{
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);

    LuauVariant self;
    self.lua_check(L, 1, builtin_class->type);

    Variant key = LuaStackOp<Variant>::check(L, 2);

    if (builtin_class->indexed_setter != nullptr && key.get_type() == Variant::INT)
    {
        // Indexed
        LuauVariant val;
        val.lua_check(L, 3, builtin_class->indexing_return_type);

        // lua is 1 indexed :))))
        builtin_class->indexed_setter(self.get_opaque_pointer(), key.operator int64_t() - 1, val.get_opaque_pointer());
        return 0;
    }
    else if (builtin_class->keyed_setter != nullptr)
    {
        // Keyed
        // if the key or val is ever assumed to not be Variant, this will segfault. nice.
        Variant val = LuaStackOp<Variant>::check(L, 3);
        builtin_class->keyed_setter(self.get_opaque_pointer(), &key, &val);
        return 0;
    }

    // All other set operations are invalid
    if (builtin_class->members.has(key.operator String()))
        luaL_error(L, "type '%s' is read-only", builtin_class->name.utf8().get_data());
    else
        luaL_error(L, "%s is not a valid member of '%s'",
                   key.operator String().utf8().get_data(),
                   builtin_class->name.utf8().get_data());
}

static int luaGD_builtin_index(lua_State *L)
{
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);

    LuauVariant self;
    self.lua_check(L, 1, builtin_class->type);

    Variant key = LuaStackOp<Variant>::check(L, 2);

    if (builtin_class->indexed_getter != nullptr && key.get_type() == Variant::INT)
    {
        // Indexed
        LuauVariant ret;
        ret.initialize(builtin_class->indexing_return_type);

        // lua is 1 indexed :))))
        builtin_class->indexed_getter(self.get_opaque_pointer(), key.operator int64_t() - 1, ret.get_opaque_pointer());

        ret.lua_push(L);
        return 1;
    }
    else if (key.get_type() == Variant::STRING)
    {
        // Members
        String name = key.operator String();

        if (builtin_class->members.has(name))
        {
            const ApiVariantMember &member = builtin_class->members.get(name);

            LuauVariant ret;
            ret.initialize(member.type);

            member.getter(self.get_opaque_pointer(), ret.get_opaque_pointer());

            ret.lua_push(L);
            return 1;
        }
    }

    // Keyed
    if (builtin_class->keyed_getter != nullptr)
    {
        Variant self_var = LuaStackOp<Variant>::check(L, 1);

        // misleading types: keyed_checker expects the type pointer, not a variant
        if (builtin_class->keyed_checker(self.get_opaque_pointer(), &key))
        {
            Variant ret;
            // this is sketchy. if key or ret is ever assumed by Godot to not be Variant, this will segfault. Cool!
            // ! see: core/variant/variant_setget.cpp
            builtin_class->keyed_getter(self.get_opaque_pointer(), &key, &ret);

            LuaStackOp<Variant>::push(L, ret);
            return 1;
        }
    }

    luaL_error(L, "%s is not a valid member of '%s'",
               key.operator String().utf8().get_data(),
               builtin_class->name.utf8().get_data());
}

static int call_builtin_method(lua_State *L, const ApiBuiltinClass &builtin_class, const ApiVariantMethod &method)
{
    // arg 1 is self for instance methods
    int arg_offset = method.is_static ? 0 : 1;
    int nargs = lua_gettop(L) - arg_offset;

    Vector<Variant> varargs;
    Vector<LuauVariant> args;

    Vector<const void *> pargs;

    if (method.arguments.size() > nargs)
        pargs.resize(method.arguments.size());
    else
        pargs.resize(nargs);

    if (method.is_vararg)
    {
        varargs.resize(nargs);

        for (int i = 0; i < nargs; i++)
        {
            Variant arg = LuaStackOp<Variant>::check(L, i + 1 + arg_offset);
            if (i < method.arguments.size())
                check_variant(L, i + 1 + arg_offset, arg, method.arguments[i].type);

            varargs.set(i, arg);
            pargs.set(i, &varargs[i]);
        }
    }
    else
    {
        args.resize(nargs);

        if (nargs > method.arguments.size())
            luaL_error(L, "too many arguments to '%s' (expected at most %d)", method.name.utf8().get_data(), method.arguments.size());

        for (int i = 0; i < nargs; i++)
        {
            LuauVariant arg;
            arg.lua_check(L, i + 1 + arg_offset, method.arguments[i].type);

            args.set(i, arg);
            pargs.set(i, args[i].get_opaque_pointer());
        }
    }

    if (nargs < method.arguments.size())
    {
        // Defaults
        for (int i = nargs; i < method.arguments.size(); i++)
        {
            const ApiArgument &api_arg = method.arguments[i];

            if (api_arg.has_default_value)
            {
                pargs.set(i, api_arg.default_value.get_opaque_pointer());
            }
            else
            {
                LuauVariant dummy;
                dummy.lua_check(L, i + 1 + arg_offset, api_arg.type);
            }
        }
    }

    if (method.is_vararg)
    {
        Variant ret;

        if (method.is_static)
        {
            internal::gde_interface->variant_call_static(builtin_class.type, &method.gd_name, pargs.ptr(), pargs.size(), &ret, nullptr);
        }
        else
        {
            Variant self = LuaStackOp<Variant>::check(L, 1);
            internal::gde_interface->variant_call(&self, &method.gd_name, pargs.ptr(), pargs.size(), &ret, nullptr);

            // HACK: since the value in self is copied,
            // it's necessary to manually assign the changed value back to Luau
            if (!method.is_const)
            {
                LuauVariant lua_self;
                lua_self.lua_check(L, 1, builtin_class.type);
                lua_self.assign_variant(self);
            }
        }

        if (method.return_type != GDEXTENSION_VARIANT_TYPE_NIL)
        {
            LuaStackOp<Variant>::push(L, ret);
            return 1;
        }

        return 0;
    }
    else
    {
        LuauVariant self;
        void *self_ptr;

        if (method.is_static)
        {
            self_ptr = nullptr;
        }
        else
        {
            self.lua_check(L, 1, builtin_class.type);
            self_ptr = self.get_opaque_pointer();
        }

        if (method.return_type != -1)
        {
            LuauVariant ret;
            ret.initialize((GDExtensionVariantType)method.return_type);

            method.func(self_ptr, pargs.ptr(), ret.get_opaque_pointer(), pargs.size());

            ret.lua_push(L);
            return 1;
        }
        else
        {
            method.func(self_ptr, pargs.ptr(), nullptr, pargs.size());
            return 0;
        }
    }
}

static int luaGD_builtin_method(lua_State *L)
{
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);
    const ApiVariantMethod *method = luaGD_lightudataup<ApiVariantMethod>(L, 2);

    return call_builtin_method(L, *builtin_class, *method);
}

static void push_builtin_method(lua_State *L, const ApiBuiltinClass &builtin_class, const ApiVariantMethod &method)
{
    lua_pushlightuserdata(L, (void *)&builtin_class);
    lua_pushlightuserdata(L, (void *)&method);
    lua_pushcclosure(L, luaGD_builtin_method, method.debug_name, 2);
}

static int luaGD_builtin_namecall(lua_State *L)
{
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);

    if (const char *name = lua_namecallatom(L, nullptr))
    {
        if (builtin_class->methods.has(name))
            return call_builtin_method(L, *builtin_class, builtin_class->methods.get(name));

        luaL_error(L, "'%s' is not a valid method of '%s'", name, builtin_class->name.utf8().get_data());
    }

    luaL_error(L, "no namecallatom");
}

static int luaGD_builtin_operator(lua_State *L)
{
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);
    GDExtensionVariantOperator var_op = (GDExtensionVariantOperator)lua_tointeger(L, lua_upvalueindex(2));

    LuauVariant self;
    self.lua_check(L, 1, builtin_class->type);

    LuauVariant right;
    void *right_ptr;

    for (const ApiVariantOperator &op : builtin_class->operators.get(var_op))
    {
        if (op.right_type == GDEXTENSION_VARIANT_TYPE_NIL)
        {
            right_ptr = nullptr;
        }
        else if (LuaStackOp<Variant>::is(L, 2) && variant_types_compatible(LuaStackOp<Variant>::get(L, 2).get_type(), (Variant::Type)op.right_type))
        {
            right.lua_check(L, 2, op.right_type);
            right_ptr = right.get_opaque_pointer();
        }
        else
        {
            continue;
        }

        LuauVariant ret;
        ret.initialize(op.return_type);

        op.eval(self.get_opaque_pointer(), right_ptr, ret.get_opaque_pointer());

        ret.lua_push(L);
        return 1;
    }

    // TODO: say something better here
    luaL_error(L, "no operator matched");
}

void luaGD_openbuiltins(lua_State *L)
{
    LUAGD_LOAD_GUARD(L, "_gdBuiltinsLoaded");

    const ExtensionApi &extension_api = get_extension_api();

    for (const ApiBuiltinClass &builtin_class : extension_api.builtin_classes)
    {
        luaGD_newlib(L, builtin_class.name.utf8().get_data(), builtin_class.metatable_name);

        // Enums
        for (const ApiEnum &class_enum : builtin_class.enums)
        {
            push_enum(L, class_enum);
            lua_setfield(L, -3, class_enum.name.utf8().get_data());
        }

        // Constants
        for (const ApiVariantConstant &constant : builtin_class.constants)
        {
            LuaStackOp<Variant>::push(L, constant.value);
            lua_setfield(L, -3, constant.name.utf8().get_data());
        }

        // Constructors (global __call)
        lua_pushlightuserdata(L, (void *)&builtin_class);
        lua_pushstring(L, builtin_class.constructor_error_string);
        lua_pushcclosure(L, luaGD_builtin_ctor, builtin_class.constructor_debug_name, 2);
        lua_setfield(L, -2, "__call");

        // Members (__newindex, __index)
        lua_pushlightuserdata(L, (void *)&builtin_class);
        lua_pushcclosure(L, luaGD_builtin_newindex, builtin_class.newindex_debug_name, 1);
        lua_setfield(L, -4, "__newindex");

        lua_pushlightuserdata(L, (void *)&builtin_class);
        lua_pushcclosure(L, luaGD_builtin_index, builtin_class.index_debug_name, 1);
        lua_setfield(L, -4, "__index");

        // Methods (__namecall)
        lua_pushlightuserdata(L, (void *)&builtin_class);
        lua_pushcclosure(L, luaGD_builtin_namecall, builtin_class.namecall_debug_name, 1);
        lua_setfield(L, -4, "__namecall");

        // All methods (global table)
        for (const KeyValue<String, ApiVariantMethod> &pair : builtin_class.methods)
        {
            push_builtin_method(L, builtin_class, pair.value);
            lua_setfield(L, -3, pair.value.name.utf8().get_data());
        }

        for (const ApiVariantMethod &static_method : builtin_class.static_methods)
        {
            push_builtin_method(L, builtin_class, static_method);
            lua_setfield(L, -3, static_method.name.utf8().get_data());
        }

        // Operators (misc metatable)
        for (const KeyValue<GDExtensionVariantOperator, Vector<ApiVariantOperator>> &pair : builtin_class.operators)
        {
            lua_pushlightuserdata(L, (void *)&builtin_class);
            lua_pushinteger(L, pair.key);
            lua_pushcclosure(L, luaGD_builtin_operator, builtin_class.operator_debug_names[pair.key], 2);

            const char *op_mt_name;
            switch (pair.key)
            {
            case GDEXTENSION_VARIANT_OP_EQUAL:
                op_mt_name = "__eq";
                break;
            case GDEXTENSION_VARIANT_OP_LESS:
                op_mt_name = "__lt";
                break;
            case GDEXTENSION_VARIANT_OP_LESS_EQUAL:
                op_mt_name = "__le";
                break;
            case GDEXTENSION_VARIANT_OP_ADD:
                op_mt_name = "__add";
                break;
            case GDEXTENSION_VARIANT_OP_SUBTRACT:
                op_mt_name = "__sub";
                break;
            case GDEXTENSION_VARIANT_OP_MULTIPLY:
                op_mt_name = "__mul";
                break;
            case GDEXTENSION_VARIANT_OP_DIVIDE:
                op_mt_name = "__div";
                break;
            case GDEXTENSION_VARIANT_OP_MODULE:
                op_mt_name = "__mod";
                break;
            case GDEXTENSION_VARIANT_OP_NEGATE:
                op_mt_name = "__unm";
                break;

            // Special (non-Godot) operators
            case GDEXTENSION_VARIANT_OP_MAX:
                op_mt_name = "__len";
                break;

            default:
                ERR_FAIL_MSG("variant operator not handled");
            }

            lua_setfield(L, -4, op_mt_name);
        }

        luaGD_poplib(L, false);
    }
}

/////////////
// Globals //
/////////////

static int luaGD_utility_function(lua_State *L)
{
    const ApiUtilityFunction *func = luaGD_lightudataup<ApiUtilityFunction>(L, 1);

    int nargs = lua_gettop(L);

    Vector<Variant> varargs;
    Vector<LuauVariant> args;

    Vector<const void *> pargs;
    pargs.resize(nargs);

    if (func->is_vararg)
    {
        varargs.resize(nargs);

        for (int i = 0; i < nargs; i++)
        {
            Variant arg = LuaStackOp<Variant>::check(L, i + 1);
            if (i < func->arguments.size())
                check_variant(L, i + 1, arg, func->arguments[i].type);

            varargs.set(i, arg);
            pargs.set(i, &varargs[i]);
        }
    }
    else
    {
        args.resize(nargs);

        for (int i = 0; i < nargs; i++)
        {
            LuauVariant arg;
            arg.lua_check(L, i + 1, func->arguments[i].type);
            args.set(i, arg);
            pargs.set(i, args[i].get_opaque_pointer());
        }
    }

    LuauVariant ret;
    ret.initialize(func->return_type);

    func->func(ret.get_opaque_pointer(), pargs.ptr(), nargs);

    if (func->return_type == GDEXTENSION_VARIANT_TYPE_NIL)
    {
        return 0;
    }
    else
    {
        ret.lua_push(L);
        return 1;
    }
}

void luaGD_openglobals(lua_State *L)
{
    LUAGD_LOAD_GUARD(L, "_gdGlobalsLoaded")

    const ExtensionApi &api = get_extension_api();

    // Enum
    lua_createtable(L, 0, api.global_enums.size());

    for (const ApiEnum &global_enum : api.global_enums)
    {
        push_enum(L, global_enum);
        lua_setfield(L, -2, global_enum.name.utf8().get_data());
    }

    lua_setreadonly(L, -1, true);
    lua_setglobal(L, "Enum");

    // Constants
    // does this work? idk
    lua_createtable(L, 0, api.global_constants.size());

    for (const ApiConstant &global_constant : api.global_constants)
    {
        lua_pushinteger(L, global_constant.value);
        lua_setfield(L, -2, global_constant.name.utf8().get_data());
    }

    lua_setreadonly(L, -1, true);
    lua_setglobal(L, "Constants");

    // Utility functions
    for (const ApiUtilityFunction &utility_function : api.utility_functions)
    {
        lua_pushlightuserdata(L, (void *)&utility_function);
        lua_pushcclosure(L, luaGD_utility_function, utility_function.debug_name, 1);
        lua_setglobal(L, utility_function.name.utf8().get_data());
    }
}

static int luaGD_variant_tostring(lua_State *L)
{
    // Special case - freed objects
    if (LuaStackOp<Object *>::is(L, 1) && LuaStackOp<Object *>::get(L, 1) == nullptr)
        lua_pushstring(L, "<Freed Object>");
    else
    {
        Variant v = LuaStackOp<Variant>::check(L, 1);
        String str = v.stringify();
        lua_pushstring(L, str.utf8().get_data());
    }

    return 1;
}

void luaGD_newlib(lua_State *L, const char *global_name, const char *mt_name)
{
    luaL_newmetatable(L, mt_name); // instance metatable
    lua_newtable(L);               // global table
    lua_createtable(L, 0, 3);      // global metatable - assume 3 fields: __fortype, __call, __index

    lua_pushstring(L, mt_name);
    lua_setfield(L, -2, "__fortype");

    // for typeof and type errors
    lua_pushstring(L, global_name);
    lua_setfield(L, -4, "__type");

    lua_pushcfunction(L, luaGD_variant_tostring, "Variant.__tostring");
    lua_setfield(L, -4, "__tostring");

    // set global table's metatable
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -3);

    lua_pushvalue(L, -2);
    lua_setglobal(L, global_name);
}

void luaGD_poplib(lua_State *L, bool is_obj)
{
    if (is_obj)
    {
        lua_pushboolean(L, true);
        lua_setfield(L, -4, "__isgdobj");
    }

    // global will be set readonly on sandbox
    lua_setreadonly(L, -3, true); // instance metatable
    lua_setreadonly(L, -1, true); // global metatable

    lua_pop(L, 3);
}
