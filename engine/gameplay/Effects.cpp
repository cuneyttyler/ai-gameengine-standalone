#include "gameplay/Effects.h"

#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

fs::path FxDir() {
#ifdef AIGE_FX_DATA_DIR
    return fs::path(AIGE_FX_DATA_DIR);
#else
    return fs::path("data") / "fx";
#endif
}

std::optional<nlohmann::json> ReadPreset(const std::string& folder, const std::string& id) {
    std::ifstream in(FxDir() / folder / (id + ".json"), std::ios::binary);
    if (!in) return std::nullopt;
    nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded() || !json.is_object()) return std::nullopt;
    return json;
}

std::vector<std::string> Presets(const std::string& folder) {
    std::vector<std::string> ids;
    std::error_code ec;
    for (const auto& file : fs::directory_iterator(FxDir() / folder, ec)) {
        if (file.path().extension() == ".json") ids.push_back(file.path().stem().string());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void ParseColor(const nlohmann::json& value, float out[4]) {
    if (value.is_string()) {
        const std::string s = value.get<std::string>();
        if (s.size() >= 7 && s[0] == '#') {
            out[0] = std::stoi(s.substr(1, 2), nullptr, 16) / 255.0f;
            out[1] = std::stoi(s.substr(3, 2), nullptr, 16) / 255.0f;
            out[2] = std::stoi(s.substr(5, 2), nullptr, 16) / 255.0f;
            if (s.size() >= 9) out[3] = std::stoi(s.substr(7, 2), nullptr, 16) / 255.0f;
        }
    } else if (value.is_array() && value.size() >= 3) {
        for (size_t i = 0; i < 4 && i < value.size(); ++i) out[i] = value[i].get<float>();
    }
}

void Pair(const nlohmann::json& json, const char* key, float& a, float& b) {
    if (!json.contains(key)) return;
    const auto& v = json[key];
    if (v.is_number()) a = b = v.get<float>();
    else if (v.is_array() && v.size() == 2) {
        a = v[0].get<float>();
        b = v[1].get<float>();
    }
}

} // namespace

ParticleDef ParticleDefFromJson(const std::string& id, const nlohmann::json& j) {
    ParticleDef d;
    d.id = id;
    d.rate = j.value("rate", d.rate);
    d.burst = j.value("burst", d.burst);
    d.duration = j.value("duration", d.duration);
    Pair(j, "lifetime", d.lifeMin, d.lifeMax);
    d.shape = j.value("shape", d.shape);
    d.radius = j.value("radius", d.radius);
    Pair(j, "speed", d.speedMin, d.speedMax);
    d.direction = j.value("direction", d.direction);
    d.spread = j.value("spread", d.spread);
    d.gravity = j.value("gravity", d.gravity);
    d.drag = j.value("drag", d.drag);
    d.rise = j.value("rise", d.rise);
    Pair(j, "size", d.sizeStart, d.sizeEnd);
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 2 && !j["color"][0].is_number()) {
        ParseColor(j["color"][0], d.color0);
        ParseColor(j["color"][1], d.color1);
    } else if (j.contains("color")) {
        ParseColor(j["color"], d.color0);
        ParseColor(j["color"], d.color1);
    }
    if (j.contains("alpha")) {
        float a0 = d.color0[3], a1 = d.color1[3];
        Pair(j, "alpha", a0, a1);
        d.color0[3] = a0;
        d.color1[3] = a1;
    }
    d.texture = j.value("texture", "");
    d.above = j.value("layer", "world") == "above";
    d.max = j.value("max", d.max);
    return d;
}

std::optional<ParticleDef> FindParticles(const std::string& id) {
    if (const Record* record = ActiveRecords().Find("particles", id)) return ParticleDefFromJson(id, record->resolved);
    if (auto preset = ReadPreset("particles", id)) return ParticleDefFromJson(id, *preset);
    return std::nullopt;
}

std::vector<std::string> ParticlePresets() { return Presets("particles"); }

fs::path FxPresetPath(const std::string& folder, const std::string& id) { return FxDir() / folder / (id + ".json"); }

// --- Particle system -----------------------------------------------------------------------

void ParticleSystem::Clear() {
    particles_.clear();
    emitters_.clear();
    oneOffs_.clear();
}

void ParticleSystem::Emit(const ParticleDef& def, float x, float y, float seconds, Entity follow) {
    Source s;
    s.def = def;
    s.x = x;
    s.y = y;
    s.follow = follow;
    s.once = true;
    s.seconds = seconds;
    Spawn(s, def.burst);
    if (seconds > 0.0f) oneOffs_.push_back(std::move(s));
}

void ParticleSystem::Stream(const ParticleDef& def, float x, float y, float dt, float scale) {
    float& owed = streams_[def.id];
    owed += def.rate * scale * dt;
    const int n = static_cast<int>(owed);
    owed -= static_cast<float>(n);
    Source s;
    s.def = def;
    s.x = x;
    s.y = y;
    s.scale = 1.0f;
    Spawn(s, n);
}

void ParticleSystem::Spawn(const Source& s, int count) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const ParticleDef& d = s.def;
    const int alive = static_cast<int>(particles_.size());
    count = std::min(count, budget - alive);
    for (int i = 0; i < count; ++i) {
        Particle p{};
        float ox = 0, oy = 0;
        if (d.shape == "circle") {
            const float a = unit(rng_) * 6.2831853f, r = std::sqrt(unit(rng_)) * d.radius;
            ox = std::cos(a) * r;
            oy = std::sin(a) * r;
        } else if (d.shape == "box") {
            ox = (unit(rng_) * 2 - 1) * d.radius;
            oy = (unit(rng_) * 2 - 1) * d.radius;
        }
        p.x = s.x + ox;
        p.y = s.y + oy;
        p.z = 0;
        // Direction on the screen; screen right/down to world cells (isometric diagonal halves).
        const float angle = (d.direction + (unit(rng_) - 0.5f) * d.spread) * 3.14159265f / 180.0f;
        const float speed = d.speedMin + (d.speedMax - d.speedMin) * unit(rng_);
        const float sx = std::cos(angle) * speed, sy = std::sin(angle) * speed;
        // Screen-up becomes height (z, pixels), screen-sideways becomes ground movement.
        p.vx = sx * 0.5f;
        p.vy = -sx * 0.5f;
        p.vz = -sy * 32.0f + d.rise;
        p.life = d.lifeMin + (d.lifeMax - d.lifeMin) * unit(rng_);
        p.size0 = d.sizeStart * s.scale;
        p.size1 = d.sizeEnd * s.scale;
        p.gravity = d.gravity;
        p.drag = d.drag;
        std::copy(d.color0, d.color0 + 4, p.c0);
        std::copy(d.color1, d.color1 + 4, p.c1);
        p.texture = d.texture;
        p.above = d.above;
        particles_.push_back(std::move(p));
    }
}

void ParticleSystem::Update(Registry& registry, float dt) {
    // Emitters on entities: start, follow, stop.
    std::vector<Entity> gone;
    for (auto& [entity, source] : emitters_) {
        if (!registry.valid(entity) || !registry.all_of<ParticleEmitter>(entity)) gone.push_back(entity);
    }
    for (Entity e : gone) emitters_.erase(e);
    for (auto [entity, emitter, world] : registry.view<ParticleEmitter, WorldTransform2D>().each()) {
        if (!emitter.playing || emitter.particles.empty()) {
            emitters_.erase(entity);
            continue;
        }
        auto it = emitters_.find(entity);
        if (it == emitters_.end() || it->second.def.id != emitter.particles) {
            auto def = FindParticles(emitter.particles);
            if (!def) continue;
            Source s;
            s.def = *def;
            s.scale = emitter.scale;
            s.x = world.x;
            s.y = world.y;
            Spawn(s, def->burst);
            it = emitters_.insert_or_assign(entity, std::move(s)).first;
        }
        Source& s = it->second;
        s.x = world.x;
        s.y = world.y;
        s.elapsed += dt;
        if (s.def.duration > 0.0f && s.elapsed > s.def.duration) continue;
        s.owed += s.def.rate * s.scale * dt;
        const int n = static_cast<int>(s.owed);
        s.owed -= static_cast<float>(n);
        Spawn(s, n);
    }
    for (auto& s : oneOffs_) {
        if (registry.valid(s.follow)) {
            if (const auto* w = registry.try_get<WorldTransform2D>(s.follow)) {
                s.x = w->x;
                s.y = w->y;
            }
        }
        s.elapsed += dt;
        s.owed += s.def.rate * dt;
        const int n = static_cast<int>(s.owed);
        s.owed -= static_cast<float>(n);
        Spawn(s, n);
    }
    std::erase_if(oneOffs_, [](const Source& s) { return s.elapsed >= s.seconds; });
    // Move and age.
    for (auto& p : particles_) {
        p.age += dt;
        const float k = std::max(0.0f, 1.0f - p.drag * dt);
        p.vx *= k;
        p.vy *= k;
        p.vz = p.vz * k - p.gravity * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.z += p.vz * dt;
    }
    std::erase_if(particles_, [](const Particle& p) { return p.age >= p.life; });
}

void ParticleSystem::AppendSprites(std::vector<SpriteItem>& out, const IsoParams& iso) const {
    out.reserve(out.size() + particles_.size());
    for (const auto& p : particles_) {
        const float t = std::clamp(p.age / std::max(0.001f, p.life), 0.0f, 1.0f);
        const ScreenPose pose = ProjectIso(p.x, p.y, iso);
        SpriteItem item;
        item.owned = true;
        const float size = p.size0 + (p.size1 - p.size0) * t;
        item.value.width = size;
        item.value.height = size;
        item.value.r = p.c0[0] + (p.c1[0] - p.c0[0]) * t;
        item.value.g = p.c0[1] + (p.c1[1] - p.c0[1]) * t;
        item.value.b = p.c0[2] + (p.c1[2] - p.c0[2]) * t;
        item.value.a = p.c0[3] + (p.c1[3] - p.c0[3]) * t;
        item.value.texture = p.texture.empty() ? "fx:dot" : p.texture;
        item.value.emissive = true; // particles carry their own light
        item.transform.x = pose.x;
        item.transform.y = pose.y - p.z + size * 0.5f;
        item.transform.depth = p.above ? 1e9f : pose.depth + 0.01f;
        out.push_back(std::move(item));
    }
}

// --- Effects ---------------------------------------------------------------------------------

EffectDef EffectDefFromJson(const std::string& id, const nlohmann::json& json) {
    EffectDef e;
    e.id = id;
    static const char* kinds[] = {"particles", "sound", "light", "flash", "shake", "hitstop", "slowmo"};
    for (const auto& part : json.value("parts", nlohmann::json::array())) {
        if (!part.is_object()) continue;
        for (const char* kind : kinds) {
            if (part.contains(kind)) {
                nlohmann::json data = part;
                e.parts.push_back({kind, data});
                break;
            }
        }
    }
    return e;
}

std::optional<EffectDef> FindEffect(const std::string& id) {
    if (const Record* record = ActiveRecords().Find("effect", id)) return EffectDefFromJson(id, record->resolved);
    if (auto preset = ReadPreset("effects", id)) return EffectDefFromJson(id, *preset);
    return std::nullopt;
}

std::vector<std::string> EffectPresets() { return Presets("effects"); }

std::vector<std::string> ValidateEffect(const EffectDef& effect) {
    std::vector<std::string> problems;
    if (effect.parts.empty()) problems.push_back("has no parts (particles, sound, light, flash, shake, hitstop, slowmo)");
    for (const auto& part : effect.parts) {
        if (part.kind == "particles" && part.data["particles"].is_string() && !FindParticles(part.data["particles"])) {
            problems.push_back("no particles '" + part.data["particles"].get<std::string>() + "'");
        }
    }
    return problems;
}

// --- Lighting -----------------------------------------------------------------------------------

void AmbientAt(float minuteOfDay, float out[3], float* darkness) {
    const auto& s = ActiveSettings().lighting;
    float day[4] = {1, 1, 1, 1}, night[4] = {0.25f, 0.3f, 0.5f, 1};
    ParseColor(nlohmann::json(s.dayAmbient), day);
    ParseColor(nlohmann::json(s.nightAmbient), night);
    float dark = 0.0f;
    if (s.dayNight) {
        const float hour = std::fmod(minuteOfDay / 60.0f + 24.0f, 24.0f);
        const float dawn = s.dawnHour, dusk = s.duskHour, fade = 1.5f; // hours of twilight
        if (hour >= dusk + fade || hour < dawn - fade) dark = 1.0f;
        else if (hour >= dusk) dark = (hour - dusk) / fade;
        else if (hour < dawn) dark = 1.0f - (hour - (dawn - fade)) / fade;
        dark = std::clamp(dark, 0.0f, 1.0f);
    }
    for (int c = 0; c < 3; ++c) out[c] = day[c] + (night[c] - day[c]) * dark;
    if (darkness) *darkness = dark;
}

FxAccessibility& ActiveFxAccessibility() {
    static FxAccessibility accessibility;
    return accessibility;
}

std::vector<std::pair<std::string, ImageData>> FxTextures() {
    std::vector<std::pair<std::string, ImageData>> out;
    // A soft white dot (particles).
    ImageData dot;
    dot.width = dot.height = 32;
    dot.rgba.resize(32 * 32 * 4);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const float dx = (x + 0.5f) / 16.0f - 1.0f, dy = (y + 0.5f) / 16.0f - 1.0f;
            const float a = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
            uint8_t* p = &dot.rgba[(y * 32 + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = static_cast<uint8_t>(a * a * 255.0f);
        }
    }
    out.push_back({"fx:dot", std::move(dot)});
    // A vignette: clear in the middle, dark at the edges.
    ImageData vignette;
    vignette.width = vignette.height = 64;
    vignette.rgba.resize(64 * 64 * 4);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const float dx = (x + 0.5f) / 32.0f - 1.0f, dy = (y + 0.5f) / 32.0f - 1.0f;
            const float d = std::clamp((std::sqrt(dx * dx + dy * dy) - 0.55f) / 0.8f, 0.0f, 1.0f);
            uint8_t* p = &vignette.rgba[(y * 64 + x) * 4];
            p[0] = p[1] = p[2] = 0;
            p[3] = static_cast<uint8_t>(d * 255.0f);
        }
    }
    out.push_back({"fx:vignette", std::move(vignette)});
    return out;
}

float Ease(const std::string& ease, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (ease == "ease_in") return t * t;
    if (ease == "ease_out") return 1.0f - (1.0f - t) * (1.0f - t);
    if (ease == "ease_in_out") return t < 0.5f ? 2 * t * t : 1.0f - 2 * (1.0f - t) * (1.0f - t);
    if (ease == "bounce") {
        const float n = 7.5625f, d = 2.75f;
        if (t < 1 / d) return n * t * t;
        if (t < 2 / d) return n * (t -= 1.5f / d) * t + 0.75f;
        if (t < 2.5 / d) return n * (t -= 2.25f / d) * t + 0.9375f;
        return n * (t -= 2.625f / d) * t + 0.984375f;
    }
    return t;
}

} // namespace aige
