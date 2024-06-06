#pragma once

#define luaGD_objnullerror(L, p_i) luaL_error(L, "argument #%d: Object is null or freed", p_i)
#define luaGD_nonamecallatomerror(L) luaL_error(L, "no namecallatom")
#define luaGD_mtnotfounderror(L, p_name) luaL_error(L, "metatable not found: '%s'", p_name)

#define luaGD_indexerror(L, p_key, p_of) luaL_error(L, "'%s' is not a valid member of %s", p_key, p_of)
#define luaGD_nomethoderror(L, p_key, p_of) luaL_error(L, "'%s' is not a valid method of %s", p_key, p_of)
#define luaGD_valueerror(L, p_key, p_got, p_expected) luaL_error(L, "invalid type for value of key %s: got %s, expected %s", p_key, p_got, p_expected)

#define luaGD_readonlyerror(L, p_type) luaL_error(L, "type '%s' is read-only", p_type)
#define luaGD_propreadonlyerror(L, p_prop) luaL_error(L, "property '%s' is read-only", p_prop)
#define luaGD_propwriteonlyerror(L, p_prop) luaL_error(L, "property '%s' is write-only", p_prop)
