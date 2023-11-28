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
#include <new>
#include <tm.hpp>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "common.hpp"
#include "macros.hpp"
#include "version_lock.hpp"

struct Block : NonCopyable {
  inline Block(const std::size_t size) noexcept { words.resize(size); }
  inline ~Block() noexcept {}

  struct BlockItem {
    VersionLock version_lock{};
    std::uint64_t data = 0;
  };

  std::vector<BlockItem> words{};
};

struct Region : NonCopyable {
  inline Region(const std::size_t size, const std::size_t align) noexcept
      : align(align), initial_block(size) {}
  inline ~Region() noexcept {}

  const std::size_t align;
  std::atomic_size_t version_clock = 0;
  Block initial_block;
};

struct Transaction : NonCopyable {
  inline Transaction(bool is_read_only, std::size_t read_version) noexcept
      : is_read_only(is_read_only), read_version(read_version) {}
  inline ~Transaction() noexcept {}

  const bool is_read_only;
  const std::size_t read_version;
namespace virtual_address {
/// A virtual address encoding the block index and word offset.
using VirtualAddress = std::uintptr_t;
constexpr std::size_t OFFSET_SIZE = 48;

inline auto encode(const std::size_t block_index, const std::size_t offset)
    -> VirtualAddress {
  return (block_index << OFFSET_SIZE) | offset;
}

inline auto decode(const VirtualAddress address)
    -> std::tuple<std::size_t, std::size_t> {
  return {address >> OFFSET_SIZE, address & ((1 << OFFSET_SIZE) - 1)};
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
  Region* region = new (std::nothrow) Region{size, align};
  if (region == nullptr) {
    return invalid_shared;
  }

  // TODO: std::align_val_t(align) for segments

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
void* tm_start(shared_t unused(shared)) noexcept {
  return reinterpret_cast<void*>(virtual_address::encode(0, 0));
}

/** [thread-safe] Return the size (in bytes) of the first allocated segment of
 *the shared memory region.
 * @param shared Shared memory region to query
 * @return First allocated segment size
 **/
size_t tm_size(shared_t unused(shared)) noexcept {
  // TODO: tm_size(shared_t)
  return 0;
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
  // TODO: tm_begin(shared_t)
  return invalid_tx;
}

/** [thread-safe] End the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to end
 * @return Whether the whole transaction committed
 **/
bool tm_end(shared_t unused(shared), tx_t unused(tx)) noexcept {
  // TODO: tm_end(shared_t, tx_t)
  return false;
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
bool tm_read(shared_t unused(shared),
             tx_t unused(tx),
             void const* unused(source),
             size_t unused(size),
             void* unused(target)) noexcept {
  // TODO: tm_read(shared_t, tx_t, void const*, size_t, void*)
  return false;
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
bool tm_write(shared_t unused(shared),
              tx_t unused(tx),
              void const* unused(source),
              size_t unused(size),
              void* unused(target)) noexcept {
  // TODO: tm_write(shared_t, tx_t, void const*, size_t, void*)
  return false;
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
Alloc tm_alloc(shared_t unused(shared),
               tx_t unused(tx),
               size_t unused(size),
               void** unused(target)) noexcept {
  // TODO: tm_alloc(shared_t, tx_t, size_t, void**)
  return Alloc::abort;
}

/** [thread-safe] Memory freeing in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param target Address of the first byte of the previously allocated segment
 *to deallocate
 * @return Whether the whole transaction can continue
 **/
bool tm_free(shared_t unused(shared),
             tx_t unused(tx),
             void* unused(target)) noexcept {
  // TODO: tm_free(shared_t, tx_t, void*)
  return false;
}
