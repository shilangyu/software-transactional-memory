#pragma once

#include <atomic>

struct VersionLock {
 private:
  std::atomic<std::uint64_t> lock = 0;

 public:
  auto try_lock() -> bool;

  /// UB if was not actually locked
  auto unlock() -> void;

  /// -1 if locked, otherwise the version
  auto read_version() -> std::int64_t;
};
