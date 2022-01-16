#include <atomic>
#include <iostream>
#define MONGO_STATIC_ASSERT(...) static_assert(__VA_ARGS__, #__VA_ARGS__)

namespace atomic_word_detail {

enum class Category { kBasic, kArithmetic, kUnsigned };

template <typename T>
constexpr Category getCategory() {
    if (std::is_integral<T>() && !std::is_same<T, bool>()) {
        if (std::is_unsigned<T>() && !std::is_same<T, char>()) {
            return Category::kUnsigned;
        }
        return Category::kArithmetic;
    }
    return Category::kBasic;
}

template <typename T, Category = getCategory<T>()>
class Base;

/**
 * Implementation of the AtomicWord interface in terms of the C++11 Atomics.
 * Defines the operations provided by a non-incrementable AtomicWord.
 * All operations have sequentially consistent semantics unless otherwise noted.
 */
template <typename T>
class Base<T, Category::kBasic> {
public:
    /**
     * Underlying value type.
     */
    using WordType = T;

    /**
     * Construct a new word, default-initialized.
     */
    constexpr Base() : _value() {}

    /**
     * Construct a new word with the given initial value.
     */
    explicit constexpr Base(WordType v) : _value(v) {}

    /**
     * Gets the current value of this AtomicWord.
     */
    WordType load() const {
        return _value.load();
    }

    /**
     * Gets the current value of this AtomicWord.
     *
     * Has relaxed semantics.
     */
    WordType loadRelaxed() const {
        return _value.load(std::memory_order_relaxed);
    }

    /**
     * Sets the value of this AtomicWord to "newValue".
     */
    void store(WordType newValue) {
        _value.store(newValue);
    }

    /**
     * Atomically swaps the current value of this with "newValue".
     *
     * Returns the old value.
     */
    WordType swap(WordType newValue) {
        return _value.exchange(newValue);
    }

    /**
     * Atomic compare and swap.
     *
     * If this value equals the value at "expected", sets this value to "newValue".
     * Otherwise, sets the storage at "expected" to this value.
     *
     * Returns true if swap successful, false otherwise
     */
    bool compareAndSwap(WordType* expected, WordType newValue) {
        // NOTE: Subtle: compare_exchange mutates its first argument.
        return _value.compare_exchange_strong(*expected, newValue);
    }

protected:
    // MONGO_STATIC_ASSERT(std::atomic<WordType>::is_always_lockfree); // TODO C++17
    std::atomic<WordType> _value;  // NOLINT
};

/**
 * Has the basic operations, plus some arithmetic operations.
 */
template <typename T>
class Base<T, Category::kArithmetic> : public Base<T, Category::kBasic> {
protected:
    using Parent = Base<T, Category::kBasic>;
    using Parent::_value;

public:
    using WordType = typename Parent::WordType;
    using Parent::Parent;

    /**
     * Get the current value of this, add "increment" and store it, atomically.
     *
     * Returns the value of this before incrementing.
     */
    WordType fetchAndAdd(WordType increment) {
        return _value.fetch_add(increment);
    }

    /**
     * Like "fetchAndAdd", but with relaxed memory order. Appropriate where relative
     * order of operations doesn't matter. A stat counter, for example.
     */
    WordType fetchAndAddRelaxed(WordType increment) {
        return _value.fetch_add(increment, std::memory_order_relaxed);
    }

    /**
     * Get the current value of this, subtract "decrement" and store it, atomically.
     * Returns the value of this before decrementing.
     */
    WordType fetchAndSubtract(WordType decrement) {
        return _value.fetch_sub(decrement);
    }

    /**
     * Get the current value of this, add "increment" and store it, atomically.
     * Returns the value of this after incrementing.
     */
    WordType addAndFetch(WordType increment) {
        return fetchAndAdd(increment) + increment;
    }

    /**
     * Get the current value of this, subtract "decrement" and store it, atomically.
     * Returns the value of this after decrementing.
     */
    WordType subtractAndFetch(WordType decrement) {
        return fetchAndSubtract(decrement) - decrement;
    }
};

template <typename T>
class Base<T, Category::kUnsigned> : public Base<T, Category::kArithmetic> {
private:
    using Parent = Base<T, Category::kArithmetic>;
    using Parent::_value;

public:
    using WordType = typename Parent::WordType;
    using Parent::Parent;

    /**
     * Atomically compute and store 'load() & bits'
     *
     * Returns the value of this before bitand-ing.
     */
    WordType fetchAndBitAnd(WordType bits) {
        return _value.fetch_and(bits);
    }

    /**
     * Atomically compute and store 'load() | bits'
     *
     * Returns the value of this before bitor-ing.
     */
    WordType fetchAndBitOr(WordType bits) {
        return _value.fetch_or(bits);
    }

    /**
     * Atomically compute and store 'load() ^ bits'
     *
     * Returns the value of this before bitxor-ing.
     */
    WordType fetchAndBitXor(WordType bits) {
        return _value.fetch_xor(bits);
    }
};

}  // namespace atomic_word_detail

/**
 * Instantiations of AtomicWord must be trivially copyable.
 */
template <typename T>
class AtomicWord : public atomic_word_detail::Base<T> {
public:
    MONGO_STATIC_ASSERT(!std::is_integral<T>::value ||
                        sizeof(T) == sizeof(atomic_word_detail::Base<T>));
    using atomic_word_detail::Base<T>::Base;

};
class A{
    private:
        int a;
        int b;
        int c;
        bool d;
        std::string e;
};
int main() {
    AtomicWord<char> c1{'a'};
    AtomicWord<bool> b1{true};
    A a;
    
    // AtomicWord<A> c2{a};
    // std::cout<<"sizeof "<<sizeof(A) << " "<<sizeof(std::atomic<A>)<< ""<<std::endl;
    c1.addAndFetch(2);
    std::cout<<"c1="<<c1.load()<<std::endl;
    AtomicWord<unsigned long> i2{2};
    std::cout<<"fetchAndBitAnd" <<i2.fetchAndBitAnd(3)<<std::endl;
    std::cout<<"i2="<<i2.load()<<std::endl;


    std::cout<<std::is_integral<std::string>::value <<std::endl;
    std::cout<<std::is_integral<std::string>() <<std::endl;

    
}