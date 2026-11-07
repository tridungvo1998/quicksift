// CODE GUIDE: Compare layout invalidation; no path copies on cache hits.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
namespace quicksift::core {
class CompareLayoutKey {
public:
    static constexpr std::size_t kCapacity = 6;
    bool Matches(const std::array<float, 5>& geometry, std::uint64_t generation, int mode,
        std::span<const std::wstring> paths, std::span<const float> aspects) const noexcept {
        if (!valid_ || geometry != geometry_ || generation != generation_ || mode != mode_ ||
            paths.size() != count_ || aspects.size() != count_) return false;
        for (std::size_t i = 0; i < count_; ++i)
            if (paths[i] != paths_[i] || aspects[i] != aspects_[i]) return false;
        return true;
    }
    void Assign(const std::array<float, 5>& geometry, std::uint64_t generation, int mode,
        std::span<const std::wstring> paths, std::span<const float> aspects) {
        valid_ = false;
        if (paths.size() > kCapacity || aspects.size() != paths.size()) return;
        for (std::size_t i = 0; i < paths.size(); ++i) { paths_[i] = paths[i]; aspects_[i] = aspects[i]; }
        geometry_ = geometry; generation_ = generation; mode_ = mode; count_ = paths.size(); valid_ = true;
    }
    void Invalidate() noexcept { valid_ = false; }
private:
    bool valid_ = false;
    std::size_t count_ = 0;
    std::array<float, 5> geometry_{};
    std::uint64_t generation_ = 0;
    int mode_ = 0;
    std::array<std::wstring, kCapacity> paths_{};
    std::array<float, kCapacity> aspects_{};
};
}
