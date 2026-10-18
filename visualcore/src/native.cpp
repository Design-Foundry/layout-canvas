#include "vc/native.h"

#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

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
    return guarded([&]() -> vc_document* {
        auto handle = new vc_document{};
        int refused = 0;
        if (!viewOf(snapshot).empty()) {
            const auto ops = vc::snapshotFromJson(snapshot);
            if (!ops) {
                delete handle;
                return nullptr;
            }
            for (const vc::OpBody& op : *ops) {
                if (!handle->document.apply(op)) ++refused;
            }
        }
        if (rejected) *rejected = refused;
        return handle;
    });
}

void vc_document_close(vc_document* document) {
    delete document;
}

char* vc_document_apply(vc_document* document, const char* ops) {
    return guarded([&]() -> char* {
        const auto parsed = document ? vc::opsFromJson(viewOf(ops)) : std::nullopt;
        if (!parsed) return nullptr;
        std::string out = "[";
        for (const vc::OpBody& op : *parsed) {
            if (out.size() > 1) out += ',';
            out += document->document.apply(op) ? "true" : "false";
        }
        out += ']';
        return copyOut(out);
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
