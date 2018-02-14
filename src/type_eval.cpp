#include "types.h"
#include "scopes.h"
#include "dbg.h"

namespace types {
    auto type_true = type_id(make_iid("true"));
    auto type_false = type_id(make_iid("false"));
    auto truthy_id = make_iid("Truthy");
    auto falsey_id = make_iid("Falsey");
    auto type_truthy_lambda = type_lambda(truthy_id, type_lambda(falsey_id, type_variable(truthy_id)));
    auto type_falsey_lambda = type_lambda(truthy_id, type_lambda(falsey_id, type_variable(falsey_id)));


    type_t::ref type_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        return shared_from_this();
    }

    type_t::ref type_t::eval(const scope_t::ref &scope, bool get_structural_type) const {
		debug_above(8, log("eval(..., %s) of %s", boolstr(get_structural_type), str().c_str()));
		return this->eval_core(scope->get_nominal_env(), scope->get_total_env(), get_structural_type);
    }

    type_t::ref type_id_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
		debug_above(8, log("type_id_t::eval_core(%s, %s)", str().c_str(), boolstr(get_structural_type)));
        auto nominal_mapping = nominal_env.find(id->get_name());
		if (nominal_mapping != nominal_env.end()) {
			auto res = nominal_mapping->second->eval_core(nominal_env, total_env, get_structural_type);
		debug_above(8, log("type_id_t::eval_core(%s, %s) -> %s", str().c_str(), boolstr(get_structural_type), res->str().c_str()));
			return res;
		} else if (get_structural_type) {
            auto structural_mapping = total_env.find(id->get_name());
			if (structural_mapping != total_env.end()) {
				auto res = structural_mapping->second->eval_core(nominal_env, total_env, get_structural_type);
				debug_above(8, log("type_id_t::eval_core(%s, %s) -> %s", str().c_str(), boolstr(get_structural_type), res->str().c_str()));
				return res;
			}
        }

		debug_above(8, log("type_id_t::eval_core(%s, %s) -> %s", str().c_str(), boolstr(get_structural_type), str().c_str()));
        return shared_from_this();
    }

    type_t::ref type_operator_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        auto oper_ = oper->eval_core(nominal_env, total_env, get_structural_type);

        if (is_type_id(oper_, TYPE_OP_NOT)) {
            auto operand_ = operand->eval_core(nominal_env, total_env, get_structural_type);
            if (is_type_id(operand_, "false")) {
                return type_true;
            } else if (oper_ != oper || operand_ != operand) {
                return type_operator(oper_, operand_);
            } else {
                return shared_from_this();
            }
        } else if (is_type_id(oper_, TYPE_OP_GC)) {
            if (is_managed_ptr(operand, nominal_env, total_env)) {
                return type_true;
            } else {
                return type_false;
            }
        } else if (is_type_id(oper_, TYPE_OP_IF)) {
            auto operand_ = operand->eval_core(nominal_env, total_env, get_structural_type);
            if (is_type_id(operand_, "true")) {
                return type_truthy_lambda;
            } else {
                return type_falsey_lambda;
            }
        } else if (auto lambda = dyncast<const type_lambda_t>(oper_)) {
            auto var_name = lambda->binding->get_name();
            return lambda->body->rebind({{var_name, operand}})->eval_core(nominal_env, total_env, get_structural_type);
        } else {
            return shared_from_this();
        }
    }

    type_t::ref type_and_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        static auto false_type = type_id(make_iid("false"));
        not_impl();
        return false_type;
    }

    type_t::ref type_ptr_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        auto expansion = element_type->eval_core(nominal_env, total_env, get_structural_type);
        if (expansion != element_type) {
            return type_ptr(expansion);
        } else {
            return shared_from_this();
        }
    }

    type_t::ref type_ref_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        auto expansion = element_type->eval_core(nominal_env, total_env, get_structural_type);
        if (expansion != element_type) {
            return type_ref(expansion);
        } else {
            return shared_from_this();
        }
    }

    type_t::ref type_maybe_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        auto expansion = just->eval_core(nominal_env, total_env, get_structural_type);
        if (expansion != just) {
            return type_maybe(expansion);
        } else {
            return shared_from_this();
        }
    }
}
