#include "luau_analysis.h"

#include <Luau/Ast.h>
#include <Luau/Lexer.h>
#include <Luau/Location.h>
#include <Luau/ParseResult.h>
#include <gdextension_interface.h>
#include <string.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luau_lib.h"
#include "utils.h"

using namespace godot;

/* BASE ANALYSIS */

static Luau::AstLocal *find_return_local(Luau::AstArray<Luau::AstStat *> p_body) {
    for (Luau::AstStat *stat : p_body) {
        if (Luau::AstStatBlock *block = stat->as<Luau::AstStatBlock>()) {
            // Can return from inside a block, for some reason.
            return find_return_local(block->body);
        } else if (Luau::AstStatReturn *ret = stat->as<Luau::AstStatReturn>()) {
            if (ret->list.size == 0)
                return nullptr;

            Luau::AstExpr *expr = *ret->list.begin();
            if (Luau::AstExprLocal *local = expr->as<Luau::AstExprLocal>()) {
                return local->local;
            }
        }
    }

    return nullptr;
}

struct LocalDefinitionFinder : public Luau::AstVisitor {
    Luau::AstLocal *local;
    Luau::AstExpr *result = nullptr;

    bool visit(Luau::AstStatLocal *p_node) override {
        Luau::AstLocal *const *vars = p_node->vars.begin();
        Luau::AstExpr *const *values = p_node->values.begin();

        for (int i = 0; i < p_node->vars.size && i < p_node->values.size; i++) {
            if (vars[i] == local) {
                result = values[i];
            }
        }

        return false;
    }

    LocalDefinitionFinder(Luau::AstLocal *p_local) :
            local(p_local) {}
};

struct TypesMethodsFinder : public Luau::AstVisitor {
    Luau::AstLocal *impl;

    HashMap<StringName, Luau::AstStatFunction *> methods;

    bool visit(Luau::AstStatFunction *p_func) override {
        if (Luau::AstExprIndexName *index = p_func->name->as<Luau::AstExprIndexName>()) {
            if (Luau::AstExprLocal *local = index->expr->as<Luau::AstExprLocal>()) {
                if (local->local == impl) {
                    methods.insert(index->index.value, p_func);
                }
            }
        }

        return false;
    }

    TypesMethodsFinder(Luau::AstLocal *p_impl) :
            impl(p_impl) {}
};

// Scans the script AST for key components. As this functionality is non-essential (for scripts running),
// it will for simplicity be quite picky about how classes are defined:
// - The returned definition and impl table (if any) must be defined as locals.
// - The impl table (if any) must be passed into `RegisterImpl` as a local.
// - The returned value must be the same local variable as the one that defined the class.
// - All methods which chain on classes (namely, `RegisterImpl`) must be called in the same expression that defines the class definition.
// Basically, make everything "idiomatic" (if such a thing exists) and don't do anything weird, then this should work.
bool luascript_analyze(const char *p_src, const Luau::ParseResult &p_parse_result, LuauScriptAnalysisResult &r_result) {
    // Step 1: Extract comments
    LocalVector<const char *> line_offsets;
    line_offsets.push_back(p_src);
    {
        const char *ptr = p_src;

        while (*ptr) {
            if (*ptr == '\n') {
                line_offsets.push_back(ptr + 1);
            }

            ptr++;
        }
    }

    for (const Luau::Comment &comment : p_parse_result.commentLocations) {
        if (comment.type == Luau::Lexeme::BrokenComment) {
            continue;
        }

        const Luau::Location &loc = comment.location;
        const char *start_line_ptr = line_offsets[loc.begin.line];

        LuauComment parsed_comment;

        if (comment.type == Luau::Lexeme::BlockComment) {
            parsed_comment.type = LuauComment::BLOCK;
        } else {
            // Search from start of line. If there is nothing other than whitespace before the comment, count it as "exclusive".
            const char *ptr = start_line_ptr;

            while (*ptr) {
                if (*ptr == '-' && *(ptr + 1) == '-') {
                    parsed_comment.type = LuauComment::SINGLE_LINE_EXCL;
                    break;
                } else if (*ptr != '\t' && *ptr != ' ' && *ptr != '\v' && *ptr != '\f') {
                    parsed_comment.type = LuauComment::SINGLE_LINE;
                    break;
                }

                ptr++;
            }
        }

        parsed_comment.location = loc;

        const char *start_ptr = start_line_ptr + loc.begin.column;
        const char *end_ptr = line_offsets[loc.end.line] + loc.end.column; // not inclusive
        parsed_comment.contents = String::utf8(start_ptr, end_ptr - start_ptr);

        r_result.comments.push_back(parsed_comment);
    }

    // Step 2: Scan root return value for definition expression.
    r_result.definition = find_return_local(p_parse_result.root->body);
    if (!r_result.definition)
        return false;

    LocalDefinitionFinder def_local_def_finder(r_result.definition);
    p_parse_result.root->visit(&def_local_def_finder);
    if (!def_local_def_finder.result)
        return false;

    // Step 3: Find the implementation table, if any.
    Luau::AstExprCall *chained_call = def_local_def_finder.result->as<Luau::AstExprCall>();

    while (chained_call) {
        Luau::AstExpr *func = chained_call->func;

        if (Luau::AstExprIndexName *index = func->as<Luau::AstExprIndexName>()) {
            if (index->op == ':' && index->index == "RegisterImpl" && chained_call->args.size >= 1) {
                if (Luau::AstExprLocal *found_local = (*chained_call->args.begin())->as<Luau::AstExprLocal>()) {
                    r_result.impl = found_local->local;
                    break;
                }
            }

            chained_call = index->expr->as<Luau::AstExprCall>();
        } else {
            break;
        }
    }

    if (!r_result.impl)
        return false;

    // Step 4: Find defined methods and types.
    TypesMethodsFinder types_methods_finder(r_result.impl);
    p_parse_result.root->visit(&types_methods_finder);

    r_result.methods = types_methods_finder.methods;

    return true;
}

/* AST FUNCTIONS */

static bool get_type(const char *p_type_name, GDProperty &r_prop) {
    static HashMap<String, GDExtensionVariantType> variant_types;
    static bool did_init = false;

    if (!did_init) {
        // Special cases
        variant_types.insert("nil", GDEXTENSION_VARIANT_TYPE_NIL);
        variant_types.insert("boolean", GDEXTENSION_VARIANT_TYPE_BOOL);
        variant_types.insert("integer", GDEXTENSION_VARIANT_TYPE_INT);
        variant_types.insert("number", GDEXTENSION_VARIANT_TYPE_FLOAT);
        variant_types.insert("string", GDEXTENSION_VARIANT_TYPE_STRING);

        for (int i = GDEXTENSION_VARIANT_TYPE_VECTOR2; i < GDEXTENSION_VARIANT_TYPE_VARIANT_MAX; i++) {
            variant_types.insert(Variant::get_type_name(Variant::Type(i)), GDExtensionVariantType(i));
        }

        did_init = true;
    }

    // Special case
    if (strcmp(p_type_name, "Variant") == 0) {
        r_prop.type = GDEXTENSION_VARIANT_TYPE_NIL;
        r_prop.usage = PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT;

        return true;
    }

    HashMap<String, GDExtensionVariantType>::ConstIterator E = variant_types.find(p_type_name);

    if (E) {
        // Variant type
        r_prop.type = E->value;
    } else if (Utils::class_exists(p_type_name)) {
        r_prop.type = GDEXTENSION_VARIANT_TYPE_OBJECT;

        if (Utils::is_parent_class(p_type_name, "Resource")) {
            // Resource
            r_prop.hint = PROPERTY_HINT_RESOURCE_TYPE;
            r_prop.hint_string = p_type_name;
        } else {
            // Object
            r_prop.class_name = p_type_name;
        }
    } else {
        return false;
    }

    return true;
}

static bool get_prop(Luau::AstTypeReference *p_type, GDProperty &r_prop) {
    const char *type_name = p_type->name.value;

    if (!p_type->hasParameterList) {
        return get_type(type_name, r_prop);
    }

    if (strcmp(type_name, "TypedArray") == 0) {
        // TypedArray
        Luau::AstType *param = p_type->parameters.begin()->type;

        if (param) {
            if (Luau::AstTypeReference *param_ref = param->as<Luau::AstTypeReference>()) {
                GDProperty type_info;
                if (!get_type(param_ref->name.value, type_info))
                    return false;

                r_prop.type = GDEXTENSION_VARIANT_TYPE_ARRAY;
                r_prop.hint = PROPERTY_HINT_ARRAY_TYPE;

                if (type_info.type == GDEXTENSION_VARIANT_TYPE_OBJECT) {
                    if (type_info.hint == PROPERTY_HINT_RESOURCE_TYPE) {
                        r_prop.hint_string = Utils::resource_type_hint(type_info.hint_string);
                    } else {
                        r_prop.hint_string = type_info.class_name;
                    }
                } else {
                    r_prop.hint_string = Variant::get_type_name(Variant::Type(type_info.type));
                }

                return true;
            }
        }
    }

    return false;
}

static Luau::AstTypeReference *get_type_reference(Luau::AstType *p_type, bool *r_was_conditional = nullptr) {
    if (Luau::AstTypeReference *ref = p_type->as<Luau::AstTypeReference>()) {
        return ref;
    }

    // Union with nil -> T?
    if (Luau::AstTypeUnion *uni = p_type->as<Luau::AstTypeUnion>()) {
        if (uni->types.size != 2)
            return nullptr;

        bool nil_found = false;
        Luau::AstTypeReference *first_non_nil_ref = nullptr;

        for (Luau::AstType *uni_type : uni->types) {
            if (Luau::AstTypeReference *ref = uni_type->as<Luau::AstTypeReference>()) {
                if (ref->name == "nil")
                    nil_found = true;
                else if (!first_non_nil_ref)
                    first_non_nil_ref = ref;

                if (nil_found && first_non_nil_ref) {
                    if (r_was_conditional)
                        *r_was_conditional = true;

                    return first_non_nil_ref;
                }
            } else {
                return nullptr;
            }
        }
    }

    return nullptr;
}

bool luascript_ast_method(const LuauScriptAnalysisResult &p_analysis, const StringName &p_method, GDMethod &r_ret) {
    HashMap<StringName, Luau::AstStatFunction *>::ConstIterator E = p_analysis.methods.find(p_method);

    if (!E)
        return false;

    Luau::AstStatFunction *stat_func = E->value;
    Luau::AstExprFunction *func = stat_func->func;

    r_ret.name = p_method;

    if (func->returnAnnotation.has_value()) {
        const Luau::AstArray<Luau::AstType *> &types = func->returnAnnotation.value().types;
        if (types.size > 1)
            return false;

        bool ret_conditional = false;
        Luau::AstTypeReference *ref = get_type_reference(*types.begin(), &ret_conditional);
        if (!ref)
            return false;

        GDProperty return_val;

        if (ret_conditional) {
            // Assume Variant if method can return nil
            return_val.type = GDEXTENSION_VARIANT_TYPE_NIL;
            return_val.usage = PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT;
        } else if (!get_prop(ref, return_val)) {
            return false;
        }

        r_ret.return_val = return_val;
    }

    if (func->vararg)
        r_ret.flags = r_ret.flags | METHOD_FLAG_VARARG;

    int arg_offset = func->self ? 0 : 1;

    int i = 0;
    r_ret.arguments.resize(func->self ? func->args.size : func->args.size - 1);

    GDProperty *arg_props = r_ret.arguments.ptrw();
    for (Luau::AstLocal *arg : func->args) {
        if (i < arg_offset) {
            i++;
            continue;
        }

        GDProperty arg_prop;
        arg_prop.name = arg->name.value;

        if (!arg->annotation)
            return false;

        Luau::AstTypeReference *arg_type = get_type_reference(arg->annotation);
        if (!arg_type || !get_prop(arg_type, arg_prop))
            return false;

        arg_props[i - arg_offset] = arg_prop;
        i++;
    }

    return true;
}
