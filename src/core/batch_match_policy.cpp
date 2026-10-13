// OWNER: Portable Batch Processing policy implementation (see batch_match_policy.h).
#include "core/batch_match_policy.h"

#include "core/image_formats.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace quicksift::core::batch_match {
namespace {

bool IsAsciiDigit(wchar_t ch) noexcept { return ch >= L'0' && ch <= L'9'; }
bool IsAsciiLetter(wchar_t ch) noexcept {
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}
// ASCII characters that are neither letters nor digits separate runs.
bool IsRunDelimiter(wchar_t ch) noexcept {
    return static_cast<std::uint32_t>(ch) < 0x80u && !IsAsciiDigit(ch) && !IsAsciiLetter(ch);
}
bool IsTrimSpace(wchar_t ch) noexcept {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == L'\v' || ch == L'\f';
}

std::wstring_view Trim(std::wstring_view text) noexcept {
    while (!text.empty() && IsTrimSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && IsTrimSpace(text.back())) text.remove_suffix(1);
    return text;
}

std::wstring NormalizeDigits(std::wstring_view digits) {
    std::size_t first = 0;
    while (first + 1 < digits.size() && digits[first] == L'0') ++first;
    return std::wstring(digits.substr(first));
}

std::wstring ReplaceAll(std::wstring text, std::wstring_view token, std::wstring_view value) {
    if (token.empty()) return text;
    std::size_t position = 0;
    while ((position = text.find(token, position)) != std::wstring::npos) {
        text.replace(position, token.size(), value);
        position += value.size();
    }
    return text;
}

} // namespace

std::wstring AsciiFold(std::wstring_view text) {
    std::wstring folded(text);
    for (wchar_t& ch : folded) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return folded;
}

bool IsListSeparator(wchar_t ch) noexcept {
    return ch == L' ' || ch == L'\t' || ch == L',' || ch == L';' || ch == L'\r' || ch == L'\n';
}

std::vector<std::wstring> SplitList(std::wstring_view text) {
    std::vector<std::wstring> tokens;
    std::size_t index = 0;
    while (index < text.size()) {
        if (IsListSeparator(text[index])) {
            ++index;
            continue;
        }
        if (text[index] == L'"') {
            const std::size_t close = text.find(L'"', index + 1);
            if (close != std::wstring_view::npos) {
                std::wstring token(text.substr(index + 1, close - index - 1));
                if (!Trim(token).empty()) tokens.push_back(std::move(token));
                index = close + 1;
                continue;
            }
            ++index; // unterminated: drop the quote, read a normal token
            continue;
        }
        const std::size_t start = index;
        while (index < text.size() && !IsListSeparator(text[index])) ++index;
        tokens.emplace_back(text.substr(start, index - start));
    }
    return tokens;
}

std::wstring NormalizeExtensionToken(std::wstring_view token) {
    token = Trim(token);
    if (!token.empty() && token.front() == L'*') token.remove_prefix(1);
    if (!token.empty() && token.front() == L'.') token.remove_prefix(1);
    if (token.empty()) return {};
    std::wstring normalized = L".";
    normalized.reserve(token.size() + 1);
    for (const wchar_t ch : token) {
        if (static_cast<std::uint32_t>(ch) >= 0x80u) return {};
        if (ch == L'.' || ch == L'/' || ch == L'\\' || ch == L'*' || ch == L'?' || IsTrimSpace(ch))
            return {};
        normalized.push_back(ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch - L'A' + L'a') : ch);
    }
    return normalized;
}

bool IsSupportedImageExtension(std::wstring_view normalized) noexcept {
    for (const std::wstring_view candidate : AllSupportedImageExtensions()) {
        if (candidate == normalized) return true;
    }
    return false;
}

ExtensionList ParseExtensionList(std::wstring_view text) {
    ExtensionList list;
    const std::vector<std::wstring> tokens = SplitList(text);
    list.allSupported = tokens.empty();
    std::unordered_set<std::wstring> seenUnknown;
    for (const std::wstring& raw : tokens) {
        const std::wstring_view trimmed = Trim(raw);
        if (trimmed.empty()) continue;
        const std::wstring normalized = NormalizeExtensionToken(trimmed);
        if (!normalized.empty() && IsSupportedImageExtension(normalized)) {
            if (std::find(list.extensions.begin(), list.extensions.end(), normalized) ==
                list.extensions.end()) list.extensions.push_back(normalized);
            continue;
        }
        if (seenUnknown.insert(AsciiFold(trimmed)).second) list.unknown.emplace_back(trimmed);
    }
    return list;
}

std::vector<std::wstring> TargetExtensions(const ExtensionList& list) {
    if (!list.allSupported) return list.extensions;
    std::vector<std::wstring> all;
    for (const std::wstring_view extension : AllSupportedImageExtensions()) all.emplace_back(extension);
    return all;
}

std::wstring EntryTextFromToken(std::wstring_view token) {
    token = Trim(token);
    const std::size_t slash = token.find_last_of(L"\\/");
    if (slash != std::wstring_view::npos) token = Trim(token.substr(slash + 1));
    const std::size_t dot = token.rfind(L'.');
    if (dot != std::wstring_view::npos && dot > 0) {
        const std::wstring extension = NormalizeExtensionToken(token.substr(dot));
        if (!extension.empty() && IsSupportedImageExtension(extension)) token = token.substr(0, dot);
    }
    return std::wstring(token);
}

std::vector<NameEntry> ParseNameList(std::wstring_view text, const NameFold& fold) {
    std::vector<NameEntry> entries;
    std::unordered_set<std::wstring> seen;
    for (const std::wstring& token : SplitList(text)) {
        NameEntry entry;
        entry.text = EntryTextFromToken(token);
        if (entry.text.empty()) continue;
        entry.key = fold ? fold(entry.text) : AsciiFold(entry.text);
        if (!seen.insert(entry.key).second) continue;
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::wstring StemOf(std::wstring_view fileName) {
    const std::size_t dot = fileName.rfind(L'.');
    if (dot == std::wstring_view::npos || dot == 0) return std::wstring(fileName);
    return std::wstring(fileName.substr(0, dot));
}

MergeResult MergeNamesIntoText(std::wstring_view existingText,
    const std::vector<std::wstring>& fileNames, const NameFold& fold) {
    MergeResult result;
    std::unordered_set<std::wstring> keys;
    for (const NameEntry& entry : ParseNameList(existingText, fold)) keys.insert(entry.key);
    std::wstring appended;
    for (const std::wstring& fileName : fileNames) {
        const std::wstring stem = StemOf(fileName);
        const std::wstring entryText = EntryTextFromToken(stem);
        if (entryText.empty()) continue;
        const std::wstring key = fold ? fold(entryText) : AsciiFold(entryText);
        if (!keys.insert(key).second) {
            ++result.duplicates;
            continue;
        }
        const bool quote = stem.front() == L'"' ||
            std::any_of(stem.begin(), stem.end(), [](wchar_t ch) { return IsListSeparator(ch); });
        if (!appended.empty()) appended += L'\n';
        if (quote) {
            appended += L'"';
            appended += stem;
            appended += L'"';
        } else {
            appended += stem;
        }
        ++result.added;
    }
    if (result.added == 0) {
        result.text = std::wstring(existingText);
        return result;
    }
    std::wstring_view kept = existingText;
    while (!kept.empty() && IsTrimSpace(kept.back())) kept.remove_suffix(1);
    result.text.reserve(kept.size() + 1 + appended.size());
    result.text.append(kept);
    if (!kept.empty()) result.text += L'\n';
    result.text += appended;
    return result;
}

Runs Tokenize(std::wstring_view text) {
    Runs runs;
    std::size_t index = 0;
    while (index < text.size()) {
        const wchar_t ch = text[index];
        if (IsRunDelimiter(ch)) {
            ++index;
            continue;
        }
        const std::size_t start = index;
        if (IsAsciiDigit(ch)) {
            while (index < text.size() && IsAsciiDigit(text[index])) ++index;
            runs.digits.push_back(NormalizeDigits(text.substr(start, index - start)));
        } else {
            while (index < text.size() && !IsAsciiDigit(text[index]) && !IsRunDelimiter(text[index]))
                ++index;
            runs.letters.emplace_back(text.substr(start, index - start));
        }
    }
    return runs;
}

std::size_t ApproximateSubstringDistance(std::wstring_view pattern, std::wstring_view text) {
    const std::size_t m = pattern.size();
    const std::size_t n = text.size();
    if (m == 0) return 0;
    if (n == 0) return m;
    // Sellers' algorithm (free start/end in text) with OSA transpositions.
    std::vector<std::size_t> before(n + 1, 0);   // row i-2
    std::vector<std::size_t> previous(n + 1, 0); // row i-1 (row 0: all zero = free start)
    std::vector<std::size_t> current(n + 1, 0);
    for (std::size_t i = 1; i <= m; ++i) {
        current[0] = i;
        for (std::size_t j = 1; j <= n; ++j) {
            const std::size_t cost = pattern[i - 1] == text[j - 1] ? 0u : 1u;
            std::size_t value = std::min({ previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost });
            if (i > 1 && j > 1 && pattern[i - 1] == text[j - 2] && pattern[i - 2] == text[j - 1])
                value = std::min(value, before[j - 2] + 1);
            current[j] = value;
        }
        before.swap(previous);
        previous.swap(current);
    }
    return *std::min_element(previous.begin(), previous.end());
}

bool PartialMatches(const Runs& entry, const Runs& stem) {
    if (entry.Empty()) return false;
    std::size_t next = 0;
    for (const std::wstring& digits : entry.digits) {
        while (next < stem.digits.size() && stem.digits[next] != digits) ++next;
        if (next == stem.digits.size()) return false;
        ++next;
    }
    for (const std::wstring& letters : entry.letters) {
        bool found = false;
        for (const std::wstring& candidate : stem.letters) {
            if (letters.size() < kFuzzyLetterMinLength) {
                found = candidate.find(letters) != std::wstring::npos;
            } else {
                found = ApproximateSubstringDistance(letters, candidate) <= kFuzzyLetterMaxEdits;
            }
            if (found) break;
        }
        if (!found) return false;
    }
    return true;
}

MatchResult MatchFiles(const std::vector<NameEntry>& entries,
    const std::vector<CandidateFile>& files, const std::vector<std::wstring>& targetExtensions,
    MatchMethod method, const NameFold& fold, const std::atomic<bool>* cancel) {
    MatchResult result;
    const auto cancelled = [cancel]() noexcept {
        return cancel && cancel->load(std::memory_order_acquire);
    };
    const auto foldText = [&fold](std::wstring_view text) {
        return fold ? fold(text) : AsciiFold(text);
    };
    const std::unordered_set<std::wstring> targets(targetExtensions.begin(), targetExtensions.end());
    std::vector<std::size_t> scope; // indices into files
    scope.reserve(files.size());
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (targets.contains(files[index].extension)) scope.push_back(index);
    }
    std::vector<bool> fileMatched(files.size(), false);
    std::vector<bool> entryMatched(entries.size(), false);
    std::size_t work = 0;
    const auto poll = [&]() {
        return (++work & 0xFFu) == 0 && cancelled();
    };

    if (method == MatchMethod::Complete) {
        std::unordered_map<std::wstring, std::vector<std::size_t>> byKey;
        for (const std::size_t index : scope) {
            byKey[foldText(files[index].stem)].push_back(index);
            if (poll()) { result.cancelled = true; return result; }
        }
        for (std::size_t entry = 0; entry < entries.size(); ++entry) {
            const auto found = byKey.find(entries[entry].key);
            if (found == byKey.end()) continue;
            entryMatched[entry] = true;
            for (const std::size_t index : found->second) fileMatched[index] = true;
        }
    } else {
        std::vector<Runs> stemRuns(files.size());
        std::unordered_map<std::wstring, std::vector<std::size_t>> byDigits;
        for (const std::size_t index : scope) {
            stemRuns[index] = Tokenize(foldText(files[index].stem));
            std::vector<std::wstring> distinct = stemRuns[index].digits;
            std::sort(distinct.begin(), distinct.end());
            distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
            for (const std::wstring& digits : distinct) byDigits[digits].push_back(index);
            if (poll()) { result.cancelled = true; return result; }
        }
        static const std::vector<std::size_t> kNone;
        for (std::size_t entry = 0; entry < entries.size(); ++entry) {
            const Runs runs = Tokenize(entries[entry].key);
            if (runs.Empty()) continue;
            const std::vector<std::size_t>* candidates = &scope;
            if (!runs.digits.empty()) {
                // Narrow by the entry's rarest digit run (every digit run must occur).
                candidates = nullptr;
                for (const std::wstring& digits : runs.digits) {
                    const auto found = byDigits.find(digits);
                    const std::vector<std::size_t>* list = found == byDigits.end() ? &kNone : &found->second;
                    if (!candidates || list->size() < candidates->size()) candidates = list;
                }
            }
            for (const std::size_t index : *candidates) {
                if (poll()) { result.cancelled = true; return result; }
                if (!PartialMatches(runs, stemRuns[index])) continue;
                entryMatched[entry] = true;
                fileMatched[index] = true;
            }
        }
    }

    std::unordered_map<std::wstring, std::size_t> counts;
    for (const std::size_t index : scope) {
        if (!fileMatched[index]) continue;
        result.matchedIds.push_back(files[index].id);
        std::wstring display = files[index].extension;
        if (!display.empty() && display.front() == L'.') display.erase(0, 1);
        for (wchar_t& ch : display) {
            if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
        }
        ++counts[display];
    }
    for (auto& [extension, count] : counts) result.extensionCounts.push_back({ extension, count });
    std::sort(result.extensionCounts.begin(), result.extensionCounts.end(),
        [](const ExtensionCount& left, const ExtensionCount& right) {
            if (left.count != right.count) return left.count > right.count;
            return left.extension < right.extension;
        });
    for (std::size_t entry = 0; entry < entries.size(); ++entry) {
        if (!entryMatched[entry]) result.unmatchedEntries.push_back(entry);
    }
    return result;
}

std::wstring FormatExtensionBreakdown(const std::vector<ExtensionCount>& counts) {
    std::wstring text;
    for (const ExtensionCount& count : counts) {
        if (count.count == 0) continue;
        if (!text.empty()) text += L", ";
        text += std::to_wstring(count.count);
        text += L' ';
        text += count.extension;
    }
    return text;
}

std::wstring FormatConfirmation(std::wstring_view pluralTemplate,
    std::wstring_view singularTemplate, std::size_t total,
    const std::vector<ExtensionCount>& counts) {
    std::wstring text(total == 1 ? singularTemplate : pluralTemplate);
    text = ReplaceAll(std::move(text), L"{count}", std::to_wstring(total));
    return ReplaceAll(std::move(text), L"{breakdown}", FormatExtensionBreakdown(counts));
}

std::wstring UnmatchedEntriesText(const std::vector<NameEntry>& entries,
    const std::vector<std::size_t>& unmatched) {
    std::wstring text;
    for (const std::size_t index : unmatched) {
        if (index >= entries.size()) continue;
        if (!text.empty()) text += L"\r\n";
        text += entries[index].text;
    }
    return text;
}

} // namespace quicksift::core::batch_match
