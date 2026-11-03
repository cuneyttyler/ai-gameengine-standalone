#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aige {

// A screen made in the UI designer (docs/systems/game-ui.md §12): ui/designs/<id>.json, a tree of elements, from which
// ui/screens/<id>.rml is generated. The game runs the generated document, as it runs a hand-written one.
//
// {"schema": "aige.ui-design/1", "id", "model": "hud" | "menu" | "dialogue", "width", "height", "root": [node...]}
// A node: {"id", "type", "name", "hidden", "anchor", "x", "y", "w", "h", "opacity", "bg", "image", "slice", "imgW", "imgH",
//          "states", "text", "size", "color", "align", "bold", "padding", "actions", "show", "bar", "list", "slots",
//          "input", "rml", "children"}
//  - type: panel, image, button, text, bar, list, slots, input, notes, rml.
//  - anchor: tl t tr l c r bl b br (x, y from that point of its parent, w and h its size); stretch (x, y, w, h the
//    margins from its parent's left, top, right, bottom); across: stretch-top, stretch-bottom, stretch-x (the middle);
//    down: stretch-left, stretch-right, stretch-y (the middle).
//  - actions, on click: {"do": "open"|"close"|"event"|"press"|"flag"|"clear"|"effects"|"quit"|"save"|"load", "arg"}.
//  - show: Conditions, as dialogue's: it shows only while they hold.
//  - rml: a block of RML kept as it is (a hand-written screen's body, or what the designer has no element for).
std::string DesignToRml(const nlohmann::json& design);
// What's wrong with it: for the designer's checks and the AI panel's.
std::vector<std::string> CheckDesign(const nlohmann::json& design);
// A screen's hand-written RML as a design: its head and body kept whole, so nothing is lost; it shows in the designer as
// one Custom RML element, and elements can be added beside it.
nlohmann::json DesignFromRml(const std::string& id, const std::string& rml);

// The text an action or condition carries through RML, where quotes can't go.
std::string HexEncode(const std::string& text);
std::string HexDecode(const std::string& hex);

} // namespace aige
