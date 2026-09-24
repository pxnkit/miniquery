# MiniQuery

A basic C++17 analytical query engine: load a small CSV into separate customer and amount columns, filter rows, aggregate partitions concurrently, and merge deterministic results.

**Status: working v0.1 learning prototype.** This is a deliberately narrow SQL implementation, not a general database or a Snowflake implementation.

## Run

Requires CMake 3.16+, a C++17 compiler, and Python 3 for tests. On Linux/macOS:

```sh
cmake -S . -B build
cmake --build build --config Release
./build/miniquery data/orders.csv "SELECT customer, SUM(amount_cents) FROM orders WHERE amount_cents >= 1000 GROUP BY customer" --threads 4 --explain
ctest --test-dir build -C Release --output-on-failure
```

On Windows with Visual Studio, the executable is `build/Release/miniquery.exe`.

Expected standard output:

```text
customer,total_cents
alice,4000
bob,1600
```

`--explain` writes the operator sequence, actual worker count, row/group counts and measured load/execution times to stderr. No speedup is claimed.

## Supported SQL and data

```sql
SELECT customer, SUM(amount_cents) FROM orders
[WHERE amount_cents >= INTEGER] GROUP BY customer;
```

Keywords are case-insensitive; whitespace is flexible. Only this statement shape is supported. Input has exactly the header `customer,amount_cents`; customer IDs contain ASCII letters, digits, underscores or hyphens. Amounts are signed 64-bit integer cents. Quoted CSV fields, NULLs, joins, arbitrary expressions and ORDER BY are not supported. Results are sorted by customer for repeatability. An overflowing intermediate sum is rejected, even if a different arithmetic order could cancel it later.

## Design and checks

CSV -> in-memory columns -> optional filter -> up to 64 partition workers -> per-worker ordered maps -> merged totals. The thread count is explicit; no task is created per row. The whole input and all groups must fit in RAM.

The integration test compares 10,003 seeded rows against SQLite at 1, 2, 4 and 7 workers, and covers empty input, invalid SQL/options/data and integer overflow. CI builds and runs the test on Linux.

## Next iteration

- Batch-oriented operators and a proper parser with an explicit logical plan.
- One equi-join and additional SQL semantics with differential tests.
- Memory accounting and external aggregation with spill files.
- Reproducible performance comparisons and peak-memory measurements.

Built with AI assistance; behavior is verified through the included executable tests. No production-scale or hiring-outcome claims.
