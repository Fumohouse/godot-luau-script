#pragma once

#include <lua.h>
#include <exception>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/method_ptrcall.hpp> // TODO: unused. required to prevent compile error when specializing PtrToArg.
#include <godot_cpp/core/type_info.hpp>
#include <type_traits>
#include <utility>

#include "luagd_permissions.h"
#include "luagd_stack.h"

// Concept similar to Godot's ClassDB implementation
// (https://github.com/godotengine/godot/blob/master/core/variant/binder_common.h)

class LuaGDClassBinder {
    template <typename P>
    static P check_arg(lua_State *L, int &p_idx) {
        return LuaStackOp<P>::check(L, p_idx++);
    }

    template <>
    lua_State *check_arg<lua_State *>(lua_State *L, int &p_idx) {
        return L;
    }

    // https://stackoverflow.com/a/48368508
    template <typename F>
    static lua_CFunction cify_lambda(F &&p_func) {
        // std::forward ensures no copy is made (or something)
        // https://en.cppreference.com/w/cpp/utility/forward
        static F func = std::forward<F>(p_func);
        static bool did_init = false;

        did_init = true;

        return [](lua_State *L) -> int {
            return func(L);
        };
    }

public:
    // Prevent two functions/methods with the same signature from interfering
    // with each other. Can't think of a better way to do it, so this is
    // probably ok.
    template <auto F, typename TF = decltype(F)>
    struct FunctionId {};

    template <auto F, typename R, typename... P>
    static lua_CFunction bind_method_static(FunctionId<F, R (*)(P...)>, const char *p_name, BitField<ThreadPermissions> p_perms = PERMISSION_BASE) {
        return cify_lambda([=](lua_State *L) -> int {
            luaGD_checkpermissions(L, p_name, p_perms);

            try {
                int stack_idx = 1;
                if constexpr (std::is_same<R, void>()) {
                    F(check_arg<P>(L, stack_idx)...);
                    return 0;
                } else {
                    LuaStackOp<R>::push(L, F(check_arg<P>(L, stack_idx)...));
                    return 1;
                }
            } catch (std::exception &e) {
                luaL_error(L, "%s", e.what());
            }
        });
    }

    template <auto F, typename T, typename R, typename... P>
    static lua_CFunction bind_method(FunctionId<F, R (T::*)(P...)>, const char *p_name, BitField<ThreadPermissions> p_perms = PERMISSION_BASE) {
        return cify_lambda([=](lua_State *L) -> int {
            luaGD_checkpermissions(L, p_name, p_perms);

            T *self = LuaStackOp<T>::check_ptr(L, 1);

            try {
                int stack_idx = 2;
                if constexpr (std::is_same<R, void>()) {
                    (self->*F)(check_arg<P>(L, stack_idx)...);
                    return 0;
                } else {
                    LuaStackOp<R>::push(L, (self->*F)(check_arg<P>(L, stack_idx)...));
                    return 1;
                }
            } catch (std::exception &e) {
                luaL_error(L, "%s", e.what());
            }
        });
    }
};

#define FID(m_func) LuaGDClassBinder::FunctionId<m_func>()
