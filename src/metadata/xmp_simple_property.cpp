// CODE GUIDE: See CODE_GUIDE.md -> "Writing metadata".
// OWNER: simple XMP property reading/updating; preserve unfamiliar XML rather than guessing.

#include "xmp_simple_property.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <regex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace quicksift::metadata {
namespace {

constexpr std::wstring_view kRdfNamespaceUri =
    L"http://www.w3.org/1999/02/22-rdf-syntax-ns#";

struct DescriptionRange {
    std::size_t openingStart = 0;
    std::size_t openingEnd = 0;       // Index of '>'.
    std::size_t contentStart = 0;
    std::size_t contentEnd = 0;       // Index of '<' in the closing tag.
    bool selfClosing = false;
};

std::size_t FindTagEnd(const std::wstring& text, std::size_t tagStart) noexcept {
    wchar_t quote = L'\0';
    for (std::size_t index = tagStart; index < text.size(); ++index) {
        const wchar_t character = text[index];
        if (quote != L'\0') {
            if (character == quote) quote = L'\0';
            continue;
        }
        if (character == L'\'' || character == L'"') {
            quote = character;
        } else if (character == L'>') {
            return index;
        }
    }
    return std::wstring::npos;
}

bool IsSelfClosingTag(const std::wstring& text, std::size_t tagStart,
                      std::size_t tagEnd) noexcept {
    std::size_t cursor = tagEnd;
    while (cursor > tagStart) {
        const wchar_t character = text[cursor - 1];
        if (character == L' ' || character == L'\t' ||
            character == L'\r' || character == L'\n') {
            --cursor;
            continue;
        }
        return character == L'/';
    }
    return false;
}

std::size_t AttributeInsertionPoint(const std::wstring& text, std::size_t tagStart,
                                    std::size_t closingBracket) noexcept {
    std::size_t insertion = closingBracket;
    while (insertion > tagStart &&
        (text[insertion - 1] == L' ' || text[insertion - 1] == L'\t' ||
         text[insertion - 1] == L'\r' || text[insertion - 1] == L'\n')) {
        --insertion;
    }
    // In a self-closing tag, attributes belong before the slash. Preserve any
    // deliberate space before that slash: <tag /> becomes <tag attr="v" />.
    if (insertion > tagStart && text[insertion - 1] == L'/') {
        --insertion;
        while (insertion > tagStart &&
            (text[insertion - 1] == L' ' || text[insertion - 1] == L'\t' ||
             text[insertion - 1] == L'\r' || text[insertion - 1] == L'\n')) {
            --insertion;
        }
    }
    return insertion;
}

bool IsSimpleXmlName(std::wstring_view name) noexcept {
    if (name.empty()) return false;
    const auto validFirst = [](wchar_t character) {
        return character == L'_' ||
            (character >= L'A' && character <= L'Z') ||
            (character >= L'a' && character <= L'z');
    };
    const auto validRest = [&](wchar_t character) {
        return validFirst(character) || (character >= L'0' && character <= L'9') ||
            character == L'-' || character == L'.';
    };
    if (!validFirst(name.front())) return false;
    for (const wchar_t character : name.substr(1)) {
        if (!validRest(character)) return false;
    }
    return true;
}

template <typename Property>
bool IsSupportedQualifiedName(const Property& update) noexcept {
    if (!IsSimpleXmlName(update.namespaceName)) return false;
    const std::wstring prefix = update.namespaceName + L":";
    if (!update.qualifiedName.starts_with(prefix)) return false;
    return IsSimpleXmlName(std::wstring_view(update.qualifiedName).substr(prefix.size()));
}

std::wstring EscapeRegex(std::wstring_view text) {
    static constexpr std::wstring_view special = LR"(\.^$|()[]{}*+?)";
    std::wstring escaped;
    escaped.reserve(text.size() * 2);
    for (const wchar_t character : text) {
        if (special.find(character) != std::wstring_view::npos) escaped.push_back(L'\\');
        escaped.push_back(character);
    }
    return escaped;
}

std::wstring EscapeXml(std::wstring_view text, bool attribute) {
    std::wstring escaped;
    escaped.reserve(text.size());
    for (const wchar_t character : text) {
        switch (character) {
        case L'&': escaped += L"&amp;"; break;
        case L'<': escaped += L"&lt;"; break;
        case L'>': escaped += L"&gt;"; break;
        case L'"':
            if (attribute) escaped += L"&quot;";
            else escaped.push_back(character);
            break;
        case L'\'':
            if (attribute) escaped += L"&apos;";
            else escaped.push_back(character);
            break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

std::optional<std::wstring> StructuralXmlText(const std::wstring& text) {
    // Keep offsets unchanged while hiding constructs whose contents are not XML
    // elements. A commented-out property must never be mistaken for live XMP.
    std::wstring structural = text;
    auto mask = [&](std::size_t begin, std::size_t end) {
        std::fill(structural.begin() + static_cast<std::ptrdiff_t>(begin),
            structural.begin() + static_cast<std::ptrdiff_t>(end), L' ');
    };

    std::size_t cursor = 0;
    while (cursor < text.size()) {
        std::wstring_view opening;
        std::wstring_view closing;
        if (text.compare(cursor, 4, L"<!--") == 0) {
            opening = L"<!--";
            closing = L"-->";
        } else if (text.compare(cursor, 9, L"<![CDATA[") == 0) {
            opening = L"<![CDATA[";
            closing = L"]]>";
        } else if (text.compare(cursor, 2, L"<?") == 0) {
            opening = L"<?";
            closing = L"?>";
        } else if (text.compare(cursor, 2, L"<!") == 0) {
            // XMP sidecars do not require DTD/entity declarations. Refusing them
            // avoids guessing through internal subsets and external entities.
            return std::nullopt;
        } else {
            ++cursor;
            continue;
        }

        const std::size_t endMarker = text.find(closing, cursor + opening.size());
        if (endMarker == std::wstring::npos) return std::nullopt;
        const std::size_t end = endMarker + closing.size();
        mask(cursor, end);
        cursor = end;
    }
    return structural;
}

enum class PrefixBindingStatus {
    Missing,
    Compatible,
    Conflicting,
};

const std::wregex& CachedNamespaceDeclarationPattern(std::wstring_view prefix) {
    // Rating, label, and pick reads ask about the same few prefixes for every
    // file. Keep regex construction out of the folder-scan hot path.
    thread_local std::unordered_map<std::wstring, std::unique_ptr<std::wregex>> cache;
    const std::wstring key(prefix);
    auto found = cache.find(key);
    if (found == cache.end()) {
        const std::wstring pattern = LR"((?:^|\s)xmlns:)" + EscapeRegex(prefix) +
            LR"(\s*=\s*["']([^"']*)["'])";
        found = cache.emplace(key, std::make_unique<std::wregex>(pattern)).first;
    }
    return *found->second;
}

PrefixBindingStatus InspectPrefixBindings(const std::wstring& text,
                                          std::wstring_view prefix,
                                          std::wstring_view expectedUri) {
    const std::wregex& declaration = CachedNamespaceDeclarationPattern(prefix);
    bool found = false;
    for (std::wsregex_iterator it(text.begin(), text.end(), declaration), end;
         it != end; ++it) {
        if (it->size() < 2 || (*it)[1].str() != expectedUri) {
            return PrefixBindingStatus::Conflicting;
        }
        found = true;
    }
    return found ? PrefixBindingStatus::Compatible : PrefixBindingStatus::Missing;
}

template <typename Property>
bool DescriptionPrefixIsCompatible(const std::wstring& descriptionTag,
                                   const Property& update) {
    std::wsmatch match;
    if (!std::regex_search(descriptionTag, match,
            CachedNamespaceDeclarationPattern(update.namespaceName))) {
        return true;
    }
    return match.size() > 1 && match.str(1) == update.namespaceUri;
}

template <typename Property>
bool DescriptionDeclaresPrefix(const std::wstring& descriptionTag,
                               const Property& update) {
    std::wsmatch match;
    return std::regex_search(descriptionTag, match,
               CachedNamespaceDeclarationPattern(update.namespaceName)) &&
        match.size() > 1 && match.str(1) == update.namespaceUri;
}

bool IsSupportedDescriptionOpeningTag(const std::wstring& openingTag) {
    // This is deliberately a narrow XML start-tag grammar. It accepts ordinary
    // qualified attributes with quoted values and rejects stray slashes, nested
    // '<' characters, unquoted values, and other malformed syntax before editing.
    static const std::wregex pattern(
        LR"(^<\s*rdf:Description(?:\s+[A-Za-z_][A-Za-z0-9_.-]*(?::[A-Za-z_][A-Za-z0-9_.-]*)?\s*=\s*(?:"[^"]*"|'[^']*'))*\s*/?>$)");
    return std::regex_match(openingTag, pattern);
}

std::optional<std::vector<DescriptionRange>> FindDescriptions(
    const std::wstring& text, const std::wstring& structural) {
    static const std::wregex openingPattern(
        LR"(<\s*rdf:Description(?=\s|/|>))");
    static const std::wregex closingPattern(LR"(<\s*/\s*rdf:Description\s*>)");

    std::vector<DescriptionRange> ranges;
    std::size_t searchOffset = 0;
    while (searchOffset < text.size()) {
        std::wsmatch openingMatch;
        const auto searchBegin = structural.cbegin() + static_cast<std::ptrdiff_t>(searchOffset);
        if (!std::regex_search(searchBegin, structural.cend(), openingMatch, openingPattern)) break;

        DescriptionRange range;
        range.openingStart = searchOffset + static_cast<std::size_t>(openingMatch.position(0));
        range.openingEnd = FindTagEnd(text, range.openingStart);
        if (range.openingEnd == std::wstring::npos) return std::nullopt;
        range.selfClosing = IsSelfClosingTag(text, range.openingStart, range.openingEnd);
        range.contentStart = range.openingEnd + 1;

        if (range.selfClosing) {
            range.contentEnd = range.contentStart;
            searchOffset = range.contentStart;
        } else {
            std::wsmatch closingMatch;
            const auto contentBegin = structural.cbegin() + static_cast<std::ptrdiff_t>(range.contentStart);
            if (!std::regex_search(contentBegin, structural.cend(), closingMatch, closingPattern)) {
                return std::nullopt;
            }
            range.contentEnd = range.contentStart +
                static_cast<std::size_t>(closingMatch.position(0));

            // Nested rdf:Description blocks make a regex-preserving edit
            // ambiguous. Refuse rather than pairing the wrong closing tag.
            std::wsmatch nestedMatch;
            if (std::regex_search(contentBegin,
                    structural.cbegin() + static_cast<std::ptrdiff_t>(range.contentEnd),
                    nestedMatch, openingPattern)) {
                return std::nullopt;
            }
            searchOffset = range.contentEnd + static_cast<std::size_t>(closingMatch.length(0));
        }
        ranges.push_back(range);
    }
    return ranges;
}

struct ValueMatch {
    std::size_t position = 0;
    std::size_t length = 0;
};

bool RecordSingleMatch(std::optional<ValueMatch>& found, std::size_t position,
                       std::size_t length) noexcept {
    if (found) return false;
    found = ValueMatch{ position, length };
    return true;
}


struct PropertySearchResult {
    bool valid = false;
    std::optional<ValueMatch> existingValue;
    std::optional<std::size_t> insertionDescriptionIndex;
};


struct PropertyPatterns {
    explicit PropertyPatterns(std::wstring_view qualifiedName)
        : broadAttribute(LR"(\s+)" + EscapeRegex(qualifiedName) + LR"(\s*=)"),
          attribute(LR"(\s+)" + EscapeRegex(qualifiedName) +
              LR"(\s*=\s*["']([^"']*)["'])"),
          broadElement(L"<\\s*" + EscapeRegex(qualifiedName) + LR"((?:\s|/|>))"),
          element(L"<\\s*" + EscapeRegex(qualifiedName) +
              LR"(\s*>([^<]*)<\s*/\s*)" + EscapeRegex(qualifiedName) + LR"(\s*>)") {}

    std::wregex broadAttribute;
    std::wregex attribute;
    std::wregex broadElement;
    std::wregex element;
};

const PropertyPatterns& CachedPropertyPatterns(std::wstring_view qualifiedName) {
    // Metadata is read on a small fixed worker pool. A thread-local cache avoids
    // both repeated regex compilation and cross-thread locking on large folders.
    thread_local std::unordered_map<std::wstring, std::unique_ptr<PropertyPatterns>> cache;
    const std::wstring key(qualifiedName);
    auto found = cache.find(key);
    if (found == cache.end()) {
        found = cache.emplace(key, std::make_unique<PropertyPatterns>(qualifiedName)).first;
    }
    return *found->second;
}

template <typename Property>
PropertySearchResult FindSimpleProperty(const std::wstring& text,
    const std::wstring& structural, const std::vector<DescriptionRange>& descriptions,
    const Property& property, const PropertyPatterns& patterns) {
    PropertySearchResult result;
    result.valid = true;
    // The leading whitespace in the attribute patterns is an XML-name boundary.
    // Without it, xmp:Rating also matches myxmp:Rating.
    for (std::size_t index = 0; index < descriptions.size(); ++index) {
        const DescriptionRange& range = descriptions[index];
        const std::wstring openingTag = text.substr(range.openingStart,
            range.openingEnd - range.openingStart + 1);
        if (!IsSupportedDescriptionOpeningTag(openingTag)) return {};
        if (!DescriptionPrefixIsCompatible(openingTag, property)) continue;
        if (!result.insertionDescriptionIndex) result.insertionDescriptionIndex = index;

        const std::size_t broadAttributeCount = static_cast<std::size_t>(std::distance(
            std::wsregex_iterator(openingTag.begin(), openingTag.end(), patterns.broadAttribute),
            std::wsregex_iterator{}));
        std::size_t exactAttributeCount = 0;
        for (std::wsregex_iterator it(openingTag.begin(), openingTag.end(), patterns.attribute), end;
             it != end; ++it) {
            ++exactAttributeCount;
            if (it->size() < 2 || !RecordSingleMatch(result.existingValue,
                    range.openingStart + static_cast<std::size_t>((*it).position(1)),
                    static_cast<std::size_t>((*it).length(1)))) {
                return {};
            }
        }
        if (broadAttributeCount != exactAttributeCount) return {};

        if (!range.selfClosing && range.contentEnd >= range.contentStart) {
            const std::wstring content = structural.substr(range.contentStart,
                range.contentEnd - range.contentStart);
            const std::size_t broadElementCount = static_cast<std::size_t>(std::distance(
                std::wsregex_iterator(content.begin(), content.end(), patterns.broadElement),
                std::wsregex_iterator{}));
            std::size_t exactElementCount = 0;
            for (std::wsregex_iterator it(content.begin(), content.end(), patterns.element), end;
                 it != end; ++it) {
                ++exactElementCount;
                if (it->size() < 2 || !RecordSingleMatch(result.existingValue,
                        range.contentStart + static_cast<std::size_t>((*it).position(1)),
                        static_cast<std::size_t>((*it).length(1)))) {
                    return {};
                }
            }
            // A property represented with attributes, nested markup, or malformed
            // quotes is real but outside this deliberately narrow accessor. Refuse
            // to read or add a second representation beside it.
            if (broadElementCount != exactElementCount) return {};
        }
    }
    return result;
}

} // namespace

XmpSimpleValueInspection InspectXmpSimpleValue(const std::wstring& text,
    const XmpPropertyQuery& query) {
    if (query.namespaceUri.empty() || !IsSupportedQualifiedName(query))
        return { XmpSimpleValueState::Unsupported, {} };
    const auto structural = StructuralXmlText(text);
    if (!structural) return { XmpSimpleValueState::Unsupported, {} };
    const PrefixBindingStatus propertyBinding = InspectPrefixBindings(
        *structural, query.namespaceName, query.namespaceUri);
    const PrefixBindingStatus rdfBinding = InspectPrefixBindings(
        *structural, L"rdf", kRdfNamespaceUri);
    if (propertyBinding == PrefixBindingStatus::Conflicting ||
        rdfBinding == PrefixBindingStatus::Conflicting) {
        return { XmpSimpleValueState::Unsupported, {} };
    }
    const auto descriptions = FindDescriptions(text, *structural);
    if (!descriptions || descriptions->empty())
        return { XmpSimpleValueState::Unsupported, {} };
    const PropertySearchResult found = FindSimpleProperty(text, *structural, *descriptions,
        query, CachedPropertyPatterns(query.qualifiedName));
    if (!found.valid) return { XmpSimpleValueState::Unsupported, {} };
    if (!found.existingValue) return { XmpSimpleValueState::Absent, {} };
    if (propertyBinding != PrefixBindingStatus::Compatible)
        return { XmpSimpleValueState::Unsupported, {} };
    return { XmpSimpleValueState::Value,
        text.substr(found.existingValue->position, found.existingValue->length) };
}

std::optional<std::wstring> ReadXmpSimpleValue(const std::wstring& text,
    const XmpPropertyQuery& query) {
    const XmpSimpleValueInspection inspected = InspectXmpSimpleValue(text, query);
    if (inspected.state != XmpSimpleValueState::Value) return std::nullopt;
    return inspected.value;
}

bool UpdateXmpSimpleValue(std::wstring& text, const XmpPropertyUpdate& update) {
    if (update.namespaceUri.empty() || !IsSupportedQualifiedName(update)) return false;
    const auto structural = StructuralXmlText(text);
    if (!structural) return false;
    const PrefixBindingStatus propertyBinding = InspectPrefixBindings(
        *structural, update.namespaceName, update.namespaceUri);
    const PrefixBindingStatus rdfBinding = InspectPrefixBindings(
        *structural, L"rdf", kRdfNamespaceUri);
    if (propertyBinding == PrefixBindingStatus::Conflicting ||
        rdfBinding == PrefixBindingStatus::Conflicting) {
        return false;
    }

    const auto descriptions = FindDescriptions(text, *structural);
    if (!descriptions || descriptions->empty()) return false;
    const PropertyPatterns patterns(update.qualifiedName);
    const PropertySearchResult found = FindSimpleProperty(text, *structural, *descriptions,
        update, patterns);
    if (!found.valid) return false;

    if (found.existingValue) {
        // An existing qualified property without a matching namespace binding is
        // malformed. Refuse to preserve that ambiguity by silently editing it.
        if (propertyBinding != PrefixBindingStatus::Compatible) return false;
        // Attribute values require quote escaping; element text does not. Both
        // representations are constrained above, so detect the representation
        // from the surrounding character without reparsing the entire document.
        const bool attributeValue = found.existingValue->position > 0 &&
            (text[found.existingValue->position - 1] == L'"' ||
             text[found.existingValue->position - 1] == L'\'');
        const std::wstring escapedValue = EscapeXml(update.value, attributeValue);
        text.replace(found.existingValue->position, found.existingValue->length, escapedValue);
        return true;
    }

    if (!found.insertionDescriptionIndex) return false;
    const DescriptionRange& insertionDescription = descriptions->at(*found.insertionDescriptionIndex);
    const std::wstring openingTag = text.substr(insertionDescription.openingStart,
        insertionDescription.openingEnd - insertionDescription.openingStart + 1);
    std::wstring insertion;
    if (propertyBinding == PrefixBindingStatus::Missing &&
        !DescriptionDeclaresPrefix(openingTag, update)) {
        insertion += L" xmlns:" + update.namespaceName + L"=\"" +
            EscapeXml(update.namespaceUri, true) + L"\"";
    }
    insertion += L" " + update.qualifiedName + L"=\"" +
        EscapeXml(update.value, true) + L"\"";
    text.insert(AttributeInsertionPoint(text, insertionDescription.openingStart,
        insertionDescription.openingEnd), insertion);
    return true;
}

} // namespace quicksift::metadata
