#pragma once

/// Deletes copying features of a struct.
struct NonCopyable {
 public:
  NonCopyable(NonCopyable const&) = delete;
  NonCopyable& operator=(NonCopyable const&) = delete;

 protected:
  NonCopyable() = default;
};
