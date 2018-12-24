#include "prefix.h"

using namespace bitter;

std::string prefix(const std::set<std::string> &bindings, std::string pre, std::string name) {
	if (in(name, bindings)) {
		return pre + "." + name;
	} else {
		return name;
	}
}

identifier_t prefix(const std::set<std::string> &bindings, std::string pre, identifier_t name) {
	return {prefix(bindings, pre, name.name), name.location};
}

token_t prefix(const std::set<std::string> &bindings, std::string pre, token_t name) {
	assert(name.tk == tk_identifier);
	return token_t{name.location, tk_identifier, prefix(bindings, pre, name.text)};
}

expr_t *prefix(const std::set<std::string> &bindings, std::string pre, expr_t *value);

predicate_t *prefix(
		const std::set<std::string> &bindings,
	   	std::string pre,
	   	predicate_t *predicate,
	   	std::set<std::string> &new_symbols)
{
	if (auto p = dcast<tuple_predicate_t *>(predicate)) {
		if (p->name_assignment.valid) {
			new_symbols.insert(p->name_assignment.t.name);
		}
		for (auto param : p->params) {
			prefix(bindings, pre, param, new_symbols);
		}
		return predicate;
	} else if (auto p = dcast<irrefutable_predicate_t *>(predicate)) {
		if (p->name_assignment.valid) {
			new_symbols.insert(p->name_assignment.t.name);
		}
		return predicate;
	} else if (auto p = dcast<ctor_predicate_t *>(predicate)) {
		if (p->name_assignment.valid) {
			new_symbols.insert(p->name_assignment.t.name);
		}
		for (auto param : p->params) {
			prefix(bindings, pre, param, new_symbols);
		}
		return predicate;
	} else {
		assert(false);
		return nullptr;
	}
}

pattern_block_t *prefix(const std::set<std::string> &bindings, std::string pre, pattern_block_t *pattern_block) {
	std::set<std::string> new_symbols;
	predicate_t *new_predicate = prefix(bindings, pre, pattern_block->predicate, new_symbols);

	return new pattern_block_t(
			new_predicate,
			prefix(set_diff(bindings, new_symbols), pre, pattern_block->result));
}

decl_t *prefix(const std::set<std::string> &bindings, std::string pre, decl_t *value) {
	return new decl_t(
			prefix(bindings, pre, value->var), 
			prefix(bindings, pre, value->value));
}

type_decl_t prefix(const std::set<std::string> &bindings, std::string pre, const type_decl_t &type_decl) {
	return type_decl_t{prefix(bindings, pre, type_decl.id), type_decl.params};
}

std::set<std::string> only_uppercase_bindings(const std::set<std::string> &bindings) {
	std::set<std::string> only_uppercase_bindings;
	for (auto binding : bindings) {
		if (isupper(binding[0])) {
			only_uppercase_bindings.insert(binding);
		}
	}
	return only_uppercase_bindings;
}

type_class_t *prefix(const std::set<std::string> &bindings, std::string pre, type_class_t *type_class) {
	auto uppercase_bindings = only_uppercase_bindings(bindings);
	return new type_class_t(
			prefix(bindings, pre, type_class->id),
			prefix(uppercase_bindings, pre, type_class->params),
			prefix(bindings, pre, type_class->superclasses),
			prefix(bindings, pre, type_class->overloads));
}

types::type_t::ref prefix(const std::set<std::string> &bindings, std::string pre, types::type_t::ref type) {
	if (type == nullptr) {
		return nullptr;
	}

	std::set<std::string> uppercase_bindings = only_uppercase_bindings(bindings);
	return type->prefix_ids(uppercase_bindings, pre);
}

expr_t *prefix(const std::set<std::string> &bindings, std::string pre, expr_t *value) {
	if (auto var = dcast<var_t*>(value)) {
		return new var_t(prefix(bindings, pre, var->id));
	} else if (auto match = dcast<match_t*>(value)) {
		return new match_t(
				prefix(bindings, pre, match->scrutinee),
				prefix(bindings, pre, match->pattern_blocks));
	} else if (auto block = dcast<block_t*>(value)) {
		return new block_t(prefix(bindings, pre, block->statements));
	} else if (auto as = dcast<as_t*>(value)) {
		return new as_t(
				prefix(bindings, pre, as->expr),
			   	prefix(bindings, pre, as->type),
			   	as->force_cast);
	} else if (auto application = dcast<application_t*>(value)) {
		return new application_t(
				prefix(bindings, pre, application->a),
				prefix(bindings, pre, application->b));
	} else if (auto lambda = dcast<lambda_t*>(value)) {
		return new lambda_t(
				lambda->var,
				prefix(bindings, pre, lambda->param_type),
				prefix(bindings, pre, lambda->return_type),
				prefix(
					without(bindings,lambda->var.name),
					pre,
					lambda->body));
	} else if (auto let = dcast<let_t*>(value)) {
		return new let_t(
				let->var,
				prefix(
					without(bindings, let->var.name),
					pre,
					let->value),
				prefix(
					without(bindings, let->var.name),
					pre,
					let->body));
	} else if (auto conditional = dcast<conditional_t*>(value)) {
		return new conditional_t(
				prefix(bindings, pre, conditional->cond),
				prefix(bindings, pre, conditional->truthy),
				prefix(bindings, pre, conditional->falsey));
	} else if (auto ret = dcast<return_statement_t*>(value)) {
		return new return_statement_t(prefix(bindings, pre, ret->value));
	} else if (auto fix = dcast<fix_t*>(value)) {
		return new fix_t(prefix(bindings, pre, fix->f));
	} else if (auto while_ = dcast<while_t*>(value)) {
		return new while_t(
				prefix(bindings, pre, while_->condition),
				prefix(bindings, pre, while_->block));
	} else if (auto literal = dcast<literal_t*>(value)) {
		return value;
	} else if (auto tuple = dcast<tuple_t*>(value)) {
		return new tuple_t(
				tuple->location,
				prefix(bindings, pre, tuple->dims));
	} else {
		std::cerr << "What should I do with " << value->str() << "?" << std::endl;
		assert(false);
		return nullptr;
	}
}
	
std::vector<expr_t *> prefix(const std::set<std::string> &bindings, std::string pre, std::vector<expr_t *> values) {
	std::vector<expr_t *> new_values;
	for (auto value : values) {
		new_values.push_back(prefix(bindings, pre, value));
	}
	return new_values;
}

module_t *prefix(const std::set<std::string> &bindings, module_t *module) {
	return new module_t(
			module->name,
		   	prefix(bindings,
			   	module->name,
			   	module->decls),
		   	prefix(bindings,
			   	module->name,
			   	module->type_decls),
		   	prefix(bindings,
			   	module->name,
			   	module->type_classes));
}
