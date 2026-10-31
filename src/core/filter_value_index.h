// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// Reference-counted distinct values behind the Date / Hour / ISO / Aperture /
// Focal / Shutter / Camera filter menus. Maintained by CatalogStore on every
// record mutation (append, edit, clear), so a menu opens without scanning the
// folder. Portable, header-only, UI-thread data (no locking).
#include "app_types.h"

#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace quicksift::core {

class FilterValueIndex {
public:
    void Clear() noexcept {
        iso_.clear(); apertureCents_.clear(); focalMm_.clear(); shutter_.clear();
        hours_.clear(); cameras_.clear(); dates_.clear();
    }
    void Add(const PhotoItem& photo) { Apply(photo, +1); }
    // True when the two records contribute identical menu values (cheap check
    // that lets the many rating/label edits skip the map updates entirely).
    [[nodiscard]] static bool SameContribution(const PhotoItem& a, const PhotoItem& b) noexcept {
        const bool aTime = a.captureTimeSource != predicate::CaptureTimeSource::Unknown;
        const bool bTime = b.captureTimeSource != predicate::CaptureTimeSource::Unknown;
        if (aTime != bTime) return false;
        if (aTime && (a.captureTime.year != b.captureTime.year || a.captureTime.month != b.captureTime.month ||
            a.captureTime.day != b.captureTime.day || a.captureTime.hour != b.captureTime.hour)) return false;
        if (a.cameraSettingsKnown != b.cameraSettingsKnown) return false;
        if (!a.cameraSettingsKnown) return true;
        return a.iso == b.iso && a.aperture == b.aperture && a.focalLength == b.focalLength &&
            a.shutterSpeed == b.shutterSpeed && a.camera == b.camera;
    }
    void Remove(const PhotoItem& photo) { Apply(photo, -1); }

    // Menu keys (sorted ascending, no duplicates).
    [[nodiscard]] std::vector<int> Iso() const { return Keys(iso_); }
    [[nodiscard]] std::vector<int> ApertureCents() const { return Keys(apertureCents_); } // f-number * 100
    [[nodiscard]] std::vector<int> FocalMm() const { return Keys(focalMm_); }
    // Reciprocal for sub-second speeds (160 = 1/160 s); negative = whole seconds.
    [[nodiscard]] std::vector<int> ShutterKeys() const { return Keys(shutter_); }
    [[nodiscard]] std::vector<int> Hours() const { return Keys(hours_); }
    [[nodiscard]] std::vector<std::wstring> Cameras() const { return Keys(cameras_); }
    [[nodiscard]] std::vector<std::wstring> Dates() const { return Keys(dates_); } // YYYY-MM-DD

    [[nodiscard]] static std::wstring DateKey(const predicate::LocalDateTime& time) {
        std::wstring stamp = std::to_wstring(time.year) + L"-";
        if (time.month < 10) stamp += L"0";
        stamp += std::to_wstring(time.month) + L"-";
        if (time.day < 10) stamp += L"0";
        stamp += std::to_wstring(time.day);
        return stamp;
    }
    [[nodiscard]] bool operator==(const FilterValueIndex& other) const = default;

private:
    template <typename Key>
    static void Bump(std::map<Key, std::size_t>& map, const Key& key, int delta) {
        if (delta > 0) { ++map[key]; return; }
        const auto found = map.find(key);
        if (found == map.end()) return; // never added (defensive)
        if (--found->second == 0) map.erase(found);
    }
    template <typename Key>
    static std::vector<Key> Keys(const std::map<Key, std::size_t>& map) {
        std::vector<Key> keys;
        keys.reserve(map.size());
        for (const auto& entry : map) keys.push_back(entry.first);
        return keys;
    }
    void Apply(const PhotoItem& p, int delta) {
        if (p.captureTimeSource != predicate::CaptureTimeSource::Unknown) {
            Bump(dates_, DateKey(p.captureTime), delta);
            Bump(hours_, p.captureTime.hour, delta);
        }
        if (!p.cameraSettingsKnown) return;
        if (p.iso > 0) Bump(iso_, p.iso, delta);
        if (p.aperture > 0.0) Bump(apertureCents_, static_cast<int>(p.aperture * 100.0 + 0.5), delta);
        if (p.focalLength > 0.0) Bump(focalMm_, static_cast<int>(p.focalLength + 0.5), delta);
        if (p.shutterSpeed > 0.0 && p.shutterSpeed < 1.0)
            Bump(shutter_, static_cast<int>(std::lround(1.0 / p.shutterSpeed)), delta);
        else if (p.shutterSpeed >= 1.0)
            Bump(shutter_, -static_cast<int>(std::lround(p.shutterSpeed)), delta);
        if (!p.camera.empty()) Bump(cameras_, p.camera, delta);
    }

    std::map<int, std::size_t> iso_, apertureCents_, focalMm_, shutter_, hours_;
    std::map<std::wstring, std::size_t> cameras_, dates_;
};

} // namespace quicksift::core
