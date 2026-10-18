#pragma once
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
#include "vc/document.hpp"
#include "vc/id.hpp"
#include "vc/op.hpp"

namespace vc {

/// A document being edited locally: undo/redo history plus the ops waiting to reach the server.
class Session {
public:
    Session() = default;
    explicit Session(IdGenerator ids) : ids_(std::move(ids)) {}

    [[nodiscard]] const Document& document() const { return document_; }
    [[nodiscard]] Id newId() { return ids_.next(); }

    /// Groups the edits until the matching `commit()` into one undo step. Calls nest.
    void begin();
    void commit();

    /// Applies a local edit. Returns false, changing nothing, if the op is invalid.
    bool edit(const OpBody& op);
    /// Deletes `id` and its descendants as one undo step.
    bool deleteTree(const Id& id);
    /// Removes `mode` from every alias that names it, then deletes it, as one undo step.
    bool deleteMode(const Id& mode);

    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }

    /// Applies an op read from the server log. It is not undoable and is not sent back.
    bool applyRemote(const OpBody& op);

    /// Shows `op` in the document without recording it, replacing the previous preview. Used while a
    /// gesture is in progress so that only its final state becomes an op.
    bool preview(const OpBody& op);
    /// Turns the current preview into a regular edit.
    bool commitPreview();
    void cancelPreview();
    [[nodiscard]] bool hasPreview() const { return preview_.has_value(); }

    /// Increases whenever the document changes. Lets the UI skip unchanged frames.
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    /// Increases only when the tree a layers panel shows changes: nodes added, removed or moved, or a
    /// name or visibility changed. Geometry and paint edits leave it alone.
    [[nodiscard]] std::uint64_t structureRevision() const { return structureRevision_; }
    /// Increases only when tokens or modes change.
    [[nodiscard]] std::uint64_t tokenRevision() const { return tokenRevision_; }

    /// Hands over the ops produced since the last call, in the order they were applied.
    [[nodiscard]] std::vector<Op> takeOutbox();

private:
    using Step = std::vector<OpBody>;

    std::optional<OpBody> apply(const OpBody& op);
    bool replay(std::vector<Step>& from, std::vector<Step>& to);
    void collectPostOrder(const Id& id, std::vector<Id>& out) const;

    Document document_;
    IdGenerator ids_;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    Step open_;
    int depth_ = 0;
    std::vector<Op> outbox_;
    std::optional<OpBody> preview_;
    std::optional<OpBody> previewInverse_;
    std::uint64_t revision_ = 0;
    std::uint64_t structureRevision_ = 0;
    std::uint64_t tokenRevision_ = 0;
};

} // namespace vc
