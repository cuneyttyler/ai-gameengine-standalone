#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <string>

namespace aige {

// Where things are in a solved layout, in words a model (and a user) can
// check a request against: "the farmland lies south of the residential ward,
// not west of it". The plan says what was asked; this says what came out.
//
// On the map, x grows east (right) and y grows south (down): "left" is west,
// "below" is south.

// {"zones": [{id, name, area, centre, extent, river: {touches, bank, nearest},
//   others: [{zone, lies, distance, touching}]}], "map": [rows], "legend": {letter: id}}.
// `names` gives zones their display names (else their ids).
nlohmann::json LayoutWhere(const nlohmann::json& solved, const std::map<std::string, std::string>& names = {});

// Checks spatial facts on a solved layout: [{node, type, to}] with the types
// north-of, south-of, east-of, west-of, near, adjacent, along, far; `to` a
// zone id or a path (river, a road's id). Returns them with "ok" and "detail"
// ("lies south of residential-ward"). Unknown parts: ok false, said so.
nlohmann::json CheckExpectations(const nlohmann::json& solved, const nlohmann::json& expects);

} // namespace aige
