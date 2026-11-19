// Internal: 128-bit helpers for intermediates such as quantity * weight (master §9).
// __extension__ keeps -Wpedantic quiet about the GCC/Clang built-in type.
#pragma once
namespace e1 {
__extension__ typedef __int128 i128;
__extension__ typedef unsigned __int128 u128;
}  // namespace e1
