/**
 * This program computes an inclusive prefix sum in parallel using a binary tree stored in
 * heap layout and the C plus plus thread library through std::async.
 *
 * It works in two passes over the tree. First it goes upward and computes the sum of each
 * subtree, storing those values in the interior nodes. Then it goes back down the tree carrying
 * running totals so each leaf can write its final inclusive prefix sum.
 *
 * The recursion only creates asynchronous tasks near the top of the tree. After that it
 * switches to sequential recursion, which helps keep the overhead low while still giving
 * useful parallel speedup.
 *
 * This code can also handle input sizes that are not powers of two by treating the input
 * as if it were padded with zeros up to the next power of two. Only the real input values
 * are stored, and any extra padded leaves are read as zero.
 *
 * I used long long for the values so large prefix sums are less likely to overflow, and I
 * stored the interior tree in a vector instead of managing raw memory directly.
 */ 

#include <iostream>
#include <vector>
#include <future>
#include <chrono>
#include <stdexcept>

using namespace std;

using Value = long long;
using Data = vector<Value>;

static constexpr int RELAX_POWER_OF_TWO = 0;

// This controls how many levels near the top of the tree are allowed to spawn asynchronous work.
static constexpr int PAR_LEVELS = 4;

// This checks whether x is a nonzero power of two.
static inline bool isPowerOfTwo(size_t x) {
    return x && ((x & (x - 1)) == 0);
}

// This returns the next power of two greater than or equal to x.
static inline size_t nextPow2(size_t x) {
    size_t p = 1;
    while (p < x) p <<= 1;
    return p;
}

/**
 * The Heaper class stores the logical binary tree using heap style indexing. The interior
 * nodes are kept in their own vector, while the leaf values come directly from the input array.
 *
 * Since the tree is treated as a complete binary tree, the node indices split naturally
 * into an interior region and a leaf region. When a value is read, the code checks which
 * region the index belongs to and then reads from the appropriate place.
 *
 * If the logical tree includes padded leaves past the end of the original input, those
 * leaves are treated as zeros.
 */
class Heaper {
public:
    /**
     * This constructor takes a pointer to the input data.
     * When RELAX_POWER_OF_TWO is zero, the input size must already be a power of two.
     * When RELAX_POWER_OF_TWO is one, the code allows any positive size and rounds it up
     * to the next power of two for the logical tree.
     */
    explicit Heaper(const Data* data)
        : nOriginal_(data ? data->size() : 0), nUsed_(0), leafBase_(0), data_(data), interior_()
    {
        if (!data_ || nOriginal_ == 0) {
            throw invalid_argument("Heaper requires a non-null data pointer with positive size.");
        }

        if (RELAX_POWER_OF_TWO) {
            nUsed_ = nextPow2(nOriginal_);
        } else {
            if (!isPowerOfTwo(nOriginal_)) {
                throw invalid_argument("Data size must be a power of 2 (or enable RELAX_POWER_OF_TWO).");
            }
            nUsed_ = nOriginal_;
        }

        leafBase_ = static_cast<int>(nUsed_ - 1);

        // This allocates space for the interior nodes of the logical tree.
        interior_.assign(nUsed_ - 1, 0);
    }

    virtual ~Heaper() = default;

    Heaper(const Heaper&) = delete;
    Heaper& operator=(const Heaper&) = delete;
    
    // This returns the total number of nodes in the logical complete tree.
    int nodeCount() const {
        return static_cast<int>(2 * nUsed_ - 1);
    }
    
    // This returns the number of leaves in the logical tree after any padding.
    int leafCountUsed() const {
        return static_cast<int>(nUsed_);
    }
    
    // This returns the number of original input elements.
    int leafCountOriginal() const {
        return static_cast<int>(nOriginal_);
    }
    
    // These helper functions return the left child, right child, and parent indices in heap layout.
    static inline int left(int i) { return 2 * i + 1; }
    static inline int right(int i) { return 2 * i + 2; }
    static inline int parent(int i) { return (i - 1) / 2; }
    
    // This checks whether node i is a leaf in the logical tree.
    inline bool isLeaf(int i) const { return i >= leafBase_; }

    /**
     * This returns the value stored at node i. Interior nodes come from the interior vector,
     * and leaf nodes come from the original input data. Any padded leaves beyond the real
     * input are treated as zero.
     */
    virtual inline Value value(int i) const {
        if (i < leafBase_) {
            return interior_[static_cast<size_t>(i)];
        }

        const int leafIdx = i - leafBase_;
        if (leafIdx < 0) return 0;

        if (static_cast<size_t>(leafIdx) < nOriginal_) {
            return (*data_)[static_cast<size_t>(leafIdx)];
        }
        // Any padded leaf beyond the original input is read as zero.
        return 0;
    }

protected:

	// This writes a value into an interior node.
    inline void setInterior(int i, Value v) {
        interior_[static_cast<size_t>(i)] = v;
    }
    
    // After the up sweep finishes, this returns the sum of the subtree rooted at node i.
    inline Value subtreeSum(int i) const {
        return value(i);
    }

    size_t nOriginal_;
    size_t nUsed_;
    int leafBase_;
    const Data* data_;
    Data interior_;
};

/**
 * SumHeap extends Heaper by first computing subtree sums during construction and then using
 * a second traversal to compute the inclusive prefix sums.
 */
class SumHeap : public Heaper {
public:

    // This builds the heap structure and immediately runs the up sweep to fill in the subtree sums.
    explicit SumHeap(const Data* data) : Heaper(data) {
        calcSum(0, 0);
    }
    
    // This writes the inclusive prefix sums into the output array.
    void prefixSums(Data* out) {
        if (!out) {
            throw invalid_argument("prefixSums requires a non-null output pointer.");
        }
        if (out->size() < nOriginal_) {
            throw invalid_argument("Output array is too small for prefix sums.");
        }

        // The carry value represents the sum of all elements strictly to the left of the
        // current node segment.
        calcPrefix(0, 0, out, 0);
    }

private:
    /**
     * This is the up sweep. It recursively computes the sum stored under each interior node.
     * Near the top of the tree it may launch asynchronous work, and farther down it
     * continues sequentially.
     */
    void calcSum(int i, int level) {
        if (isLeaf(i)) return;

        const int li = left(i);
        const int ri = right(i);

        if (level < PAR_LEVELS) {
            future<void> handle;
            bool spawned = false;

            if (!isLeaf(li)) {
                handle = async(launch::async, &SumHeap::calcSum, this, li, level + 1);
                spawned = true;
            }

            if (!isLeaf(ri)) {
                calcSum(ri, level + 1);
            }

            if (spawned) {
                handle.get();
            }
        } else {
            if (!isLeaf(li)) calcSum(li, level + 1);
            if (!isLeaf(ri)) calcSum(ri, level + 1);
        }

        setInterior(i, subtreeSum(li) + subtreeSum(ri));
    }

    // This is the down sweep. It carries the running total for everything to the left of
    // the current segment and uses that to write the final inclusive prefix sum at each real leaf.
    void calcPrefix(int i, Value carry, Data* out, int level) {
        if (isLeaf(i)) {
            const int leafIdx = i - leafBase_;
            if (leafIdx >= 0 && static_cast<size_t>(leafIdx) < nOriginal_) {
                (*out)[static_cast<size_t>(leafIdx)] = carry + value(i);
            }
            return;
        }

        const int li = left(i);
        const int ri = right(i);

        const Value leftCarry  = carry;
        const Value rightCarry = carry + subtreeSum(li);

        if (level < PAR_LEVELS) {
            future<void> handle;
            bool spawned = false;

            if (!isLeaf(li)) {
                handle = async(
                    launch::async,
                    &SumHeap::calcPrefix,
                    this,
                    li,
                    leftCarry,
                    out,
                    level + 1
                );
                spawned = true;
            } else {
                calcPrefix(li, leftCarry, out, level + 1);
            }

            calcPrefix(ri, rightCarry, out, level + 1);

            if (spawned) {
                handle.get();
            }
        } else {
            calcPrefix(li, leftCarry, out, level + 1);
            calcPrefix(ri, rightCarry, out, level + 1);
        }
    }
};

const int N = 1 << 26;

int main() {
    Data data(N, 1);
    data[0] = 10;
    Data prefix(N, 1);

    auto start = chrono::steady_clock::now();

    SumHeap heap(&data);
    heap.prefixSums(&prefix);

    auto end = chrono::steady_clock::now();
    auto elapsed = chrono::duration<double, milli>(end - start).count();

    Value check = 10;
    for (Value elem : prefix) {
        if (elem != check++) {
            cout << "FAILED RESULT at " << check - 1 << endl;
            return 1;
        }
    }

    cout << "PASS in " << elapsed << "ms" << endl;
    return 0;
}