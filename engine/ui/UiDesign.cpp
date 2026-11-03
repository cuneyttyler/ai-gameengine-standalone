#include "ui/UiDesign.h"

#include <cctype>
#include <cstdio>
#include <functional>
#include <set>
#include <sstream>

namespace aige {

using json = nlohmann::json;

namespace {

std::string Num(double v) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", v);
    return buffer;
}

std::string Escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

// A class-safe id.
std::string Safe(const std::string& id) {
    std::string out;
    for (char c : id) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
    return out.empty() ? "n" : out;
}

// A colour: a theme token ("panel", "accent": var(--panel)) or a CSS colour as it is.
std::string Colour(const std::string& value) {
    if (value.empty()) return {};
    if (value[0] == '#' || value.rfind("rgb", 0) == 0) return value;
    return "var(--" + value + ")";
}

// A picture's path as a document reads it: the project's from its root.
std::string Picture(const std::string& path) { return path.empty() || path[0] == '/' ? path : "/" + path; }

// Where it sits in its parent, from its anchor: a corner, an edge's middle or the middle (x, y from there; w, h its size),
// or stretched across (x and w its margins from the left and right) and/or down (y and h from the top and bottom).
struct Sides {
    char horizontal, vertical; // l c r s, t c b s
};
Sides SidesOf(const std::string& anchor) {
    if (anchor == "stretch") return {'s', 's'};
    if (anchor == "stretch-x") return {'s', 'c'};
    if (anchor == "stretch-top") return {'s', 't'};
    if (anchor == "stretch-bottom") return {'s', 'b'};
    if (anchor == "stretch-y") return {'c', 's'};
    if (anchor == "stretch-left") return {'l', 's'};
    if (anchor == "stretch-right") return {'r', 's'};
    const char h = anchor.find('l') != std::string::npos ? 'l' : anchor.find('r') != std::string::npos ? 'r' : 'c';
    const char v = anchor.find('t') != std::string::npos ? 't' : anchor.find('b') != std::string::npos ? 'b' : 'c';
    return {h, v};
}

std::string Rect(const json& n) {
    const Sides sides = SidesOf(n.value("anchor", std::string("tl")));
    const double x = n.value("x", 0.0), y = n.value("y", 0.0), w = n.value("w", 100.0), h = n.value("h", 40.0);
    std::string css = "position: absolute; ";
    if (sides.horizontal == 's') css += "left: " + Num(x) + "dp; right: " + Num(w) + "dp; ";
    else if (sides.horizontal == 'l') css += "left: " + Num(x) + "dp; width: " + Num(w) + "dp; ";
    else if (sides.horizontal == 'r') css += "right: " + Num(x) + "dp; width: " + Num(w) + "dp; ";
    else css += "left: 50%; margin-left: " + Num(x - w / 2) + "dp; width: " + Num(w) + "dp; ";
    if (sides.vertical == 's') css += "top: " + Num(y) + "dp; bottom: " + Num(h) + "dp; ";
    else if (sides.vertical == 't') css += "top: " + Num(y) + "dp; height: " + Num(h) + "dp; ";
    else if (sides.vertical == 'b') css += "bottom: " + Num(y) + "dp; height: " + Num(h) + "dp; ";
    else css += "top: 50%; margin-top: " + Num(y - h / 2) + "dp; height: " + Num(h) + "dp; ";
    return css;
}

// What's over people's heads (game-ui.md §13): each element type and the hud model's list it repeats for.
const char* OverheadSource(const std::string& type) {
    return type == "bubbles" ? "bubbles" : type == "labels" ? "labels" : type == "numbers" ? "numbers" : nullptr;
}

struct Writer {
    std::string model;
    std::ostringstream style, body, notes, outside; // outside: blocks with their own data models
    std::set<std::string> sheets, fonts;

    // A font of the project's (a .ttf or .otf): loaded once, named by its file (as the engine's font engine knows it).
    std::string Font(const std::string& path) {
        if (path.empty()) return {};
        std::string family = path.substr(path.find_last_of("/\\") + 1);
        family = family.substr(0, family.find_last_of('.'));
        if (fonts.insert(path).second) style << "@font-face { font-family: " << family << "; src: " << Picture(path) << "; }\n";
        return "font-family: " + family + "; ";
    }

    // A picture as a background: whole, or in nine pieces (its corners kept, its edges and middle stretched).
    std::string Decorator(const json& n, const std::string& image, const std::string& name) {
        if (image.empty()) return {};
        const json slice = n.value("slice", json::array());
        const int iw = n.value("imgW", 0), ih = n.value("imgH", 0);
        if (slice.is_array() && slice.size() == 4 && iw > 0 && ih > 0 &&
            (slice[0].get<int>() > 0 || slice[1].get<int>() > 0 || slice[2].get<int>() > 0 || slice[3].get<int>() > 0)) {
            const int l = slice[0], t = slice[1], r = slice[2], b = slice[3];
            const std::string sheet = "s-" + name;
            if (sheets.insert(sheet).second) {
                style << "@spritesheet " << sheet << " { src: " << Picture(image) << "; " << sheet << "-outer: 0px 0px " << iw << "px "
                      << ih << "px; " << sheet << "-inner: " << l << "px " << t << "px " << (iw - l - r) << "px " << (ih - t - b) << "px; }\n";
            }
            return "decorator: ninepatch(" + sheet + "-outer, " + sheet + "-inner, 1.0); ";
        }
        return "decorator: image(" + Picture(image) + "); ";
    }

    std::string Look(const json& n, const std::string& name, const json& state) {
        std::string css;
        if (const std::string bg = Colour(state.value("bg", std::string())); !bg.empty()) css += "background-color: " + bg + "; ";
        css += Decorator(n, state.value("image", std::string()), name);
        if (const std::string c = Colour(state.value("color", std::string())); !c.empty()) css += "color: " + c + "; ";
        return css;
    }

    std::string Click(const json& n) {
        std::string calls;
        for (const auto& a : n.value("actions", json::array())) {
            const std::string what = a.value("do", std::string());
            if (what.empty()) continue;
            std::string arg = a.contains("arg") ? (a["arg"].is_string() ? a["arg"].get<std::string>() : a["arg"].dump()) : std::string();
            // Effects (JSON) and anything with a character a quoted name can't hold go as hex.
            const bool plain = !arg.empty() && arg.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-./") == std::string::npos;
            const std::string call = what == "effects" || (!arg.empty() && !plain) ? what + "~" + HexEncode(arg) : arg.empty() ? what : what + ":" + arg;
            calls += (calls.empty() ? "" : "; ") + std::string("action('") + call + "')";
        }
        return calls;
    }

    void Node(const json& n, std::ostringstream& out, int depth) {
        const std::string type = n.value("type", std::string("panel"));
        if (n.value("hidden", false)) return;
        const std::string pad(static_cast<size_t>(depth) * 2 + 4, ' ');
        if (type == "rml") {
            // At the top, beside the designer's model (a hand-written body brings its own); inside an element, as it is.
            (depth == 0 ? outside : out) << pad << n.value("rml", std::string()) << "\n";
            return;
        }
        if (type == "notes" && depth > 0) return; // notifications sit at the top, in their own model
        const std::string id = Safe(n.value("id", std::string("n")));
        const std::string cls = "n-" + id;
        // Its look, and a button's states. Over someone's head: its bottom middle on the point above them (x, y move it
        // from there), as wide as its text up to w.
        const char* overhead = OverheadSource(type);
        std::string css = overhead ? "position: absolute; transform: translate(-50%, -100%); white-space: nowrap; max-width: " +
                                         Num(n.value("w", 260.0)) + "dp; margin-left: " + Num(n.value("x", 0.0)) + "dp; margin-top: " +
                                         Num(-n.value("y", 0.0)) + "dp; "
                                   : Rect(n);
        if (overhead && n.value("wrap", false)) css += "white-space: normal; width: " + Num(n.value("w", 260.0)) + "dp; ";
        css += Font(n.value("font", std::string()));
        if (n.contains("opacity") && n["opacity"].is_number()) css += "opacity: " + Num(n["opacity"].get<double>()) + "; ";
        css += Look(n, id, {{"bg", n.value("bg", std::string())}, {"image", n.value("image", std::string())}, {"color", n.value("color", std::string())}});
        if (n.contains("size")) css += "font-size: " + Num(n.value("size", 16.0)) + "dp; ";
        if (n.contains("align")) css += "text-align: " + n.value("align", std::string("left")) + "; ";
        if (n.value("bold", false)) css += "font-weight: bold; ";
        if (n.contains("padding")) css += "padding: " + Num(n.value("padding", 0.0)) + "dp; ";
        if (type == "button" || type == "slots" || type == "list" || type == "input") css += "pointer-events: auto; ";
        if (type == "button") css += "cursor: pointer; ";
        style << "." << cls << " { " << css << "}\n";
        const json states = n.value("states", json::object());
        for (const auto& [state, selector] : {std::pair<const char*, const char*>{"hover", ":hover"}, {"pressed", ":active"}, {"disabled", ".off"}}) {
            if (states.contains(state) && states[state].is_object()) {
                const std::string look = Look(n, id + "-" + state, states[state]);
                if (!look.empty()) style << "." << cls << selector << " { " << look << "}\n";
            }
        }
        // When it shows: its conditions (the game answers them each frame).
        std::string attrs = " class=\"" + cls + "\"";
        if (const json show = n.value("show", json::array()); show.is_array() && !show.empty()) {
            attrs += " data-if=\"tick | holds('" + HexEncode(show.dump()) + "')\"";
        }
        if (const std::string click = Click(n); !click.empty()) attrs += " data-event-click=\"" + click + "\"";
        const std::string text = Escape(n.value("text", std::string()));
        if (overhead) {
            // One for each: its text says {{it.text}} (and a bubble's {{it.name}}); numbers styled by their kind.
            const json kinds = n.value("kinds", json::object());
            for (const auto& [kind, colour] : kinds.items()) {
                if (colour.is_string() && !colour.get<std::string>().empty()) style << "." << cls << "." << Safe(kind) << " { color: " << Colour(colour.get<std::string>()) << "; }\n";
            }
            const json show = n.value("show", json::array());
            const bool gated = show.is_array() && !show.empty();
            if (gated) out << pad << "<div style=\"position: absolute; left: 0; top: 0; right: 0; bottom: 0;\" data-if=\"tick | holds('" << HexEncode(show.dump()) << "')\">\n";
            out << pad << "<div class=\"" << cls << "\" data-for=\"it : " << overhead
                << "\" data-style-left=\"it.x + 'px'\" data-style-top=\"it.y + 'px'\" data-style-opacity=\"it.alpha\"";
            if (type == "numbers") {
                for (const char* kind : {"damage", "heal", "item"}) out << " data-class-" << kind << "=\"it.kind == '" << kind << "'\"";
            }
            out << ">" << (text.empty() ? std::string("{{it.text}}") : text) << "\n";
            for (const auto& child : n.value("children", json::array())) Node(child, out, depth + 1);
            out << pad << "</div>\n";
            if (gated) out << pad << "</div>\n";
        } else if (type == "bar") {
            const json bar = n.value("bar", json::object());
            const std::string value = bar.value("value", std::string("health")), max = bar.value("max", std::string("health_max"));
            out << pad << "<div" << attrs << " data-if=\"" << max << " > 0\">"
                << "<div style=\"height: 100%; background-color: " << Colour(bar.value("color", std::string("health")))
                << ";\" data-style-width=\"(" << value << " / (" << max << " > 0 ? " << max << " : 1) * 100) + '%'\"></div></div>\n";
        } else if (type == "list") {
            const json list = n.value("list", json::object());
            const std::string source = list.value("source", std::string(model == "dialogue" ? "choices" : "items"));
            const std::string row = list.value("row", std::string(source == "items" && model == "hud" ? "{{item.name}}" : "{{item.label}}"));
            const double rowH = list.value("rowH", 32.0);
            const bool chooses = model == "menu" || model == "dialogue";
            out << pad << "<div" << attrs << ">\n" << pad << "  <div class=\"item\" style=\"height: " << Num(rowH) << "dp;\" data-for=\"item, i : "
                << source << "\"" << (chooses ? " data-class-lit=\"i == highlighted\" data-event-click=\"choose(i)\"" : "")
                << (model == "menu" ? " data-class-off=\"!item.enabled\" data-event-mousedown=\"context(i)\"" : "") << ">" << Escape(row) << "</div>\n"
                << pad << "</div>\n";
        } else if (type == "slots") {
            const json slots = n.value("slots", json::object());
            const double cell = slots.value("cell", 64.0), gap = slots.value("gap", 6.0);
            const std::string slotLook = Look(n, id + "-slot", slots.value("look", json::object()));
            style << "." << cls << " .slot { display: inline-block; position: relative; width: " << Num(cell) << "dp; height: " << Num(cell)
                  << "dp; margin: 0 " << Num(gap) << "dp " << Num(gap) << "dp 0; " << (slotLook.empty() ? "background-color: var(--panel); " : slotLook) << "}\n";
            style << "." << cls << " .slot img { width: 100%; height: 100%; }\n";
            style << "." << cls << " .slot span { position: absolute; right: 4dp; bottom: 2dp; font-size: 13dp; }\n";
            out << pad << "<div" << attrs << ">\n" << pad
                << "  <div class=\"slot\" data-for=\"item : items\" data-attr-title=\"item.name\" data-event-click=\"action('use:' + item.id)\">"
                   "<img data-if=\"item.icon != ''\" data-attr-src=\"item.icon\"/><span data-if=\"item.count > 1\">{{item.count}}</span></div>\n"
                << pad << "</div>\n";
        } else if (type == "input") {
            const json input = n.value("input", json::object());
            const std::string kind = input.value("kind", std::string("checkbox"));
            const std::string event = input.value("event", std::string());
            out << pad << "<input" << attrs << " type=\"" << (kind == "slider" ? "range" : kind) << "\""
                << (event.empty() ? "" : " data-event-change=\"action('event:" + event + "')\"") << "/>\n";
        } else if (type == "notes") {
            notes << "    <div" << attrs << ">\n";
            for (const char* channel : {"toast", "feed", "banner"}) {
                notes << "      <div class=\"" << channel << "\" data-for=\"note : " << channel << "\">{{note.text}}</div>\n";
            }
            notes << "    </div>\n";
        } else {
            const char* tag = "div";
            out << pad << "<" << tag << attrs << ">" << text << "\n";
            for (const auto& child : n.value("children", json::array())) Node(child, out, depth + 1);
            out << pad << "</" << tag << ">\n";
        }
    }
};

} // namespace

std::string HexEncode(const std::string& text) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (unsigned char c : text) out += digits[c >> 4], out += digits[c & 15];
    return out;
}

std::string HexDecode(const std::string& hex) {
    auto value = [](char c) { return c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'; };
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out += static_cast<char>(value(hex[i]) * 16 + value(hex[i + 1]));
    return out;
}

std::string DesignToRml(const json& design) {
    Writer w;
    w.model = design.value("model", std::string("hud"));
    std::ostringstream out;
    for (const auto& n : design.value("root", json::array())) {
        if (n.value("type", std::string()) == "notes" && !n.value("hidden", false)) w.Node(n, w.notes, 0);
        else w.Node(n, w.body, 0);
    }
    const std::string id = design.value("id", std::string("screen"));
    out << "<rml>\n<head>\n\t<title>" << Escape(id) << "</title>\n\t<link type=\"text/rcss\" href=\"../styles/base.rcss\"/>\n";
    if (const std::string head = design.value("head", std::string()); !head.empty()) out << head << "\n";
    out << "\t<style>\n\t\tbody.design { width: 100%; height: 100%; pointer-events: none; }\n"
        << "\t\t.design-root { position: absolute; left: 0; top: 0; right: 0; bottom: 0; }\n"
        << w.style.str() << "\t</style>\n</head>\n"
        << "<!-- Made in the UI designer (ui/designs/" << Escape(id) << ".json): change it there; this file is written from it. -->\n"
        << "<body class=\"design\">\n\t<div data-model=\"" << w.model << "\" class=\"design-root\">\n" << w.body.str() << "\t</div>\n";
    out << w.outside.str();
    if (!w.notes.str().empty()) out << "\t<div data-model=\"notes\" class=\"design-root\">\n" << w.notes.str() << "\t</div>\n";
    out << "</body>\n</rml>\n";
    return out.str();
}

std::vector<std::string> CheckDesign(const json& design) {
    std::vector<std::string> problems;
    if (!design.is_object() || !design.contains("root") || !design["root"].is_array()) {
        problems.push_back("a design needs a \"root\" list");
        return problems;
    }
    static const std::set<std::string> types = {"panel", "image", "button", "text", "bar", "list", "slots", "input", "notes", "rml",
                                                "bubbles", "labels", "numbers"};
    static const std::set<std::string> actions = {"open", "close", "event", "press", "flag", "clear", "effects", "quit", "save", "load"};
    static const std::set<std::string> anchors = {"tl", "t", "tr", "l", "c", "r", "bl", "b", "br", "stretch", "stretch-x", "stretch-y", "stretch-top", "stretch-bottom", "stretch-left", "stretch-right"};
    std::set<std::string> ids;
    std::function<void(const json&)> visit = [&](const json& n) {
        const std::string id = n.value("id", std::string());
        const std::string where = "element " + (n.value("name", std::string()).empty() ? id : n.value("name", std::string()));
        if (id.empty()) problems.push_back("an element has no id");
        else if (!ids.insert(id).second) problems.push_back(where + ": the id " + id + " is used twice");
        if (!types.contains(n.value("type", std::string("panel")))) problems.push_back(where + ": no element type " + n.value("type", std::string()));
        if (!anchors.contains(n.value("anchor", std::string("tl")))) problems.push_back(where + ": no anchor " + n.value("anchor", std::string()));
        for (const auto& a : n.value("actions", json::array())) {
            if (!actions.contains(a.value("do", std::string()))) problems.push_back(where + ": no action " + a.value("do", std::string()));
            if (a.value("do", std::string()) == "effects" && !a.value("arg", json()).is_array() && !json::parse(a.value("arg", std::string("[]")), nullptr, false).is_array()) {
                problems.push_back(where + ": its effects aren't a list");
            }
        }
        if (n.value("type", std::string()) == "button" && n.value("actions", json::array()).empty()) problems.push_back(where + ": a button that does nothing");
        if (OverheadSource(n.value("type", std::string())) && design.value("model", std::string("hud")) != "hud") {
            problems.push_back(where + ": over people's heads shows on the HUD only (its model hud)");
        }
        if (const std::string font = n.value("font", std::string()); !font.empty() && !font.ends_with(".ttf") && !font.ends_with(".otf")) {
            problems.push_back(where + ": its font isn't a .ttf or .otf file");
        }
        for (const auto& c : n.value("children", json::array())) visit(c);
    };
    for (const auto& n : design["root"]) visit(n);
    return problems;
}

json DesignFromRml(const std::string& id, const std::string& rml) {
    // Its head's own styles kept; its body whole, as one block.
    auto between = [&](const std::string& open, const std::string& close) -> std::string {
        const size_t a = rml.find(open);
        if (a == std::string::npos) return {};
        const size_t start = rml.find('>', a) + 1, end = rml.find(close, start);
        return end == std::string::npos ? std::string() : rml.substr(start, end - start);
    };
    std::string head = between("<head", "</head>");
    // The designer writes its own title and base styles.
    for (const std::string tag : {"<title>", "<link"}) {
        for (size_t a = head.find(tag); a != std::string::npos; a = head.find(tag)) {
            const size_t end = tag == "<title>" ? head.find("</title>", a) + 8 : head.find('>', a) + 1;
            head.erase(a, end - a);
        }
    }
    const std::string body = between("<body", "</body>");
    json root = json::array();
    root.push_back({{"id", "custom"}, {"type", "rml"}, {"name", "Custom RML (from " + id + ".rml)"}, {"rml", body}});
    return {{"schema", "aige.ui-design/1"}, {"id", id}, {"model", "hud"}, {"width", 1920}, {"height", 1080}, {"head", head}, {"root", root}};
}

} // namespace aige
