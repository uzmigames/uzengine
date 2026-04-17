#include "vfx_expression.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace uz {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Built-in registry
// ─────────────────────────────────────────────────────────────────────────────

struct BuiltinInfo { int argCount; };

const std::unordered_map<std::string, BuiltinInfo>& GetBuiltins() {
    static const std::unordered_map<std::string, BuiltinInfo> map = {
        {"add",       {2}},
        {"sub",       {2}},
        {"mul",       {2}},
        {"div",       {2}},
        {"lerp",      {3}},
        {"clamp",     {3}},
        {"sin",       {1}},
        {"cos",       {1}},
        {"dot",       {2}},
        {"normalize", {1}},
    };
    return map;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tokeniser
// ─────────────────────────────────────────────────────────────────────────────

enum class TokKind : uint8_t {
    Number, Ident, LParen, RParen, Comma,
    Plus, Minus, Star, Slash,
    Bang, AmpAmp, PipePipe,
    EqEq, BangEq, Lt, Gt, LtEq, GtEq,
    Question, Colon,
    End, Error
};

struct Token {
    TokKind     kind   = TokKind::End;
    std::string text;
    float       number = 0.0f;
    uint32_t    offset = 0u;
};

class Lexer {
public:
    explicit Lexer(std::string_view src) : src_(src) {}

    Token Next() {
        SkipWhitespace();
        Token t;
        t.offset = pos_;
        if (pos_ >= src_.size()) { t.kind = TokKind::End; return t; }

        char c = src_[pos_];

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            return LexNumber();
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            return LexIdent();
        }

        auto make = [&](TokKind k, uint32_t len = 1) {
            t.kind = k; t.text = std::string(src_.substr(pos_, len)); pos_ += len; return t;
        };

        if (c == '(') return make(TokKind::LParen);
        if (c == ')') return make(TokKind::RParen);
        if (c == ',') return make(TokKind::Comma);
        if (c == '+') return make(TokKind::Plus);
        if (c == '-') return make(TokKind::Minus);
        if (c == '*') return make(TokKind::Star);
        if (c == '/') return make(TokKind::Slash);
        if (c == '?') return make(TokKind::Question);
        if (c == ':') return make(TokKind::Colon);

        if (c == '&' && Peek(1) == '&') return make(TokKind::AmpAmp, 2);
        if (c == '|' && Peek(1) == '|') return make(TokKind::PipePipe, 2);
        if (c == '=' && Peek(1) == '=') return make(TokKind::EqEq, 2);
        if (c == '!' && Peek(1) == '=') return make(TokKind::BangEq, 2);
        if (c == '!') return make(TokKind::Bang);
        if (c == '<' && Peek(1) == '=') return make(TokKind::LtEq, 2);
        if (c == '>' && Peek(1) == '=') return make(TokKind::GtEq, 2);
        if (c == '<') return make(TokKind::Lt);
        if (c == '>') return make(TokKind::Gt);

        t.kind = TokKind::Error;
        t.text = std::string(1, c);
        ++pos_;
        return t;
    }

private:
    void SkipWhitespace() {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_])))
            ++pos_;
    }

    char Peek(uint32_t ahead) const {
        return (pos_ + ahead < src_.size()) ? src_[pos_ + ahead] : '\0';
    }

    Token LexNumber() {
        Token t; t.offset = pos_; t.kind = TokKind::Number;
        uint32_t start = pos_;
        while (pos_ < src_.size() && (std::isdigit(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '.'))
            ++pos_;
        t.text = std::string(src_.substr(start, pos_ - start));
        t.number = std::strtof(t.text.c_str(), nullptr);
        return t;
    }

    Token LexIdent() {
        Token t; t.offset = pos_; t.kind = TokKind::Ident;
        uint32_t start = pos_;
        while (pos_ < src_.size() &&
               (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_'))
            ++pos_;
        t.text = std::string(src_.substr(start, pos_ - start));
        return t;
    }

    std::string_view src_;
    uint32_t pos_ = 0u;
};

// ─────────────────────────────────────────────────────────────────────────────
// Recursive-descent parser
// ─────────────────────────────────────────────────────────────────────────────

using Node = std::unique_ptr<VfxExprNode>;

Node MakeLit(float v) {
    auto n = std::make_unique<VfxExprNode>();
    n->kind = VfxExprKind::Literal; n->literalValue = v; return n;
}
Node MakeIdent(const std::string& name) {
    auto n = std::make_unique<VfxExprNode>();
    n->kind = VfxExprKind::Identifier; n->name = name; return n;
}
Node MakeUnary(const std::string& op, Node operand) {
    auto n = std::make_unique<VfxExprNode>();
    n->kind = VfxExprKind::Unary; n->name = op; n->left = std::move(operand); return n;
}
Node MakeBinary(const std::string& op, Node lhs, Node rhs) {
    auto n = std::make_unique<VfxExprNode>();
    n->kind = VfxExprKind::Binary; n->name = op;
    n->left = std::move(lhs); n->right = std::move(rhs); return n;
}
Node MakeTernary(Node cond, Node a, Node b) {
    auto n = std::make_unique<VfxExprNode>();
    n->kind = VfxExprKind::Ternary;
    n->left = std::move(cond); n->right = std::move(a); n->third = std::move(b); return n;
}
Node MakeCall(const std::string& name, std::vector<Node> args) {
    auto n = std::make_unique<VfxExprNode>();
    n->kind = VfxExprKind::Call; n->name = name; n->args = std::move(args); return n;
}

class Parser {
public:
    explicit Parser(std::string_view src) : lex_(src) { Advance(); }

    VfxExprParseResult Run() {
        VfxExprParseResult res;
        res.root = ParseExpr();
        if (!res.root) { res.errorMessage = error_; res.errorOffset = errorOff_; return res; }
        if (cur_.kind != TokKind::End) {
            res.root = nullptr;
            res.errorMessage = "unexpected token '" + cur_.text + "'";
            res.errorOffset  = cur_.offset;
        }
        return res;
    }

private:
    void Advance() { cur_ = lex_.Next(); }

    bool Match(TokKind k) { if (cur_.kind == k) { Advance(); return true; } return false; }

    void Error(const std::string& msg) {
        if (error_.empty()) { error_ = msg; errorOff_ = cur_.offset; }
    }

    Node ParseExpr() { return ParseTernary(); }

    Node ParseTernary() {
        auto cond = ParseLogic();
        if (!cond) return nullptr;
        if (!Match(TokKind::Question)) return cond;
        auto a = ParseExpr();
        if (!a) return nullptr;
        if (!Match(TokKind::Colon)) { Error("expected ':'"); return nullptr; }
        auto b = ParseExpr();
        if (!b) return nullptr;
        return MakeTernary(std::move(cond), std::move(a), std::move(b));
    }

    Node ParseLogic() {
        auto node = ParseCompare();
        while (node && (cur_.kind == TokKind::AmpAmp || cur_.kind == TokKind::PipePipe)) {
            std::string op = cur_.text; Advance();
            auto rhs = ParseCompare();
            if (!rhs) return nullptr;
            node = MakeBinary(op, std::move(node), std::move(rhs));
        }
        return node;
    }

    Node ParseCompare() {
        auto node = ParseAddSub();
        if (!node) return nullptr;
        if (cur_.kind == TokKind::EqEq || cur_.kind == TokKind::BangEq ||
            cur_.kind == TokKind::Lt   || cur_.kind == TokKind::Gt ||
            cur_.kind == TokKind::LtEq || cur_.kind == TokKind::GtEq) {
            std::string op = cur_.text; Advance();
            auto rhs = ParseAddSub();
            if (!rhs) return nullptr;
            node = MakeBinary(op, std::move(node), std::move(rhs));
        }
        return node;
    }

    Node ParseAddSub() {
        auto node = ParseMulDiv();
        while (node && (cur_.kind == TokKind::Plus || cur_.kind == TokKind::Minus)) {
            std::string op = cur_.text; Advance();
            auto rhs = ParseMulDiv();
            if (!rhs) return nullptr;
            node = MakeBinary(op, std::move(node), std::move(rhs));
        }
        return node;
    }

    Node ParseMulDiv() {
        auto node = ParseUnary();
        while (node && (cur_.kind == TokKind::Star || cur_.kind == TokKind::Slash)) {
            std::string op = cur_.text; Advance();
            auto rhs = ParseUnary();
            if (!rhs) return nullptr;
            node = MakeBinary(op, std::move(node), std::move(rhs));
        }
        return node;
    }

    Node ParseUnary() {
        if (cur_.kind == TokKind::Minus || cur_.kind == TokKind::Bang) {
            std::string op = cur_.text; Advance();
            auto operand = ParseUnary();
            if (!operand) return nullptr;
            return MakeUnary(op, std::move(operand));
        }
        return ParseCall();
    }

    Node ParseCall() {
        if (cur_.kind == TokKind::Ident) {
            std::string name = cur_.text;
            uint32_t nameOff = cur_.offset;
            Advance();
            if (cur_.kind == TokKind::LParen) {
                Advance(); // consume '('
                std::vector<Node> args;
                if (cur_.kind != TokKind::RParen) {
                    auto a = ParseExpr();
                    if (!a) return nullptr;
                    args.push_back(std::move(a));
                    while (Match(TokKind::Comma)) {
                        auto b = ParseExpr();
                        if (!b) return nullptr;
                        args.push_back(std::move(b));
                    }
                }
                if (!Match(TokKind::RParen)) { Error("expected ')'"); return nullptr; }
                return MakeCall(name, std::move(args));
            }
            // Plain identifier (no parens).
            return MakeIdent(name);
        }
        return ParsePrimary();
    }

    Node ParsePrimary() {
        if (cur_.kind == TokKind::Number) {
            float v = cur_.number; Advance(); return MakeLit(v);
        }
        if (Match(TokKind::LParen)) {
            auto node = ParseExpr();
            if (!node) return nullptr;
            if (!Match(TokKind::RParen)) { Error("expected ')'"); return nullptr; }
            return node;
        }
        Error("expected expression");
        return nullptr;
    }

    Lexer  lex_;
    Token  cur_;
    std::string error_;
    uint32_t    errorOff_ = 0u;
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

VfxExprParseResult VfxParseExpression(std::string_view source) {
    Parser p(source);
    return p.Run();
}

bool VfxExprIsBuiltinFunction(std::string_view name) {
    return GetBuiltins().count(std::string(name)) > 0;
}

int VfxExprBuiltinArgCount(std::string_view name) {
    auto it = GetBuiltins().find(std::string(name));
    return (it != GetBuiltins().end()) ? it->second.argCount : -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// HLSL codegen
// ─────────────────────────────────────────────────────────────────────────────

std::string VfxExprToHLSL(const VfxExprNode* root) {
    if (!root) return {};

    switch (root->kind) {
        case VfxExprKind::Literal: {
            std::ostringstream ss;
            ss << root->literalValue;
            std::string s = ss.str();
            if (s.find('.') == std::string::npos) s += ".0";
            return s;
        }
        case VfxExprKind::Identifier:
            return root->name;

        case VfxExprKind::Unary:
            return "(" + root->name + VfxExprToHLSL(root->left.get()) + ")";

        case VfxExprKind::Binary: {
            const auto& op = root->name;
            // Built-in aliases: add/sub/mul/div map to operator form in
            // the binary node (the parser already produces Binary nodes for
            // infix operators).
            return "(" + VfxExprToHLSL(root->left.get()) + " " + op + " " +
                   VfxExprToHLSL(root->right.get()) + ")";
        }
        case VfxExprKind::Ternary:
            return "((" + VfxExprToHLSL(root->left.get()) + ") ? (" +
                   VfxExprToHLSL(root->right.get()) + ") : (" +
                   VfxExprToHLSL(root->third.get()) + "))";

        case VfxExprKind::Call: {
            // Built-in function-style calls that map to HLSL operators:
            // add(a,b) -> (a + b), sub(a,b) -> (a - b), etc.
            const auto& name = root->name;
            if (name == "add" && root->args.size() == 2)
                return "(" + VfxExprToHLSL(root->args[0].get()) + " + " +
                       VfxExprToHLSL(root->args[1].get()) + ")";
            if (name == "sub" && root->args.size() == 2)
                return "(" + VfxExprToHLSL(root->args[0].get()) + " - " +
                       VfxExprToHLSL(root->args[1].get()) + ")";
            if (name == "mul" && root->args.size() == 2)
                return "(" + VfxExprToHLSL(root->args[0].get()) + " * " +
                       VfxExprToHLSL(root->args[1].get()) + ")";
            if (name == "div" && root->args.size() == 2)
                return "(" + VfxExprToHLSL(root->args[0].get()) + " / " +
                       VfxExprToHLSL(root->args[1].get()) + ")";

            // All other builtins and user calls: name(arg0, arg1, …).
            std::string out = name + "(";
            for (size_t i = 0; i < root->args.size(); ++i) {
                if (i > 0) out += ", ";
                out += VfxExprToHLSL(root->args[i].get());
            }
            out += ")";
            return out;
        }
    }
    return {};
}

} // namespace uz
