# AtomicWord
这个类主要是对`atomic`再一次封装，作用就是变量进行一些原子性的操作。对比与`std::atomic`按类型区分了不同的原子性api。这个类比较独立，代码大部分都在[atomic_word.h](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/platform/atomic_word.h)中。

## 定义三种Base类
```cpp
// 定义Base三种类型kBasic, kArithmetic, kUnsigned
enum class Category { kBasic, kArithmetic, kUnsigned };

// kUnsigned: 当T是整些 && 不等于bool && 无符号 && 不等于char； 只剩下了无符号的int、short、long、long long
// kArithmetic: 当T是整些 && 不等于bool ； 只剩下了有符号的 int、short、long、long long 和char
// kBasic: 排查kUnsigned和kArithmetic之外的类型
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
```

## kBasic
基类, 主要实现了获取值、修改值的方法。
```cpp
template <typename T>
class Base<T, Category::kBasic> {
public:
    /**
     * 类型T的别名
     */
    using WordType = T;

    /**
     * 构造函数 默认value是WordType对应的默认值
     */
    constexpr Base() : _value() {}

    /**
     * 构造函数 传入指定的值
     */
    explicit constexpr Base(WordType v) : _value(v) {}

    /**
     * 获取值
     */
    WordType load() const {
        return _value.load();
    }

    /**
     * 获取值 没有同步或顺序制约，仅对此操作要求原子性
     */
    WordType loadRelaxed() const {
        return _value.load(std::memory_order_relaxed);
    }

    /**
     * 设置一个新的值
     */
    void store(WordType newValue) {
        _value.store(newValue);
    }

    /**
     * 设置一个新的值 返回旧的值
     *
     */
    WordType swap(WordType newValue) {
        return _value.exchange(newValue);
    }

    /**
     * 对比expected和当前存储的值是否一致
     * 如果一致，则value替换为newValue
     * 否则，value替换为expected
     * 成功交换则返回true 否则返回false
     */
    bool compareAndSwap(WordType* expected, WordType newValue) {
        return _value.compare_exchange_strong(*expected, newValue);
    }

protected:
    // 属性为_value
    std::atomic<WordType> _value;
};
```

## kArithmetic
继承了kBasic，并且增加了对value加、减的操作。
```cpp
template <typename T>
class Base<T, Category::kArithmetic> : public Base<T, Category::kBasic> {
protected:
    // 定义父类别名 Parent
    using Parent = Base<T, Category::kBasic>;
    // 访问父类的_value
    using Parent::_value;

public:
    // WordType的别名
    using WordType = typename Parent::WordType;
    using Parent::Parent;

    /**
     * 原子性的对value增加increment
     * value = value + decrement
     * 返回值计算之前的value
     */
    WordType fetchAndAdd(WordType increment) {
        return _value.fetch_add(increment);
    }
    /**
     * 原子性的对value增加increment 没有同步或顺序制约，仅对此操作要求原子性
     * value = value + decrement
     * 返回值计算之前的value
     */
    WordType fetchAndAddRelaxed(WordType increment) {
        return _value.fetch_add(increment, std::memory_order_relaxed);
    }

    /**
     * 原子性的对value减少increment
     * value = value - decrement
     * 返回值计算之前的value
     */
    WordType fetchAndSubtract(WordType decrement) {
        return _value.fetch_sub(decrement);
    }

    /**
     * 原子性的对value增加increment
     * value = value + decrement
     * 返回值计算后的value
     */
    WordType addAndFetch(WordType increment) {
        return fetchAndAdd(increment) + increment;
    }

    /**
     * 原子性的对value减少increment
     * value = value - decrement
     * 返回值计算后的value
     */
    WordType subtractAndFetch(WordType decrement) {
        return fetchAndSubtract(decrement) - decrement;
    }
};
```

## kUnsigned
继承了`kArithmetic`，并且增加了对value位操作。
```cpp
template <typename T>
class Base<T, Category::kUnsigned> : public Base<T, Category::kArithmetic> {
private:
    using Parent = Base<T, Category::kArithmetic>;
    using Parent::_value;

public:
    using WordType = typename Parent::WordType;
    using Parent::Parent;

    /**
     * 原子性的对value和bits进行按位与运算，运算的结果替换value
     * value = value & bits
     * 返回运算之前的value
     */
    WordType fetchAndBitAnd(WordType bits) {
        return _value.fetch_and(bits);
    }

    /**
     * 原子性的对value和bits进行按位或运算，运算的结果替换value
     * value = value | bits
     * 返回运算之前的value
     */
    WordType fetchAndBitOr(WordType bits) {
        return _value.fetch_or(bits);
    }

   /**
     * 原子性的对value和bits进行按位异或运算，运算的结果替换value 
     * value = value ^ bits
     * 返回运算之前的value
     */
    WordType fetchAndBitXor(WordType bits) {
        return _value.fetch_xor(bits);
    }
};
```

## AtomicWord
定义`AtomicWord`，最终代码可调用的类。
```cpp
template <typename T>
class AtomicWord : public atomic_word_detail::Base<T> {
public:
    // 
    MONGO_STATIC_ASSERT(!std::is_integral<T>::value ||
                        sizeof(T) == sizeof(atomic_word_detail::Base<T>));
    using atomic_word_detail::Base<T>::Base;
};
```
## 使用
```cpp
// AtomicWord => kArithmetic => kBasic
AtomicWord<int> intWord{5};
// AtomicWord => kArithmetic => kBasic
AtomicWord<long long> longWord{5};
// AtomicWord => kBasic
AtomicWord<bool> boolWord{true};
// AtomicWord => kArithmetic => kBasic
AtomicWord<char> boolWord{true};
// AtomicWord => kUnsigned => kArithmetic => kBasic
AtomicWord<unsigned int> unsignedIntWord{1};
```

