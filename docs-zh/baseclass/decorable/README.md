# Decorable
`Decorable`翻译过来就是装饰器，mongo中的装饰器模式属于`CRTP`的一种实现。使用时子类继承`Decorable`,保证子类的对象在不同的装饰器实例上不同。

## 装饰器模式
允许向一个现有的对象添加新的功能，同时又不改变其结构。这种类型的设计模式属于结构型模式，它是作为现有的类的一个包装。
这种模式创建了一个装饰类，用来包装原有的类，并在保持类方法签名完整性的前提下，提供了额外的功能。
![结构图](https://www.runoob.com/wp-content/uploads/2014/08/20210420-decorator-1-decorator-decorator.svg)

## CRTP
奇异递归模板模式（curiously recurring template pattern，CRTP）是C++模板编程时的一种惯用法（idiom）：把派生类作为基类的模板参数。
### 简单例子
```cpp
template <typename D>
class Decorable {
    void getDecorable()
    {
        T& derived = static_cast<T&>(*this);
    }
}
class ServerContext : public Decorable<ServerContext> {

};
```
> 两个特点： 1 继承自模板类. 2 派生类将自身作为参数传给模板类。

### 与多态的区别
- 多态是动态绑定（运行时绑定），CRTP是静态绑定（编译时绑定）
- 在实现多态时，需要重写虚函数，因而这是运行时绑定的操作。
- CRTP在编译期确定通过基类来得到派生类的行为，它通过派生类覆盖基类成员函数来实现静态绑定的。

[更多实例](https://www.cnblogs.com/rainySue/p/c-qi-te-de-di-gui-mo-ban-mo-shi-CRTP.html)

## mongodb的实现
对于c++不熟悉的来说感觉设计的太复杂了，接下来是按照我目前的理解加上查资料总结出来的，可能存在不对的地方。
### 相关的类说明
- [Decorable](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/util/decorable.h): 装饰器类，也就是基类，有两个静态方法`declareDecoration`用来获取`Decoration`的，`getRegistry`用来获取`DecorationRegistry<D>`的实例。
- [Decoration](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/util/decorable.h): 装饰类，`Decorable`的内部类，重写了()操作。
- [DecorationRegistry](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/util/decoration_registry.h): 装饰器注册类，成员属性有`DecorationInfoVector`的实例对象和`_totalSizeBytes`。
- [DecorationContainer](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/util/decoration_container.h): 装饰器容器类，保存了`DecorationRegistry`的实例对象和8个大小的`char[]`。
- [DecorationDescriptor](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/util/decoration_container.h): 装饰描述类，里面只有一个`index`属性。
- [DecorationDescriptorWithType](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/util/decoration_container.h): 装饰描述模板类，就是`DecorationDescriptor`的再一次封装。
- [DecorationInfo](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/util/decoration_registry.h): 装饰数据类，主要有`index`、构造函数、析构函数。

### declareDecoration
前面也说过这个方法是一个静态,主要负责的是实例化`Decoration`。通过`getRegistry`拿到了`DecorationRegistry`对象,并且这个对象是`static`。再调用`DecorationRegistry`的`declareDecoration`拿到`DecorationDescriptorWithType`的实例。
```cpp
template <typename D>
class Decorable {
public:
    template <typename T>
    static Decoration<T> declareDecoration() {
        return Decoration<T>(getRegistry()->template declareDecoration<T>());
    }
private:
    static DecorationRegistry<D>* getRegistry() {
        static DecorationRegistry<D>* theRegistry = new DecorationRegistry<D>();
        return theRegistry;
    }
}
```
这里有两个`declareDecoration`方法，第一个指`auto declareDecoration()`，第二个指`typename DecorationContainer<DecoratedType>::DecorationDescriptor declareDecoration()`。
    typename DecorationContainer<DecoratedType>::DecorationDescriptor declareDecoration(
第一个主要主要类型检测，然后调用第二个对`DecorationDescriptor`进行实例用。这里实参有四个，第一个sizeof`T`;第二个对齐后的`T`的大小;第三个和第四个分别对应构造和析构`T`对象的函数。`_totalSizeBytes`的默认值为8，每次进行`declareDecoration`会累加相应的值，并且此时的`index`为`_totalSizeBytes`的值。

```cpp
template <typename DecoratedType>
class DecorationRegistry {
public:
    template <typename T>
    auto declareDecoration() {
        // 检测T是否为可破坏的类型
        MONGO_STATIC_ASSERT_MSG(std::is_nothrow_destructible<T>::value,
                                "Decorations must be nothrow destructible");
        return
            typename DecorationContainer<DecoratedType>::template DecorationDescriptorWithType<T>(
                std::move(declareDecoration(
                    sizeof(T), std::alignment_of<T>::value, &constructAt<T>, &destroyAt<T>)));
    }
private:
    typename DecorationContainer<DecoratedType>::DecorationDescriptor declareDecoration(
        const size_t sizeBytes,
        const size_t alignBytes,
        const DecorationConstructorFn constructor,
        const DecorationDestructorFn destructor) {
        // 总的大小 如果不能整除当前对象的对齐系数 
        const size_t misalignment = _totalSizeBytes % alignBytes;
        if (misalignment) {
            _totalSizeBytes += alignBytes - misalignment;
        }
        // 创建DecorationInfo对象 并且加到_decorationInfo中
        typename DecorationContainer<DecoratedType>::DecorationDescriptor result(_totalSizeBytes);
        _decorationInfo.push_back(DecorationInfo(result, constructor, destructor));
        // 最后累加一次大小
        _totalSizeBytes += sizeBytes;
        return result;
    }
    template <typename T>
    static void constructAt(void* location) {
        new (location) T();
    }

    template <typename T>
    static void destroyAt(void* location) {
        static_cast<T*>(location)->~T();
    }
    DecorationInfoVector _decorationInfo;
    size_t _totalSizeBytes{sizeof(void*)};
}
```
到这里`DecorationRegistry`对象的`_decorationInfo`就保存每次`declareDecoration`过程中生成的各种对象。

### Decorable子类的实例化
通过`declareDecoration`进行注册后，需要对`Decorable`进行实例化。
```cpp
class MyDecorable : public Decorable<MyDecorable> {};
MyDecorable decorable;
```
这里的`_decorationData`的size就是`_totalSizeBytes`的值。
```cpp
template <typename DecoratedType>
class DecorationContainer {
    explicit DecorationContainer(Decorable<DecoratedType>* const decorated,
                                 const DecorationRegistry<DecoratedType>* const registry)
        : _registry(registry),
          _decorationData(new unsigned char[registry->getDecorationBufferSizeBytes()]) {
        Decorable<DecoratedType>** const backLink =
            reinterpret_cast<Decorable<DecoratedType>**>(_decorationData.get());
        *backLink = decorated;
        _registry->construct(this);
    }
    
    
};
```
`construct`方法用于调用无参构造函数
```cpp
template <typename DecoratedType>
class DecorationRegistry {
    size_t getDecorationBufferSizeBytes() const {
        return _totalSizeBytes;
    }
    void construct(DecorationContainer<DecoratedType>* const container) const {
        using std::cbegin;

        auto iter = cbegin(_decorationInfo);

        auto cleanupFunction = [&iter, container, this ]() noexcept->void {
            using std::crend;
            std::for_each(std::make_reverse_iterator(iter),
                          crend(this->_decorationInfo),
                          [&](auto&& decoration) {
                              decoration.destructor(
                                  container->getDecoration(decoration.descriptor));
                          });
        };

        auto cleanup = makeGuard(std::move(cleanupFunction));

        using std::cend;
        // 循环调用_decorationInfo数组中的所有构造函数
        for (; iter != cend(_decorationInfo); ++iter) {
            iter->constructor(container->getDecoration(iter->descriptor));
        }

        cleanup.dismiss();
    }
};
```
### 使用Decoration实例
执行`decoration(d)`实际调用的是`T& operator()((D& d)`，`Decoration`重载了操作符`()`有四个成员方法。下面我们看其中一个实现就可以了。
```cpp
template <typename T>
class Decoration {
    T& operator()(D& d) const {
        return static_cast<Decorable&>(d)._decorations.getDecoration(this->_raw);
    }
}
```
- 入参`d`对应的是`Decorable`的对象或子类对象
- `static_cast<Decorable&>`将`d`静态转换为`Decorable&`类型
- `this->_raw`对应的是`typename DecorationContainer<D>::template DecorationDescriptorWithType<T>`，实际就是`DecorationDescriptor`，拿到对应的`_index`属性。
- `_decorations.getDecoration`执行的是`DecorationContainer`的`getDecoration`成员方法

#### getDecoration
`DecorationContainer`有两个`getDecoration`，一个模板函数，一个非模板函数。
```cpp
template <typename DecoratedType>
class DecorationContainer {
    void* getDecoration(DecorationDescriptor descriptor) {
        // 返回的是对应_decorationData保存的对象地址
        return _decorationData.get() + descriptor._index;
    }
    template <typename T>
    T& getDecoration(DecorationDescriptorWithType<T> descriptor) {
        // 转为地址对应的对象引用
        return *static_cast<T*>(getDecoration(descriptor._raw));
    }
}
```
## 总结
`declareDecoration`
## mongo中ServiceContext






