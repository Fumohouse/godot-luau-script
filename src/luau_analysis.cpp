#include "luau_analysis.h"

#include <Luau/Ast.h>
#include <gdextension_interface.h>
#include <string.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luau_lib.h"
#include "utils.h"

using namespace godot;

/* BASE ANALYSIS */

static void search_stat_return(Luau::AstArray<Luau::AstStat *> body, Luau::AstLocal *&result) {
    for (Luau::AstStat *stat : body) {
        if (Luau::AstStatBlock *block = stat->as<Luau::AstStatBlock>()) {
            // Can return from inside a block, for some reason.
            search_stat_return(block->body, result);
        } else if (Luau::AstStatReturn *ret = stat->as<Luau::AstStatReturn>()) {
            if (ret->list.size == 0)
                return;

            Luau::AstExpr *expr = *ret->list.begin();

            if (Luau::AstExprLocal *local = expr->as<Luau::AstExprLocal>()) {
                result = local->local;
            }
        }
    }
}

struct LocalDefinitionFinder : public Luau::AstVisitor {
    Luau::AstLocal *local;
    Luau::AstExpr *result = nullptr;

    bool visit(Luau::AstStatLocal *node) override {
        Luau::AstLocal *const *vars = node->vars.begin();
        Luau::AstExpr *const *values = node->values.begin();

        for (int i = 0; i < node->vars.size && i < node->values.size; i++) {
            if (vars[i] == local) {
                result = values[i];
            }
        }

        return false;
    }

    LocalDefinitionFinder(Luau::AstLocal *local) :
            local(local) {}
};

struct TypesMethodsFinder : public Luau::AstVisitor {
    Luau::AstLocal *impl;

    HashMap<StringName, Luau::AstStatFunction *> methods;
    HashMap<StringName, Luau::AstStatTypeAlias *> types;

    bool visit(Luau::AstStatFunction *func) override {
        // look at this
        if (Luau::AstExprIndexName *index = func->name->as<Luau::AstExprIndexName>()) {
            if (Luau::AstExprLocal *local = index->expr->as<Luau::AstExprLocal>()) {
                if (local->local == impl) {
                    methods.insert(index->index.value, func);
                }
            }
        }

        return false;
    }

    bool visit(Luau::AstStatTypeAlias *type) override {
        types.insert(type->name.value, type);
        return false;
    }

    TypesMethodsFinder(Luau::AstLocal *impl) :
            impl(impl) {}
};

// Scans the script AST for key components. As this functionality is non-essential (for scripts running),
// it will for simplicity be quite picky about how classes are defined:
// - The returned definition and impl table (if any) must be defined as locals.
// - The impl table (if any) must be passed into `RegisterImpl` as a local.
// - The returned value must be the same local variable as the one that defined the class.
// - All methods which chain on classes (namely, `RegisterImpl`) must be called in the same expression that defines the class definition.
// Basically, make everything "idiomatic" (if such a thing exists) and don't do anything weird, then this should work.
bool luascript_analyze(const Luau::ParseResult &parse_result, LuauScriptAnalysisResult &result) {
    // Step 1: Scan root return value for definition expression.
    search_stat_return(parse_result.root->body, result.definition);

    if (result.definition == nullptr)
        return false;

    // Step 2: Find the implementation table, if any.
    LocalDefinitionFinder def_local_def_finder(result.definition);
    parse_result.root->visit(&def_local_def_finder);
    if (def_local_def_finder.result == nullptr)
        return false;

    Luau::AstExprCall *chained_call = def_local_def_finder.result->as<Luau::AstExprCall>();

    while (chained_call != nullptr) {
        Luau::AstExpr *func = chained_call->func;

        if (Luau::AstExprIndexName *index = func->as<Luau::AstExprIndexName>()) {
            if (index->op == ':' && index->index == "RegisterImpl" && chained_call->args.size >= 1) {
                if (Luau::AstExprLocal *found_local = (*chained_call->args.begin())->as<Luau::AstExprLocal>()) {
                    result.impl = found_local->local;
                    break;
                }
            }

            chained_call = index->expr->as<Luau::AstExprCall>();
        } else {
            break;
        }
    }

    if (result.impl == nullptr)
        result.impl = result.definition;

    // Step 3: Find defined methods and types.
    TypesMethodsFinder types_methods_finder(result.impl);
    parse_result.root->visit(&types_methods_finder);

    result.methods = types_methods_finder.methods;
    result.types = types_methods_finder.types;

    return true;
}

/* AST FUNCTIONS */

static bool get_type(const char *type_name, GDProperty &prop) {
    static HashMap<String, GDExtensionVariantType> variant_types;
    static bool did_init = false;

    if (!did_init) {
        // Special cases
        variant_types.insert("nil", GDEXTENSION_VARIANT_TYPE_NIL);
        variant_types.insert("Variant", GDEXTENSION_VARIANT_TYPE_NIL);
        variant_types.insert("boolean", GDEXTENSION_VARIANT_TYPE_BOOL);
        variant_types.insert("integer", GDEXTENSION_VARIANT_TYPE_INT);
        variant_types.insert("number", GDEXTENSION_VARIANT_TYPE_FLOAT);
        variant_types.insert("string", GDEXTENSION_VARIANT_TYPE_STRING);

        for (int i = GDEXTENSION_VARIANT_TYPE_VECTOR2; i < GDEXTENSION_VARIANT_TYPE_VARIANT_MAX; i++) {
            variant_types.insert(Variant::get_type_name(Variant::Type(i)), GDExtensionVariantType(i));
        }

        did_init = true;
    }

    HashMap<String, GDExtensionVariantType>::ConstIterator E = variant_types.find(type_name);

    if (E) {
        // Variant
        prop.type = E->value;
    } else if (Utils::class_exists(type_name)) {
        prop.type = GDEXTENSION_VARIANT_TYPE_OBJECT;

        if (Utils::is_parent_class(type_name, "Resource")) {
            // Resource
            prop.hint = PROPERTY_HINT_RESOURCE_TYPE;
            prop.hint_string = type_name;
        } else {
            // Object
            prop.class_name = type_name;
        }
    } else {
        return false;
    }

    return true;
}

static bool get_prop(Luau::AstTypeReference *type, GDProperty &prop) {
    const char *type_name = type->name.value;

    if (!type->hasParameterList) {
        return get_type(type_name, prop);
    }

    if (strcmp(type_name, "TypedArray") == 0) {
        // TypedArray
        Luau::AstType *param = type->parameters.begin()->type;

        if (param != nullptr) {
            if (Luau::AstTypeReference *param_ref = param->as<Luau::AstTypeReference>()) {
                GDProperty type_info;
                if (!get_type(param_ref->name.value, type_info))
                    return false;

                prop.type = GDEXTENSION_VARIANT_TYPE_ARRAY;
                prop.hint = PROPERTY_HINT_ARRAY_TYPE;

                if (type_info.type == GDEXTENSION_VARIANT_TYPE_OBJECT) {
                    if (type_info.hint == PROPERTY_HINT_RESOURCE_TYPE) {
                        prop.hint_string = Utils::resource_type_hint(type_info.hint_string);
                    } else {
                        prop.hint_string = type_info.class_name;
                    }
                } else {
                    prop.hint_string = Variant::get_type_name(Variant::Type(type_info.type));
                }

                return true;
            }
        }
    }

    return false;
}

bool luascript_ast_method(const LuauScriptAnalysisResult &analysis, const StringName &method, GDMethod &ret) {
    HashMap<StringName, Luau::AstStatFunction *>::ConstIterator E = analysis.methods.find(method);

    if (!E)
        return false;

    Luau::AstStatFunction *stat_func = E->value;
    Luau::AstExprFunction *func = stat_func->func;

    ret.name = method;

    if (func->returnAnnotation.has_value()) {
        const Luau::AstArray<Luau::AstType *> &types = func->returnAnnotation.value().types;
        if (types.size > 1)
            return false;

        Luau::AstTypeReference *ref = (*types.begin())->as<Luau::AstTypeReference>();
        if (ref == nullptr)
            return false;

        GDProperty return_val;
        if (!get_prop(ref, return_val))
            return false;

        ret.return_val = return_val;
    }

    if (func->vararg)
        ret.flags = ret.flags | METHOD_FLAG_VARARG;

    bool has_self = func->self != nullptr;
    bool arg_offset = has_self ? 0 : 1;

    int i = 0;
    ret.arguments.resize(has_self ? func->args.size : func->args.size - 1);

    GDProperty *arg_props = ret.arguments.ptrw();
    for (Luau::AstLocal *arg : func->args) {
        if (i < arg_offset) {
            i++;
            continue;
        }

        GDProperty arg_prop;
        arg_prop.name = arg->name.value;

        if (arg->annotation == nullptr)
            return false;

        Luau::AstTypeReference *arg_type = arg->annotation->as<Luau::AstTypeReference>();
        if (arg_type == nullptr || !get_prop(arg_type, arg_prop))
            return false;

        arg_props[i - arg_offset] = arg_prop;
        i++;
    }

    return true;
}
