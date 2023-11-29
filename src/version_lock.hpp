#pragma once

#include <atomic>

struct VersionLock {
 private:
  std::atomic<std::uint64_t> lock = 0;

 public:
  inline auto try_lock() -> bool {
    std::uint64_t value = lock;

    if (value & 0b1) {
      return false;
    }

    return lock.compare_exchange_strong(value, value | 0b1);
  }

  /// UB if was not actually locked
  inline auto unlock() -> void { lock.fetch_sub(1); }

  inline auto unlock_with_version(const std::uint64_t version) -> void {
    lock.store(version << 1);
  }

  /// -1 if locked, otherwise the version
  inline auto read_version() const -> std::int64_t {
    std::uint64_t value = lock;

    if (value & 0b1) {
      return -1;
    }

    return value >> 1;
  }
};
