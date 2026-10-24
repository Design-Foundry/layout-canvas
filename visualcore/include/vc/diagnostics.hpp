#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace vc {

/// Why the document or a reader refused its input, and where in that input.
struct Diagnostic {
    /// Dotted location such as `nodes[4].definition.rules[2].patch`, empty at the top level.
    std::string path;
    std::string message;

    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

/// Records `message` at the current path, unless an inner check on this thread already gave the
/// reason, and returns false for a check to return.
bool refuse(std::string_view message);

/// Records like `refuse` and returns nothing, for readers and ops that return an optional.
std::nullopt_t refused(std::string_view message);

/// Appends a segment to the path of refusals recorded while it lives. Segments starting with `[`
/// attach to the previous one, others are joined with a dot.
class DiagnosticScope {
public:
    explicit DiagnosticScope(std::string_view segment);
    ~DiagnosticScope();
    DiagnosticScope(const DiagnosticScope&) = delete;
    DiagnosticScope& operator=(const DiagnosticScope&) = delete;

private:
    std::size_t restore_;
};

/// Forgets any recorded refusal. Call before each validation whose reason will be read.
void clearDiagnostic();

/// The first refusal recorded on this thread since the last clear.
[[nodiscard]] std::optional<Diagnostic> lastDiagnostic();

/// Puts a diagnostic back as the recorded one, such as the first of several refused ops.
void restoreDiagnostic(std::optional<Diagnostic> diagnostic);

} // namespace vc
