#include <cassert>
#include <iostream>
#include <tm.hpp>

constexpr std::size_t alignment = 8;

void empty_tx() {
  auto reg = tm_create(alignment, alignment);
  assert(reg != invalid_shared);
  auto tx = tm_begin(reg, false);
  assert(tx != invalid_tx);

  assert(tm_end(reg, tx));
}

void one_read_tx() {
  auto reg = tm_create(alignment, alignment);
  assert(reg != invalid_shared);
  auto tx = tm_begin(reg, false);
  assert(tx != invalid_tx);

  auto start = tm_start(reg);
  std::uint64_t value = 1;

  assert(tm_read(reg, tx, start, alignment, &value));
  assert(value == 0);

  assert(tm_end(reg, tx));
}

void one_write_tx() {
  auto reg = tm_create(alignment, alignment);
  assert(reg != invalid_shared);
  auto tx = tm_begin(reg, false);
  assert(tx != invalid_tx);

  auto start = tm_start(reg);
  std::uint64_t value = 1;

  assert(tm_write(reg, tx, &value, alignment, start));

  assert(tm_end(reg, tx));
}

void read_after_write_tx() {
  auto reg = tm_create(alignment, alignment);
  assert(reg != invalid_shared);
  auto tx = tm_begin(reg, false);
  assert(tx != invalid_tx);

  auto start = tm_start(reg);

  {
    std::uint64_t value = 1;
    assert(tm_write(reg, tx, &value, alignment, start));
  }

  std::uint64_t value = 0;
  assert(tm_read(reg, tx, start, alignment, &value));
  assert(value == 1);

  assert(tm_end(reg, tx));
}

void write_same_address_twice_tx() {
  auto reg = tm_create(alignment, alignment);
  assert(reg != invalid_shared);
  auto tx = tm_begin(reg, false);
  assert(tx != invalid_tx);

  auto start = tm_start(reg);

  {
    std::uint64_t value = 1;
    assert(tm_write(reg, tx, &value, alignment, start));
  }

  {
    std::uint64_t value = 2;
    assert(tm_write(reg, tx, &value, alignment, start));
  }

  std::uint64_t value = 0;
  assert(tm_read(reg, tx, start, alignment, &value));
  assert(value == 2);

  assert(tm_end(reg, tx));
}

void write_after_read_tx() {
  auto reg = tm_create(alignment, alignment);
  assert(reg != invalid_shared);
  auto tx = tm_begin(reg, false);
  assert(tx != invalid_tx);

  auto start = tm_start(reg);

  {
    std::uint64_t value = 1;
    assert(tm_read(reg, tx, start, alignment, &value));
    assert(value == 0);
  }

  std::uint64_t value = 1;
  assert(tm_write(reg, tx, &value, alignment, start));

  assert(tm_end(reg, tx));
}

int main() {
  empty_tx();
  one_read_tx();
  one_write_tx();
  read_after_write_tx();
  write_same_address_twice_tx();
  write_after_read_tx();

  return 0;
}
