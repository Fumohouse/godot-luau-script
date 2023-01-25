#include "luagd_variant.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd_bindings_stack.gen.h" // IWYU pragma: keep
#include "luagd_stack.h"

struct VariantMethods {
    virtual void initialize(LuauVariant &self) const = 0;
    virtual void destroy(LuauVariant &self) const {}

    virtual void *get(LuauVariant &self, bool is_arg) const = 0;
    virtual const void *get(const LuauVariant &self) const = 0;

    virtual bool is(lua_State *L, int idx, const String &type_name) const = 0;
    virtual bool check(LuauVariant &self, lua_State *L, int idx, const String &type_name) const = 0;
    virtual void push(const LuauVariant &self, lua_State *L) const = 0;

    virtual void copy(LuauVariant &self, const LuauVariant &other) const = 0;
};

template <typename T>
struct VariantMethodsBase : public VariantMethods {
    virtual bool is(lua_State *L, int idx, const String &type_name) const override {
        return LuaStackOp<T>::is(L, idx);
    }

    virtual void push(const LuauVariant &self, lua_State *L) const override {
        LuaStackOp<T>::push(L, *(const T *)get(self));
    }
};

// Assigns the value directly to _opaque. Destructor and copy constructor are not run.
// Used for basic types (int, bool).
template <typename T>
struct VariantAssignMethods : public VariantMethodsBase<T> {
    static_assert(sizeof(T) <= DATA_SIZE, "this type should not be assigned directly to _opaque");

    virtual void initialize(LuauVariant &self) const override {
        memnew_placement(self._data._opaque, T);
    }

    virtual void *get(LuauVariant &self, bool is_arg) const override {
        return self._data._opaque;
    }

    virtual const void *get(const LuauVariant &self) const override {
        return self._data._opaque;
    }

    virtual bool check(LuauVariant &self, lua_State *L, int idx, const String &type_name) const override {
        initialize(self);
        *(T *)self._data._opaque = LuaStackOp<T>::check(L, idx);
        return false;
    }

    virtual void copy(LuauVariant &self, const LuauVariant &other) const override {
        initialize(self);
        memcpy(self._data._opaque, other._data._opaque, sizeof(T));
    }
};

// Assigns value to _opaque. Runs destructor.
template <typename T>
struct VariantAssignMethodsDtor : public VariantAssignMethods<T> {
    virtual void destroy(LuauVariant &self) const override {
        ((T *)self._data._opaque)->~T();
    }
};

// Assigns value to _ptr when coming from Luau, and to _opaque otherwise.
// Used for any type bound to a userdata.
template <typename T>
struct VariantUserdataMethods : public VariantAssignMethods<T> {
    virtual void *get(LuauVariant &self, bool is_arg) const override {
        if (self.is_from_luau())
            return self._data._ptr;
        else
            return self._data._opaque;
    }

    virtual const void *get(const LuauVariant &self) const override {
        if (self.is_from_luau())
            return self._data._ptr;
        else
            return self._data._opaque;
    }

    virtual bool check(LuauVariant &self, lua_State *L, int idx, const String &type_name) const override {
        self._data._ptr = LuaStackOp<T>::check_ptr(L, idx);
        return true;
    }

    virtual void copy(LuauVariant &self, const LuauVariant &other) const override {
        if (other.is_from_luau()) {
            self._data._ptr = other._data._ptr;
        } else {
            this->initialize(self);
            *(T *)self._data._opaque = *(T *)other._data._opaque;
        }
    }
};

// Assigns value to _ptr when it is a userdata, and to _opaque if it is coerced from a Luau type.
// Used for String and Array types.
template <typename T>
struct VariantCoercedMethods : public VariantUserdataMethods<T> {
    bool check(LuauVariant &self, lua_State *L, int idx, const String &type_name) const override {
        if (T *ptr = LuaStackOp<T>::get_ptr(L, idx)) {
            self._data._ptr = ptr;
            return true;
        }

        this->initialize(self);
        *(T *)self._data._opaque = LuaStackOp<T>::check(L, idx);
        return false;
    }
};

template <typename T>
struct VariantPtrMethodsBase : public VariantMethodsBase<T> {
    virtual void initialize(LuauVariant &self) const override {
        self._data._ptr = memnew(T);
    }

    virtual void destroy(LuauVariant &self) const override {
        memdelete((T *)self._data._ptr);
    }

    virtual void *get(LuauVariant &self, bool is_arg) const override {
        return self._data._ptr;
    }

    virtual const void *get(const LuauVariant &self) const override {
        return self._data._ptr;
    }

    virtual void copy(LuauVariant &self, const LuauVariant &other) const override {
        if (other.is_from_luau()) {
            self._data._ptr = other._data._ptr;
        } else {
            initialize(self);
            *(T *)self._data._ptr = *(T *)other._data._ptr;
        }
    }
};

// Assigns value to _ptr always, allocating memory where necessary.
// Used for types that are larger than DATA_SIZE, like Transforms, Projection, AABB, Basis
template <typename T>
struct VariantPtrMethods : public VariantPtrMethodsBase<T> {
    virtual bool check(LuauVariant &self, lua_State *L, int idx, const String &type_name) const override {
        self._data._ptr = LuaStackOp<T>::check_ptr(L, idx);
        return true;
    }
};

// Never pulls pointer from Luau.
struct VariantVariantMethods : public VariantPtrMethodsBase<Variant> {
    bool check(LuauVariant &self, lua_State *L, int idx, const String &type_name) const override {
        initialize(self);
        *(Variant *)self._data._ptr = LuaStackOp<Variant>::check(L, idx);
        return false;
    }
};

// Class type handling, null handling for Object type.
struct VariantObjectMethods : public VariantMethods {
    void initialize(LuauVariant &self) const override {
        self._data._ptr = nullptr;
    }

    void *get(LuauVariant &self, bool is_arg) const override {
        // TODO: 2023-01-09: This may change. Tracking https://github.com/godotengine/godot/issues/61967.
        // Special case. Object pointers are treated as GodotObject * when passing to Godot,
        // and GodotObject ** when returning from Godot.

        // Current understanding of the situation:
        // - Use _arg for all arguments
        // - _arg is never needed for return values or builtin/class `self`
        // - _arg is never needed for values which will never be Object

        // This is a mess. Hopefully it gets fixed at some point.
        if (is_arg)
            return self._data._ptr;
        else
            return &self._data._ptr;
    }

    const void *get(const LuauVariant &self) const override {
        return &self._data._ptr;
    }

    bool is(lua_State *L, int idx, const String &type_name) const override {
        Object *obj = LuaStackOp<Object *>::get(L, idx);
        if (obj == nullptr)
            return false;

        return type_name.is_empty() || obj->is_class(type_name);
    }

    bool check(LuauVariant &self, lua_State *L, int idx, const String &type_name) const override {
        Object *obj = LuaStackOp<Object *>::check(L, idx);
        if (obj == nullptr) {
            self._data._ptr = nullptr;
            return false;
        }

        if (!type_name.is_empty() && !obj->is_class(type_name))
            luaL_typeerrorL(L, idx, type_name.utf8().get_data());

        self._data._ptr = obj->_owner;
        return false;
    }

    void push(const LuauVariant &self, lua_State *L) const override {
        GDExtensionObjectPtr obj = (GDExtensionObjectPtr)self._data._ptr;

        if (obj == nullptr) {
            LuaStackOp<Object *>::push(L, nullptr);
            return;
        }

        Object *inst = ObjectDB::get_instance(internal::gde_interface->object_get_instance_id(obj));
        LuaStackOp<Object *>::push(L, inst);
    }

    void copy(LuauVariant &self, const LuauVariant &other) const override {
        self._data._ptr = other._data._ptr;
    }
};

static GDExtensionVariantFromTypeConstructorFunc to_variant_ctors[GDEXTENSION_VARIANT_TYPE_VARIANT_MAX] = { nullptr };
static GDExtensionTypeFromVariantConstructorFunc from_variant_ctors[GDEXTENSION_VARIANT_TYPE_VARIANT_MAX] = { nullptr };
static VariantMethods *type_methods[GDEXTENSION_VARIANT_TYPE_VARIANT_MAX] = { nullptr };

template <typename T>
void register_type(GDExtensionVariantType type) {
    static T methods;
    type_methods[type] = &methods;

    if (type != GDEXTENSION_VARIANT_TYPE_NIL) {
        to_variant_ctors[type] = internal::gde_interface->get_variant_from_type_constructor(type);
        from_variant_ctors[type] = internal::gde_interface->get_variant_to_type_constructor(type);
    }
}

void LuauVariant::_register_types() {
    register_type<VariantVariantMethods>(GDEXTENSION_VARIANT_TYPE_NIL);
    register_type<VariantAssignMethods<bool>>(GDEXTENSION_VARIANT_TYPE_BOOL);
    register_type<VariantAssignMethods<int64_t>>(GDEXTENSION_VARIANT_TYPE_INT);
    register_type<VariantAssignMethods<double>>(GDEXTENSION_VARIANT_TYPE_FLOAT);
    register_type<VariantAssignMethodsDtor<String>>(GDEXTENSION_VARIANT_TYPE_STRING);

    register_type<VariantUserdataMethods<Vector2>>(GDEXTENSION_VARIANT_TYPE_VECTOR2);
    register_type<VariantUserdataMethods<Vector2i>>(GDEXTENSION_VARIANT_TYPE_VECTOR2I);
    register_type<VariantUserdataMethods<Rect2>>(GDEXTENSION_VARIANT_TYPE_RECT2);
    register_type<VariantUserdataMethods<Rect2i>>(GDEXTENSION_VARIANT_TYPE_RECT2I);
    register_type<VariantUserdataMethods<Vector3>>(GDEXTENSION_VARIANT_TYPE_VECTOR3);
    register_type<VariantUserdataMethods<Vector3i>>(GDEXTENSION_VARIANT_TYPE_VECTOR3I);
    register_type<VariantPtrMethods<Transform2D>>(GDEXTENSION_VARIANT_TYPE_TRANSFORM2D);
    register_type<VariantUserdataMethods<Vector4>>(GDEXTENSION_VARIANT_TYPE_VECTOR4);
    register_type<VariantUserdataMethods<Vector4i>>(GDEXTENSION_VARIANT_TYPE_VECTOR4I);
    register_type<VariantUserdataMethods<Plane>>(GDEXTENSION_VARIANT_TYPE_PLANE);
    register_type<VariantUserdataMethods<Quaternion>>(GDEXTENSION_VARIANT_TYPE_QUATERNION);
    register_type<VariantPtrMethods<AABB>>(GDEXTENSION_VARIANT_TYPE_AABB);
    register_type<VariantPtrMethods<Basis>>(GDEXTENSION_VARIANT_TYPE_BASIS);
    register_type<VariantPtrMethods<Transform3D>>(GDEXTENSION_VARIANT_TYPE_TRANSFORM3D);
    register_type<VariantPtrMethods<Projection>>(GDEXTENSION_VARIANT_TYPE_PROJECTION);

    register_type<VariantUserdataMethods<Color>>(GDEXTENSION_VARIANT_TYPE_COLOR);
    register_type<VariantCoercedMethods<StringName>>(GDEXTENSION_VARIANT_TYPE_STRING_NAME);
    register_type<VariantCoercedMethods<NodePath>>(GDEXTENSION_VARIANT_TYPE_NODE_PATH);
    register_type<VariantUserdataMethods<RID>>(GDEXTENSION_VARIANT_TYPE_RID);
    register_type<VariantObjectMethods>(GDEXTENSION_VARIANT_TYPE_OBJECT);
    register_type<VariantUserdataMethods<Callable>>(GDEXTENSION_VARIANT_TYPE_CALLABLE);
    register_type<VariantUserdataMethods<Signal>>(GDEXTENSION_VARIANT_TYPE_SIGNAL);
    register_type<VariantUserdataMethods<Dictionary>>(GDEXTENSION_VARIANT_TYPE_DICTIONARY);
    register_type<VariantCoercedMethods<Array>>(GDEXTENSION_VARIANT_TYPE_ARRAY);

    register_type<VariantCoercedMethods<PackedByteArray>>(GDEXTENSION_VARIANT_TYPE_PACKED_BYTE_ARRAY);
    register_type<VariantCoercedMethods<PackedInt32Array>>(GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY);
    register_type<VariantCoercedMethods<PackedInt64Array>>(GDEXTENSION_VARIANT_TYPE_PACKED_INT64_ARRAY);
    register_type<VariantCoercedMethods<PackedFloat32Array>>(GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY);
    register_type<VariantCoercedMethods<PackedFloat64Array>>(GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT64_ARRAY);
    register_type<VariantCoercedMethods<PackedStringArray>>(GDEXTENSION_VARIANT_TYPE_PACKED_STRING_ARRAY);
    register_type<VariantCoercedMethods<PackedVector2Array>>(GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR2_ARRAY);
    register_type<VariantCoercedMethods<PackedVector3Array>>(GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR3_ARRAY);
    register_type<VariantCoercedMethods<PackedColorArray>>(GDEXTENSION_VARIANT_TYPE_PACKED_COLOR_ARRAY);

#ifdef DEBUG_ENABLED
    for (int i = 0; i < GDEXTENSION_VARIANT_TYPE_VARIANT_MAX; i++) {
        CRASH_COND_MSG(type_methods[i] == nullptr, "variant type was left uninitialized");
    }
#endif // DEBUG_ENABLED
}

void *LuauVariant::get_opaque_pointer_arg() {
    return type_methods[type]->get(*this, true);
}

void *LuauVariant::get_opaque_pointer() {
    return type_methods[type]->get(*this, false);
}

const void *LuauVariant::get_opaque_pointer() const {
    return type_methods[type]->get(*this);
}

void LuauVariant::initialize(GDExtensionVariantType init_type) {
    clear();
    type_methods[init_type]->initialize(*this);
    type = init_type;
}

void LuauVariant::clear() {
    if (type == -1)
        return;

    if (!from_luau)
        type_methods[type]->destroy(*this);

    type = -1;
}

bool LuauVariant::lua_is(lua_State *L, int idx, GDExtensionVariantType required_type, const String &type_name) {
    return type_methods[required_type]->is(L, idx, type_name);
}

void LuauVariant::lua_check(lua_State *L, int idx, GDExtensionVariantType required_type, const String &type_name) {
    // This means that the type will get reinitialized every time `check` is run, even if this variant
    // is already initialized to the same type. This, for now, is probably a non-issue as lua_check
    // is rarely run on an already-initialized object where the type is the same.
    clear();

    from_luau = type_methods[required_type]->check(*this, L, idx, type_name);
    type = required_type;
}

void LuauVariant::lua_push(lua_State *L) const {
    ERR_FAIL_COND_MSG(from_luau, "pushing a value from Luau back to Luau is unsupported");
    type_methods[type]->push(*this, L);
}

void LuauVariant::assign_variant(const Variant &val) {
    if (type == -1)
        return;

    // A NIL constructor doesn't exist
    if (type == GDEXTENSION_VARIANT_TYPE_NIL) {
        *get_ptr<Variant>() = val;
        return;
    }

    // Godot will not modify the value, so getting rid of const should be ok.
    from_variant_ctors[type](get_opaque_pointer(), (Variant *)&val);
}

Variant LuauVariant::to_variant() {
    if (type == -1)
        return Variant();

    if (type == GDEXTENSION_VARIANT_TYPE_NIL)
        return *get_ptr<Variant>();

    Variant ret;
    to_variant_ctors[type](&ret, get_opaque_pointer_arg());

    return ret;
}

void LuauVariant::copy_variant(LuauVariant &to, const LuauVariant &from) {
    if (from.type != -1) {
        type_methods[from.type]->copy(to, from);
    }

    to.type = from.type;
    to.from_luau = from.from_luau;
}

// Inherit from the original constructor to initialize type and from_luau.
// Safeguards against unintended behavior if these values are uninitialized (i.e. random).
LuauVariant::LuauVariant(const LuauVariant &from) :
        LuauVariant() {
    copy_variant(*this, from);
}

LuauVariant &LuauVariant::operator=(const LuauVariant &from) {
    if (this == &from)
        return *this;

    copy_variant(*this, from);
    return *this;
}

LuauVariant::~LuauVariant() {
    clear();
}
