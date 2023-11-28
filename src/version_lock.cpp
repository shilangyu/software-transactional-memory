#include "version_lock.hpp"

auto VersionLock::try_lock() -> bool {
  std::uint64_t value = lock;

  if (value & 0b1) {
    return false;
  }

  return lock.compare_exchange_strong(value, value | 0b1);
}

auto VersionLock::read_version() -> std::int64_t {
  std::uint64_t value = lock;

  if (value & 0b1) {
    return -1;
  }

  return value >> 1;
}

auto VersionLock::unlock() -> void {
  lock.fetch_sub(1);
}
