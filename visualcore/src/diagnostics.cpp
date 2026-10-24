#include "vc/diagnostics.hpp"

namespace vc {
namespace {

thread_local std::string currentPath;
thread_local std::optional<Diagnostic> recorded;

} // namespace

bool refuse(std::string_view message) {
    if (!recorded) recorded = Diagnostic{currentPath, std::string{message}};
    return false;
}

std::nullopt_t refused(std::string_view message) {
    refuse(message);
    return std::nullopt;
}

DiagnosticScope::DiagnosticScope(std::string_view segment) : restore_{currentPath.size()} {
    if (!currentPath.empty() && !segment.starts_with('[')) currentPath += '.';
    currentPath += segment;
}

DiagnosticScope::~DiagnosticScope() {
    currentPath.resize(restore_);
}

void clearDiagnostic() {
    recorded.reset();
}

std::optional<Diagnostic> lastDiagnostic() {
    return recorded;
}

void restoreDiagnostic(std::optional<Diagnostic> diagnostic) {
    recorded = std::move(diagnostic);
}

} // namespace vc
