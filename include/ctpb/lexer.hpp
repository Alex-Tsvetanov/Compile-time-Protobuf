#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

namespace ctpb {

enum class tok_kind {
    eof,
    ident,
    str_lit,
    int_lit,
    float_lit,
    sym,
    kw_syntax,
    kw_package,
    kw_import,
    kw_public,
    kw_weak,
    kw_option,
    kw_message,
    kw_enum,
    kw_repeated,
    kw_optional,
    kw_required,
    kw_map,
    kw_oneof,
    kw_reserved,
    kw_to,
    kw_max,
    kw_extensions,
    kw_extend,
    kw_service,
    kw_rpc,
    kw_returns,
    kw_true,
    kw_false
};

struct source_location {
    int line = 1;
    int column = 1;
};

struct token {
    tok_kind kind = tok_kind::eof;
    std::string_view text;
    std::int64_t int_value = 0;
    source_location loc{};
};

class lexer {
public:
    constexpr explicit lexer(std::string_view src) noexcept
        : src_(src), pos_(0), line_(1), col_(1) {
        next();
    }

    constexpr const token& peek() const noexcept { return current_; }

    constexpr token next() {
        token prev = current_;
        current_ = scan_token();
        return prev;
    }

    constexpr token expect(tok_kind k, std::string_view ctx = "") {
        if (current_.kind != k) {
            // In constexpr context, error reporting is done via compile failure or diagnostic
            return token{tok_kind::eof, "", 0, current_.loc};
        }
        return next();
    }

    constexpr bool match_sym(std::string_view s) {
        if (current_.kind == tok_kind::sym && current_.text == s) {
            next();
            return true;
        }
        return false;
    }

private:
    constexpr void skip_whitespace_and_comments() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r') {
                advance();
            } else if (c == '\n') {
                pos_++;
                line_++;
                col_ = 1;
            } else if (c == '/' && pos_ + 1 < src_.size()) {
                if (src_[pos_ + 1] == '/') {
                    // Line comment
                    pos_ += 2;
                    col_ += 2;
                    while (pos_ < src_.size() && src_[pos_] != '\n') {
                        pos_++;
                    }
                } else if (src_[pos_ + 1] == '*') {
                    // Block comment
                    pos_ += 2;
                    col_ += 2;
                    while (pos_ + 1 < src_.size() && !(src_[pos_] == '*' && src_[pos_ + 1] == '/')) {
                        if (src_[pos_] == '\n') {
                            line_++;
                            col_ = 1;
                        } else {
                            col_++;
                        }
                        pos_++;
                    }
                    if (pos_ + 1 < src_.size()) {
                        pos_ += 2;
                        col_ += 2;
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }
    }

    constexpr char advance() {
        char c = src_[pos_++];
        col_++;
        return c;
    }

    constexpr token scan_token() {
        skip_whitespace_and_comments();
        if (pos_ >= src_.size()) {
            return token{tok_kind::eof, "", 0, {line_, col_}};
        }

        source_location loc{line_, col_};
        char c = src_[pos_];

        // Identifier or keyword
        if (is_alpha(c) || c == '_') {
            std::size_t start = pos_;
            while (pos_ < src_.size() && (is_alnum(src_[pos_]) || src_[pos_] == '_' || src_[pos_] == '.')) {
                advance();
            }
            std::string_view text = src_.substr(start, pos_ - start);
            return token{classify_ident(text), text, 0, loc};
        }

        // Numbers (decimal, hex, float)
        if (is_digit(c) || (c == '-' && pos_ + 1 < src_.size() && is_digit(src_[pos_ + 1]))) {
            std::size_t start = pos_;
            bool is_hex = false;
            bool is_float = false;

            if (c == '-') advance();

            if (src_[pos_] == '0' && pos_ + 1 < src_.size() && (src_[pos_ + 1] == 'x' || src_[pos_ + 1] == 'X')) {
                is_hex = true;
                advance(); advance();
                while (pos_ < src_.size() && is_hex_digit(src_[pos_])) {
                    advance();
                }
            } else {
                while (pos_ < src_.size() && is_digit(src_[pos_])) {
                    advance();
                }
                if (pos_ < src_.size() && src_[pos_] == '.') {
                    is_float = true;
                    advance();
                    while (pos_ < src_.size() && is_digit(src_[pos_])) {
                        advance();
                    }
                }
            }

            std::string_view text = src_.substr(start, pos_ - start);
            if (is_float) {
                return token{tok_kind::float_lit, text, 0, loc};
            }
            std::int64_t val = parse_int_literal(text, is_hex);
            return token{tok_kind::int_lit, text, val, loc};
        }

        // String literal
        if (c == '"' || c == '\'') {
            char quote = advance();
            std::size_t start = pos_;
            while (pos_ < src_.size() && src_[pos_] != quote) {
                if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
                    advance();
                }
                advance();
            }
            std::string_view text = src_.substr(start, pos_ - start);
            if (pos_ < src_.size() && src_[pos_] == quote) {
                advance();
            }
            return token{tok_kind::str_lit, text, 0, loc};
        }

        // Symbols
        advance();
        return token{tok_kind::sym, src_.substr(pos_ - 1, 1), 0, loc};
    }

    static constexpr bool is_alpha(char c) noexcept {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    static constexpr bool is_digit(char c) noexcept {
        return c >= '0' && c <= '9';
    }

    static constexpr bool is_alnum(char c) noexcept {
        return is_alpha(c) || is_digit(c);
    }

    static constexpr bool is_hex_digit(char c) noexcept {
        return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    static constexpr std::int64_t parse_int_literal(std::string_view s, bool is_hex) noexcept {
        std::int64_t val = 0;
        bool neg = false;
        std::size_t i = 0;
        if (s.empty()) return 0;
        if (s[0] == '-') { neg = true; i = 1; }
        if (is_hex && i + 2 <= s.size()) {
            i += 2; // skip 0x
            for (; i < s.size(); ++i) {
                char c = s[i];
                val *= 16;
                if (c >= '0' && c <= '9') val += (c - '0');
                else if (c >= 'a' && c <= 'f') val += (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') val += (c - 'A' + 10);
            }
        } else {
            for (; i < s.size(); ++i) {
                val = val * 10 + (s[i] - '0');
            }
        }
        return neg ? -val : val;
    }

    static constexpr tok_kind classify_ident(std::string_view s) noexcept {
        if (s == "syntax") return tok_kind::kw_syntax;
        if (s == "package") return tok_kind::kw_package;
        if (s == "import") return tok_kind::kw_import;
        if (s == "public") return tok_kind::kw_public;
        if (s == "weak") return tok_kind::kw_weak;
        if (s == "option") return tok_kind::kw_option;
        if (s == "message") return tok_kind::kw_message;
        if (s == "enum") return tok_kind::kw_enum;
        if (s == "repeated") return tok_kind::kw_repeated;
        if (s == "optional") return tok_kind::kw_optional;
        if (s == "required") return tok_kind::kw_required;
        if (s == "map") return tok_kind::kw_map;
        if (s == "oneof") return tok_kind::kw_oneof;
        if (s == "reserved") return tok_kind::kw_reserved;
        if (s == "to") return tok_kind::kw_to;
        if (s == "max") return tok_kind::kw_max;
        if (s == "extensions") return tok_kind::kw_extensions;
        if (s == "extend") return tok_kind::kw_extend;
        if (s == "service") return tok_kind::kw_service;
        if (s == "rpc") return tok_kind::kw_rpc;
        if (s == "returns") return tok_kind::kw_returns;
        if (s == "true") return tok_kind::kw_true;
        if (s == "false") return tok_kind::kw_false;
        return tok_kind::ident;
    }

    std::string_view src_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    token current_{};
};

} // namespace ctpb
