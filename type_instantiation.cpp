#include "zion.h"
#include <iostream>
#include <numeric>
#include <list>
#include "type_instantiation.h"
#include "bound_var.h"
#include "scopes.h"
#include "ast.h"
#include "llvm_types.h"
#include "llvm_utils.h"
#include "phase_scope_setup.h"
#include "types.h"
#include "code_id.h"

/* When we encounter the Empty declaration, we have to instantiate something.
 * When we create Empty() with term __obj__{__tuple__}. We don't bother
 * associating anything with the base type. We also create a bound type with
 * term 'Empty' that just maps to the raw __obj__{__tuple__} one.
 *
 * When we encounter Just, we create an unchecked data ctor, which would look
 * like:
 *
 *     def Just(any X) Just{any X}
 *
 * if it needed to have an AST. And, importantly, we do not create a type for
 * Just yet because it's not fully bound.
 * 
 * When we encounter a bound instance of the base type, like:
 * 
 *     var m Maybe{int} = ...
 *
 * we instantiate all the data ctors that are not yet instantiated.
 *
 * In the case of self-references like:
 *
 * type IntList is Node(int, IntList) or Done
 *
 * We notice that the base type is not parameterized. So, we immediately create
 * the base sum type IntList that maps to term __or__{Node{int, IntList},
 * Done} where the LLVM representation of this is just a raw var_t pointer that
 * can later be upcast, based on pattern matching on the type_id.
 */

bound_var_t::ref bind_ctor_to_scope(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		ast::item_t::ref node,
		types::type_function_t::ref function)
{
	assert(!!status);
	assert(id != nullptr);
	assert(function != nullptr);
	bool is_instantiation = bool(dyncast<generic_substitution_scope_t>(scope));
	assert(is_instantiation);
	/* create or find an existing ctor function that satisfies the term of
	 * this node */
	debug_above(5, log(log_info, "finding/creating data ctor for " c_type("%s") " with return type %s",
			id->str().c_str(), function->return_type->str().c_str()));

	debug_above(5, log(log_info, "function return_type %s expands to %s",
				function->return_type->str().c_str(),
				eval(function->return_type, scope->get_typename_env())->str().c_str()));

	if (auto args = dyncast<const types::type_args_t>(function->args)) {
		bound_type_t::refs bound_args = upsert_bound_types(status, builder, scope, args->args);

		if (!!status) {
			/* now that we know the parameter types, let's see what the term looks
			 * like */
			debug_above(5, log(log_info, "ctor type should be %s",
						function->str().c_str()));

			if (function->return_type != nullptr) {
				/* now we know the type of the ctor we want to create. let's check
				 * whether this ctor already exists. if so, we'll just return it. if
				 * not, we'll generate it. */
				auto tuple_pair = instantiate_tagged_tuple_ctor(status, builder, scope, id, node,
						function->return_type);

				if (!!status) {
					debug_above(5, log(log_info, "created a ctor %s", tuple_pair.first->str().c_str()));
					return tuple_pair.first;
				}
			} else {
				user_error(status, node->get_location(),
						"constructor is not returning a product type: %s",
						function->str().c_str());
			}
		}
	} else {
		user_error(status, node->get_location(), "arguments do not appear to be ... erm... arguments...");
	}

	assert(!status);
	return nullptr;
}

void get_generics_and_lambda_vars(
		status_t &status,
	   	types::type_t::ref subtype,
		identifier::refs type_variables,
	   	scope_t::ref scope,
		std::list<identifier::ref> &lambda_vars,
		std::set<std::string> &generics)
{
	assert(generics.size() == 0);
	assert(lambda_vars.size() == 0);
	debug_above(5, log(log_info, "get_generics_and_lambda_vars(%s, %s)",
				subtype->str().c_str(),
				::str(type_variables).c_str()));

	/* create a type that takes the used type variables in the data ctor and
	 * returns placement in given type variable order */
	/* instantiate the necessary components of a data ctor */
	generics = to_atom_set(type_variables);

	/* ensure that there are no duplicate type variables */
	if (generics.size() != type_variables.size()) {
		/* this is a fail because there are some reused type variables, find
		 * them and report on them */
		std::set<std::string> seen;
		for (auto type_variable : type_variables) {
			std::string name = type_variable->get_name();
			if (seen.find(name) == seen.end()) {
				seen.insert(name);
			} else {
				user_error(status, type_variable->get_location(),
						"found duplicate type variable " c_id("%s"),
						name.c_str());
			}
		}
	} else {
		debug_above(5, log(log_info,
				   	"getting lambda_vars for value type %s",
					subtype->str().c_str()));

		/* if any of the type names are actually inbound type variables, take
		 * note of the order they are mentioned. tell us how to create the
		 * lambda we'll place into the type environment to represent the fact
		 * that this data ctor is a subtype of the supertype. and, tell us which
		 * types are parametrically bound to this subtype, and which are still
		 * quantified */

		std::set<std::string> unbound_vars = subtype->get_ftvs();
		for (auto type_var : type_variables) {
			/* this variable is referenced by the current data ctor (the
			 * subtype), therefore it has opinions about its role in the
			 * supertype */
			lambda_vars.push_front(type_var);
		}
		assert(lambda_vars.size() == type_variables.size());
	}
}

void instantiate_data_ctor_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		types::type_t::ref unbound_type,
		identifier::refs type_variables,
		scope_t::ref scope,
		ptr<const ast::item_t> node,
		identifier::ref id,
		identifier::ref supertype_id,
		bool native)
{
	/* get the name of the ctor */
	std::string tag_name = id->get_name();
	std::string fqn_tag_name = scope->make_fqn(tag_name);
	auto qualified_id = make_iid_impl(fqn_tag_name, id->get_location());

	/* create the tag type */
	auto tag_type = type_id(qualified_id);

	/* create the basic struct type */
	ptr<const types::type_struct_t> struct_ = dyncast<const types::type_struct_t>(unbound_type);
	assert(struct_ != nullptr);

	/* lambda_vars tracks the order of the lambda variables we'll accept as we abstract our
	 * supertype expansion */
	std::list<identifier::ref> lambda_vars;
	std::set<std::string> generics;

	get_generics_and_lambda_vars(status, struct_, type_variables, scope,
			lambda_vars, generics);

	if (!status) {
		return;
	}

	/**********************************************/
	/* Register a data ctor for this struct_ type */
	/**********************************************/
	assert(id->get_name() == tag_name);

	/* we're declaring a ctor at module scope */
	if (auto module_scope = dyncast<module_scope_t>(scope)) {

		/* let's create the return type (an unexpanded operator) that will be the codomain of the ctor fn. */
		auto ctor_return_type = tag_type;
		for (auto lambda_var_iter = lambda_vars.rbegin(); lambda_var_iter != lambda_vars.rend(); ++lambda_var_iter) {
			ctor_return_type = type_operator(ctor_return_type, type_variable(*lambda_var_iter));
		}

		/* for now assume all ctors return refs */
		debug_above(4, log(log_info, "return type for %s will be %s",
					id->str().c_str(), ctor_return_type->str().c_str()));

		/* we need to register this constructor as an override for the name `tag_name` */
		debug_above(2, log(log_info, "adding %s as an unchecked generic data_ctor",
					id->str().c_str()));

		types::type_function_t::ref data_ctor_sig = type_function(
				type_args(struct_->dimensions),
				ctor_return_type);

		module_scope->get_program_scope()->put_unchecked_variable(tag_name,
				unchecked_data_ctor_t::create(id, node,
					module_scope, data_ctor_sig, native));

		/* now build the actual typename expansion we'll put in the typename env */
		/* 1. create the actual expanded type signature of this type */
		types::type_t::ref type;
		if (native) {
			type = struct_;
		} else {
			type = type_ptr(type_managed(struct_));
		}

		/* 2. make sure we allow for parameterized expansion */
		for (auto lambda_var : lambda_vars) {
			type = type_lambda(lambda_var, type);
		}

		scope->put_typename(status, tag_name, type);

		return;
	} else {
		user_error(status, node->token.location, "local type definitions are not yet impl");
	}

	assert(!status);
}

void ast::type_product_t::register_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::ref id_,
		identifier::refs type_variables,
		scope_t::ref scope) const
{
	debug_above(5, log(log_info, "creating product type for %s", str().c_str()));

	std::string name = id_->get_name();
	auto location = id_->get_location();

	if (auto found_type = scope->get_bound_type(id_->get_name())) {
		/* simple check for an already bound monotype */
		user_error(status, location, "symbol " c_id("%s") " was already defined",
				name.c_str());
		user_message(log_warning, status, found_type->get_location(),
				"previous version of %s defined here",
				found_type->str().c_str());
	} else {
		/* instantiate a lazily bound data ctor, and inject the typename for this type into the
		 * type environment */
		auto env = scope->get_typename_env();
		auto env_iter = env.find(name);
		if (env_iter == env.end()) {
			/* instantiate_data_ctor_type has the side-effect of creating an
			 * unchecked data ctor for the type */
			instantiate_data_ctor_type(status, builder, type,
					type_variables, scope, shared_from_this(), id_, nullptr, native);
			return;
		} else {
			/* simple check for an already bound typename env variable */
			user_error(status, location,
					"symbol " c_id("%s") " is already taken in typename env by %s",
					name.c_str(),
					env_iter->second->str().c_str());
		}
	}

	assert(!status);
}

void ast::type_sum_t::register_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::ref id,
		identifier::refs type_variables,
		scope_t::ref scope) const
{
	debug_above(3, log(log_info, "creating subtypes to %s with type variables [%s]",
				token.text.c_str(),
				join(type_variables, ", ").c_str()));

	scope->put_typename(status, id->get_name(), type);
}

void ast::type_link_t::register_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::ref id,
		identifier::refs type_variables,
		scope_t::ref scope) const
{
	debug_above(3, log("registering type link for %s link " c_type("%s")
			   	", " c_id("%s") ", " c_id("%s"),
				id->get_name().c_str(),
				underlying_type->str().c_str(),
				finalize_fn.text.c_str(),
				mark_fn.text.c_str()));

	/* first construct the inner type which will basically be a call back to the outer type.
	 * type_links are constructed recursively - being defined by themselves - since they are not
	 * defined inside the language. */
	types::type_t::ref inner = type_id(id);
	for (auto type_variable : type_variables) {
		inner = type_operator(inner, ::type_variable(type_variable));
	}
		
	/* now construct the lambda that points back to the type */
	auto type = type_extern(inner, underlying_type, make_code_id(finalize_fn), make_code_id(mark_fn));
	for (auto iter = type_variables.rbegin();
			iter != type_variables.rend();
			++iter)
	{
		type = ::type_lambda(*iter, type);
	}

	scope->put_typename(status, id->get_name(), type);

	if (!!status) {
		return;
	}

	assert(!status);
	return;
}
