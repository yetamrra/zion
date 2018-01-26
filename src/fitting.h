#pragma once

struct fitting_t {
	bound_var_t::ref fn;
	int coercions;
};

typedef std::vector<fitting_t> fittings_t;

bound_var_t::ref get_best_fit(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		std::string alias,
		types::type_args_t::ref args,
		types::type_t::ref return_type,
		var_t::refs &fns);
