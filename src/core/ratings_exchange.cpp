// OWNER: Portable QuickSift ratings exchange format implementation.
#include "core/ratings_exchange.h"
#include "core/metadata_vocabulary.h"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace quicksift::core::ratings_exchange {
namespace {

constexpr std::array<std::string_view, 12> kMonths{ {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" } };

bool IsBlankChar(char c) noexcept { return c == ' ' || c == '\t'; }

std::string_view TrimRight(std::string_view text) noexcept {
    while (!text.empty() && (IsBlankChar(text.back()) || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

std::string_view TrimBoth(std::string_view text) noexcept {
    text = TrimRight(text);
    while (!text.empty() && IsBlankChar(text.front())) text.remove_prefix(1);
    return text;
}

bool StartsWith(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view text, std::string_view suffix) noexcept {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::optional<std::size_t> ParseCount(std::string_view digits) noexcept {
    if (digits.empty() || digits.size() > 18) return std::nullopt;
    std::size_t value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10u + static_cast<std::size_t>(c - '0');
    }
    return value;
}

std::optional<int> ParseFixedDigits(std::string_view text, std::size_t width) noexcept {
    if (text.size() != width) return std::nullopt;
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

bool IsVersionShape(std::string_view version) noexcept {
    // x.x.x.x : four non-empty decimal components.
    int components = 0;
    std::size_t start = 0;
    while (start <= version.size()) {
        const std::size_t dot = version.find('.', start);
        const std::string_view part = version.substr(start,
            dot == std::string_view::npos ? std::string_view::npos : dot - start);
        if (part.empty() || part.size() > 9) return false;
        for (const char c : part) if (c < '0' || c > '9') return false;
        ++components;
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return components == 4;
}

std::string TwoDigits(int value) {
    std::string text = std::to_string(value);
    if (text.size() < 2) text.insert(text.begin(), '0');
    return text;
}

std::string FourDigits(int value) {
    std::string text = std::to_string(value);
    while (text.size() < 4) text.insert(text.begin(), '0');
    return text;
}

bool IsLeapYear(int year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysInMonth(int year, int month) noexcept {
    static constexpr std::array<int, 12> days{ { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 } };
    if (month < 1 || month > 12) return 0;
    if (month == 2 && IsLeapYear(year)) return 29;
    return days[static_cast<std::size_t>(month - 1)];
}

void AppendCodePoint(std::wstring& output, char32_t codePoint) {
    if constexpr (sizeof(wchar_t) == 2) {
        if (codePoint >= 0x10000u) {
            const char32_t value = codePoint - 0x10000u;
            output.push_back(static_cast<wchar_t>(0xD800u + (value >> 10)));
            output.push_back(static_cast<wchar_t>(0xDC00u + (value & 0x3FFu)));
            return;
        }
    }
    output.push_back(static_cast<wchar_t>(codePoint));
}

void AppendUtf8(std::string& output, char32_t codePoint) {
    if (codePoint < 0x80u) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800u) {
        output.push_back(static_cast<char>(0xC0u | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else if (codePoint < 0x10000u) {
        output.push_back(static_cast<char>(0xE0u | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else {
        output.push_back(static_cast<char>(0xF0u | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
}

bool IsForbiddenFileNameChar(wchar_t c) noexcept {
    if (static_cast<std::uint32_t>(c) < 0x20u) return true;
    switch (c) {
    case L'\\': case L'/': case L':': case L'*': case L'?':
    case L'"': case L'<': case L'>': case L'|':
        return true;
    default:
        return false;
    }
}

} // namespace

bool IsValidStarRating(int value) noexcept { return vocabulary::IsValidStars(value); }
bool IsValidPickState(int value) noexcept { return value >= -1 && value <= 1; }
bool IsValidColorLabel(int value) noexcept { return vocabulary::IsValidColor(value); }

bool HasData(const RatingsEntry& entry) noexcept {
    return entry.starRating != 0 || entry.pickState != 0 || entry.colorLabel != 0;
}

// Tokens come from the ONE canonical vocabulary (core/metadata_vocabulary.h); the
// export format is unchanged ("0".."5", Picked/Rejected/None, None/Red/.../Purple).
std::string StarToken(int rating) {
    return vocabulary::StarExportToken(rating);
}

std::string_view PickToken(int pickState) noexcept {
    return vocabulary::PickExportToken(pickState);
}

std::string_view ColorToken(int colorLabel) noexcept {
    return vocabulary::ColorExportToken(colorLabel);
}

std::optional<int> ParseStarToken(std::string_view token) noexcept {
    return vocabulary::ParseStarExportToken(token);
}

std::optional<int> ParsePickToken(std::string_view token) noexcept {
    return vocabulary::ParsePickExportToken(token);
}

std::optional<int> ParseColorToken(std::string_view token) noexcept {
    return vocabulary::ParseColorExportToken(token);
}

bool IsValidExportTime(const ExportTime& time) noexcept {
    return time.year >= 1 && time.year <= 9999 && time.month >= 1 && time.month <= 12 &&
        time.day >= 1 && time.day <= DaysInMonth(time.year, time.month) &&
        time.hour >= 0 && time.hour <= 23 && time.minute >= 0 && time.minute <= 59 &&
        time.second >= 0 && time.second <= 59;
}

std::string FormatExportTime(const ExportTime& time) {
    const int month = std::clamp(time.month, 1, 12);
    std::string text;
    text.reserve(20);
    text += TwoDigits(time.hour);
    text += ':';
    text += TwoDigits(time.minute);
    text += ':';
    text += TwoDigits(time.second);
    text += ' ';
    text += TwoDigits(time.day);
    text += ' ';
    text += kMonths[static_cast<std::size_t>(month - 1)];
    text += ' ';
    text += FourDigits(time.year);
    return text;
}

std::optional<ExportTime> ParseExportTime(std::string_view text) noexcept {
    // HH:MM:SS DD mmm YYYY  (exactly 20 characters)
    if (text.size() != 20 || text[2] != ':' || text[5] != ':' || text[8] != ' ' ||
        text[11] != ' ' || text[15] != ' ') return std::nullopt;
    ExportTime time{};
    const auto hour = ParseFixedDigits(text.substr(0, 2), 2);
    const auto minute = ParseFixedDigits(text.substr(3, 2), 2);
    const auto second = ParseFixedDigits(text.substr(6, 2), 2);
    const auto day = ParseFixedDigits(text.substr(9, 2), 2);
    const auto year = ParseFixedDigits(text.substr(16, 4), 4);
    if (!hour || !minute || !second || !day || !year) return std::nullopt;
    const std::string_view month = text.substr(12, 3);
    int monthIndex = 0;
    for (std::size_t index = 0; index < kMonths.size(); ++index) {
        if (kMonths[index] == month) monthIndex = static_cast<int>(index) + 1;
    }
    if (monthIndex == 0) return std::nullopt;
    time.hour = *hour;
    time.minute = *minute;
    time.second = *second;
    time.day = *day;
    time.month = monthIndex;
    time.year = *year;
    if (!IsValidExportTime(time)) return std::nullopt;
    return time;
}

std::wstring DefaultExportFileName(const ExportTime& time) {
    const std::string stamp = FourDigits(time.year) + TwoDigits(time.month) + TwoDigits(time.day) +
        "-" + TwoDigits(time.hour) + TwoDigits(time.minute) + TwoDigits(time.second);
    std::wstring name = L"QuickSift-Ratings-";
    for (const char c : stamp) name.push_back(static_cast<wchar_t>(c));
    name += L".txt";
    return name;
}

std::string WideToUtf8(std::wstring_view text) {
    std::string output;
    output.reserve(text.size() + text.size() / 4);
    for (std::size_t index = 0; index < text.size(); ++index) {
        char32_t codePoint = static_cast<char32_t>(static_cast<std::uint32_t>(text[index]));
        if constexpr (sizeof(wchar_t) == 2) {
            if (codePoint >= 0xD800u && codePoint <= 0xDBFFu && index + 1 < text.size()) {
                const char32_t low = static_cast<char32_t>(static_cast<std::uint32_t>(text[index + 1]));
                if (low >= 0xDC00u && low <= 0xDFFFu) {
                    codePoint = 0x10000u + ((codePoint - 0xD800u) << 10) + (low - 0xDC00u);
                    ++index;
                }
            }
        }
        if ((codePoint >= 0xD800u && codePoint <= 0xDFFFu) || codePoint > 0x10FFFFu)
            codePoint = 0xFFFDu; // lone surrogate: never emit invalid UTF-8
        AppendUtf8(output, codePoint);
    }
    return output;
}

std::optional<std::wstring> Utf8ToWide(std::string_view text) {
    std::wstring output;
    output.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        char32_t codePoint = 0;
        std::size_t length = 0;
        if (lead < 0x80u) { codePoint = lead; length = 1; }
        else if (lead >= 0xC2u && lead <= 0xDFu) { codePoint = lead & 0x1Fu; length = 2; }
        else if (lead >= 0xE0u && lead <= 0xEFu) { codePoint = lead & 0x0Fu; length = 3; }
        else if (lead >= 0xF0u && lead <= 0xF4u) { codePoint = lead & 0x07u; length = 4; }
        else return std::nullopt;
        if (index + length > text.size()) return std::nullopt;
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[index + k]);
            if ((next & 0xC0u) != 0x80u) return std::nullopt;
            codePoint = (codePoint << 6) | (next & 0x3Fu);
        }
        if ((length == 3 && codePoint < 0x800u) || (length == 4 && codePoint < 0x10000u) ||
            codePoint > 0x10FFFFu || (codePoint >= 0xD800u && codePoint <= 0xDFFFu))
            return std::nullopt;
        AppendCodePoint(output, codePoint);
        index += length;
    }
    return output;
}

std::string WriteDocumentUtf8(const ExportDocument& document) {
    std::size_t dataCount = 0;
    for (const RatingsEntry& entry : document.entries) if (HasData(entry)) ++dataCount;
    std::string out;
    out.reserve(256 + dataCount * 48);
    out += kHeaderPrefix;
    out += document.version;
    out += kHeaderSuffix;
    out += kLineBreak;
    out += kExportedTimePrefix;
    out += FormatExportTime(document.time);
    out += kLineBreak;
    out += kTotalCountPrefix;
    out += std::to_string(document.totalFiles);
    out += kTotalCountSuffix;
    out += kLineBreak;
    out += kDataCountPrefix;
    out += std::to_string(dataCount);
    out += kDataCountSuffix;
    out += kLineBreak;
    out += kSeparator;
    out += kLineBreak;
    out += kDataLabel;
    out += kLineBreak;
    for (const RatingsEntry& entry : document.entries) {
        if (!HasData(entry)) continue;
        out += WideToUtf8(entry.fileName);
        out += " | ";
        out += StarToken(entry.starRating);
        out += " | ";
        out += PickToken(entry.pickState);
        out += " | ";
        out += ColorToken(entry.colorLabel);
        out += kLineBreak;
    }
    out += kSeparator;
    out += kLineBreak;
    out += kFooter;
    out += kLineBreak;
    return out;
}

const char* ParseErrorName(ParseError error) noexcept {
    switch (error) {
    case ParseError::None: return "none";
    case ParseError::Empty: return "empty file";
    case ParseError::InvalidUtf8: return "not valid UTF-8";
    case ParseError::BadHeader: return "header line";
    case ParseError::BadExportTime: return "exported time line";
    case ParseError::BadTotalCount: return "total files count line";
    case ParseError::BadDataCount: return "rated files count line";
    case ParseError::MissingFirstSeparator: return "first em-dash separator";
    case ParseError::MissingDataLabel: return "\"Exported data:\" line";
    case ParseError::BadDataLine: return "data line";
    case ParseError::DuplicateEntry: return "duplicate file name";
    case ParseError::MissingSecondSeparator: return "second em-dash separator";
    case ParseError::MissingFooter: return "footer line";
    case ParseError::TrailingContent: return "content after the footer";
    case ParseError::DataCountMismatch: return "rated files count does not match the data lines";
    }
    return "unknown";
}

ParseResult ParseDocumentUtf8(std::string_view bytes) {
    ParseResult result;
    auto fail = [&](ParseError error, std::size_t lineNumber, std::string_view line) {
        result.ok = false;
        result.error = error;
        result.lineNumber = lineNumber;
        result.offendingLine = Utf8ToWide(TrimRight(line)).value_or(std::wstring{});
        result.document = {};
        return result;
    };

    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEFu &&
        static_cast<unsigned char>(bytes[1]) == 0xBBu && static_cast<unsigned char>(bytes[2]) == 0xBFu)
        bytes.remove_prefix(3);
    if (!Utf8ToWide(bytes)) return fail(ParseError::InvalidUtf8, 0, {});

    enum class Stage { Header, Time, Total, DataCount, FirstSeparator, DataLabel,
        ColumnHeaderOrData, Data, Footer, AfterFooter };
    Stage stage = Stage::Header;
    std::size_t declaredDataCount = 0;
    std::unordered_map<std::string, std::size_t> seen;
    std::size_t lineNumber = 0;
    std::size_t position = 0;
    bool sawAnyContent = false;

    while (position <= bytes.size()) {
        const std::size_t newline = bytes.find('\n', position);
        const std::string_view rawLine = bytes.substr(position,
            newline == std::string_view::npos ? std::string_view::npos : newline - position);
        ++lineNumber;
        position = newline == std::string_view::npos ? bytes.size() + 1 : newline + 1;

        const std::string_view right = TrimRight(rawLine);
        const std::string_view line = TrimBoth(rawLine);
        if (line.empty()) continue; // blank lines are not significant anywhere
        sawAnyContent = true;

        switch (stage) {
        case Stage::Header: {
            if (!StartsWith(line, kHeaderPrefix) || !EndsWith(line, kHeaderSuffix) ||
                line.size() < kHeaderPrefix.size() + kHeaderSuffix.size())
                return fail(ParseError::BadHeader, lineNumber, rawLine);
            const std::string_view version = line.substr(kHeaderPrefix.size(),
                line.size() - kHeaderPrefix.size() - kHeaderSuffix.size());
            if (!IsVersionShape(version)) return fail(ParseError::BadHeader, lineNumber, rawLine);
            result.document.version = std::string(version);
            stage = Stage::Time;
            break;
        }
        case Stage::Time: {
            if (!StartsWith(line, kExportedTimePrefix))
                return fail(ParseError::BadExportTime, lineNumber, rawLine);
            const auto time = ParseExportTime(line.substr(kExportedTimePrefix.size()));
            if (!time) return fail(ParseError::BadExportTime, lineNumber, rawLine);
            result.document.time = *time;
            stage = Stage::Total;
            break;
        }
        case Stage::Total: {
            if (!StartsWith(line, kTotalCountPrefix) || !EndsWith(line, kTotalCountSuffix) ||
                line.size() <= kTotalCountPrefix.size() + kTotalCountSuffix.size())
                return fail(ParseError::BadTotalCount, lineNumber, rawLine);
            const auto total = ParseCount(line.substr(kTotalCountPrefix.size(),
                line.size() - kTotalCountPrefix.size() - kTotalCountSuffix.size()));
            if (!total) return fail(ParseError::BadTotalCount, lineNumber, rawLine);
            result.document.totalFiles = *total;
            stage = Stage::DataCount;
            break;
        }
        case Stage::DataCount: {
            if (!StartsWith(line, kDataCountPrefix) || !EndsWith(line, kDataCountSuffix) ||
                line.size() <= kDataCountPrefix.size() + kDataCountSuffix.size())
                return fail(ParseError::BadDataCount, lineNumber, rawLine);
            const auto count = ParseCount(line.substr(kDataCountPrefix.size(),
                line.size() - kDataCountPrefix.size() - kDataCountSuffix.size()));
            if (!count) return fail(ParseError::BadDataCount, lineNumber, rawLine);
            declaredDataCount = *count;
            stage = Stage::FirstSeparator;
            break;
        }
        case Stage::FirstSeparator:
            if (line != kSeparator) return fail(ParseError::MissingFirstSeparator, lineNumber, rawLine);
            stage = Stage::DataLabel;
            break;
        case Stage::DataLabel:
            if (line != kDataLabel) return fail(ParseError::MissingDataLabel, lineNumber, rawLine);
            stage = Stage::ColumnHeaderOrData;
            break;
        case Stage::ColumnHeaderOrData:
            stage = Stage::Data;
            if (line == kColumnHeader) break;
            [[fallthrough]];
        case Stage::Data: {
            if (line == kSeparator) {
                stage = Stage::Footer;
                break;
            }
            // Exactly four '|'-separated fields. '|' can never occur in a Windows
            // file name, so the split is unambiguous.
            if (std::count(right.begin(), right.end(), '|') != 3)
                return fail(ParseError::BadDataLine, lineNumber, rawLine);
            const std::size_t p1 = right.find('|');
            const std::size_t p2 = right.find('|', p1 + 1);
            const std::size_t p3 = right.find('|', p2 + 1);
            // File name: keep leading characters verbatim (a Windows name may start
            // with a space); drop the trailing " " separator padding (a Windows name
            // can never end with a space).
            const std::string_view nameField = TrimRight(right.substr(0, p1));
            const auto star = ParseStarToken(TrimBoth(right.substr(p1 + 1, p2 - p1 - 1)));
            const auto pick = ParsePickToken(TrimBoth(right.substr(p2 + 1, p3 - p2 - 1)));
            const auto color = ParseColorToken(TrimBoth(right.substr(p3 + 1)));
            if (nameField.empty() || !star || !pick || !color)
                return fail(ParseError::BadDataLine, lineNumber, rawLine);
            auto name = Utf8ToWide(nameField);
            if (!name || name->empty() ||
                std::any_of(name->begin(), name->end(), IsForbiddenFileNameChar) ||
                *name == L"." || *name == L"..")
                return fail(ParseError::BadDataLine, lineNumber, rawLine);
            const std::wstring folded = AsciiFold(*name);
            const std::string key = WideToUtf8(folded);
            if (seen.contains(key)) return fail(ParseError::DuplicateEntry, lineNumber, rawLine);
            seen.emplace(key, result.document.entries.size());
            RatingsEntry entry;
            entry.fileName = std::move(*name);
            entry.starRating = *star;
            entry.pickState = *pick;
            entry.colorLabel = *color;
            std::string_view verbatim = rawLine;
            if (!verbatim.empty() && verbatim.back() == '\r') verbatim.remove_suffix(1);
            entry.sourceLine = Utf8ToWide(verbatim).value_or(std::wstring{});
            entry.lineNumber = lineNumber;
            result.document.entries.push_back(std::move(entry));
            break;
        }
        case Stage::Footer:
            if (line != kFooter) return fail(ParseError::MissingFooter, lineNumber, rawLine);
            stage = Stage::AfterFooter;
            break;
        case Stage::AfterFooter:
            return fail(ParseError::TrailingContent, lineNumber, rawLine);
        }
    }

    if (!sawAnyContent) return fail(ParseError::Empty, 0, {});
    switch (stage) {
    case Stage::AfterFooter: break;
    case Stage::Header: return fail(ParseError::BadHeader, lineNumber, {});
    case Stage::Time: return fail(ParseError::BadExportTime, lineNumber, {});
    case Stage::Total: return fail(ParseError::BadTotalCount, lineNumber, {});
    case Stage::DataCount: return fail(ParseError::BadDataCount, lineNumber, {});
    case Stage::FirstSeparator: return fail(ParseError::MissingFirstSeparator, lineNumber, {});
    case Stage::DataLabel: return fail(ParseError::MissingDataLabel, lineNumber, {});
    case Stage::ColumnHeaderOrData:
    case Stage::Data: return fail(ParseError::MissingSecondSeparator, lineNumber, {});
    case Stage::Footer: return fail(ParseError::MissingFooter, lineNumber, {});
    }
    if (declaredDataCount != result.document.entries.size())
        return fail(ParseError::DataCountMismatch, 0, {});
    result.ok = true;
    result.error = ParseError::None;
    return result;
}

std::wstring AsciiFold(std::wstring_view name) {
    std::wstring folded(name);
    for (wchar_t& c : folded) {
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
    }
    return folded;
}

ImportReconciliation ReconcileWithFolder(const ExportDocument& document,
    const std::vector<FolderFile>& folderFiles, const NameFold& fold) {
    ImportReconciliation reconciliation;
    reconciliation.expectedFiles = document.totalFiles;
    reconciliation.presentFiles = folderFiles.size();
    const NameFold& key = fold ? fold : NameFold(AsciiFold);
    std::unordered_map<std::wstring, std::size_t> byName;
    byName.reserve(folderFiles.size());
    for (const FolderFile& file : folderFiles) byName.emplace(key(file.fileName), file.catalogIndex);
    std::unordered_map<std::size_t, std::size_t> matchedCatalog;
    for (std::size_t index = 0; index < document.entries.size(); ++index) {
        const auto found = byName.find(key(document.entries[index].fileName));
        if (found == byName.end()) {
            reconciliation.missingEntries.push_back(index);
        } else if (matchedCatalog.contains(found->second)) {
            // Two lines that differ only by (non-ASCII) case address one file.
            reconciliation.duplicateEntries.push_back(index);
        } else {
            matchedCatalog.emplace(found->second, index);
            reconciliation.matches.push_back({ index, found->second });
        }
    }
    return reconciliation;
}

std::wstring MissingLinesText(const ExportDocument& document,
    const ImportReconciliation& reconciliation) {
    std::wstring text;
    for (const std::size_t index : reconciliation.missingEntries) {
        if (index >= document.entries.size()) continue;
        if (!text.empty()) text += L"\r\n";
        text += document.entries[index].sourceLine;
    }
    return text;
}

} // namespace quicksift::core::ratings_exchange
