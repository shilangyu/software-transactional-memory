// Requested features
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#ifdef __STDC_NO_ATOMICS__
#error Current C11 compiler does not support atomic operations
#endif

#include <atomic>
#include <cassert>
#include <cstring>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <new>
#include <tm.hpp>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common.hpp"
#include "version_lock.hpp"

constexpr std::size_t YIELD_RETRIES = 16;

struct Region : NonCopyable {
  inline Region(const std::size_t align) noexcept : align(align) {
    locks.reserve(512);
    blocks.reserve(512);
  }
  inline ~Region() noexcept {}

  inline auto add_block(const std::size_t size) noexcept -> std::size_t {
    std::lock_guard<std::mutex> guard(alloc_mutex);
    blocks.emplace_back(size);
    locks.emplace_back(size / align);
    return blocks.size() - 1;
  }

  const std::size_t align;
  std::mutex alloc_mutex;
  std::atomic<std::uint64_t> version_clock = 0;
  std::vector<std::vector<VersionLock>> locks;
  std::vector<std::vector<std::uint8_t>> blocks;
};

struct Transaction : NonCopyable {
  inline Transaction(bool is_read_only, std::uint64_t read_version) noexcept
      : is_read_only(is_read_only), read_version(read_version) {}
  inline ~Transaction() noexcept {}

  struct WriteLog {
    std::uint64_t value;
    std::reference_wrapper<VersionLock> lock;
  };

  const bool is_read_only;
  const std::uint64_t read_version;
  std::map<uintptr_t, WriteLog> write_set;
  std::unordered_set<uintptr_t> read_set;
};

namespace virtual_address {
/// A virtual address encoding the block index and word offset.
using VirtualAddress = std::uintptr_t;
constexpr std::size_t OFFSET_SIZE = 48;

inline auto encode(const std::size_t block_index,
                   const std::size_t offset) noexcept -> VirtualAddress {
  return ((block_index + 1) << OFFSET_SIZE) | offset;
}

inline auto decode(const VirtualAddress address) noexcept
    -> std::tuple<std::size_t, std::size_t> {
  return {(address >> OFFSET_SIZE) - 1,
          address & ((static_cast<uint64_t>(1) << OFFSET_SIZE) - 1)};
}
}  // namespace virtual_address

/** Create (i.e. allocate + init) a new shared memory region, with one first
 *non-free-able allocated segment of the requested size and alignment.
 * @param size  Size of the first shared segment of memory to allocate (in
 *bytes), must be a positive multiple of the alignment
 * @param align Alignment (in bytes, must be a power of 2) that the shared
 *memory region must support
 * @return Opaque shared memory region handle, 'invalid_shared' on failure
 **/
shared_t tm_create(size_t size, size_t align) noexcept {
  assert(align <= 8);

  Region* region = new (std::nothrow) Region{align};
  if (region == nullptr) {
    return invalid_shared;
  }

  region->add_block(size);

  return region;
}

/** Destroy (i.e. clean-up + free) a given shared memory region.
 * @param shared Shared memory region to destroy, with no running transaction
 **/
void tm_destroy(shared_t shared) noexcept {
  Region* region = static_cast<Region*>(shared);
  delete region;
}

/** [thread-safe] Return the start address of the first allocated segment in the
 *shared memory region.
 * @param shared Shared memory region to query
 * @return Start address of the first allocated segment
 **/
void* tm_start([[maybe_unused]] shared_t shared) noexcept {
  return reinterpret_cast<void*>(virtual_address::encode(0, 0));
}

/** [thread-safe] Return the size (in bytes) of the first allocated segment of
 *the shared memory region.
 * @param shared Shared memory region to query
 * @return First allocated segment size
 **/
size_t tm_size(shared_t shared) noexcept {
  Region* region = static_cast<Region*>(shared);
  return region->blocks.begin()->size();
}

/** [thread-safe] Return the alignment (in bytes) of the memory accesses on the
 *given shared memory region.
 * @param shared Shared memory region to query
 * @return Alignment used globally
 **/
size_t tm_align(shared_t shared) noexcept {
  return static_cast<Region*>(shared)->align;
}

/** [thread-safe] Begin a new transaction on the given shared memory region.
 * @param shared Shared memory region to start a transaction on
 * @param is_ro  Whether the transaction is read-only
 * @return Opaque transaction ID, 'invalid_tx' on failure
 **/
tx_t tm_begin(shared_t shared, bool is_ro) noexcept {
  Region* region = static_cast<Region*>(shared);

  Transaction* transaction =
      new (std::nothrow) Transaction{is_ro, region->version_clock};
  if (transaction == nullptr) {
    return invalid_tx;
  }

  return reinterpret_cast<uintptr_t>(transaction);
}

/** [thread-safe] End the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to end
 * @return Whether the whole transaction committed
 **/
bool tm_end(shared_t shared, tx_t tx) noexcept {
  Region* region = static_cast<Region*>(shared);
  Transaction* transaction =
      static_cast<Transaction*>(reinterpret_cast<void*>(tx));

  if (transaction->is_read_only || transaction->write_set.empty()) {
    delete transaction;
    return true;
  }

  for (auto it = transaction->write_set.begin();
       it != transaction->write_set.end(); ++it) {
    auto& lock = it->second.lock.get();
    if (!retry_yield<YIELD_RETRIES>([&lock] { return lock.try_lock(); })) {
      while (it != transaction->write_set.begin()) {
        --it;

        it->second.lock.get().unlock();
      }

      delete transaction;
      return false;
    }
  }

  std::uint64_t write_version = region->version_clock.fetch_add(1) + 1;

  if (transaction->read_version + 1 != write_version) {
    for (const auto addr : transaction->read_set) {
      const auto [block_index, block_offset] = virtual_address::decode(addr);
      const std::size_t word_index = block_offset / region->align;

      std::int64_t read_version =
          region->locks[block_index][word_index].read_version();
      if (read_version == -1 || static_cast<std::uint64_t>(read_version) >
                                    transaction->read_version) {
        for (const auto it : transaction->write_set) {
          it.second.lock.get().unlock();
        }

        delete transaction;
        return false;
      }
    }
  }

  for (const auto it : transaction->write_set) {
    const auto [block_index, block_offset] = virtual_address::decode(it.first);

    std::memcpy(region->blocks[block_index].data() + block_offset,
                &it.second.value, region->align);

    it.second.lock.get().unlock_with_version(write_version);
  }

  delete transaction;

  return true;
}

/** [thread-safe] Read operation in the given transaction, source in the shared
 *region and target in a private region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in the shared region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the
 *alignment
 * @param target Target start address (in a private region)
 * @return Whether the whole transaction can continue
 **/
bool tm_read(shared_t shared,
             tx_t tx,
             void const* source,
             size_t size,
             void* target) noexcept {
  Region* region = static_cast<Region*>(shared);
  Transaction* transaction =
      static_cast<Transaction*>(reinterpret_cast<void*>(tx));

  const std::size_t word_count = size / region->align;
  const auto [block_index, block_offset] =
      virtual_address::decode(reinterpret_cast<uintptr_t>(source));
  const std::size_t word_index = block_offset / region->align;
  std::uint8_t* start = region->blocks[block_index].data() + block_offset;
  auto& locks = region->locks[block_index];

  for (size_t i = 0; i < word_count; i++) {
    const uintptr_t addr =
        reinterpret_cast<uintptr_t>(source) + i * region->align;

    if (transaction->is_read_only) {
      const std::int64_t read_version = locks[word_index + i].read_version();

      if (read_version == -1 || static_cast<std::uint64_t>(read_version) >
                                    transaction->read_version) {
        delete transaction;
        return false;
      }

      std::memcpy(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(target) +
                                          i * region->align),
                  start + i * region->align, region->align);
    } else if (auto it = transaction->write_set.find(addr);
               it != transaction->write_set.end()) {
      std::uint64_t value = it->second.value;

      std::memcpy(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(target) +
                                          i * region->align),
                  &value, region->align);
    } else {
      const std::int64_t read_version = locks[word_index + i].read_version();

      if (read_version == -1 || static_cast<std::uint64_t>(read_version) >
                                    transaction->read_version) {
        delete transaction;
        return false;
      }

      std::memcpy(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(target) +
                                          i * region->align),
                  start + i * region->align, region->align);

      if (locks[word_index + i].read_version() != read_version) {
        delete transaction;
        return false;
      }

      transaction->read_set.emplace(addr);
    }
  }

  return true;
}

/** [thread-safe] Write operation in the given transaction, source in a private
 *region and target in the shared region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in a private region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the
 *alignment
 * @param target Target start address (in the shared region)
 * @return Whether the whole transaction can continue
 **/
bool tm_write(shared_t shared,
              tx_t tx,
              void const* source,
              size_t size,
              void* target) noexcept {
  Region* region = static_cast<Region*>(shared);
  Transaction* transaction =
      static_cast<Transaction*>(reinterpret_cast<void*>(tx));

  const std::size_t word_count = size / region->align;
  const std::size_t virtual_address_start = reinterpret_cast<uintptr_t>(target);
  const auto [block_index, block_offset] =
      virtual_address::decode(virtual_address_start);
  const std::size_t word_index = block_offset / region->align;
  auto& locks = region->locks[block_index];

  for (size_t i = 0; i < word_count; i++) {
    std::uint64_t value = 0;
    std::memcpy(&value,
                reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(source) +
                                        i * region->align),
                region->align);

    const std::size_t addr = virtual_address_start + i * region->align;
    auto& lock = locks[word_index + i];

    transaction->write_set.insert_or_assign(addr,
                                            Transaction::WriteLog{value, lock});
  }

  return true;
}

/** [thread-safe] Memory allocation in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param size   Allocation requested size (in bytes), must be a positive
 *multiple of the alignment
 * @param target Pointer in private memory receiving the address of the first
 *byte of the newly allocated, aligned segment
 * @return Whether the whole transaction can continue (success/nomem), or not
 *(abort_alloc)
 **/
Alloc tm_alloc(shared_t shared, tx_t tx, size_t size, void** target) noexcept {
  Region* region = static_cast<Region*>(shared);
  [[maybe_unused]] Transaction* transaction =
      static_cast<Transaction*>(reinterpret_cast<void*>(tx));

  std::size_t block_index = region->add_block(size);
  *target = reinterpret_cast<void*>(virtual_address::encode(block_index, 0));

  return Alloc::success;
}

/** [thread-safe] Memory freeing in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param target Address of the first byte of the previously allocated segment
 *to deallocate
 * @return Whether the whole transaction can continue
 **/
bool tm_free([[maybe_unused]] shared_t shared,
             [[maybe_unused]] tx_t tx,
             [[maybe_unused]] void* target) noexcept {
  // freeing delayed till tm_destroy
  return true;
}
