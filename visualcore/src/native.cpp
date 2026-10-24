#include "vc/native.h"

#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "vc/diagnostics.hpp"
#include "vc/document.hpp"
#include "vc/json.hpp"

struct vc_document {
    vc::Document document;
};

namespace {

char* copyOut(const std::string& text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (out) std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

std::string_view viewOf(const char* text) {
    return text ? std::string_view{text} : std::string_view{};
}

std::string quoted(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            constexpr char hex[] = "0123456789abcdef";
            out += "\\u00";
            out += hex[(c >> 4) & 0xf];
            out += hex[c & 0xf];
        } else {
            out += c;
        }
    }
    return out + '"';
}

std::string errorJson(const vc::Diagnostic& diagnostic) {
    return "{\"path\":" + quoted(diagnostic.path) + ",\"message\":" + quoted(diagnostic.message) + "}";
}

/// Where a snapshot op comes from, such as `nodes[<id>]`, since the snapshot's own order is lost.
std::string originOf(const vc::OpBody& op) {
    return std::visit([](const auto& body) -> std::string {
        using T = std::decay_t<decltype(body)>;
        if constexpr (std::is_same_v<T, vc::CreateNode>) return "nodes[" + body.node.id.toString() + "]";
        else if constexpr (std::is_same_v<T, vc::SetProps>) return "nodes[" + body.id.toString() + "]";
        else if constexpr (std::is_same_v<T, vc::SetOverride>) return "nodes[" + body.instance.toString() + "].overrides";
        else if constexpr (std::is_same_v<T, vc::SetComponentDefinition>) return "nodes[" + body.component.toString() + "]";
        else if constexpr (std::is_same_v<T, vc::CreateToken> || std::is_same_v<T, vc::ReplaceToken>) return "tokens[" + body.token.id.toString() + "]";
        else if constexpr (std::is_same_v<T, vc::CreateMode> || std::is_same_v<T, vc::ReplaceMode>) return "modes[" + body.mode.id.toString() + "]";
        else return "";
    }, op);
}

/// The recorded reason, or a generic one for refusals no check explained yet.
vc::Diagnostic reasonOrDefault(std::string_view fallback) {
    return vc::lastDiagnostic().value_or(vc::Diagnostic{"", std::string{fallback}});
}

/// The mode a query names: the default one for null or empty, nothing if it doesn't exist.
std::optional<vc::Id> modeOf(const vc::Document& document, const char* mode) {
    if (viewOf(mode).empty()) return vc::kDefaultModeId;
    const auto id = vc::Id::parse(mode);
    if (!id || !document.findMode(*id)) return std::nullopt;
    return id;
}

/// Exceptions must not cross into the caller, so every entry point runs through here.
template <typename Fn>
auto guarded(Fn&& fn) noexcept -> decltype(fn()) {
    try {
        return fn();
    } catch (...) {
        return {};
    }
}

} // namespace

extern "C" {

vc_document* vc_document_open(const char* snapshot, int* rejected) {
    vc::clearDiagnostic();
    return guarded([&]() -> vc_document* {
        auto handle = new vc_document{};
        int refused = 0;
        std::optional<vc::Diagnostic> first;
        if (!viewOf(snapshot).empty()) {
            const auto ops = vc::snapshotFromJson(snapshot);
            if (!ops) {
                vc::restoreDiagnostic(reasonOrDefault("the snapshot is malformed"));
                delete handle;
                return nullptr;
            }
            for (const vc::OpBody& op : *ops) {
                vc::clearDiagnostic();
                const vc::DiagnosticScope origin{originOf(op)};
                if (!handle->document.apply(op)) {
                    if (!first) first = reasonOrDefault("the document refused a layer, token or mode of the snapshot");
                    ++refused;
                }
            }
        }
        vc::restoreDiagnostic(first);
        if (rejected) *rejected = refused;
        return handle;
    });
}

void vc_document_close(vc_document* document) {
    delete document;
}

char* vc_document_apply(vc_document* document, const char* ops) {
    vc::clearDiagnostic();
    return guarded([&]() -> char* {
        const auto parsed = document ? vc::opsFromJson(viewOf(ops)) : std::nullopt;
        if (!parsed) {
            vc::restoreDiagnostic(reasonOrDefault("ops must be an array of { kind, data }"));
            return nullptr;
        }
        std::string out = "[";
        std::optional<vc::Diagnostic> first;
        for (std::size_t i = 0; i < parsed->size(); ++i) {
            if (out.size() > 1) out += ',';
            vc::clearDiagnostic();
            if (document->document.apply((*parsed)[i].body)) {
                out += "true";
                continue;
            }
            auto reason = reasonOrDefault("the document refused this op");
            reason.path = "ops[" + std::to_string(i) + "]" + (reason.path.empty() ? "" : "." + reason.path);
            out += errorJson(reason);
            if (!first) first = std::move(reason);
        }
        out += ']';
        vc::restoreDiagnostic(first);
        return copyOut(out);
    });
}

char* vc_last_error(void) {
    return guarded([]() -> char* {
        const auto diagnostic = vc::lastDiagnostic();
        return diagnostic ? copyOut(errorJson(*diagnostic)) : nullptr;
    });
}

char* vc_document_snapshot(const vc_document* document) {
    return guarded([&]() -> char* { return document ? copyOut(vc::snapshotToJson(document->document)) : nullptr; });
}

char* vc_document_layers(const vc_document* document) {
    return guarded([&]() -> char* { return document ? copyOut(vc::layersToJson(document->document)) : nullptr; });
}

char* vc_document_node(const vc_document* document, const char* id, const char* mode) {
    return guarded([&]() -> char* {
        if (!document) return nullptr;
        const vc::Document& doc = document->document;
        const auto parsed = vc::Id::parse(viewOf(id));
        const auto modeId = modeOf(doc, mode);
        if (!parsed || !modeId) return nullptr;
        const vc::Node* node = doc.find(*parsed);
        if (!node) node = doc.view(*parsed);
        return node ? copyOut(vc::inspectNodeJson(doc, *node, *modeId)) : nullptr;
    });
}

char* vc_document_tokens(const vc_document* document, const char* mode) {
    return guarded([&]() -> char* {
        if (!document) return nullptr;
        const auto modeId = modeOf(document->document, mode);
        return modeId ? copyOut(vc::tokensToJson(document->document, *modeId)) : nullptr;
    });
}

char* vc_document_modes(const vc_document* document) {
    return guarded([&]() -> char* { return document ? copyOut(vc::modesToJson(document->document)) : nullptr; });
}

void vc_free(char* text) {
    std::free(text);
}

} // extern "C"
