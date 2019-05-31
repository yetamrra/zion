#include "translate.h"

#include <unordered_set>

#include "ast.h"
#include "unification.h"
#include "user_error.h"

using namespace bitter;

void check_typing_for_ftvs(const std::string &context,
                           const tracked_types_t &typing) {
  for (auto pair : typing) {
    if (pair.second->ftv_count() != 0) {
      log("in the context of %s", context.c_str());
      log_location(pair.first->get_location(),
                   "expression %s appears to have unbound type variable(s) "
                   "post-translation :: %s",
                   pair.first->str().c_str(), pair.second->str().c_str());
      dbg();
    }
  }
}

struct TTC {
  /* the typing checker */
  TTC(const std::string &&context, const tracked_types_t &typing)
      : context(std::move(context)), typing(typing) {
  }
  ~TTC() {
    check_typing_for_ftvs(context, typing);
  }

  const std::string context;
  const tracked_types_t &typing;
};

expr_t *texpr(const defn_id_t &for_defn_id,
              bitter::expr_t *expr,
              const std::unordered_set<std::string> &bound_vars,
              types::type_t::ref type,
              const types::type_env_t &type_env,
              const translation_env_t &tenv,
              tracked_types_t &typing,
              needed_defns_t &needed_defns,
              bool &returns) {
  TTC ttc(string_format("texpr(%s, %s, ..., %s, ...)",
                        for_defn_id.str().c_str(), expr->str().c_str(),
                        type->str().c_str()),
          typing);

  bool starts_already_returned = returns;
  try {
    /* the job of this function is to create a new ast that is constrained to
     * monomorphically typed nodes */
    assert(type != nullptr);
    debug_above(2, log("monomorphizing %s to have type %s", expr->str().c_str(),
                       type->str().c_str()));
    if (type->ftv_count() != 0) {
      log_location(expr->get_location(),
                   "cannot monomorphize %s for %s to have type %s because it "
                   "is not fully bound",
                   expr->str().c_str(), for_defn_id.str().c_str(),
                   type->str().c_str());
      dbg();
    }

    /* check for fully concrete type */
    if (type->generalize({})->btvs() != 0) {
      throw user_error(expr->get_location(),
                       "while (%s) is type-safe, Zion cannot figure out which "
                       "instance within %s to use. please use an 'as' operator "
                       "to add a type hint.",
                       expr->str().c_str(), type->str().c_str());
    }

    if (auto literal = dcast<literal_t *>(expr)) {
      typing[literal] = type;
      return literal;
    } else if (auto static_print = dcast<static_print_t *>(expr)) {
      bool fake_returns = false;
      auto inner_expr = texpr(for_defn_id, static_print->expr, bound_vars,
                              tenv.get_type(static_print->expr), type_env, tenv,
                              typing, needed_defns, fake_returns);
      log_location(static_print->expr->get_location(),
                   "within %s the type is %s", for_defn_id.str().c_str(),
                   typing[inner_expr]->str().c_str());
      auto unit_ret = unit_expr(static_print->get_location());
      typing[unit_ret] = type_unit(static_print->get_location());
      return unit_ret;
    } else if (auto var = dcast<var_t *>(expr)) {
      if (!in(var->id.name, bound_vars)) {
        auto defn_id = defn_id_t{var->id, type->generalize({})->normalize()};
        debug_above(6, log(c_id("%s") " depends on " c_id("%s"),
                           for_defn_id.str().c_str(), defn_id.str().c_str()));
        insert_needed_defn(needed_defns, defn_id, var->get_location(),
                           for_defn_id);
        auto new_var = new var_t(var->id);
        typing[new_var] = type;
        return new_var;
      } else {
        typing[var] = type;
        return var;
      }
    } else if (auto lambda = dcast<lambda_t *>(expr)) {
      auto new_bound_vars = bound_vars;
      new_bound_vars.insert(lambda->var.name);
      bool lambda_returns = false;
      auto new_body = texpr(for_defn_id, lambda->body, new_bound_vars,
                            tenv.get_type(lambda->body), type_env, tenv, typing,
                            needed_defns, lambda_returns);
      types::type_t::refs lambda_terms;
      unfold_binops_rassoc(ARROW_TYPE_OPERATOR, type, lambda_terms);
      assert(lambda_terms.size() >= 2);
      if (!lambda_returns &&
          !unify(lambda_terms.back(), type_unit(INTERNAL_LOC())).result) {
        auto error = user_error(lambda->get_location(),
                                "not all control paths return a value");
        error.add_info(lambda_terms.back()->get_location(), "return type is %s",
                       lambda_terms.back()->str().c_str());
        throw error;
      }
      auto new_lambda = new lambda_t(lambda->var, nullptr, nullptr, new_body);
      typing[new_lambda] = type;
      return new_lambda;
    } else if (auto application = dcast<application_t *>(expr)) {
      types::type_t::ref operator_type = tenv.get_type(application->a);
      types::type_t::ref operand_type = tenv.get_type(application->b);

      /* if we have unresolved types below us in the tree, we need to
       * propagate our known types down into them */
      types::type_t::refs terms;
      unfold_binops_rassoc(ARROW_TYPE_OPERATOR, operator_type, terms);
      assert(terms.size() > 1);

      types::type_t::ref resolution_type = type_arrows({operand_type, type});
      unification_t unification = unify(operator_type, resolution_type);
      assert(unification.result);
      operator_type = operator_type->rebind(unification.bindings);
      operand_type = operand_type->rebind(unification.bindings);

      auto a = texpr(for_defn_id, application->a, bound_vars, operator_type,
                     type_env, tenv, typing, needed_defns, returns);
      auto b = texpr(for_defn_id, application->b, bound_vars, operand_type,
                     type_env, tenv, typing, needed_defns, returns);
      auto new_app = new application_t(a, b);
      typing[new_app] = type;
      return new_app;
    } else if (auto let = dcast<let_t *>(expr)) {
      auto new_value = texpr(for_defn_id, let->value, bound_vars,
                             tenv.get_type(let->value), type_env, tenv, typing,
                             needed_defns, returns);
      auto new_bound_vars = bound_vars;
      new_bound_vars.insert(let->var.name);
      auto new_body = texpr(for_defn_id, let->body, new_bound_vars, type,
                            type_env, tenv, typing, needed_defns, returns);
      auto new_let = new let_t(let->var, new_value, new_body);
      typing[new_let] = type;
      return new_let;
    } else if (auto fix = dcast<fix_t *>(expr)) {
      assert(false);
    } else if (auto condition = dcast<conditional_t *>(expr)) {
      auto cond = texpr(for_defn_id, condition->cond, bound_vars,
                        tenv.get_type(condition->cond), type_env, tenv, typing,
                        needed_defns, returns);
      bool truthy_returns = false;
      auto truthy = texpr(for_defn_id, condition->truthy, bound_vars,
                          tenv.get_type(condition->truthy), type_env, tenv,
                          typing, needed_defns, truthy_returns);
      bool falsey_returns = false;
      auto falsey = texpr(for_defn_id, condition->falsey, bound_vars,
                          tenv.get_type(condition->falsey), type_env, tenv,
                          typing, needed_defns, falsey_returns);
      if (truthy_returns && falsey_returns) {
        returns = true;
      }
      auto new_conditional = new conditional_t(cond, truthy, falsey);
      typing[new_conditional] = type;
      return new_conditional;
    } else if (auto block = dcast<block_t *>(expr)) {
      std::vector<expr_t *> statements;
      for (auto stmt : block->statements) {
        if (returns && !starts_already_returned) {
          throw user_error(stmt->get_location(), "this code will never run");
        }
        statements.push_back(texpr(for_defn_id, stmt, bound_vars,
                                   tenv.get_type(stmt), type_env, tenv, typing,
                                   needed_defns, returns));
      }
      auto new_block = new block_t(statements);
      typing[new_block] = type;
      return new_block;
    } else if (auto while_ = dcast<while_t *>(expr)) {
      bool block_returns = false;
      auto condition = texpr(for_defn_id, while_->condition, bound_vars,
                             tenv.get_type(while_->condition), type_env, tenv,
                             typing, needed_defns, returns);
      auto block = texpr(for_defn_id, while_->block, bound_vars,
                         tenv.get_type(while_->block), type_env, tenv, typing,
                         needed_defns, block_returns);
      /* NB: we don't really care if the block returns because we can't
       * validate that the loop ever actually runs */
      auto new_while = new while_t(condition, block);
      typing[new_while] = type;
      return new_while;
    } else if (auto break_ = dcast<break_t *>(expr)) {
      auto new_break = new break_t(break_->get_location());
      typing[new_break] = type_unit(INTERNAL_LOC());
      return new_break;
    } else if (auto continue_ = dcast<continue_t *>(expr)) {
      auto new_continue = new continue_t(continue_->get_location());
      typing[new_continue] = type_unit(INTERNAL_LOC());
      return new_continue;
    } else if (auto return_ = dcast<return_statement_t *>(expr)) {
      auto ret = new return_statement_t(
          texpr(for_defn_id, return_->value, bound_vars,
                tenv.get_type(return_->value), type_env, tenv, typing,
                needed_defns, returns));
      typing[ret] = type_unit(return_->get_location());
      returns = true;
      return ret;
    } else if (auto tuple = dcast<tuple_t *>(expr)) {
      std::vector<expr_t *> dims;
      for (auto dim : tuple->dims) {
        if (returns && !starts_already_returned) {
          throw user_error(expr->get_location(),
                           "this code will never run due to a prior return");
        }
        dims.push_back(texpr(for_defn_id, dim, bound_vars, tenv.get_type(dim),
                             type_env, tenv, typing, needed_defns, returns));
      }
      auto new_tuple = new tuple_t(tuple->get_location(), dims);
      typing[new_tuple] = type;
      return new_tuple;
    } else if (auto match = dcast<match_t *>(expr)) {
      return translate_match_expr(for_defn_id, match, bound_vars, type_env,
                                  tenv, typing, needed_defns, returns);
    } else if (auto as = dcast<as_t *>(expr)) {
      auto expr = texpr(for_defn_id, as->expr, bound_vars,
                        as->force_cast ? tenv.get_type(as->expr) : type,
                        type_env, tenv, typing, needed_defns, returns);
      if (as->force_cast) {
        auto new_as = new as_t(expr, scheme({}, {}, type), true /*force_cast*/);
        typing[new_as] = type;
        return new_as;
      } else {
        /* eliminate non-forceful casts */
        assert(typing.count(expr));
        return expr;
      }
    } else if (auto builtin = dcast<builtin_t *>(expr)) {
      std::vector<expr_t *> exprs;
      for (auto expr : builtin->exprs) {
        exprs.push_back(texpr(for_defn_id, expr, bound_vars,
                              tenv.get_type(expr), type_env, tenv, typing,
                              needed_defns, returns));
      }
      auto new_builtin = new builtin_t(
          dynamic_cast<var_t *>(texpr(for_defn_id, builtin->var, bound_vars,
                                      tenv.get_type(builtin->var), type_env,
                                      tenv, typing, needed_defns, returns)),
          exprs);
      typing[new_builtin] = type;
      return new_builtin;
    } else if (auto sizeof_ = dcast<sizeof_t *>(expr)) {
      auto builtin_word_id = identifier_t{"__builtin_word_size",
                                          sizeof_->get_location()};
      auto new_sizeof = new builtin_t(new var_t(builtin_word_id), {});
      typing[new_sizeof] = type;
      return new_sizeof;
    } else if (auto tuple_deref = dcast<tuple_deref_t *>(expr)) {
      auto new_tuple_deref = new tuple_deref_t(
          texpr(for_defn_id, tuple_deref->expr, bound_vars,
                tenv.get_type(tuple_deref->expr), type_env, tenv, typing,
                needed_defns, returns),
          tuple_deref->index, tuple_deref->max);
      typing[new_tuple_deref] = type;
      return new_tuple_deref;
    }
  } catch (user_error &e) {
    auto type = tenv.get_type(expr);
    e.add_info(expr->get_location(), "error while translating %s :: %s",
               expr->str().c_str(), type->str().c_str());
    throw;
  }
  log_location(expr->get_location(), "don't know how to texpr %s",
               expr->str().c_str());
  assert(false);
  return nullptr;
}

translation_t::ref translate_expr(
    const defn_id_t &for_defn_id,
    bitter::expr_t *expr,
    const std::unordered_set<std::string> &bound_vars,
    const types::type_env_t &type_env,
    const translation_env_t &tenv,
    needed_defns_t &needed_defns,
    bool &returns) {
  tracked_types_t typing;
  expr_t *translated_expr = texpr(for_defn_id, expr, bound_vars,
                                  tenv.get_type(expr), type_env, tenv, typing,
                                  needed_defns, returns);
  return std::make_shared<translation_t>(translated_expr, typing);
}

translation_t::translation_t(const bitter::expr_t *expr,
                             const tracked_types_t &typing)
    : expr(expr), typing(typing) {
  check_typing_for_ftvs(std::string("making a translation_t"), typing);
}

std::string translation_t::str() const {
  return string_format("%s :: %s", expr->str().c_str(),
                       get(typing, expr, {})->str().c_str());
}

location_t translation_t::get_location() const {
  return expr->get_location();
}

types::type_t::ref translation_env_t::get_type(const bitter::expr_t *e) const {
  auto t = (*tracked_types)[e];
  if (t == nullptr) {
    log_location(log_error, e->get_location(),
                 "translation env does not contain a type for %s",
                 e->str().c_str());
    assert(false && !!"missing type for expression");
  }
  return t;
}

types::type_t::refs translation_env_t::get_data_ctor_terms(
    types::type_t::ref type,
    identifier_t ctor_id) const {
  // TODO: destructure the inbound type operator to find the id and the params.
  // look up the type to find the ctors, then look up the ctor from the inbound
  // ctor_id, and apply the params to the ctor's lambda to get the ctor_type.
  // unfold / destructure the terms of the ctor_type and return that list of
  // terms.

  // types::type_t::refs ctor_terms;
  // unfold_binops_rassoc(ARROW_TYPE_OPERATOR, ctor_type, ctor_terms);

  types::type_t::refs type_terms;
  unfold_ops_lassoc(type, type_terms);
  assert(type_terms.size() != 0);

  auto id = safe_dyncast<const types::type_id_t>(type_terms[0]);
  debug_above(7, log("looking for %s in data_ctors_map of size %d",
                     id->str().c_str(), int(data_ctors_map.size())));
  debug_above(8, log("%s", ::str(data_ctors_map).c_str()));
  assert(data_ctors_map.count(id->id.name) != 0);
  auto &data_ctors = data_ctors_map.at(id->id.name);

  auto ctor_type = get(data_ctors, ctor_id.name, {});
  if (ctor_type == nullptr) {
    throw user_error(ctor_id.location, "data ctor %s does not exist",
                     ctor_id.str().c_str());
  }

  debug_above(7, log("starting with ctor_type as %s and type_terms as %s",
                     ctor_type->str().c_str(), ::str(type_terms).c_str()));

  for (int i = 1; i < type_terms.size(); ++i) {
    ctor_type = ctor_type->apply(type_terms[i]);
  }
  debug_above(7, log("resolved ctor_type as %s", ctor_type->str().c_str()));

  types::type_t::refs ctor_terms;
  unfold_binops_rassoc(ARROW_TYPE_OPERATOR, ctor_type, ctor_terms);
  return ctor_terms;
}

std::map<std::string, types::type_t::refs> translation_env_t::
    get_data_ctors_terms(types::type_t::ref type) const {
  types::type_t::refs type_terms;
  unfold_ops_lassoc(type, type_terms);
  assert(type_terms.size() != 0);

  auto id = safe_dyncast<const types::type_id_t>(type_terms[0]);
  debug_above(7, log("looking for %s in data_ctors_map of size %d",
                     id->str().c_str(), int(data_ctors_map.size())));
  debug_above(7, log("%s", ::str(data_ctors_map).c_str()));

  assert(data_ctors_map.count(id->id.name) != 0);
  auto &data_ctors = data_ctors_map.at(id->id.name);

  std::map<std::string, types::type_t::refs> data_ctors_terms;

  for (auto pair : data_ctors) {
    auto ctor_type = pair.second;
    debug_above(7, log("starting with ctor_type as %s and type_terms as %s",
                       ctor_type->str().c_str(), ::str(type_terms).c_str()));

    for (int i = 1; i < type_terms.size(); ++i) {
      ctor_type = ctor_type->apply(type_terms[i]);
    }
    debug_above(7, log("resolved ctor_type as %s", ctor_type->str().c_str()));

    types::type_t::refs ctor_terms;
    unfold_binops_rassoc(ARROW_TYPE_OPERATOR, ctor_type, ctor_terms);

    data_ctors_terms[pair.first] = ctor_terms;
  }
  return data_ctors_terms;
}

types::type_t::refs translation_env_t::get_fresh_data_ctor_terms(
    identifier_t ctor_id) const {
  // FUTURE: build an index to make this faster
  for (auto type_ctors : data_ctors_map) {
    for (auto ctors : type_ctors.second) {
      if (ctors.first == ctor_id.name) {
        types::type_t::ref ctor_type = ctors.second;
        while (true) {
          if (auto type_lambda = dyncast<const types::type_lambda_t>(
                  ctor_type)) {
            ctor_type = type_lambda->apply(type_variable(INTERNAL_LOC()));
          } else {
            break;
          }
        }
        types::type_t::refs terms;
        unfold_binops_rassoc(ARROW_TYPE_OPERATOR, ctor_type, terms);
        return terms;
      }
    }
  }
  throw user_error(ctor_id.location, "no data constructor found for %s",
                   ctor_id.str().c_str());
}

int translation_env_t::get_ctor_id(std::string ctor_name) const {
  auto iter = ctor_id_map.find(ctor_name);
  if (iter == ctor_id_map.end()) {
    throw user_error(INTERNAL_LOC(),
                     "bad ctor name requested during translation (%s)",
                     ctor_name.c_str());
  } else {
    return iter->second;
  }
}
