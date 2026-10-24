#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
#include "vc/diagnostics.hpp"
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

TokenType familyOf(TokenFamilyHint hint) {
    switch (hint) {
    case TokenFamilyHint::Color:
        return TokenType::Color;
    case TokenFamilyHint::Dimension:
        return TokenType::Dimension;
    case TokenFamilyHint::Opacity:
        return TokenType::Opacity;
    case TokenFamilyHint::Border:
        return TokenType::Border;
    case TokenFamilyHint::Shadow:
        return TokenType::Shadow;
    case TokenFamilyHint::Typography:
        return TokenType::Typography;
    }
    return TokenType::Color;
}

bool isComposite(TokenType type) {
    return type == TokenType::Border || type == TokenType::Shadow || type == TokenType::Typography;
}

std::string familyName(TokenType type) {
    switch (type) {
    case TokenType::Color:
        return "color";
    case TokenType::Dimension:
        return "dimension";
    case TokenType::Duration:
        return "duration";
    case TokenType::Ratio:
        return "ratio";
    case TokenType::Opacity:
        return "opacity";
    case TokenType::Border:
        return "border";
    case TokenType::Shadow:
        return "shadow";
    case TokenType::Typography:
        return "typography";
    }
    return "color";
}

std::string allowedUnits(TokenType type) {
    const auto units = unitsFor(type);
    if (units.size() == 1 && units.front() == Unit::None) return "no unit";
    std::string out = "unit ";
    for (std::size_t i = 0; i < units.size(); ++i) {
        out += i == 0 ? "" : i + 1 == units.size() ? " or " : ", ";
        out += unitName(units[i]);
    }
    return out;
}

std::string refProblem(const Id& id, const Token* target, TokenType expected, std::string_view field) {
    std::string out{field};
    out += " references token " + id.toString();
    if (!target) return out + " which does not exist";
    return out + ", a " + familyName(target->type) + " token, but it must be a " + familyName(expected) + " token";
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
    bool found = false;
    for (const auto& [id, node] : nodes_) {
        forEachPatchTokenRef(fullPatch(node), [&](const Id& target, TokenFamilyHint) {
            found = found || target == token;
        });
        if (found) return true;
        for (const auto& rule : node.definition.rules) {
            forEachPatchTokenRef(rule.patch, [&](const Id& target, TokenFamilyHint) { found = found || target == token; });
        }
        if (found) return true;
    }
    for (const auto& [id, other] : tokens_) {
        forEachTokenReference(other, [&](const Id& target) { found = found || target == token; });
        if (found) return true;
    }
    return false;
}

std::optional<TokenLiteral> Document::resolveToken(const Id& token, const Id& mode) const {
    return resolveToken(token, modes_.contains(mode) ? mode : kDefaultModeId, 0);
}

bool Document::applyColorModifiers(Oklch& color, const std::vector<Modifier>& modifiers, const Id& mode,
                                   int depth) const {
    for (const Modifier& modifier : modifiers) {
        if (const auto* channel = std::get_if<ColorChannelModifier>(&modifier)) {
            double operand = channel->value.value_or(0.0);
            if (channel->ref) {
                const auto resolved = resolveToken(*channel->ref, mode, depth + 1);
                const auto* magnitude = resolved ? std::get_if<Magnitude>(&*resolved) : nullptr;
                if (!magnitude) return false;
                operand = magnitude->value;
            }
            switch (channel->channel) {
            case ColorChannel::L:
                color.l = std::clamp(applyOp(channel->op, color.l, operand), 0.0, 1.0);
                break;
            case ColorChannel::C:
                color.c = std::max(0.0, applyOp(channel->op, color.c, operand));
                break;
            case ColorChannel::H:
                color.h = wrapHue(applyOp(channel->op, color.h, operand));
                break;
            case ColorChannel::Alpha:
                color.a = std::clamp(applyOp(channel->op, color.a, operand), 0.0, 1.0);
                break;
            }
        } else if (const auto* mixer = std::get_if<ColorMixModifier>(&modifier)) {
            const auto other = resolveToken(mixer->token, mode, depth + 1);
            const auto* otherColor = other ? std::get_if<Oklch>(&*other) : nullptr;
            if (!otherColor) return false;
            color = mix(color, *otherColor, std::clamp(mixer->amount, 0.0, 1.0));
        } else {
            return false;
        }
    }
    return true;
}

std::optional<TokenLiteral> Document::resolveToken(const Id& id, const Id& mode, int depth) const {
    const Token* token = findToken(id);
    if (!token || depth > kMaxResolveDepth) {
        return std::nullopt;
    }
    if (isComposite(token->type)) {
        return token->value;
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

    if (auto* color = std::get_if<Oklch>(&*value)) {
        if (!token->isAlias()) color->a = 1.0;
        if (!applyColorModifiers(*color, token->modifiers, mode, depth)) return std::nullopt;
        return value;
    }

    auto* magnitude = std::get_if<Magnitude>(&*value);
    if (!magnitude) return std::nullopt;
    for (const Modifier& modifier : token->modifiers) {
        const auto* step = std::get_if<MagnitudeModifier>(&modifier);
        if (!step) return std::nullopt;
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
    if (token->type == TokenType::Opacity) {
        magnitude->value = std::clamp(magnitude->value, 0.0, 1.0);
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

double Document::resolveOpacity(const Value<double>& value, const Id& mode) const {
    if (const auto* opacity = std::get_if<double>(&value)) {
        return *opacity;
    }
    const auto resolved = resolveToken(std::get<TokenRef>(value).token, mode);
    const auto* magnitude = resolved ? std::get_if<Magnitude>(&*resolved) : nullptr;
    return magnitude ? std::clamp(magnitude->value, 0.0, 1.0) : 1.0;
}

std::array<double, 4> Document::resolveCorners(const Node& node, const Id& mode) const {
    const double all = resolveLength(node.cornerRadius, mode).value_or(0.0);
    std::array<double, 4> corners{all, all, all, all};
    for (std::size_t i = 0; i < 4; ++i) {
        if (node.cornerRadii[i]) {
            corners[i] = resolveLength(*node.cornerRadii[i], mode).value_or(0.0);
        }
    }
    return corners;
}

std::optional<ResolvedStroke> Document::resolveStroke(const Value<Stroke>& value, const Id& mode) const {
    ResolvedStroke out;
    if (const auto* ref = std::get_if<TokenRef>(&value)) {
        const auto resolved = resolveToken(ref->token, mode);
        const auto* border = resolved ? std::get_if<BorderValue>(&*resolved) : nullptr;
        if (!border) return std::nullopt;
        const auto width = resolveLength(TokenRef{border->width}, mode);
        const auto color = resolveColor(TokenRef{border->color}, mode);
        if (!width || !color) return std::nullopt;
        out.widths = {*width, *width, *width, *width};
        out.style = border->style;
        out.color = *color;
        out.align = StrokeAlign::Inside;
    } else {
        const Stroke& stroke = std::get<Stroke>(value);
        const double all = resolveLength(stroke.width, mode).value_or(0.0);
        for (std::size_t i = 0; i < 4; ++i) {
            out.widths[i] = stroke.sides[i] ? resolveLength(*stroke.sides[i], mode).value_or(0.0) : all;
        }
        const auto color = resolveColor(stroke.color, mode);
        if (!color) return std::nullopt;
        out.style = stroke.style;
        out.color = *color;
        out.align = stroke.align;
    }
    const bool anyWidth = std::any_of(out.widths.begin(), out.widths.end(), [](double w) { return w > 0.0; });
    if (out.style == StrokeStyle::None || !anyWidth) {
        return std::nullopt;
    }
    return out;
}

std::vector<ResolvedShadow> Document::resolveShadows(const Value<std::vector<Shadow>>& value, const Id& mode) const {
    std::vector<ResolvedShadow> out;
    if (const auto* ref = std::get_if<TokenRef>(&value)) {
        const auto resolved = resolveToken(ref->token, mode);
        const auto* shadow = resolved ? std::get_if<ShadowValue>(&*resolved) : nullptr;
        if (!shadow) return out;
        for (const ShadowLayerValue& layer : shadow->layers) {
            auto color = resolveColor(TokenRef{layer.color}, mode);
            if (!color || !applyColorModifiers(*color, layer.colorModifiers, mode, 0)) continue;
            out.push_back({layer.x, layer.y, layer.blur, layer.spread, *color, layer.inset});
        }
        return out;
    }
    for (const Shadow& layer : std::get<std::vector<Shadow>>(value)) {
        if (!layer.visible) continue;
        if (const auto color = resolveColor(layer.color, mode)) {
            out.push_back({layer.x, layer.y, layer.blur, layer.spread, *color, layer.inset});
        }
    }
    return out;
}

bool Document::isValidColorModifiers(const std::vector<Modifier>& modifiers) const {
    for (std::size_t i = 0; i < modifiers.size(); ++i) {
        const DiagnosticScope scope{"modifiers[" + std::to_string(i) + "]"};
        const Modifier& modifier = modifiers[i];
        if (const auto* channel = std::get_if<ColorChannelModifier>(&modifier)) {
            if (!hasOneOperand(channel->value, channel->ref)) {
                return refuse("a channel modifier takes exactly one operand: value or ref");
            }
            if (channel->ref) {
                if (channel->channel != ColorChannel::Alpha) {
                    return refuse("only the alpha channel takes a ref operand");
                }
                const Token* target = findToken(*channel->ref);
                if (!target || target->type != TokenType::Opacity) {
                    return refuse(refProblem(*channel->ref, target, TokenType::Opacity, "the alpha operand"));
                }
            }
        } else if (const auto* mixer = std::get_if<ColorMixModifier>(&modifier)) {
            const Token* target = findToken(mixer->token);
            if (!target || target->type != TokenType::Color) {
                return refuse(refProblem(mixer->token, target, TokenType::Color, "the mix"));
            }
            if (mixer->amount < 0.0 || mixer->amount > 1.0) return refuse("the mix amount must be between 0 and 1");
        } else {
            return refuse("a color modifier is a channel modifier (l, c, h or alpha) or a mix");
        }
    }
    return true;
}

bool Document::isValidToken(const Token& token) const {
    if (token.id.isNil()) return refuse("a token needs an id");
    if (!isValidIdentifier(token.identifier)) {
        return refuse("identifier \"" + token.identifier +
                      "\" must be dot-separated lowercase kebab-case segments that each start with a letter, such as space.lg or color.text-subtle");
    }
    if (token.value.has_value() == token.isAlias()) {
        return refuse(token.value ? "a token holds either a value or an alias source, not both"
                                  : "a token needs either a value or an alias source");
    }
    for (const auto& [id, other] : tokens_) {
        if (id != token.id && other.type == token.type && other.identifier == token.identifier) {
            return refuse("identifier \"" + token.identifier + "\" is already used by another " + familyName(token.type) +
                          " token");
        }
    }
    const auto refersTo = [this](const Id& id, TokenType type) {
        const Token* target = findToken(id);
        return target && target->type == type;
    };
    const auto refuseRef = [this](const Id& id, TokenType type, std::string_view field) {
        return refuse(refProblem(id, findToken(id), type, field));
    };
    const auto refuseCycle = [] { return refuse("this alias or modifier would create a reference cycle"); };

    if (isComposite(token.type)) {
        if (token.isAlias()) return refuse(familyName(token.type) + " tokens cannot be aliases");
        if (!token.modifiers.empty()) return refuse(familyName(token.type) + " tokens cannot have modifiers");
        if (token.type == TokenType::Typography) {
            const auto* style = std::get_if<TextStyle>(&*token.value);
            if (!style) return refuse("a typography token holds a text style");
            if (!style->font) return refuse("a typography token needs font");
            if (!style->fontSize) return refuse("a typography token needs fontSize");
            if (!style->lineHeight) return refuse("a typography token needs lineHeight");
            if (!style->letterSpacing) return refuse("a typography token needs letterSpacing");
            if (!style->align) return refuse("a typography token needs align");
            TextRun run; Node node; node.text=run; NodePatch patch; patch.textStyle=*style;
            applyPatch(node,patch);
            if (const char* why = textProblem(node.text)) return refuse(why);
            return true;
        }
        if (token.type == TokenType::Border) {
            const auto* border = std::get_if<BorderValue>(&*token.value);
            if (!border) return refuse("a border token holds a width, a style and a color");
            if (!refersTo(border->width, TokenType::Dimension)) {
                return refuseRef(border->width, TokenType::Dimension, "the border width");
            }
            if (!refersTo(border->color, TokenType::Color)) {
                return refuseRef(border->color, TokenType::Color, "the border color");
            }
            return !hasCycle(token) || refuseCycle();
        }
        const auto* shadow = std::get_if<ShadowValue>(&*token.value);
        if (!shadow) return refuse("a shadow token holds a list of layers");
        if (shadow->layers.empty()) return refuse("a shadow token needs at least one layer");
        for (std::size_t i = 0; i < shadow->layers.size(); ++i) {
            const ShadowLayerValue& layer = shadow->layers[i];
            const DiagnosticScope scope{"layers[" + std::to_string(i) + "]"};
            if (layer.blur < 0.0) return refuse("blur cannot be negative");
            if (!refersTo(layer.color, TokenType::Color)) {
                return refuseRef(layer.color, TokenType::Color, "the layer color");
            }
            if (!isValidColorModifiers(layer.colorModifiers)) return false;
        }
        return !hasCycle(token) || refuseCycle();
    }

    if (token.value) {
        if (token.type == TokenType::Color) {
            const auto* color = std::get_if<Oklch>(&*token.value);
            if (!color) return refuse("a color token holds an oklch color");
            if (color->l < 0.0 || color->l > 1.0) return refuse("color l must be between 0 and 1");
            if (color->c < 0.0) return refuse("color c cannot be negative");
            if (color->h < 0.0 || color->h > 360.0) return refuse("color h must be between 0 and 360");
        } else {
            const auto* magnitude = std::get_if<Magnitude>(&*token.value);
            if (!magnitude) return refuse("a " + familyName(token.type) + " token holds a value with a unit");
            const auto units = unitsFor(token.type);
            if (std::find(units.begin(), units.end(), magnitude->unit) == units.end()) {
                return refuse(familyName(token.type) + " values take " + allowedUnits(token.type));
            }
            if (token.type == TokenType::Opacity && (magnitude->value < 0.0 || magnitude->value > 1.0)) {
                return refuse("opacity must be between 0 and 1");
            }
        }
    } else {
        if (!token.source.contains(kDefaultModeId)) return refuse("an alias needs a source for the default mode");
        for (const auto& [mode, source] : token.source) {
            if (!modes_.contains(mode)) return refuse("alias source for mode " + mode.toString() + " names a mode that does not exist");
            if (source == token.id) return refuse("an alias cannot point to itself");
            if (!refersTo(source, token.type)) {
                return refuseRef(source, token.type, "the alias source");
            }
        }
    }

    if (token.type == TokenType::Color) {
        if (!isValidColorModifiers(token.modifiers)) return false;
    } else {
        for (std::size_t i = 0; i < token.modifiers.size(); ++i) {
            const DiagnosticScope scope{"modifiers[" + std::to_string(i) + "]"};
            const auto* step = std::get_if<MagnitudeModifier>(&token.modifiers[i]);
            if (!step) return refuse(familyName(token.type) + " tokens take only magnitude modifiers (factor, delta, min, max)");
            if (!hasOneOperand(step->value, step->ref)) return refuse("a modifier takes exactly one operand: value or ref");
            if (step->ref && !refersTo(*step->ref, token.type)) {
                return refuseRef(*step->ref, token.type, "the modifier operand");
            }
        }
    }
    return !hasCycle(token) || refuseCycle();
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
    return isValidPatchRefs(fullPatch(node));
}

bool Document::isValidPatchRefs(const NodePatch& patch) const {
    bool valid = true;
    forEachPatchTokenRef(patch, [&](const Id& id, TokenFamilyHint hint) {
        if (!valid) return;
        const Token* token = findToken(id);
        if (token && token->type == familyOf(hint)) return;
        valid = false;
        if (!token) {
            refuse("token " + id.toString() + " does not exist");
        } else {
            refuse("token " + id.toString() + " is a " + familyName(token->type) + ", but this field takes a " +
                   familyName(familyOf(hint)) + " token");
        }
    });
    return valid;
}

std::optional<OpBody> Document::createToken(const CreateToken& op) {
    if (tokens_.contains(op.token.id)) return refused("a token with this id already exists");
    if (!isValidToken(op.token)) return std::nullopt;
    tokens_.emplace(op.token.id, op.token);
    return DeleteToken{op.token.id};
}

std::optional<OpBody> Document::deleteToken(const DeleteToken& op) {
    const auto it = tokens_.find(op.id);
    if (it == tokens_.end()) return refused("no token has this id");
    if (isTokenReferenced(op.id)) return refused("token is still referenced by a node or another token, remove those references first");
    CreateToken inverse{it->second};
    tokens_.erase(it);
    return inverse;
}

std::optional<OpBody> Document::replaceToken(const ReplaceToken& op) {
    const auto it = tokens_.find(op.token.id);
    if (it == tokens_.end()) return refused("no token has this id");
    if (it->second.type != op.token.type) {
        return refused("a token cannot change family, it is a " + familyName(it->second.type));
    }
    if (!isValidToken(op.token)) return std::nullopt;
    ReplaceToken inverse{it->second};
    it->second = op.token;
    return inverse;
}

std::optional<OpBody> Document::createMode(const CreateMode& op) {
    if (op.mode.id.isNil()) return refused("a mode needs an id");
    if (modes_.contains(op.mode.id)) return refused("a mode with this id already exists");
    if (op.mode.name.empty()) return refused("a mode needs a name");
    modes_.emplace(op.mode.id, op.mode);
    return DeleteMode{op.mode.id};
}

std::optional<OpBody> Document::deleteMode(const DeleteMode& op) {
    const auto it = modes_.find(op.id);
    if (it == modes_.end()) return refused("no mode has this id");
    if (op.id == kDefaultModeId) return refused("the default mode cannot be deleted");
    for (const auto& [id, token] : tokens_) {
        if (token.source.contains(op.id)) {
            return refused("token \"" + token.identifier + "\" still has an alias source for this mode, remove it first");
        }
    }
    CreateMode inverse{it->second};
    modes_.erase(it);
    return inverse;
}

std::optional<OpBody> Document::replaceMode(const ReplaceMode& op) {
    const auto it = modes_.find(op.mode.id);
    if (it == modes_.end()) return refused("no mode has this id");
    if (op.mode.name.empty()) return refused("a mode needs a name");
    ReplaceMode inverse{it->second};
    it->second = op.mode;
    return inverse;
}

} // namespace vc
