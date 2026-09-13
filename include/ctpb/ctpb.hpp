#pragma once

// ctpb: Compile-time Protocol Buffers via C++26 Static Reflection & #embed
//
// Umbrella header providing:
//   - ctpb::ast
//   - ctpb::lexer
//   - ctpb::parser
//   - ctpb::wire
//   - ctpb::embed
//   - ctpb::reflection
//   - ctpb::parse, ctpb::encode, ctpb::decode

#include <ctpb/ast.hpp>
#include <ctpb/lexer.hpp>
#include <ctpb/parser.hpp>
#include <ctpb/wire.hpp>
#include <ctpb/embed.hpp>
#include <ctpb/reflection.hpp>
#include <ctpb/view.hpp>

namespace ctpb {

inline constexpr int VERSION_MAJOR = 0;
inline constexpr int VERSION_MINOR = 1;
inline constexpr int VERSION_PATCH = 0;

} // namespace ctpb
