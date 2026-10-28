#include "gameplay/TileLayer.h"

#include <cmath>

#include <algorithm>
#include <climits>

namespace aige {

namespace {

constexpr int kN = TileLayer::kChunkSize;

std::pair<int, int> ChunkOf(int gx, int gy) { return {TileLayer::FloorDiv(gx, kN), TileLayer::FloorDiv(gy, kN)}; }
int IndexIn(int gx, int gy, std::pair<int, int> chunk) { return (gy - chunk.second * kN) * kN + (gx - chunk.first * kN); }
std::pair<int, int> RegionOfChunk(std::pair<int, int> chunk) {
    return {TileLayer::FloorDiv(chunk.first, TileLayer::kRegionChunks), TileLayer::FloorDiv(chunk.second, TileLayer::kRegionChunks)};
}

int64_t CellKey(int gx, int gy) { return (static_cast<int64_t>(gx) << 32) ^ static_cast<uint32_t>(gy); }

} // namespace

bool TileDef::operator==(const TileDef& o) const {
    const Sprite& a = sprite;
    const Sprite& b = o.sprite;
    return solid == o.solid && prefab == o.prefab && a.texture == b.texture && a.width == b.width && a.height == b.height &&
           a.u0 == b.u0 && a.v0 == b.v0 && a.u1 == b.u1 && a.v1 == b.v1 && a.r == b.r && a.g == b.g && a.b == b.b &&
           a.a == b.a && a.flat == b.flat && a.rotation == b.rotation && a.lift == b.lift && a.cornerLift[0] == b.cornerLift[0] &&
           a.cornerLift[1] == b.cornerLift[1] && a.cornerLift[2] == b.cornerLift[2] && a.cornerLift[3] == b.cornerLift[3] &&
           a.uvCells == b.uvCells && a.uvOriginX == b.uvOriginX && a.uvOriginY == b.uvOriginY;
}

namespace {

size_t DefHash(const TileDef& d) {
    const Sprite& s = d.sprite;
    size_t h = std::hash<std::string>{}(s.texture) ^ (std::hash<std::string>{}(d.prefab) << 1);
    for (const float f : {s.width, s.height, s.u0, s.v0, s.u1, s.v1, s.r, s.g, s.b, s.lift}) h = h * 31 + std::hash<float>{}(f);
    return h * 31 + static_cast<size_t>(s.rotation * 7 + s.uvCells * 131 + s.uvOriginX * 8191 + s.uvOriginY * 131071 + (s.flat ? 1 : 0));
}

} // namespace

uint16_t TileLayer::Intern(const TileDef& def) {
    // By its hash (a built place's ground has a kind per picture, a few hundred of them).
    const size_t h = DefHash(def);
    for (auto [it, end] = index_.equal_range(h); it != end; ++it) {
        if (palette_[it->second - 1] == def) return it->second;
    }
    if (palette_.size() >= 0xFFFF) return kEmpty; // full: nothing, rather than another kind's id
    palette_.push_back(def);
    const auto id = static_cast<uint16_t>(palette_.size());
    index_.emplace(h, id);
    return id;
}

void TileLayer::SetPalette(std::vector<TileDef> palette) {
    palette_ = std::move(palette);
    index_.clear();
    for (size_t i = 0; i < palette_.size(); ++i) index_.emplace(DefHash(palette_[i]), static_cast<uint16_t>(i + 1));
}

const TileLayer::CornerLift* TileLayer::Lift(int gx, int gy) const {
    auto it = lifts_.find(LiftKey(gx, gy));
    return it == lifts_.end() ? nullptr : &it->second;
}

TileLayer::CornerLift TileLayer::SetLift(int gx, int gy, const CornerLift& lift) {
    const int64_t key = LiftKey(gx, gy);
    CornerLift before{};
    if (auto it = lifts_.find(key); it != lifts_.end()) before = it->second;
    if (before == lift) return before;
    if (lift == CornerLift{}) lifts_.erase(key);
    else lifts_[key] = lift;
    dirty_.insert({FloorDiv(FloorDiv(gx, kChunkSize), kRegionChunks), FloorDiv(FloorDiv(gy, kChunkSize), kRegionChunks)});
    ++revision_;
    return before;
}

Sprite CellSprite(const TileLayer& layer, int gx, int gy, const TileDef& def) {
    Sprite s = def.sprite;
    if (s.uvCells > 0) {
        const int n = s.uvCells;
        const int x = ((gx - s.uvOriginX) % n + n) % n, y = ((gy - s.uvOriginY) % n + n) % n;
        s.u0 = static_cast<float>(x) / n, s.v0 = static_cast<float>(y) / n;
        s.u1 = s.u0 + 1.0f / n, s.v1 = s.v0 + 1.0f / n;
    }
    if (const TileLayer::CornerLift* l = layer.Lift(gx, gy)) {
        for (int i = 0; i < 4; ++i) s.cornerLift[i] += (*l)[i];
        // Its slope's light: rising to the right and down (away from the light, upper left on
        // screen) it faces the light.
        const float slope = ((*l)[1] + (*l)[2]) - ((*l)[0] + (*l)[3]);
        const float shade = std::clamp(1.0f + slope * 0.006f, 0.72f, 1.25f);
        s.r *= shade, s.g *= shade, s.b *= shade;
    }
    return s;
}

const TileDef* TileLayer::Def(uint16_t id) const {
    if (id == kEmpty || id > palette_.size()) return nullptr;
    return &palette_[id - 1];
}

uint16_t TileLayer::Get(int gx, int gy) const {
    const auto chunk = ChunkOf(gx, gy);
    auto it = chunks_.find(chunk);
    return it == chunks_.end() ? kEmpty : it->second.cells[IndexIn(gx, gy, chunk)];
}

uint16_t TileLayer::Set(int gx, int gy, uint16_t id) {
    const auto key = ChunkOf(gx, gy);
    auto it = chunks_.find(key);
    if (it == chunks_.end()) {
        if (id == kEmpty) return kEmpty;
        it = chunks_.emplace(key, Chunk{}).first;
    }
    uint16_t& cell = it->second.cells[IndexIn(gx, gy, key)];
    const uint16_t before = cell;
    if (before == id) return before;
    cell = id;
    it->second.count += (id != kEmpty) - (before != kEmpty);
    if (it->second.count == 0) chunks_.erase(it);
    dirty_.insert(RegionOfChunk(key));
    ++revision_;
    return before;
}

void TileLayer::Clear() {
    for (const auto& [key, chunk] : chunks_) dirty_.insert(RegionOfChunk(key));
    chunks_.clear();
    lifts_.clear();
    ++revision_;
}

size_t TileLayer::CellCount() const {
    size_t total = 0;
    for (const auto& [key, chunk] : chunks_) total += static_cast<size_t>(chunk.count);
    return total;
}

void TileLayer::ForEachIn(int x0, int y0, int x1, int y1, const std::function<void(int, int, uint16_t)>& fn) const {
    if (x0 >= x1 || y0 >= y1) return;
    const auto lo = ChunkOf(x0, y0);
    const auto hi = ChunkOf(x1 - 1, y1 - 1);
    // Few chunks in view: look each up; many: walk the map instead.
    const long long span = static_cast<long long>(hi.first - lo.first + 1) * (hi.second - lo.second + 1);
    auto visit = [&](const std::pair<int, int>& key, const Chunk& chunk) {
        const int bx = key.first * kN, by = key.second * kN;
        const int sx = std::max(x0, bx) - bx, ex = std::min(x1, bx + kN) - bx;
        const int sy = std::max(y0, by) - by, ey = std::min(y1, by + kN) - by;
        for (int y = sy; y < ey; ++y) {
            for (int x = sx; x < ex; ++x) {
                const uint16_t id = chunk.cells[y * kN + x];
                if (id != kEmpty) fn(bx + x, by + y, id);
            }
        }
    };
    if (span <= static_cast<long long>(chunks_.size())) {
        for (int cy = lo.second; cy <= hi.second; ++cy) {
            for (int cx = lo.first; cx <= hi.first; ++cx) {
                if (auto it = chunks_.find({cx, cy}); it != chunks_.end()) visit(it->first, it->second);
            }
        }
    } else {
        for (const auto& [key, chunk] : chunks_) {
            if (key.first >= lo.first && key.first <= hi.first && key.second >= lo.second && key.second <= hi.second) {
                visit(key, chunk);
            }
        }
    }
}

void TileLayer::ForEach(const std::function<void(int, int, uint16_t)>& fn) const {
    for (const auto& [key, chunk] : chunks_) {
        for (int i = 0; i < kN * kN; ++i) {
            if (chunk.cells[i] != kEmpty) fn(key.first * kN + i % kN, key.second * kN + i / kN, chunk.cells[i]);
        }
    }
}

std::array<int, 4> TileLayer::Bounds() const {
    if (chunks_.empty()) return {0, 0, 0, 0};
    std::array<int, 4> b = {INT_MAX, INT_MAX, INT_MIN, INT_MIN};
    ForEach([&](int gx, int gy, uint16_t) {
        b[0] = std::min(b[0], gx);
        b[1] = std::min(b[1], gy);
        b[2] = std::max(b[2], gx + 1);
        b[3] = std::max(b[3], gy + 1);
    });
    return b;
}

std::array<int, 4> TileLayer::ChunkBounds() const {
    if (chunks_.empty()) return {0, 0, 0, 0};
    std::array<int, 4> b = {INT_MAX, INT_MAX, INT_MIN, INT_MIN};
    for (const auto& [key, chunk] : chunks_) {
        b[0] = std::min(b[0], key.first * kN);
        b[1] = std::min(b[1], key.second * kN);
        b[2] = std::max(b[2], (key.first + 1) * kN);
        b[3] = std::max(b[3], (key.second + 1) * kN);
    }
    return b;
}

void TileLayer::MarkAllDirty() {
    for (const auto& r : Regions()) dirty_.insert(r);
}

std::set<std::pair<int, int>> TileLayer::Regions() const {
    std::set<std::pair<int, int>> out;
    for (const auto& [key, chunk] : chunks_) out.insert(RegionOfChunk(key));
    return out;
}

void TileLayer::SetChunk(int cx, int cy, const Chunk& chunk) {
    Chunk c = chunk;
    c.count = static_cast<int>(std::count_if(c.cells.begin(), c.cells.end(), [](uint16_t id) { return id != kEmpty; }));
    if (c.count == 0) chunks_.erase({cx, cy});
    else chunks_[{cx, cy}] = c;
    ++revision_;
}

// --- the scene's layers --------------------------------------------------------------

TileLayer& TileLayers::Get(const std::string& name) {
    auto [it, added] = layers.try_emplace(name);
    if (added) {
        it->second.name = name;
        removed.erase(name);
    }
    return it->second;
}

TileLayer* TileLayers::Find(const std::string& name) {
    auto it = layers.find(name);
    return it == layers.end() ? nullptr : &it->second;
}

const TileLayer* TileLayers::Find(const std::string& name) const {
    auto it = layers.find(name);
    return it == layers.end() ? nullptr : &it->second;
}

void TileLayers::Remove(const std::string& name) {
    if (layers.erase(name)) removed.insert(name);
}

std::vector<const TileLayer*> TileLayers::Ordered() const {
    std::vector<const TileLayer*> out;
    for (const auto& [name, layer] : layers) out.push_back(&layer);
    std::stable_sort(out.begin(), out.end(), [](const TileLayer* a, const TileLayer* b) { return a->order < b->order; });
    return out;
}

size_t TileLayers::CellCount() const {
    size_t total = 0;
    for (const auto& [name, layer] : layers) total += layer.CellCount();
    return total;
}

std::optional<float> GroundHeightAt(Registry& registry, float wx, float wy) {
    const TileLayers* found = registry.ctx().find<TileLayers>();
    if (found == nullptr) return std::nullopt;
    const TileLayers& layers = *found;
    const int x = static_cast<int>(std::floor(wx)), y = static_cast<int>(std::floor(wy));
    const auto ordered = layers.Ordered();
    for (auto l = ordered.rbegin(); l != ordered.rend(); ++l) {
        if ((*l)->Get(x, y) == TileLayer::kEmpty) continue;
        const TileLayer::CornerLift* lift = (*l)->Lift(x, y);
        if (lift == nullptr) return 0.0f;
        const auto& c = *lift; // top (x,y), right (x+1,y), bottom (x+1,y+1), left (x,y+1)
        const float fx = wx - x, fy = wy - y;
        return (c[0] * (1 - fx) + c[1] * fx) * (1 - fy) + (c[3] * (1 - fx) + c[2] * fx) * fy;
    }
    return std::nullopt;
}

TileLayers& LayersOf(Registry& registry) {
    if (auto* layers = registry.ctx().find<TileLayers>()) return *layers;
    return registry.ctx().emplace<TileLayers>();
}

const TileLayers* FindLayers(const Registry& registry) { return registry.ctx().find<TileLayers>(); }

void ResetLayers(Registry& registry) {
    if (registry.ctx().contains<TileLayers>()) registry.ctx().erase<TileLayers>();
}

// --- edits ---------------------------------------------------------------------------------

void TileLayerEdit::Set(TileLayer& target, int gx, int gy, uint16_t id) {
    const uint16_t before = target.Set(gx, gy, id);
    const int64_t key = CellKey(gx, gy);
    if (auto it = seen_.find(key); it != seen_.end()) {
        changes[it->second].after = id;
        return;
    }
    if (before == id) return;
    seen_[key] = changes.size();
    changes.push_back({gx, gy, before, id});
}

void TileLayerEdit::SetLift(TileLayer& target, int gx, int gy, const TileLayer::CornerLift& lift) {
    const TileLayer::CornerLift before = target.SetLift(gx, gy, lift);
    const int64_t key = TileLayer::LiftKey(gx, gy);
    if (auto it = seenLift_.find(key); it != seenLift_.end()) {
        lifts[it->second].after = lift;
        return;
    }
    if (before == lift) return;
    seenLift_[key] = lifts.size();
    lifts.push_back({gx, gy, before, lift});
}

void TileLayerEdit::Apply(Registry& registry) const {
    TileLayer& target = LayersOf(registry).Get(layer);
    for (const auto& c : changes) target.Set(c.gx, c.gy, c.after);
    for (const auto& l : lifts) target.SetLift(l.gx, l.gy, l.after);
}

void TileLayerEdit::Revert(Registry& registry) const {
    TileLayer& target = LayersOf(registry).Get(layer);
    for (auto it = changes.rbegin(); it != changes.rend(); ++it) target.Set(it->gx, it->gy, it->before);
    for (auto it = lifts.rbegin(); it != lifts.rend(); ++it) target.SetLift(it->gx, it->gy, it->before);
}

} // namespace aige
