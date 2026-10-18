#include <algorithm>
#include <cmath>
#include <unordered_set>
#include "vc/document.hpp"

namespace vc {
namespace {

constexpr int kMaxResolveDepth = 64;
constexpr double kPxPerRem = 16.0;
constexpr double kPxPerPt = 4.0 / 3.0;

bool hasOneOperand(const std::optional<double>& value, const std::optional<Id>& ref) {
    return value.has_value() != ref.has_value();
}

double wrapHue(double hue) {
    const double wrapped = std::fmod(hue, 360.0);
    return wrapped < 0.0 ? wrapped + 360.0 : wrapped;
}

double applyOp(ColorOp op, double current, double operand) {
    switch (op) {
    case ColorOp::Set:
        return operand;
    case ColorOp::Delta:
        return current + operand;
    case ColorOp::Factor:
        return current * operand;
    }
    return current;
}

Oklch mix(const Oklch& from, const Oklch& to, double amount) {
    double fromHue = from.h;
    double toHue = to.h;
    if (from.c == 0.0) fromHue = toHue;
    if (to.c == 0.0) toHue = fromHue;
    double delta = toHue - fromHue;
    if (delta > 180.0) delta -= 360.0;
    if (delta < -180.0) delta += 360.0;
    return {
        from.l + (to.l - from.l) * amount,
        from.c + (to.c - from.c) * amount,
        wrapHue(fromHue + delta * amount),
        from.a + (to.a - from.a) * amount,
    };
}

double lengthToPx(const Magnitude& length) {
    switch (length.unit) {
    case Unit::Rem:
    case Unit::Em:
        return length.value * kPxPerRem;
    case Unit::Pt:
        return length.value * kPxPerPt;
    default:
        return length.value;
    }
}

} // namespace

const Token* Document::findToken(const Id& id) const {
    const auto it = tokens_.find(id);
    return it == tokens_.end() ? nullptr : &it->second;
}

std::vector<const Token*> Document::tokens() const {
    std::vector<const Token*> out;
    out.reserve(tokens_.size());
    for (const auto& [id, token] : tokens_) {
        out.push_back(&token);
    }
    std::sort(out.begin(), out.end(), [](const Token* a, const Token* b) {
        if (a->type != b->type) return a->type < b->type;
        if (a->order != b->order) return a->order < b->order;
        return a->id < b->id;
    });
    return out;
}

const Mode* Document::findMode(const Id& id) const {
    const auto it = modes_.find(id);
    return it == modes_.end() ? nullptr : &it->second;
}

std::vector<const Mode*> Document::modes() const {
    std::vector<const Mode*> out;
    for (const auto& [id, mode] : modes_) {
        out.push_back(&mode);
    }
    std::sort(out.begin(), out.end(), [](const Mode* a, const Mode* b) {
        const bool aDefault = a->id == kDefaultModeId;
        const bool bDefault = b->id == kDefaultModeId;
        if (aDefault != bDefault) return aDefault;
        if (a->order != b->order) return a->order < b->order;
        return a->id < b->id;
    });
    return out;
}

bool Document::isTokenReferenced(const Id& token) const {
    const TokenRef ref{token};
    for (const auto& [id, node] : nodes_) {
        if (node.fill == Value<Oklch>{ref} || node.cornerRadius == Value<double>{ref}) {
            return true;
        }
    }
    for (const auto& [id, other] : tokens_) {
        bool found = false;
        forEachTokenReference(other, [&](const Id& target) { found = found || target == token; });
        if (found) return true;
    }
    return false;
}

std::optional<TokenLiteral> Document::resolveToken(const Id& token, const Id& mode) const {
    return resolveToken(token, modes_.contains(mode) ? mode : kDefaultModeId, 0);
}

std::optional<TokenLiteral> Document::resolveToken(const Id& id, const Id& mode, int depth) const {
    const Token* token = findToken(id);
    if (!token || depth > kMaxResolveDepth) {
        return std::nullopt;
    }

    std::optional<TokenLiteral> value = token->value;
    if (token->isAlias()) {
        auto source = token->source.find(mode);
        if (source == token->source.end()) source = token->source.find(kDefaultModeId);
        if (source == token->source.end()) return std::nullopt;
        value = resolveToken(source->second, mode, depth + 1);
    }
    if (!value) {
        return std::nullopt;
    }
    if (auto* color = std::get_if<Oklch>(&*value); color && !token->isAlias()) {
        color->a = 1.0;
    }

    for (const Modifier& modifier : token->modifiers) {
        if (const auto* channel = std::get_if<ColorChannelModifier>(&modifier)) {
            auto* color = std::get_if<Oklch>(&*value);
            if (!color) return std::nullopt;
            double operand = channel->value.value_or(0.0);
            if (channel->ref) {
                const auto resolved = resolveToken(*channel->ref, mode, depth + 1);
                const auto* magnitude = resolved ? std::get_if<Magnitude>(&*resolved) : nullptr;
                if (!magnitude) return std::nullopt;
                operand = magnitude->value;
            }
            switch (channel->channel) {
            case ColorChannel::L:
                color->l = std::clamp(applyOp(channel->op, color->l, operand), 0.0, 1.0);
                break;
            case ColorChannel::C:
                color->c = std::max(0.0, applyOp(channel->op, color->c, operand));
                break;
            case ColorChannel::H:
                color->h = wrapHue(applyOp(channel->op, color->h, operand));
                break;
            case ColorChannel::Alpha:
                color->a = std::clamp(applyOp(channel->op, color->a, operand), 0.0, 1.0);
                break;
            }
        } else if (const auto* mixer = std::get_if<ColorMixModifier>(&modifier)) {
            auto* color = std::get_if<Oklch>(&*value);
            const auto other = resolveToken(mixer->token, mode, depth + 1);
            const auto* otherColor = other ? std::get_if<Oklch>(&*other) : nullptr;
            if (!color || !otherColor) return std::nullopt;
            *color = mix(*color, *otherColor, std::clamp(mixer->amount, 0.0, 1.0));
        } else if (const auto* step = std::get_if<MagnitudeModifier>(&modifier)) {
            auto* magnitude = std::get_if<Magnitude>(&*value);
            if (!magnitude) return std::nullopt;
            double operand = step->value.value_or(0.0);
            if (step->ref) {
                const auto resolved = resolveToken(*step->ref, mode, depth + 1);
                const auto* other = resolved ? std::get_if<Magnitude>(&*resolved) : nullptr;
                if (!other) return std::nullopt;
                operand = other->value;
            }
            switch (step->op) {
            case MagnitudeOp::Factor:
                magnitude->value *= operand;
                break;
            case MagnitudeOp::Delta:
                magnitude->value += operand;
                break;
            case MagnitudeOp::Min:
                magnitude->value = std::min(magnitude->value, operand);
                break;
            case MagnitudeOp::Max:
                magnitude->value = std::max(magnitude->value, operand);
                break;
            }
        }
    }
    if (token->type == TokenType::Opacity) {
        auto& magnitude = std::get<Magnitude>(*value);
        magnitude.value = std::clamp(magnitude.value, 0.0, 1.0);
    }
    return value;
}

std::optional<Oklch> Document::resolveColor(const Value<Oklch>& value, const Id& mode) const {
    if (const auto* color = std::get_if<Oklch>(&value)) {
        return *color;
    }
    const auto resolved = resolveToken(std::get<TokenRef>(value).token, mode);
    const auto* color = resolved ? std::get_if<Oklch>(&*resolved) : nullptr;
    return color ? std::optional{*color} : std::nullopt;
}

std::optional<double> Document::resolveLength(const Value<double>& value, const Id& mode) const {
    if (const auto* length = std::get_if<double>(&value)) {
        return *length;
    }
    const Id id = std::get<TokenRef>(value).token;
    const Token* token = findToken(id);
    if (!token || token->type != TokenType::Dimension) {
        return std::nullopt;
    }
    const auto resolved = resolveToken(id, mode);
    const auto* magnitude = resolved ? std::get_if<Magnitude>(&*resolved) : nullptr;
    return magnitude ? std::optional{lengthToPx(*magnitude)} : std::nullopt;
}

bool Document::isValidToken(const Token& token) const {
    if (token.id.isNil() || !isValidIdentifier(token.identifier) || token.value.has_value() == token.isAlias()) {
        return false;
    }
    for (const auto& [id, other] : tokens_) {
        if (id != token.id && other.type == token.type && other.identifier == token.identifier) {
            return false;
        }
    }

    if (token.value) {
        if (token.type == TokenType::Color) {
            const auto* color = std::get_if<Oklch>(&*token.value);
            if (!color || color->l < 0.0 || color->l > 1.0 || color->c < 0.0 || color->h < 0.0 || color->h > 360.0) {
                return false;
            }
        } else {
            const auto* magnitude = std::get_if<Magnitude>(&*token.value);
            const auto units = unitsFor(token.type);
            if (!magnitude || std::find(units.begin(), units.end(), magnitude->unit) == units.end()) {
                return false;
            }
            if (token.type == TokenType::Opacity && (magnitude->value < 0.0 || magnitude->value > 1.0)) {
                return false;
            }
        }
    } else {
        if (!token.source.contains(kDefaultModeId)) {
            return false;
        }
        for (const auto& [mode, source] : token.source) {
            const Token* target = findToken(source);
            if (!modes_.contains(mode) || !target || target->type != token.type || source == token.id) {
                return false;
            }
        }
    }

    for (const Modifier& modifier : token.modifiers) {
        if (const auto* channel = std::get_if<ColorChannelModifier>(&modifier)) {
            if (token.type != TokenType::Color || !hasOneOperand(channel->value, channel->ref)) return false;
            if (channel->ref) {
                const Token* target = findToken(*channel->ref);
                if (channel->channel != ColorChannel::Alpha || !target || target->type != TokenType::Opacity) {
                    return false;
                }
            }
        } else if (const auto* mixer = std::get_if<ColorMixModifier>(&modifier)) {
            const Token* target = findToken(mixer->token);
            if (token.type != TokenType::Color || !target || target->type != TokenType::Color ||
                mixer->amount < 0.0 || mixer->amount > 1.0) {
                return false;
            }
        } else {
            const auto& step = std::get<MagnitudeModifier>(modifier);
            if (token.type == TokenType::Color || !hasOneOperand(step.value, step.ref)) return false;
            if (step.ref) {
                const Token* target = findToken(*step.ref);
                if (!target || target->type != token.type) return false;
            }
        }
    }
    return !hasCycle(token);
}

bool Document::hasCycle(const Token& candidate) const {
    std::unordered_set<Id> visiting;
    std::unordered_set<Id> done;
    const auto references = [&](const Id& id, auto&& fn) {
        const Token* token = id == candidate.id ? &candidate : findToken(id);
        if (token) forEachTokenReference(*token, fn);
    };
    const auto visit = [&](const Id& id, auto&& self) -> bool {
        if (done.contains(id)) return false;
        if (!visiting.insert(id).second) return true;
        bool cycle = false;
        references(id, [&](const Id& next) { cycle = cycle || self(next, self); });
        visiting.erase(id);
        done.insert(id);
        return cycle;
    };
    return visit(candidate.id, visit);
}

bool Document::isValidNodeRefs(const Node& node) const {
    NodePatch patch;
    patch.fill = node.fill;
    patch.cornerRadius = node.cornerRadius;
    return isValidPatchRefs(patch);
}

bool Document::isValidPatchRefs(const NodePatch& patch) const {
    const auto refersTo = [this](const auto& value, TokenType type) {
        const auto* ref = std::get_if<TokenRef>(&value);
        if (!ref) return true;
        const Token* token = findToken(ref->token);
        return token && token->type == type;
    };
    return (!patch.fill || refersTo(*patch.fill, TokenType::Color)) &&
           (!patch.cornerRadius || refersTo(*patch.cornerRadius, TokenType::Dimension));
}

std::optional<OpBody> Document::createToken(const CreateToken& op) {
    if (tokens_.contains(op.token.id) || !isValidToken(op.token)) {
        return std::nullopt;
    }
    tokens_.emplace(op.token.id, op.token);
    return DeleteToken{op.token.id};
}

std::optional<OpBody> Document::deleteToken(const DeleteToken& op) {
    const auto it = tokens_.find(op.id);
    if (it == tokens_.end() || isTokenReferenced(op.id)) {
        return std::nullopt;
    }
    CreateToken inverse{it->second};
    tokens_.erase(it);
    return inverse;
}

std::optional<OpBody> Document::replaceToken(const ReplaceToken& op) {
    const auto it = tokens_.find(op.token.id);
    if (it == tokens_.end() || it->second.type != op.token.type || !isValidToken(op.token)) {
        return std::nullopt;
    }
    ReplaceToken inverse{it->second};
    it->second = op.token;
    return inverse;
}

std::optional<OpBody> Document::createMode(const CreateMode& op) {
    if (op.mode.id.isNil() || modes_.contains(op.mode.id) || op.mode.name.empty()) {
        return std::nullopt;
    }
    modes_.emplace(op.mode.id, op.mode);
    return DeleteMode{op.mode.id};
}

std::optional<OpBody> Document::deleteMode(const DeleteMode& op) {
    const auto it = modes_.find(op.id);
    if (it == modes_.end() || op.id == kDefaultModeId) {
        return std::nullopt;
    }
    for (const auto& [id, token] : tokens_) {
        if (token.source.contains(op.id)) {
            return std::nullopt;
        }
    }
    CreateMode inverse{it->second};
    modes_.erase(it);
    return inverse;
}

std::optional<OpBody> Document::replaceMode(const ReplaceMode& op) {
    const auto it = modes_.find(op.mode.id);
    if (it == modes_.end() || op.mode.name.empty()) {
        return std::nullopt;
    }
    ReplaceMode inverse{it->second};
    it->second = op.mode;
    return inverse;
}

} // namespace vc
