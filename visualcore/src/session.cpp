#include "vc/session.hpp"

#include <utility>
#include <set>

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
        return set->patch.name || set->patch.visible || set->patch.locked || set->patch.type || set->patch.component || set->patch.componentValues;
    }
    return std::holds_alternative<CreateNode>(op) || std::holds_alternative<DeleteNode>(op) ||
           std::holds_alternative<MoveNode>(op) || std::holds_alternative<SetOverride>(op) || std::holds_alternative<SetComponentDefinition>(op);
}

bool changesTokens(const OpBody& op) {
    return std::holds_alternative<CreateToken>(op) || std::holds_alternative<DeleteToken>(op) ||
           std::holds_alternative<ReplaceToken>(op) || std::holds_alternative<CreateMode>(op) ||
           std::holds_alternative<DeleteMode>(op) || std::holds_alternative<ReplaceMode>(op);
}

// Token rows also expose whether a resource is referenced by scene data. Refresh
// that state when bindings change, without rebuilding the token panel on a drag.
std::set<Id> sceneReferences(const OpBody& op) {
    std::set<Id> refs;
    const auto patch=[&](const NodePatch& p) { forEachPatchTokenRef(p,[&](const Id& id,TokenFamilyHint){refs.insert(id);}); };
    const auto definition=[&](const ComponentDefinition& d) { for(const auto& rule:d.rules) patch(rule.patch); };
    if (const auto* set=std::get_if<SetProps>(&op)) patch(set->patch);
    if (const auto* set=std::get_if<SetOverride>(&op)) patch(set->patch);
    if (const auto* set=std::get_if<SetComponentDefinition>(&op)) definition(set->definition);
    if (const auto* create=std::get_if<CreateNode>(&op)) {
        patch(fullPatch(create->node)); definition(create->node.definition);
        for(const auto& override:create->node.overrides) patch(override.patch);
    }
    return refs;
}

} // namespace

std::optional<OpBody> Session::apply(const OpBody& op) {
    auto inverse = document_.apply(op);
    if (inverse) {
        ++revision_;
        if (changesStructure(op)) {
            ++structureRevision_;
        }
        if (changesTokens(op) || sceneReferences(op) != sceneReferences(*inverse)) {
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
    record(op, *inverse);
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

    std::vector<OpBody> ops;
    for (const Id& node : ids) for (const auto& o : document_.find(node)->overrides)
        ops.push_back(SetOverride{node, o.node, {}});
    for (const Id& node : ids) if (!document_.find(node)->componentValues.empty()) {
        NodePatch p; p.componentValues = ComponentValues{}; ops.push_back(SetProps{node, p});
    }
    for (const Id& node : ids) if (!document_.find(node)->definition.empty()) ops.push_back(SetComponentDefinition{node, {}});
    for (const Id& node : ids) ops.push_back(DeleteNode{node});
    return preview(ops) && commitPreview();
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

void Session::record(const OpBody& op, const OpBody& inverse) {
    const Id id = newId();
    outbox_.push_back({id, op});
    unconfirmed_.push_back({id, op, inverse});
}

int Session::receive(const std::vector<Op>& ops) {
    // Our own edits coming back in the order we made them change nothing locally.
    std::size_t next = 0;
    while (next < ops.size() && !unconfirmed_.empty() && ops[next].id == unconfirmed_.front().id) {
        unconfirmed_.erase(unconfirmed_.begin());
        ++next;
    }
    int applied = static_cast<int>(next);
    if (next == ops.size()) {
        return applied;
    }

    const std::vector<OpBody> shown = preview_;
    cancelPreview();
    for (auto it = unconfirmed_.rbegin(); it != unconfirmed_.rend(); ++it) {
        apply(it->inverse);
    }
    for (; next < ops.size(); ++next) {
        const Op& op = ops[next];
        std::erase_if(unconfirmed_, [&](const Unconfirmed& u) { return u.id == op.id; });
        applied += apply(op.body) ? 1 : 0;
    }
    std::vector<Unconfirmed> kept;
    for (Unconfirmed& u : unconfirmed_) {
        if (auto inverse = apply(u.op)) {
            u.inverse = std::move(*inverse);
            kept.push_back(std::move(u));
        } else {
            std::erase_if(outbox_, [&](const Op& o) { return o.id == u.id; });
        }
    }
    unconfirmed_ = std::move(kept);
    if (!shown.empty()) {
        preview(shown);
    }
    return applied;
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
            record(*it, *inverse);
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
