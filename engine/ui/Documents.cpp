#include "ui/Documents.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/MeshUtilities.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;

std::filesystem::path EngineUiDir() {
#ifdef AIGE_UI_DATA_DIR
    return fs::path(AIGE_UI_DATA_DIR);
#else
    return fs::path("data") / "ui";
#endif
}

std::string ApplyThemeTokens(const std::string& style, const nlohmann::json& tokens) {
    std::string out;
    out.reserve(style.size());
    size_t at = 0;
    while (at < style.size()) {
        const size_t start = style.find("var(--", at);
        if (start == std::string::npos) {
            out.append(style, at, std::string::npos);
            break;
        }
        const size_t end = style.find(')', start);
        if (end == std::string::npos) {
            out.append(style, at, std::string::npos);
            break;
        }
        out.append(style, at, start - at);
        const std::string name = style.substr(start + 6, end - start - 6);
        if (tokens.is_object() && tokens.contains(name)) {
            const auto& value = tokens[name];
            out += value.is_string() ? value.get<std::string>() : value.dump();
        } else {
            out.append(style, start, end - start + 1);
        }
        at = end + 1;
    }
    return out;
}

std::vector<std::string> CheckUiFile(const std::string& path, const std::string& content) {
    std::vector<std::string> problems;
    const fs::path p = fs::path(path).lexically_normal();
    const std::string ext = p.extension().string();
    const std::string generic = p.generic_string();
    if (p.is_absolute() || generic.rfind("ui/", 0) != 0 || generic.find("..") != std::string::npos) {
        problems.push_back("UI files live under ui/");
        return problems;
    }
    if (ext == ".json") {
        const nlohmann::json json = nlohmann::json::parse(content, nullptr, false);
        if (json.is_discarded() || !json.is_object()) {
            problems.push_back(path + " isn't a JSON object");
            return problems;
        }
        if (generic == "ui/theme.json") {
            const std::string base = json.value("base", "stone");
            std::error_code ec;
            if (!fs::exists(EngineUiDir() / "themes" / (base + ".json"), ec)) problems.push_back("no built-in theme '" + base + "'");
            if (json.contains("tokens") && !json["tokens"].is_object()) problems.push_back("tokens must be an object");
        } else if (generic == "ui/screens.json") {
            if (!json.contains("screens") || !json["screens"].is_array()) problems.push_back("needs a \"screens\" list");
            for (const auto& s : json.value("screens", nlohmann::json::array())) {
                if (!s.is_object() || s.value("id", "").empty()) problems.push_back("every screen needs an id");
                else if (const std::string layer = s.value("layer", "screen");
                         layer != "screen" && layer != "window" && layer != "hud" && layer != "popup") {
                    problems.push_back("screen '" + s.value("id", "") + "': layer is screen, window, hud or popup");
                }
            }
        } else if (generic == "ui/notifications.json") {
            if (!json.contains("rules") || !json["rules"].is_array()) problems.push_back("needs a \"rules\" list");
            static const std::set<std::string> channels = {"toast", "feed", "banner", "alert"};
            for (const auto& r : json.value("rules", nlohmann::json::array())) {
                if (!r.is_object() || r.value("event", "").empty()) problems.push_back("every rule needs an event");
                else if (!channels.contains(r.value("channel", "toast"))) {
                    problems.push_back("rule for '" + r.value("event", "") + "': channel is toast, feed, banner or alert");
                }
            }
        }
    } else if (ext == ".rml") {
        if (content.find("<rml>") == std::string::npos || content.find("</rml>") == std::string::npos) {
            problems.push_back(path + " must be an <rml> document");
        }
        static const std::set<std::string> models = {"menu", "dialogue", "hud", "notes", "ui"};
        for (size_t at = content.find("data-model=\""); at != std::string::npos; at = content.find("data-model=\"", at + 1)) {
            const size_t start = at + 12, end = content.find('"', start);
            const std::string name = content.substr(start, end - start);
            if (!models.contains(name)) problems.push_back("no data model '" + name + "' (menu, dialogue, hud, notes, ui)");
            const size_t tag = content.rfind('<', at);
            if (tag != std::string::npos && content.compare(tag, 5, "<body") == 0) {
                problems.push_back("put data-model on an element inside <body>, not on <body> itself");
            }
        }
    } else if (ext != ".rcss") {
        problems.push_back(path + ": UI files are .rml, .rcss or .json");
    }
    return problems;
}

void MeshToQuads(const std::vector<UiMeshVertex>& v, const std::vector<int>& indices, float dx, float dy,
                 const std::string& texture, const float* clip, std::vector<UiQuad>& out) {
    auto emit = [&](const std::vector<int>& corners) {
        if (corners.empty()) return;
        // The rectangle the corners span, and the texture coordinates at its corners.
        const UiMeshVertex* lo = &v[corners[0]];
        const UiMeshVertex* hi = &v[corners[0]];
        float x0 = lo->x, y0 = lo->y, x1 = lo->x, y1 = lo->y;
        float r = 0, g = 0, b = 0, a = 0;
        for (int i : corners) {
            const auto& p = v[i];
            if (p.x + p.y < lo->x + lo->y) lo = &p;
            if (p.x + p.y > hi->x + hi->y) hi = &p;
            x0 = std::min(x0, p.x);
            y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x);
            y1 = std::max(y1, p.y);
            r += p.r;
            g += p.g;
            b += p.b;
            a += p.a;
        }
        const float n = static_cast<float>(corners.size());
        UiQuad q;
        q.x0 = x0 + dx;
        q.y0 = y0 + dy;
        q.x1 = x1 + dx;
        q.y1 = y1 + dy;
        q.u0 = lo->u;
        q.v0 = lo->v;
        q.u1 = hi->u;
        q.v1 = hi->v;
        q.r = r / n;
        q.g = g / n;
        q.b = b / n;
        q.a = a / n;
        q.texture = texture;
        if (texture.empty()) q.u0 = q.v0 = q.u1 = q.v1 = -1.0f; // untextured
        if (clip) {
            const float cx0 = std::max(q.x0, clip[0]), cy0 = std::max(q.y0, clip[1]);
            const float cx1 = std::min(q.x1, clip[2]), cy1 = std::min(q.y1, clip[3]);
            if (cx0 >= cx1 || cy0 >= cy1) return;
            if (!texture.empty() && q.x1 > q.x0 && q.y1 > q.y0) {
                const float du = (q.u1 - q.u0) / (q.x1 - q.x0), dv = (q.v1 - q.v0) / (q.y1 - q.y0);
                const float u0 = q.u0 + (cx0 - q.x0) * du, u1 = q.u0 + (cx1 - q.x0) * du;
                const float v0 = q.v0 + (cy0 - q.y0) * dv, v1 = q.v0 + (cy1 - q.y0) * dv;
                q.u0 = u0;
                q.u1 = u1;
                q.v0 = v0;
                q.v1 = v1;
            }
            q.x0 = cx0;
            q.y0 = cy0;
            q.x1 = cx1;
            q.y1 = cy1;
        }
        if (q.x1 > q.x0 && q.y1 > q.y0 && q.a > 0.0f) out.push_back(q);
    };
    // Two triangles sharing an edge (RmlUi's quads) become one rectangle; anything
    // else, triangle by triangle.
    for (size_t i = 0; i + 2 < indices.size();) {
        if (i + 5 < indices.size()) {
            std::vector<int> corners;
            for (size_t k = i; k < i + 6; ++k) {
                if (std::find(corners.begin(), corners.end(), indices[k]) == corners.end()) corners.push_back(indices[k]);
            }
            if (corners.size() == 4) {
                emit(corners);
                i += 6;
                continue;
            }
        }
        emit({indices[i], indices[i + 1], indices[i + 2]});
        i += 3;
    }
}

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// --- Global RmlUi plumbing: files, time and log, fonts -------------------------------------

struct FileSystem : Rml::FileInterface {
    std::vector<fs::path> roots; // the project's ui/, then the engine's
    nlohmann::json tokens;
    std::map<Rml::FileHandle, std::pair<std::string, size_t>> open; // contents, position
    Rml::FileHandle next = 1;

    std::optional<fs::path> Find(const std::string& path) const {
        const fs::path relative = fs::path(path).lexically_normal();
        if (relative.is_absolute()) {
            std::error_code ec;
            return fs::exists(relative, ec) ? std::optional<fs::path>(relative) : std::nullopt;
        }
        for (const auto& root : roots) {
            std::error_code ec;
            if (fs::exists(root / relative, ec)) return root / relative;
        }
        return std::nullopt;
    }
    Rml::FileHandle Open(const Rml::String& path) override {
        const auto file = Find(path);
        if (!file) return 0;
        std::ifstream in(*file, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string text = buffer.str();
        if (file->extension() == ".rcss" || file->extension() == ".rml") text = ApplyThemeTokens(text, tokens);
        open[next] = {std::move(text), 0};
        return next++;
    }
    void Close(Rml::FileHandle file) override { open.erase(file); }
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        auto it = open.find(file);
        if (it == open.end()) return 0;
        auto& [text, at] = it->second;
        const size_t n = std::min(size, text.size() - at);
        std::memcpy(buffer, text.data() + at, n);
        at += n;
        return n;
    }
    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        auto it = open.find(file);
        if (it == open.end()) return false;
        auto& [text, at] = it->second;
        const long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? static_cast<long>(at) : static_cast<long>(text.size());
        const long to = base + offset;
        if (to < 0 || to > static_cast<long>(text.size())) return false;
        at = static_cast<size_t>(to);
        return true;
    }
    size_t Tell(Rml::FileHandle file) override {
        auto it = open.find(file);
        return it == open.end() ? 0 : it->second.second;
    }
};

struct System : Rml::SystemInterface {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::vector<std::string> problems;
    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_WARNING || type == Rml::Log::LT_ASSERT) {
            if (problems.size() < 200) problems.push_back(message);
        }
        return true;
    }
};

// Our TrueType fonts (renderer2d/Text) as RmlUi's font engine: the built-in face for
// any family, or a .ttf a style loads with @font-face (by its file name).
struct Fonts : Rml::FontEngineInterface {
    struct Face {
        std::unique_ptr<Font> own;
        Font* font = nullptr;
        Rml::FontMetrics metrics{};
    };
    std::map<std::string, std::vector<uint8_t>> families;
    std::map<std::pair<std::string, int>, std::unique_ptr<Face>> faces;
    std::map<std::string, Font*> byAtlas;

    bool LoadFontFace(const Rml::String& file, bool, Rml::Style::FontWeight) override {
        std::ifstream in(file, std::ios::binary);
        if (!in) return false;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.empty()) return false;
        families[Lower(fs::path(file).stem().string())] = std::move(bytes);
        return true;
    }
    bool LoadFontFace(Rml::Span<const Rml::byte> data, const Rml::String& family, Rml::Style::FontStyle, Rml::Style::FontWeight,
                      bool) override {
        families[Lower(family)] = std::vector<uint8_t>(data.begin(), data.end());
        return true;
    }
    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle, Rml::Style::FontWeight, int size) override {
        const std::string key = families.contains(Lower(family)) ? Lower(family) : std::string();
        size = std::clamp(size, 6, 160);
        auto& face = faces[{key, size}];
        if (!face) {
            face = std::make_unique<Face>();
            if (key.empty()) {
                face->font = &Font::Builtin(static_cast<float>(size));
            } else {
                const auto& bytes = families[key];
                face->own = std::make_unique<Font>(std::span<const uint8_t>(bytes.data(), bytes.size()), static_cast<float>(size));
                face->font = face->own.get();
            }
            const Font& f = *face->font;
            face->metrics.size = size;
            face->metrics.ascent = f.Ascent();
            face->metrics.descent = std::max(0.0f, f.LineHeight() - f.Ascent());
            face->metrics.line_spacing = f.LineHeight();
            face->metrics.x_height = f.Ascent() * 0.55f;
            face->metrics.underline_position = 2.0f;
            face->metrics.underline_thickness = std::max(1.0f, size / 14.0f);
            byAtlas[f.AtlasName()] = face->font;
        }
        return reinterpret_cast<Rml::FontFaceHandle>(face.get());
    }
    Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle, const Rml::FontEffectList&) override { return 0; }
    const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override {
        return reinterpret_cast<Face*>(handle)->metrics;
    }
    int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext&,
                       Rml::Character) override {
        return static_cast<int>(std::ceil(reinterpret_cast<Face*>(handle)->font->Measure(std::string(string.begin(), string.end()))));
    }
    int GenerateString(Rml::RenderManager& render, Rml::FontFaceHandle handle, Rml::FontEffectsHandle, Rml::StringView string,
                       Rml::Vector2f position, Rml::ColourbPremultiplied colour, float, const Rml::TextShapingContext&,
                       Rml::TexturedMeshList& meshes) override {
        Face* face = reinterpret_cast<Face*>(handle);
        std::vector<UiQuad> glyphs;
        const float width = face->font->Draw(glyphs, std::string(string.begin(), string.end()), std::round(position.x),
                                             std::round(position.y - face->font->Ascent()), {1, 1, 1, 1});
        meshes.resize(1);
        meshes[0].texture = render.LoadTexture("font:" + face->font->AtlasName());
        for (const auto& g : glyphs) {
            Rml::MeshUtilities::GenerateQuad(meshes[0].mesh, {g.x0, g.y0}, {g.x1 - g.x0, g.y1 - g.y0}, colour, {g.u0, g.v0},
                                             {g.u1, g.v1});
        }
        return static_cast<int>(std::ceil(width));
    }
    int GetVersion(Rml::FontFaceHandle) override { return 1; }
    void ReleaseFontResources() override {}
};

FileSystem& Files() {
    static FileSystem files;
    return files;
}
System& SystemHooks() {
    static System system;
    return system;
}
Fonts& FontEngine() {
    static Fonts fonts;
    return fonts;
}

// --- Rendering into UiQuads ---------------------------------------------------------------

struct Renderer : Rml::RenderInterface {
    struct Geometry {
        std::vector<UiMeshVertex> vertices;
        std::vector<int> indices;
    };
    std::map<Rml::CompiledGeometryHandle, Geometry> geometry;
    std::map<Rml::TextureHandle, std::string> textures; // handle -> the renderer's texture name
    std::vector<DocumentUi::NewTexture> fresh;
    std::vector<UiQuad>* target = nullptr;
    DocumentUi::ImageLoader images;
    std::set<std::string> registered;
    uintptr_t next = 1;
    bool scissor = false;
    float clip[4] = {0, 0, 0, 0};

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
        Geometry g;
        g.vertices.reserve(vertices.size());
        for (const auto& v : vertices) {
            const float a = v.colour.alpha / 255.0f;
            const float k = a > 0.0f ? 1.0f / (255.0f * a) : 0.0f; // premultiplied to straight
            g.vertices.push_back({v.position.x, v.position.y, v.tex_coord.x, v.tex_coord.y, v.colour.red * k, v.colour.green * k,
                                  v.colour.blue * k, a});
        }
        g.indices.assign(indices.begin(), indices.end());
        const auto handle = static_cast<Rml::CompiledGeometryHandle>(next++);
        geometry[handle] = std::move(g);
        return handle;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
        auto it = geometry.find(handle);
        if (it == geometry.end() || target == nullptr) return;
        const std::string name = texture ? textures[texture] : std::string();
        MeshToQuads(it->second.vertices, it->second.indices, translation.x, translation.y, name, scissor ? clip : nullptr,
                    *target);
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override { geometry.erase(handle); }

    Rml::TextureHandle Remember(const std::string& name, const ImageData* image) {
        if (image && !registered.contains(name)) {
            fresh.push_back({name, *image});
            registered.insert(name);
        }
        const auto handle = static_cast<Rml::TextureHandle>(next++);
        textures[handle] = name;
        return handle;
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
        if (source.rfind("font:", 0) == 0) {
            const std::string atlas = source.substr(5);
            auto it = FontEngine().byAtlas.find(atlas);
            if (it == FontEngine().byAtlas.end()) return 0;
            const ImageData& image = it->second->Atlas();
            dimensions = {static_cast<int>(image.width), static_cast<int>(image.height)};
            return Remember(atlas, &image);
        }
        // A picture: the project's (by its path) or one beside the engine's documents.
        std::optional<ImageData> image = images ? images(source) : std::nullopt;
        std::string name = source;
        if (!image) {
            if (auto file = Files().Find(source); file && images) {
                image = images(file->generic_string());
                name = file->generic_string();
            }
        }
        if (!image || image->width == 0) return 0;
        dimensions = {static_cast<int>(image->width), static_cast<int>(image->height)};
        return Remember("ui:" + name, &*image);
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i size) override {
        ImageData image;
        image.width = static_cast<uint32_t>(size.x);
        image.height = static_cast<uint32_t>(size.y);
        image.rgba.assign(source.begin(), source.end());
        for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) { // premultiplied to straight
            const int a = image.rgba[i + 3];
            for (int c = 0; c < 3 && a > 0; ++c) image.rgba[i + c] = static_cast<uint8_t>(std::min(255, image.rgba[i + c] * 255 / a));
        }
        return Remember("ui:generated-" + std::to_string(next), &image);
    }
    void ReleaseTexture(Rml::TextureHandle texture) override { textures.erase(texture); }
    void EnableScissorRegion(bool enable) override { scissor = enable; }
    void SetScissorRegion(Rml::Rectanglei region) override {
        clip[0] = static_cast<float>(region.Left());
        clip[1] = static_cast<float>(region.Top());
        clip[2] = static_cast<float>(region.Right());
        clip[3] = static_cast<float>(region.Bottom());
    }
};

int g_instances = 0;

// --- The bound models ------------------------------------------------------------------------

struct MenuItemModel {
    Rml::String label;
    bool enabled = true;
};
struct NoteModel {
    Rml::String channel, text, icon;
    int count = 1;
};

} // namespace

struct DocumentUi::Impl {
    fs::path projectRoot;
    Renderer renderer;
    Rml::Context* context = nullptr;
    int width = 1280, height = 720;
    nlohmann::json tokens = nlohmann::json::object();
    std::vector<ScreenEntry> screens;
    std::map<std::string, Rml::ElementDocument*> documents; // by document path
    std::string menuScreen;
    bool menuVisible = false, dialogueVisible = false;
    std::vector<Notification> live, history;

    // The models' data.
    struct {
        Rml::String screen, title, subtitle, body, footer;
        Rml::Vector<MenuItemModel> items;
        int highlighted = 0;
        bool dim = true;
    } menu;
    struct {
        Rml::String speaker, text, portrait;
        Rml::Vector<MenuItemModel> choices;
        int highlighted = 0;
    } dialogue;
    HudData hud;
    struct {
        Rml::Vector<NoteModel> toast, feed, banner, alert;
    } notes;
    Rml::DataModelHandle menuModel, dialogueModel, hudModel, notesModel, uiModel;
    std::set<std::string> customShown;
    nlohmann::json accessible = nlohmann::json::object();

    void LoadTheme() {
        // The built-in theme named by ui/theme.json's "base" (else "stone"), then its overrides.
        nlohmann::json theme = nlohmann::json::object();
        std::ifstream in(projectRoot / "ui" / "theme.json", std::ios::binary);
        if (in) theme = nlohmann::json::parse(in, nullptr, false);
        if (!theme.is_object()) theme = nlohmann::json::object();
        const std::string base = theme.value("base", "stone");
        tokens = nlohmann::json::object();
        std::ifstream builtIn(EngineUiDir() / "themes" / (base + ".json"), std::ios::binary);
        if (builtIn) {
            const nlohmann::json json = nlohmann::json::parse(builtIn, nullptr, false);
            if (json.is_object() && json.contains("tokens") && json["tokens"].is_object()) tokens = json["tokens"];
        }
        const nlohmann::json overrides = theme.value("tokens", nlohmann::json::object());
        if (overrides.is_object()) {
            for (const auto& [name, value] : overrides.items()) tokens[name] = value;
        }
        for (const auto& [name, value] : accessible.items()) tokens[name] = value; // the player's options win
        Files().tokens = tokens;
    }

    void LoadManifest() {
        screens.clear();
        for (const fs::path& file : {EngineUiDir() / "screens.json", projectRoot / "ui" / "screens.json"}) {
            std::ifstream in(file, std::ios::binary);
            if (!in) continue;
            const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
            if (!json.is_object()) continue;
            for (const auto& s : json.value("screens", nlohmann::json::array())) {
                if (!s.is_object() || s.value("id", "").empty()) continue;
                ScreenEntry entry{s.value("id", ""), s.value("document", ""), s.value("layer", "screen"), s.value("pauses", true)};
                auto it = std::find_if(screens.begin(), screens.end(), [&](const ScreenEntry& e) { return e.id == entry.id; });
                if (it != screens.end()) *it = entry; // the project's wins
                else screens.push_back(entry);
            }
        }
    }

    std::string DocumentFor(const std::string& screen) const {
        for (const auto& s : screens) {
            if (s.id == screen && !s.document.empty() && Files().Find(s.document)) return s.document;
        }
        return screen == "hud" || screen == "dialogue" ? "screens/" + screen + ".rml" : "screens/menu.rml";
    }

    Rml::ElementDocument* Document(const std::string& path) {
        auto it = documents.find(path);
        if (it != documents.end()) return it->second;
        Rml::ElementDocument* doc = context ? context->LoadDocument(path) : nullptr;
        documents[path] = doc;
        return doc;
    }

    void Show(const std::string& path, bool visible) {
        Rml::ElementDocument* doc = visible ? Document(path) : (documents.contains(path) ? documents[path] : nullptr);
        if (!doc) return;
        if (visible && !doc->IsVisible()) doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        if (!visible && doc->IsVisible()) doc->Hide();
    }

    void CreateModels() {
        if (auto c = context->CreateDataModel("menu")) {
            if (auto item = c.RegisterStruct<MenuItemModel>()) {
                item.RegisterMember("label", &MenuItemModel::label);
                item.RegisterMember("enabled", &MenuItemModel::enabled);
            }
            c.RegisterArray<Rml::Vector<MenuItemModel>>();
            c.Bind("screen", &menu.screen);
            c.Bind("title", &menu.title);
            c.Bind("subtitle", &menu.subtitle);
            c.Bind("body", &menu.body);
            c.Bind("footer", &menu.footer);
            c.Bind("items", &menu.items);
            c.Bind("highlighted", &menu.highlighted);
            c.Bind("dim", &menu.dim);
            c.BindEventCallback("choose", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
                if (!args.empty() && owner->onMenuChoose) owner->onMenuChoose(args[0].Get<int>());
            });
            menuModel = c.GetModelHandle();
        }
        if (auto c = context->CreateDataModel("dialogue")) {
            c.Bind("speaker", &dialogue.speaker);
            c.Bind("text", &dialogue.text);
            c.Bind("portrait", &dialogue.portrait);
            c.Bind("choices", &dialogue.choices);
            c.Bind("highlighted", &dialogue.highlighted);
            c.BindEventCallback("choose", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
                if (!args.empty() && owner->onDialogueChoose) owner->onDialogueChoose(args[0].Get<int>());
            });
            dialogueModel = c.GetModelHandle();
        }
        if (auto c = context->CreateDataModel("hud")) {
            c.Bind("visible", &hud.visible);
            c.Bind("health", &hud.health);
            c.Bind("health_max", &hud.healthMax);
            c.Bind("mana", &hud.mana);
            c.Bind("mana_max", &hud.manaMax);
            c.Bind("stamina", &hud.stamina);
            c.Bind("stamina_max", &hud.staminaMax);
            c.Bind("level", &hud.level);
            c.Bind("xp", &hud.xp);
            c.Bind("xp_next", &hud.xpNext);
            c.Bind("gold", &hud.gold);
            c.Bind("quest", &hud.quest);
            c.Bind("objective", &hud.objective);
            c.Bind("clock", &hud.clock);
            c.Bind("prompt_key", &hud.promptKey);
            c.Bind("prompt_text", &hud.promptText);
            c.Bind("subtitle", &hud.subtitle);
            c.Bind("marker", &hud.marker);
            c.Bind("marker_x", &hud.markerX);
            c.Bind("marker_y", &hud.markerY);
            c.Bind("abilities", &hud.abilities);
            hudModel = c.GetModelHandle();
        }
        if (auto c = context->CreateDataModel("ui")) {
            c.BindEventCallback("action", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
                if (!args.empty() && owner->onAction) owner->onAction(args[0].Get<Rml::String>());
            });
            uiModel = c.GetModelHandle();
        }
        if (auto c = context->CreateDataModel("notes")) {
            if (auto note = c.RegisterStruct<NoteModel>()) {
                note.RegisterMember("channel", &NoteModel::channel);
                note.RegisterMember("text", &NoteModel::text);
                note.RegisterMember("icon", &NoteModel::icon);
                note.RegisterMember("count", &NoteModel::count);
            }
            c.RegisterArray<Rml::Vector<NoteModel>>();
            c.Bind("toast", &notes.toast);
            c.Bind("feed", &notes.feed);
            c.Bind("banner", &notes.banner);
            c.Bind("alert", &notes.alert);
            notesModel = c.GetModelHandle();
        }
    }

    void SyncNotes() {
        notes.toast.clear();
        notes.feed.clear();
        notes.banner.clear();
        notes.alert.clear();
        for (const auto& n : live) {
            NoteModel m{n.channel, n.text, n.icon, n.count};
            if (n.channel == "feed") notes.feed.push_back(m);
            else if (n.channel == "banner") notes.banner.push_back(m);
            else if (n.channel == "alert") notes.alert.push_back(m);
            else if (notes.toast.empty()) notes.toast.push_back(m); // one at a time
        }
        if (notesModel) notesModel.DirtyAllVariables();
    }

    DocumentUi* owner = nullptr;
};

DocumentUi::DocumentUi(fs::path projectRoot, ImageLoader images) : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    impl_->projectRoot = std::move(projectRoot);
    impl_->renderer.images = std::move(images);
    Files().roots = {impl_->projectRoot / "ui", EngineUiDir()};
    if (g_instances++ == 0) {
        Rml::SetSystemInterface(&SystemHooks());
        Rml::SetFileInterface(&Files());
        Rml::SetFontEngineInterface(&FontEngine());
        Rml::SetRenderInterface(&impl_->renderer);
        if (!Rml::Initialise()) {
            SystemHooks().problems.push_back("RmlUi didn't start");
            return;
        }
    }
    static int contexts = 0;
    impl_->context = Rml::CreateContext("game-" + std::to_string(++contexts), {impl_->width, impl_->height}, &impl_->renderer);
    if (!impl_->context) return;
    impl_->CreateModels();
    impl_->LoadTheme();
    impl_->LoadManifest();
}

DocumentUi::~DocumentUi() {
    if (impl_->context) Rml::RemoveContext(impl_->context->GetName());
    impl_->context = nullptr;
    if (--g_instances == 0) Rml::Shutdown();
}

bool DocumentUi::Ready() const { return impl_->context != nullptr; }

void DocumentUi::SetImageLoader(ImageLoader images) { impl_->renderer.images = std::move(images); }

void DocumentUi::ShowScreen(const std::string& id, bool visible) {
    if (!impl_->context) return;
    impl_->Show(impl_->DocumentFor(id) == "screens/menu.rml" ? "screens/" + id + ".rml" : impl_->DocumentFor(id), visible);
    if (visible) impl_->customShown.insert(id);
    else impl_->customShown.erase(id);
}

bool DocumentUi::IsShown(const std::string& id) const { return impl_->customShown.contains(id); }

void DocumentUi::SetViewport(int width, int height) {
    if (!impl_->context || width <= 0 || height <= 0) return;
    if (width == impl_->width && height == impl_->height) return;
    impl_->width = width;
    impl_->height = height;
    impl_->context->SetDimensions({width, height});
}

void DocumentUi::Reload() {
    if (!impl_->context) return;
    for (auto& [path, doc] : impl_->documents) {
        if (doc) doc->Close();
    }
    impl_->documents.clear();
    Files().roots = {impl_->projectRoot / "ui", EngineUiDir()};
    impl_->LoadTheme();
    impl_->LoadManifest();
    Rml::Factory::ClearStyleSheetCache();
    Rml::Factory::ClearTemplateCache();
}

nlohmann::json DocumentUi::ThemeTokens() const { return impl_->tokens; }

void DocumentUi::SetAccessibilityTokens(const nlohmann::json& tokens) {
    const nlohmann::json next = tokens.is_object() ? tokens : nlohmann::json::object();
    if (next == impl_->accessible) return;
    impl_->accessible = next;
    Reload();
}

nlohmann::json ProjectThemeTokens(const std::filesystem::path& projectRoot) {
    nlohmann::json theme = nlohmann::json::object();
    std::ifstream in(projectRoot / "ui" / "theme.json", std::ios::binary);
    if (in) theme = nlohmann::json::parse(in, nullptr, false);
    if (!theme.is_object()) theme = nlohmann::json::object();
    nlohmann::json tokens = nlohmann::json::object();
    std::ifstream builtIn(EngineUiDir() / "themes" / (theme.value("base", "stone") + ".json"), std::ios::binary);
    if (builtIn) {
        const nlohmann::json json = nlohmann::json::parse(builtIn, nullptr, false);
        if (json.is_object() && json.contains("tokens") && json["tokens"].is_object()) tokens = json["tokens"];
    }
    const nlohmann::json overrides = theme.value("tokens", nlohmann::json::object());
    if (overrides.is_object()) {
        for (const auto& [name, value] : overrides.items()) tokens[name] = value;
    }
    return tokens;
}

std::vector<std::string> DocumentUi::Themes() const {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& file : fs::directory_iterator(EngineUiDir() / "themes", ec)) {
        if (file.path().extension() == ".json") names.push_back(file.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

void DocumentUi::SetMenu(const std::optional<MenuView>& view, const std::string& screen) {
    if (!impl_->context) return;
    auto& m = impl_->menu;
    const std::string wanted = view ? impl_->DocumentFor(screen) : std::string();
    // Hide the documents of other screens.
    for (auto& [path, doc] : impl_->documents) {
        bool custom = false;
        for (const auto& id : impl_->customShown) custom = custom || path == "screens/" + id + ".rml";
        if (doc && !custom && path != wanted && path != impl_->DocumentFor("hud") && path != impl_->DocumentFor("dialogue") &&
            doc->IsVisible()) {
            doc->Hide();
        }
    }
    impl_->menuVisible = view.has_value();
    impl_->menuScreen = screen;
    if (!view) return;
    m.screen = screen;
    m.title = view->title;
    m.subtitle = view->subtitle;
    m.body = view->body;
    m.footer = view->footer;
    m.highlighted = view->highlighted;
    m.dim = view->dimBackground;
    m.items.clear();
    for (const auto& item : view->items) m.items.push_back({item.label, item.enabled});
    impl_->menuModel.DirtyAllVariables();
    impl_->Show(wanted, true);
}

void DocumentUi::SetDialogue(const std::optional<DialogueView>& view) {
    if (!impl_->context) return;
    impl_->dialogueVisible = view.has_value();
    if (view) {
        auto& d = impl_->dialogue;
        d.speaker = view->speaker;
        d.text = view->text;
        d.portrait = view->portrait;
        d.highlighted = view->highlighted;
        d.choices.clear();
        for (const auto& c : view->choices) d.choices.push_back({c, true});
        impl_->dialogueModel.DirtyAllVariables();
    }
    impl_->Show(impl_->DocumentFor("dialogue"), view.has_value());
}

void DocumentUi::SetHud(const HudData& hud) {
    if (!impl_->context) return;
    impl_->hud = hud;
    impl_->hudModel.DirtyAllVariables();
    impl_->Show(impl_->DocumentFor("hud"), hud.visible);
}

void DocumentUi::Notify(Notification note) {
    // A feed entry with the same key merges ("+3 gold" then "+4 gold" is "+7 gold").
    if (note.channel == "feed" && !note.key.empty()) {
        for (auto& n : impl_->live) {
            if (n.channel == "feed" && n.key == note.key) {
                n.count += note.count;
                n.text = note.text;
                n.seconds = std::max(n.seconds, note.seconds);
                impl_->SyncNotes();
                return;
            }
        }
    }
    impl_->history.push_back(note);
    if (impl_->history.size() > 50) impl_->history.erase(impl_->history.begin());
    impl_->live.push_back(std::move(note));
    impl_->SyncNotes();
}

const std::vector<Notification>& DocumentUi::History() const { return impl_->history; }
const std::vector<Notification>& DocumentUi::Live() const { return impl_->live; }

namespace {
Rml::Element* NthOfClass(Rml::ElementDocument* doc, const std::string& className, int index) {
    if (!doc || index < 0) return nullptr;
    Rml::ElementList found;
    doc->GetElementsByClassName(found, className);
    // A data-for element stays as the hidden template of its copies.
    std::erase_if(found, [](Rml::Element* e) { return e->HasAttribute("data-for"); });
    return index < static_cast<int>(found.size()) ? found[index] : nullptr;
}
} // namespace

bool DocumentUi::ClickMenuItem(int index) {
    if (!impl_->context || !impl_->menuVisible) return false;
    impl_->context->Update(); // the items exist once the model has been read
    auto it = impl_->documents.find(impl_->DocumentFor(impl_->menuScreen));
    Rml::Element* item = it == impl_->documents.end() ? nullptr : NthOfClass(it->second, "item", index);
    if (!item) return false;
    item->Click();
    return true;
}

bool DocumentUi::ClickDialogueChoice(int index) {
    if (!impl_->context || !impl_->dialogueVisible) return false;
    impl_->context->Update();
    auto it = impl_->documents.find(impl_->DocumentFor("dialogue"));
    Rml::Element* choice = it == impl_->documents.end() ? nullptr : NthOfClass(it->second, "choice", index);
    if (!choice) return false;
    choice->Click();
    return true;
}

std::vector<std::string> DocumentUi::TextOf(const std::string& className) const {
    std::vector<std::string> out;
    if (!impl_->context) return out;
    impl_->context->Update();
    for (const auto& [path, doc] : impl_->documents) {
        if (!doc || !doc->IsVisible()) continue;
        Rml::ElementList found;
        doc->GetElementsByClassName(found, className);
        for (Rml::Element* e : found) {
            if (!e->HasAttribute("data-for")) out.push_back(e->GetInnerRML());
        }
    }
    return out;
}

bool DocumentUi::MouseMove(int x, int y) {
    return impl_->context && !impl_->context->ProcessMouseMove(x, y, 0);
}
bool DocumentUi::MouseButton(int button, bool down) {
    if (!impl_->context) return false;
    return !(down ? impl_->context->ProcessMouseButtonDown(button, 0) : impl_->context->ProcessMouseButtonUp(button, 0));
}
bool DocumentUi::MouseWheel(float delta) {
    return impl_->context && !impl_->context->ProcessMouseWheel(delta, 0);
}

void DocumentUi::Update(double seconds) {
    if (!impl_->context) return;
    // Toasts one at a time (only the first counts down); the others all at once.
    bool toastSeen = false, changed = false;
    for (auto& n : impl_->live) {
        const bool toast = n.channel != "feed" && n.channel != "banner" && n.channel != "alert";
        if (toast && toastSeen) continue;
        toastSeen = toastSeen || toast;
        n.seconds -= seconds;
    }
    const size_t before = impl_->live.size();
    std::erase_if(impl_->live, [](const Notification& n) { return n.seconds <= 0.0; });
    changed = before != impl_->live.size();
    if (changed) impl_->SyncNotes();
    impl_->context->Update();
}

void DocumentUi::Render(std::vector<UiQuad>& out) {
    if (!impl_->context) return;
    impl_->renderer.target = &out;
    impl_->context->Update(); // a document shown this frame is laid out before it's drawn
    impl_->context->Render();
    impl_->renderer.target = nullptr;
}

std::vector<DocumentUi::NewTexture> DocumentUi::TakeNewTextures() {
    std::vector<NewTexture> out;
    out.swap(impl_->renderer.fresh);
    return out;
}

std::vector<std::string> DocumentUi::TakeProblems() {
    std::vector<std::string> out;
    out.swap(SystemHooks().problems);
    return out;
}

const std::vector<ScreenEntry>& DocumentUi::Screens() const { return impl_->screens; }

std::string DocumentUi::SourceOf(const std::string& screen) const {
    const std::string document = impl_->DocumentFor(screen);
    std::error_code ec;
    if (fs::exists(impl_->projectRoot / "ui" / document, ec)) return "project";
    if (fs::exists(EngineUiDir() / document, ec)) return document == "screens/menu.rml" && screen != "menu" ? "" : "engine";
    return "";
}

} // namespace aige
