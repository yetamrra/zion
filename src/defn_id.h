#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "identifier.h"
#include "types.h"
#include "scheme.h"

namespace types {
struct Scheme;
struct Type;
} // namespace types

struct DefnId {
  DefnId(Identifier const id, const types::SchemeRef &scheme)
      : id(id), scheme(scheme) {
  }

  Identifier const id;
  types::SchemeRef const scheme;

  /* convert all free type variables to type unit */
  DefnId unitize() const;

private:
  mutable std::string cached_repr;
  std::string repr() const;
  Identifier repr_id() const;

public:
  std::string repr_public() const {
    return repr();
  }
  Location get_location() const;
  std::string str() const;
  bool operator<(const DefnId &rhs) const;
  std::shared_ptr<const types::Type> get_lambda_param_type() const;
  std::shared_ptr<const types::Type> get_lambda_return_type() const;
};

struct DefnRef {
  Location location;
  DefnId from_defn_id;
};

typedef std::map<DefnId, std::vector<DefnRef>> NeededDefns;
std::ostream &operator<<(std::ostream &os, const DefnId &defn_id);

void insert_needed_defn(NeededDefns &needed_defns,
                        const DefnId &defn_id,
                        Location location,
                        const DefnId &from_defn_id);

