#pragma once
#include "lexer.h"
#include "ast.h"
#include <memory>
#include <sstream>
#include "parse_state.h"

template <typename T, typename... Args>
ptr<T> parse_text(std::istream &is, std::string filename = "repl.zion") {
	zion_lexer_t lexer(filename, is);
	status_t status;
	type_macros_t global_type_macros;
	parse_state_t ps(status, filename, lexer, {}, global_type_macros);
	ps.module_id = make_iid("__parse_text__");

	auto item = T::parse(ps);
	if (ps.token.tk != tk_none) {
		assert(!status);
		return nullptr;
	}
	return item;
}

template <typename T, typename... Args>
ptr<T> parse_text(const std::string &text, std::string filename = "repl.zion") {
	std::istringstream iss(text);
	return parse_text<T>(iss, filename);
}

types::type_t::ref parse_maybe_type(parse_state_t &ps,
	   	identifier::ref supertype_id,
	   	identifier::refs type_variables,
	   	identifier::set generics);

types::type_t::ref _parse_type(
		parse_state_t &ps,
	   	identifier::ref supertype_id,
	   	identifier::refs type_variables,
	   	identifier::set generics);
