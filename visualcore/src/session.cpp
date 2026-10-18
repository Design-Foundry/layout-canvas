#include "vc/session.hpp"

#include <utility>

namespace vc {

void Session::begin() {
    ++depth_;
}

void Session::commit() {
    if (depth_ == 0 || --depth_ > 0) {
        return;
    }
    if (!open_.empty()) {
        undo_.push_back(std::move(open_));
        open_.clear();
    }
}

namespace {

bool changesStructure(const OpBody& op) {
    if (const auto* set = std::get_if<SetProps>(&op)) {
        return set->patch.name || set->patch.visible || set->patch.locked || set->patch.type;
    }
    return std::holds_alternative<CreateNode>(op) || std::holds_alternative<DeleteNode>(op) ||
           std::holds_alternative<MoveNode>(op) || std::holds_alternative<SetOverride>(op);
}

bool changesTokens(const OpBody& op) {
    return std::holds_alternative<CreateToken>(op) || std::holds_alternative<DeleteToken>(op) ||
           std::holds_alternative<ReplaceToken>(op) || std::holds_alternative<CreateMode>(op) ||
           std::holds_alternative<DeleteMode>(op) || std::holds_alternative<ReplaceMode>(op);
}

} // namespace

std::optional<OpBody> Session::apply(const OpBody& op) {
    auto inverse = document_.apply(op);
    if (inverse) {
        ++revision_;
        if (changesStructure(op)) {
            ++structureRevision_;
        }
        if (changesTokens(op)) {
            ++tokenRevision_;
        }
    }
    return inverse;
}

bool Session::edit(const OpBody& op) {
    auto inverse = apply(op);
    if (!inverse) {
        return false;
    }
    outbox_.push_back({newId(), op});
    open_.push_back(std::move(*inverse));
    redo_.clear();
    if (depth_ == 0) {
        undo_.push_back(std::move(open_));
        open_.clear();
    }
    return true;
}

bool Session::deleteTree(const Id& id) {
    if (!document_.find(id)) {
        return false;
    }
    std::vector<Id> ids;
    collectPostOrder(id, ids);

    begin();
    for (const Id& node : ids) {
        edit(DeleteNode{node});
    }
    commit();
    return true;
}

bool Session::deleteMode(const Id& mode) {
    if (mode == kDefaultModeId || !document_.findMode(mode)) {
        return false;
    }
    begin();
    for (const Token* token : document_.tokens()) {
        if (token->source.contains(mode)) {
            Token stripped = *token;
            stripped.source.erase(mode);
            edit(ReplaceToken{std::move(stripped)});
        }
    }
    const bool deleted = edit(DeleteMode{mode});
    commit();
    return deleted;
}

bool Session::undo() {
    return replay(undo_, redo_);
}

bool Session::redo() {
    return replay(redo_, undo_);
}

bool Session::applyRemote(const OpBody& op) {
    return apply(op).has_value();
}

bool Session::preview(const OpBody& op) {
    return preview(std::vector<OpBody>{op});
}

bool Session::preview(const std::vector<OpBody>& ops) {
    cancelPreview();
    for (const OpBody& op : ops) {
        auto inverse = apply(op);
        if (!inverse) {
            cancelPreview();
            return false;
        }
        previewInverse_.push_back(std::move(*inverse));
    }
    preview_ = ops;
    return !ops.empty();
}

bool Session::commitPreview() {
    if (preview_.empty()) {
        return false;
    }
    const std::vector<OpBody> ops = std::move(preview_);
    cancelPreview();
    begin();
    bool applied = true;
    for (const OpBody& op : ops) {
        applied = edit(op) && applied;
    }
    commit();
    return applied;
}

void Session::cancelPreview() {
    for (auto it = previewInverse_.rbegin(); it != previewInverse_.rend(); ++it) {
        apply(*it);
    }
    preview_.clear();
    previewInverse_.clear();
}

std::vector<Op> Session::takeOutbox() {
    return std::exchange(outbox_, {});
}

bool Session::replay(std::vector<Step>& from, std::vector<Step>& to) {
    if (from.empty()) {
        return false;
    }
    Step step = std::move(from.back());
    from.pop_back();

    Step reverse;
    for (auto it = step.rbegin(); it != step.rend(); ++it) {
        if (auto inverse = apply(*it)) {
            outbox_.push_back({newId(), *it});
            reverse.push_back(std::move(*inverse));
        }
    }
    if (!reverse.empty()) {
        to.push_back(std::move(reverse));
    }
    return true;
}

void Session::collectPostOrder(const Id& id, std::vector<Id>& out) const {
    for (const Id& child : document_.children(id)) {
        collectPostOrder(child, out);
    }
    out.push_back(id);
}

} // namespace vc
