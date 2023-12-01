#pragma once

#include <thread>

/// Deletes copying features of a struct.
struct NonCopyable {
 public:
  NonCopyable(NonCopyable const&) = delete;
  NonCopyable& operator=(NonCopyable const&) = delete;

 protected:
  NonCopyable() = default;
};

/// Recursive auxiliary call of retry_yield
template <auto Start, auto End, class F>
constexpr inline bool _retry_yield(F&& f) {
  if constexpr (Start < End) {
    if constexpr (Start != 0) {
      std::this_thread::yield();
    }
    if (f()) {
      return true;
    }
    return _retry_yield<Start + 1, End>(f);
  } else {
    return false;
  }
}

/// Executes `f` `Amount` of times until `f` returns true`. Will yield the
/// thread between each failed call of `f` (ie. one that returned false).
template <std::size_t Amount, class F>
constexpr inline bool retry_yield(F&& f) {
  return _retry_yield<0, Amount>(f);
}
