// Portable checks for Localizer::Text (src/localization.cpp is compiled in directly)
// and for the user-error catalog's Vietnamese coverage.
//   g++ -std=c++20 -Isrc scripts/checks/localization_checks.cpp src/localization.cpp
//       build/libQuickSiftCore.a -pthread
#include "localization.h"
#include "core/user_error_catalog.h"

#include <cstdio>
#include <string>

namespace {
int gChecks = 0;
int gFailures = 0;

void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", what);
    }
}

bool HasPlaceholder(const std::wstring& text, const std::wstring& token) {
    return text.find(token) != std::wstring::npos;
}
} // namespace

int main() {
    const quicksift::Localizer en(quicksift::Language::English);
    const quicksift::Localizer vi(quicksift::Language::Vietnamese);

    Check(en.Text(L"Retry") == L"Retry", "English is the identity");
    Check(vi.Text(L"Retry") == L"Thử lại", "exact key");
    Check(vi.Text(L"Skip") == L"Bỏ qua", "exact key (Skip)");

    // Regression: a shorter fragment listed earlier ("Batch processing") used to win
    // and leave "Xử lý hàng loạt cancelled". Whole-string fragment keys are exact now
    // and the fragment pass is longest-first.
    Check(vi.Text(L"Batch processing cancelled") == L"Đã hủy xử lý hàng loạt",
        "fragment table entries match as whole strings");
    Check(vi.Text(L"Batch processing incomplete") == L"Xử lý hàng loạt chưa hoàn tất",
        "longest fragment wins");

    // Status line built from streamed pieces, localized through fragments.
    const std::wstring status = vi.Text(L"12 photos  •  3 selected  •  date filter  •  metadata pending");
    Check(status.find(L"filter") == std::wstring::npos, "status filter tags are localized");
    Check(status.find(L"pending") == std::wstring::npos, "metadata pending tag is localized");

    // Composed conflict detail: every variant is a full-sentence fragment.
    const std::wstring conflict = vi.Text(
        L"The file's color label changed outside QuickSift, so the metadata update was stopped.");
    Check(conflict.find(L"changed") == std::wstring::npos && conflict.find(L"Nhãn màu") == 0,
        "composed conflict detail is localized");
    // Details extended with sizes keep the numbers and translate the sentence.
    const std::wstring extended = vi.Text(
        L"The embedded metadata container could not be updated safely.\n\nRequired working space: 12 MB");
    Check(extended.find(L"12 MB") != std::wstring::npos && extended.find(L"Required") == std::wstring::npos,
        "extended detail keeps values and localizes text");

    // User-error catalog: every title/message has a distinct Vietnamese text and
    // keeps its {file} placeholder.
    std::size_t count = 0;
    const quicksift::core::UserErrorEntry* table = quicksift::core::UserErrorTable(count);
    Check(table != nullptr && count > 0, "user error table is present");
    for (std::size_t index = 0; table && index < count; ++index) {
        const std::wstring title(table[index].title);
        const std::wstring message(table[index].message);
        if (!title.empty()) Check(vi.Text(title) != title, "user error title translated");
        if (!message.empty()) {
            const std::wstring localized = vi.Text(message);
            Check(localized != message, "user error message translated");
            Check(HasPlaceholder(message, L"{file}") == HasPlaceholder(localized, L"{file}"),
                "user error message keeps {file}");
        }
    }

    // Help mentions search and Ctrl+F in both languages; export format note.
    const std::wstring helpEn = en.HelpDocument();
    const std::wstring helpVi = vi.HelpDocument();
    Check(helpEn.find(L"Ctrl+F") != std::wstring::npos && helpEn.find(L"SEARCH") != std::wstring::npos,
        "English help documents search");
    Check(helpVi.find(L"Ctrl+F") != std::wstring::npos && helpVi.find(L"TÌM KIẾM") != std::wstring::npos,
        "Vietnamese help documents search");
    Check(helpEn.find(L"always English") != std::wstring::npos, "English help explains the English export format");
    Check(helpVi.find(L"bằng tiếng Anh") != std::wstring::npos, "Vietnamese help explains the English export format");

    std::printf("localization_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
