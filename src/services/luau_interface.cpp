#include "luau_interface.h"

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/variant/string.hpp>

#include "luagd_binder.h"
#include "luagd_stack.h"
#include "services/debug_service.h"
#include "services/sandbox_service.h"

/* Service */

void Service::register_metatables(lua_State *L) {
    get_lua_class().init_metatable(L);
}

/* LuauInterface */

#define LUAU_INTERFACE_NAME "LuauInterface"
#define LUAU_INTERFACE_MT_NAME "Luau." LUAU_INTERFACE_NAME
SVC_STACK_OP_IMPL(LuauInterface, LUAU_INTERFACE_MT_NAME)

LuauInterface *LuauInterface::singleton = nullptr;

const LuaGDClass &LuauInterface::get_lua_class() const {
    static LuaGDClass lua_class;
    static bool did_init = false;

    if (!did_init) {
        lua_class.set_name(LUAU_INTERFACE_NAME, LUAU_INTERFACE_MT_NAME);
        lua_class.set_index_override(index_override);

        did_init = true;
    }

    return lua_class;
}

void LuauInterface::lua_push(lua_State *L) {
    LuaStackOp<LuauInterface *>::push(L, this);
}

void LuauInterface::register_metatables(lua_State *L) {
    Service::register_metatables(L);

    for (const KeyValue<String, Service *> &E : services) {
        E.value->register_metatables(L);
    }
}

int LuauInterface::index_override(lua_State *L, const char *p_name) {
    HashMap<String, Service *>::Iterator E = LuauInterface::get_singleton()->services.find(p_name);
    if (E) {
        E->value->lua_push(L);
        return 1;
    }

    return 0;
}

void LuauInterface::register_service(Service *p_svc) {
    services.insert(p_svc->get_name(), p_svc);
}

void LuauInterface::init_services() {
    register_service(memnew(SandboxService));
    register_service(memnew(DebugService));
}

void LuauInterface::deinit_services() {
    for (const KeyValue<String, Service *> &E : services) {
        memdelete(E.value);
    }
}

LuauInterface::LuauInterface() {
    if (!singleton) {
        singleton = this;
    }

    init_services();
}

LuauInterface::~LuauInterface() {
    if (singleton == this) {
        singleton = nullptr;
    }

    deinit_services();
}
