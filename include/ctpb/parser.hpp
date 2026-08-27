#pragma once

#include <stdexcept>
#include <string_view>
#include <vector>

#include <ctpb/ast.hpp>
#include <ctpb/lexer.hpp>

namespace ctpb {

class parse_error : public std::runtime_error {
public:
    constexpr explicit parse_error(const char* msg) : std::runtime_error(msg) {}
};

class parser {
public:
    constexpr explicit parser(std::string_view src) : lex_(src) {}

    constexpr ast::file parse_file() {
        ast::file file;

        // Parse optional syntax: syntax = "proto3";
        if (lex_.peek().kind == tok_kind::kw_syntax) {
            lex_.next();
            expect_sym("=", "syntax declaration");
            token s = expect(tok_kind::str_lit, "syntax version");
            file.syntax = s.text;
            expect_sym(";", "syntax semicolon");
        }

        while (lex_.peek().kind != tok_kind::eof) {
            token t = lex_.peek();
            if (t.kind == tok_kind::kw_package) {
                lex_.next();
                token p = expect(tok_kind::ident, "package name");
                file.package = p.text;
                expect_sym(";", "package semicolon");
            } else if (t.kind == tok_kind::kw_import) {
                lex_.next();
                ast::import_stmt imp;
                if (lex_.peek().kind == tok_kind::kw_public) {
                    imp.is_public = true;
                    lex_.next();
                } else if (lex_.peek().kind == tok_kind::kw_weak) {
                    imp.is_weak = true;
                    lex_.next();
                }
                token p = expect(tok_kind::str_lit, "import path");
                imp.path = p.text;
                expect_sym(";", "import semicolon");
                file.imports.push_back(imp);
            } else if (t.kind == tok_kind::kw_option) {
                file.options.push_back(parse_option());
            } else if (t.kind == tok_kind::kw_message) {
                file.messages.push_back(parse_message());
            } else if (t.kind == tok_kind::kw_enum) {
                file.top_level_enums.push_back(parse_enum());
            } else if (t.kind == tok_kind::sym && t.text == ";") {
                lex_.next(); // stray semicolon
            } else {
                break;
            }
        }

        return file;
    }

    constexpr ast::message parse_message() {
        ast::message msg;
        lex_.next(); // 'message'
        token name_tok = expect(tok_kind::ident, "message name");
        msg.name = name_tok.text;
        expect_sym("{", "message body opening");

        while (lex_.peek().kind != tok_kind::eof) {
            token t = lex_.peek();
            if (t.kind == tok_kind::sym && t.text == "}") {
                lex_.next();
                return msg;
            }
            if (t.kind == tok_kind::kw_message) {
                msg.nested_messages.push_back(parse_message());
            } else if (t.kind == tok_kind::kw_enum) {
                msg.nested_enums.push_back(parse_enum());
            } else if (t.kind == tok_kind::kw_oneof) {
                msg.oneofs.push_back(parse_oneof());
            } else if (t.kind == tok_kind::kw_map) {
                msg.maps.push_back(parse_map_field());
            } else if (t.kind == tok_kind::kw_reserved) {
                parse_reserved(msg);
            } else if (t.kind == tok_kind::kw_option) {
                parse_option(); // skip message option
            } else if (t.kind == tok_kind::sym && t.text == ";") {
                lex_.next();
            } else {
                msg.fields.push_back(parse_field());
            }
        }

        return msg;
    }

    constexpr ast::enum_def parse_enum() {
        ast::enum_def e;
        lex_.next(); // 'enum'
        token name_tok = expect(tok_kind::ident, "enum name");
        e.name = name_tok.text;
        expect_sym("{", "enum body opening");

        while (lex_.peek().kind != tok_kind::eof) {
            token t = lex_.peek();
            if (t.kind == tok_kind::sym && t.text == "}") {
                lex_.next();
                return e;
            }
            if (t.kind == tok_kind::kw_option) {
                parse_option(); // skip enum option
            } else if (t.kind == tok_kind::kw_reserved) {
                // skip reserved in enum
                while (lex_.peek().kind != tok_kind::eof && !(lex_.peek().kind == tok_kind::sym && lex_.peek().text == ";")) {
                    lex_.next();
                }
                if (lex_.peek().kind == tok_kind::sym && lex_.peek().text == ";") lex_.next();
            } else if (t.kind == tok_kind::ident) {
                token val_name = lex_.next();
                expect_sym("=", "enum value assign");
                token num_tok = expect(tok_kind::int_lit, "enum numeric value");
                e.values.push_back(ast::enum_value{val_name.text, static_cast<std::int32_t>(num_tok.int_value)});
                if (lex_.peek().kind == tok_kind::sym && lex_.peek().text == "[") {
                    skip_bracketed_options();
                }
                expect_sym(";", "enum value semicolon");
            } else if (t.kind == tok_kind::sym && t.text == ";") {
                lex_.next();
            } else {
                lex_.next();
            }
        }
        return e;
    }

    constexpr ast::field parse_field() {
        ast::field f;
        token t = lex_.peek();
        if (t.kind == tok_kind::kw_repeated) {
            f.label = ast::field_label::repeated;
            lex_.next();
        } else if (t.kind == tok_kind::kw_optional) {
            f.proto3_optional = true;
            lex_.next();
        } else if (t.kind == tok_kind::kw_required) {
            lex_.next();
        }

        token type_tok = expect(tok_kind::ident, "field type");
        f.type = ast::type_ref{type_tok.text};

        token name_tok = expect(tok_kind::ident, "field name");
        f.name = name_tok.text;

        expect_sym("=", "field assignment");
        token num_tok = expect(tok_kind::int_lit, "field number");
        f.number = num_tok.int_value;

        if (lex_.peek().kind == tok_kind::sym && lex_.peek().text == "[") {
            skip_bracketed_options();
        }

        expect_sym(";", "field semicolon");
        return f;
    }

    constexpr ast::map_field parse_map_field() {
        ast::map_field mf;
        lex_.next(); // 'map'
        expect_sym("<", "map key opening");
        token key_tok = expect(tok_kind::ident, "map key type");
        mf.key_type = ast::type_ref{key_tok.text};
        expect_sym(",", "map comma");
        token val_tok = expect(tok_kind::ident, "map value type");
        mf.value_type = ast::type_ref{val_tok.text};
        expect_sym(">", "map closing");

        token name_tok = expect(tok_kind::ident, "map field name");
        mf.name = name_tok.text;

        expect_sym("=", "map assignment");
        token num_tok = expect(tok_kind::int_lit, "map field number");
        mf.number = num_tok.int_value;

        if (lex_.peek().kind == tok_kind::sym && lex_.peek().text == "[") {
            skip_bracketed_options();
        }

        expect_sym(";", "map semicolon");
        return mf;
    }

    constexpr ast::oneof_def parse_oneof() {
        ast::oneof_def od;
        lex_.next(); // 'oneof'
        token name_tok = expect(tok_kind::ident, "oneof name");
        od.name = name_tok.text;
        expect_sym("{", "oneof opening");

        while (lex_.peek().kind != tok_kind::eof) {
            token t = lex_.peek();
            if (t.kind == tok_kind::sym && t.text == "}") {
                lex_.next();
                return od;
            }
            if (t.kind == tok_kind::kw_option) {
                parse_option();
            } else if (t.kind == tok_kind::ident) {
                token type_tok = lex_.next();
                token f_name = expect(tok_kind::ident, "oneof branch name");
                expect_sym("=", "oneof branch assign");
                token num_tok = expect(tok_kind::int_lit, "oneof branch number");
                ast::oneof_branch branch;
                branch.type = ast::type_ref{type_tok.text};
                branch.name = f_name.text;
                branch.number = num_tok.int_value;
                if (lex_.peek().kind == tok_kind::sym && lex_.peek().text == "[") {
                    skip_bracketed_options();
                }
                expect_sym(";", "oneof branch semicolon");
                od.branches.push_back(branch);
            } else {
                lex_.next();
            }
        }
        return od;
    }

    constexpr void parse_reserved(ast::message& msg) {
        lex_.next(); // 'reserved'
        while (lex_.peek().kind != tok_kind::eof) {
            token t = lex_.peek();
            if (t.kind == tok_kind::sym && t.text == ";") {
                lex_.next();
                break;
            }
            if (t.kind == tok_kind::str_lit) {
                msg.reserved_field_names.push_back(t.text);
                lex_.next();
            } else if (t.kind == tok_kind::int_lit) {
                std::int64_t start = t.int_value;
                lex_.next();
                if (lex_.peek().kind == tok_kind::kw_to) {
                    lex_.next();
                    if (lex_.peek().kind == tok_kind::kw_max) {
                        lex_.next();
                        msg.reserved_field_numbers.push_back(ast::reserved_range{start, 536870911});
                    } else if (lex_.peek().kind == tok_kind::int_lit) {
                        std::int64_t end = lex_.peek().int_value;
                        lex_.next();
                        msg.reserved_field_numbers.push_back(ast::reserved_range{start, end});
                    }
                } else {
                    msg.reserved_field_numbers.push_back(ast::reserved_range{start, std::nullopt});
                }
            } else if (t.kind == tok_kind::sym && t.text == ",") {
                lex_.next();
            } else {
                lex_.next();
            }
        }
    }

    constexpr ast::option_stmt parse_option() {
        ast::option_stmt opt;
        lex_.next(); // 'option'
        token name_tok = expect(tok_kind::ident, "option name");
        opt.name = name_tok.text;
        expect_sym("=", "option assign");
        token val_tok = lex_.next();
        opt.value = val_tok.text;
        expect_sym(";", "option semicolon");
        return opt;
    }

    constexpr void skip_bracketed_options() {
        expect_sym("[", "field options start");
        while (lex_.peek().kind != tok_kind::eof) {
            token t = lex_.peek();
            if (t.kind == tok_kind::sym && t.text == "]") {
                lex_.next();
                break;
            }
            lex_.next();
        }
    }

    constexpr token expect(tok_kind k, std::string_view ctx) {
        return lex_.expect(k, ctx);
    }

    constexpr void expect_sym(std::string_view s, std::string_view ctx) {
        if (!lex_.match_sym(s)) {
            // Non-fatal
        }
    }

private:
    lexer lex_;
};

} // namespace ctpb
