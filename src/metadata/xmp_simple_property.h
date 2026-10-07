// CODE GUIDE: See CODE_GUIDE.md -> "Writing metadata".
// OWNER: Narrow simple-XMP property contract; preserve unfamiliar documents when unsupported.

#pragma once

#include <optional>
#include <string>

namespace quicksift::metadata {

struct XmpPropertyQuery {
    std::wstring qualifiedName;
    std::wstring namespaceName;
    std::wstring namespaceUri;
};


enum class XmpSimpleValueState {
    Absent,
    Value,
    Unsupported,
};

struct XmpSimpleValueInspection {
    XmpSimpleValueState state = XmpSimpleValueState::Absent;
    std::wstring value;
};

struct XmpPropertyUpdate {
    std::wstring qualifiedName;
    std::wstring namespaceName;
    std::wstring namespaceUri;
    std::wstring value;
};

// Distinguishes an absent property from a present/ambiguous representation
// that QuickSift must preserve without interpreting.
[[nodiscard]] XmpSimpleValueInspection InspectXmpSimpleValue(
    const std::wstring& text, const XmpPropertyQuery& query);

// Reads one unambiguous simple attribute or text-only element. Comments,
// malformed syntax, duplicate values, and unsupported RDF forms are rejected.
[[nodiscard]] std::optional<std::wstring> ReadXmpSimpleValue(
    const std::wstring& text, const XmpPropertyQuery& query);

// Updates an existing XMP attribute/element or appends an attribute to the first
// rdf:Description start tag. The function preserves unknown XMP content and
// handles both ordinary and self-closing rdf:Description tags.
[[nodiscard]] bool UpdateXmpSimpleValue(std::wstring& text, const XmpPropertyUpdate& update);

} // namespace quicksift::metadata
