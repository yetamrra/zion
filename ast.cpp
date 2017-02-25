#include "zion.h"
#include "ast.h"
#include "type_checker.h"
#include "disk.h"
#include "scopes.h"
#include "utils.h"

const char *skstr(syntax_kind_t sk) {
	switch (sk) {

#define sk_case(x) case sk_##x: return (":" #x);

		sk_case(nil)

#define OP sk_case
#include "sk_ops.h"
#undef OP

		sk_case(expression)
		sk_case(statement)
	};
	return "<error>";
}

namespace ast {
	void log_named_item_create(const char *type, const std::string &name) {
		if (name.size() > 0) {
			debug_above(9, log(log_info, "creating a " c_ast("%s") " named " c_var("%s"),
						type, name.c_str()));
		} else {
			debug_above(9, log(log_info, "creating a " c_ast("%s"), type));
		}
	}

	module_t::module_t(const atom filename, bool global) : global(global), filename(filename) {
	}

	std::string module_t::get_canonical_name() const {
		return decl->get_canonical_name();
	}

	zion_token_t module_decl_t::get_name() const {
		return name;
	}

	std::string module_decl_t::get_canonical_name() const {
		static std::string ext = ".zion";
		if (name.text == "_") {
			/* this name is too generic, let's use the leaf filename */
			std::string filename = name.location.filename.str();
			auto leaf = leaf_from_file_path(filename);
			if (ends_with(leaf, ext)) {
				return leaf.substr(0, leaf.size() - ext.size());
			} else {
				return leaf;
			}
		} else {
			return name.text;
		}
	}

	item_t::~item_t() throw() {
	}

	typeid_expr_t::typeid_expr_t(ptr<expression_t> expr) : expr(expr) {
	}

	sizeof_expr_t::sizeof_expr_t(types::type_t::ref type) : type(type) {
	}

	type_decl_t::type_decl_t(identifier::refs type_variables) :
		type_variables(type_variables)
	{
	}

	type_sum_t::type_sum_t(types::type_t::ref type) :
		type(type)
	{
	}

	dimension_t::dimension_t(atom name, types::type_t::ref type) :
		name(name), type(type)
	{
	}

	type_product_t::type_product_t(
			types::type_t::ref type,
			identifier::set type_variables) :
		type(type),
		type_variables(type_variables)
	{
	}
}
