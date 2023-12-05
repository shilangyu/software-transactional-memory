# Software Transactional Memory

An implementation of software transaction memory (STM) using the [TL2 algorithm](https://link.springer.com/chapter/10.1007/11864219_14). It aims to create atomic, consistent, and isolated access to memory as transactions (similar to database transactions).

While it cannot outperform handcrafted concurrent algorithms, it outperforms basic implementations such as placing a lock over a large portion of code. Additionally, STMs provide a generic interface allowing to integrate it in non-concurrent algorithms in a rather mechanical way.

This repository provides a [C interface](include/tm.h) for a generic word-based STM. See [source code](src/tm.cpp) for information about call invariants.

```cpp
constexpr size_t word_size = 8;
// create a managed region of memory of size 1000 bytes and word size 8 bytes
auto region = tm_create(1024, word_size);
// start of the region memory
auto start = tm_start(region);

// start a transaction
while (true) {
	// all transaction operations return booleans indicating whether a transaction can continue
	bool ok;
	uint64_t value;

	// start transaction
	auto tx = tm_begin(region);

	// read first word in memory region
	ok = tm_read(region, tx, start, word_size, &value);
	if (!ok) continue;

	// write to the second word in memory region
	ok = tm_write(region, tx, &value, word_size, start + word_size);
	if (!ok) continue;

	// finish transaction
	ok = tm_end(region, tx);
	if (ok) break;
}

tm_destroy(region);
```
