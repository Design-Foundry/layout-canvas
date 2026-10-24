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
    /// Applies a page of the server log, in log order, beneath the local edits the server hasn't
    /// confirmed yet: those are taken back, the page applied, and the rest put back on top, so the
    /// document ends up as the server will have it once they land. A log op carrying the id of a
    /// local edit confirms it. Local edits that no longer apply are dropped, and unsent ones never go
    /// out. Returns how many log ops applied.
    int receive(const std::vector<Op>& ops);
    /// True when the document holds only what the server has confirmed: no local edit is waiting
    /// for the log and no preview is showing.
    [[nodiscard]] bool isSettled() const { return unconfirmed_.empty() && preview_.empty(); }

    /// Shows `op` in the document without recording it, replacing the previous preview. Used while a
    /// gesture is in progress so that only its final state becomes an op.
    bool preview(const OpBody& op);
    /// Shows several ops as one preview. All apply or none do; they commit as one undo step.
    bool preview(const std::vector<OpBody>& ops);
    /// Turns the current preview into a regular edit.
    bool commitPreview();
    void cancelPreview();
    [[nodiscard]] bool hasPreview() const { return !preview_.empty(); }

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

    /// A local edit not yet seen in the server log, with the op that takes it back.
    struct Unconfirmed {
        Id id;
        OpBody op;
        OpBody inverse;
    };

    void record(const OpBody& op, const OpBody& inverse);

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
    std::vector<Unconfirmed> unconfirmed_;
    std::vector<OpBody> preview_;
    std::vector<OpBody> previewInverse_;
    std::uint64_t revision_ = 0;
    std::uint64_t structureRevision_ = 0;
    std::uint64_t tokenRevision_ = 0;
};

} // namespace vc
