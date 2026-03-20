# Parallel Inclusive Prefix Sum in C++

A C++ implementation of a **parallel inclusive prefix sum** using a **binary tree in heap layout** and the standard library **`std::async`** interface for recursive task parallelism.

This project computes running totals over a large input array by performing an **up sweep** to build subtree sums and a **down sweep** to propagate carries to the leaves. It is designed as a clean demonstration of parallel tree based scan, correctness checking, and basic benchmarking.

## What this program does

Given an input array such as

```text
10 1 1 1 1
```

it computes the inclusive prefix sums

```text
10 11 12 13 14
```

Each output position contains the sum of all values from the beginning of the array through that index.

In the current `main` function, the program creates a large array of size `1 << 26`, fills it with `1`s, changes the first value to `10`, computes the prefix sums, verifies correctness, and prints the runtime.

## Example behavior

### Input pattern

```text
10, 1, 1, 1, 1, 1, ...
```

### Expected prefix sums

```text
10, 11, 12, 13, 14, 15, ...
```

### Example output

```text
PASS in 123.45ms
```

## How it works

The algorithm uses a complete binary tree stored in heap layout.

### 1. Up sweep

The program recursively combines values from the leaves upward and stores subtree sums in the interior nodes.

### 2. Down sweep

The program recursively passes a running carry downward through the tree so each leaf can compute its final inclusive prefix sum.

### 3. Parallel recursion

Near the top of the tree, the recursion spawns asynchronous tasks with `std::async`. Below a fixed depth, it switches to sequential recursion to avoid too much task overhead.

### 4. Optional non power of two support

The code can also handle input sizes that are not powers of two by logically padding the leaf layer with zeros up to the next power of two.

## Project structure

- `Prefix_Sum.cpp` contains the full implementation
- `Heaper` manages the logical tree layout and access to interior and leaf values
- `SumHeap` performs the up sweep and down sweep
- `main()` builds the test case, runs the algorithm, checks correctness, and prints timing information

## Build and run

### Compile

```bash
g++ -std=c++17 -O2 -pthread "Prefix_Sum.cpp" -o prefix_sum
```

### Run

```bash
./prefix_sum
```

### Compile and run using your full path

Replace the example path below with the path to your own project folder. Keep the quotes if your folder name contains spaces.

```bash
g++ -std=c++17 -O2 -pthread "/path/to/your/project/Prefix_Sum.cpp" -o "/path/to/your/project/prefix_sum"
"/path/to/your/project/prefix_sum"
```

## Complexity

Let `n` be the number of input elements.

### Work

The total work is **O(n)** because every node in the logical tree is processed a constant number of times.

### Span

The span is **O(log n)** because both the up sweep and down sweep follow the height of the tree.

### Space

The extra space is **O(n)** for the interior tree storage and output array.

## Why this project is useful

This project demonstrates several important parallel programming ideas.

- Tree based parallel decomposition
- Prefix sum as a fundamental parallel primitive
- Task parallelism with `std::async`
- Heap style tree storage without pointer based nodes
- Correctness verification and timing
- Support for non power of two inputs through logical zero padding

## Notes

- The implementation uses `long long` to reduce the chance of overflow for large prefix sums
- The current code sets `PAR_LEVELS = 4`, which gives parallel work near the top of the recursion tree
- The current test size is `1 << 26`, which is large enough to make timing meaningful

## Possible extensions

- Compare against a sequential prefix sum baseline
- Tune the parallel depth for different machines
- Replace `std::async` with a thread pool
- Add command line arguments for input size and initialization pattern
- Benchmark different data distributions and array sizes

## License

No license has been added yet.
