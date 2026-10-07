// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Localization API and language identity; UI captions should use keys, not duplicated literals.

#pragma once

#include <string>
#include <string_view>

namespace quicksift {

enum class Language {
    English = 0,
    Vietnamese = 1
};

class Localizer {
public:
    explicit Localizer(Language language = Language::English) noexcept : language_(language) {}

    Language GetLanguage() const noexcept { return language_; }
    Language CurrentLanguage() const noexcept { return language_; }
    void SetLanguage(Language language) noexcept { language_ = language; }

    std::wstring Text(std::wstring_view english) const;
    std::wstring WindowTitle() const;
    std::wstring HelpDocument() const;
    std::wstring AboutDocument() const;

private:
    Language language_ = Language::English;
};

} // namespace quicksift
