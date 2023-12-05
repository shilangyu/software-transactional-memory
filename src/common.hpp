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

/// Executes `f` `Amount` of times until `f` returns true. Will yield the
/// thread between each failed call of `f` (ie. one that returned false).
template <std::size_t Amount, class F>
constexpr inline bool retry_yield(F&& f) {
  if constexpr (Amount == 0) {
    return false;
  } else {
    if (f()) {
      return true;
    }

    if constexpr (Amount != 1) {
      std::this_thread::yield();
    }

    return retry_yield<Amount - 1>(f);
  }
}
