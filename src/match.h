#pragma once
#include "types.h"

namespace match {
	struct Pattern;

	struct CtorPattern;
	struct CtorPatterns;
	struct AllOf;
	struct Nothing;

	struct Pattern {
		typedef ptr<const Pattern> ref;

		location_t location;

		Pattern(location_t location) : location(location) {}
		virtual ~Pattern() {}

		virtual ptr<const CtorPattern> asCtorPattern() const { return nullptr; }
		virtual ptr<const CtorPatterns> asCtorPatterns() const { return nullptr; }
		virtual ptr<const AllOf> asAllOf() const { return nullptr; }
		virtual ptr<const Nothing> asNothing() const { return nullptr; }
		virtual std::string str() const = 0;
	};

	struct CtorPatternValue {
		std::string type_name;
		std::string name;
		std::vector<Pattern::ref> args;

		std::string str() const;
	};

	struct CtorPattern : std::enable_shared_from_this<CtorPattern>, Pattern {
		CtorPatternValue cpv;
		CtorPattern(location_t location, CtorPatternValue cpv);

		virtual ptr<const CtorPattern> asCtorPattern() const { return shared_from_this(); }
		virtual std::string str() const;
	};

	struct CtorPatterns : std::enable_shared_from_this<CtorPatterns>, Pattern {
		std::vector<CtorPatternValue> cpvs;
		CtorPatterns(location_t location, std::vector<CtorPatternValue> cpvs);

		virtual ptr<const CtorPatterns> asCtorPatterns() const { return shared_from_this(); }
		virtual std::string str() const;
	};

	struct AllOf : std::enable_shared_from_this<AllOf>, Pattern {
		env_t::ref env;
		types::type_t::ref type;
		AllOf(location_t location, env_t::ref env, types::type_t::ref type);

		virtual ptr<const AllOf> asAllOf() const { return shared_from_this(); }
		virtual std::string str() const;
	};

	struct Nothing : std::enable_shared_from_this<Nothing>, Pattern {
		Nothing() : Pattern(INTERNAL_LOC()) {}

		virtual ptr<const Nothing> asNothing() const { return shared_from_this(); }
		virtual std::string str() const;
	};

	extern ptr<Nothing> theNothing;
	Pattern::ref intersect(Pattern::ref lhs, Pattern::ref rhs);
	Pattern::ref difference(Pattern::ref lhs, Pattern::ref rhs);
	Pattern::ref pattern_union(Pattern::ref lhs, Pattern::ref rhs);
}
