
#include <algorithm>
#include <functional>
#include <iterator>
#include <type_traits>
#include <vector>
#include <iostream>

#include <cstdint>
#include <memory>

template <typename Function>
class ScopeGuard {
  Function function;
  bool dismissed;

 public:
  explicit ScopeGuard(Function&& function)
      : function(std::move(function)), dismissed(false) {}

  void dismiss() {
    dismissed = true;
  }

  ~ScopeGuard() noexcept {
    if (!dismissed) {
      function();
    }
  }
};
template <typename F>
auto makeGuard(F&& fun) {
    return ScopeGuard<std::decay_t<F>>(std::forward<F>(fun));
}

template <typename DecoratedType>
class DecorationRegistry;

template <typename DecoratedType>
class Decorable;

/**
 * An container for decorations.
 */
template <typename DecoratedType>
class DecorationContainer {
    DecorationContainer(const DecorationContainer&) = delete;
    DecorationContainer& operator=(const DecorationContainer&) = delete;

public:
    /**
     * Opaque descriptor of a decoration.  It is an identifier to a field on the
     * DecorationContainer that is private to those modules that have access to the descriptor.
     */
    class DecorationDescriptor {
    public:
        DecorationDescriptor() = default;

    private:
        friend DecorationContainer;
        friend DecorationRegistry<DecoratedType>;
        friend Decorable<DecoratedType>;

        explicit DecorationDescriptor(size_t index) : _index(index) {}

        size_t _index;
    };

    /**
     * Opaque description of a decoration of specified type T.  It is an identifier to a field
     * on the DecorationContainer that is private to those modules that have access to the
     * descriptor.
     */
    template <typename T>
    class DecorationDescriptorWithType {
    public:
        DecorationDescriptorWithType() = default;

    private:
        friend DecorationContainer;
        friend DecorationRegistry<DecoratedType>;
        friend Decorable<DecoratedType>;

        explicit DecorationDescriptorWithType(DecorationDescriptor raw) : _raw(std::move(raw)) {}

        DecorationDescriptor _raw;
    };

    /**
     * Constructs a decorable built based on the given "registry."
     *
     * The registry must stay in scope for the lifetime of the DecorationContainer, and must not
     * have any declareDecoration() calls made on it while a DecorationContainer dependent on it
     * is in scope.
     */
    explicit DecorationContainer(Decorable<DecoratedType>* const decorated,
                                 const DecorationRegistry<DecoratedType>* const registry)
        : _registry(registry),
          _decorationData(new unsigned char[registry->getDecorationBufferSizeBytes()]) {
        // Because the decorations live in the externally allocated storage buffer at
        // `_decorationData`, there needs to be a way to get back from a known location within this
        // buffer to the type which owns those decorations.  We place a pointer to ourselves, a
        // "back link" in the front of this storage buffer, as this is the easiest "well known
        // location" to compute.
        std::cout<<"byte"<<registry->getDecorationBufferSizeBytes()<<std::endl;
        Decorable<DecoratedType>** const backLink =
            reinterpret_cast<Decorable<DecoratedType>**>(_decorationData.get());
        *backLink = decorated;
        _registry->construct(this);
    }

    ~DecorationContainer() {
        _registry->destroy(this);
    }

    /**
     * Gets the decorated value for the given descriptor.
     *
     * The descriptor must be one returned from this DecorationContainer's associated _registry.
     */
    void* getDecoration(DecorationDescriptor descriptor) {
        return _decorationData.get() + descriptor._index;
    }

    /**
     * Same as the non-const form above, but returns a const result.
     */
    const void* getDecoration(DecorationDescriptor descriptor) const {
        return _decorationData.get() + descriptor._index;
    }

    /**
     * Gets the decorated value or the given typed descriptor.
     */
    template <typename T>
    T& getDecoration(DecorationDescriptorWithType<T> descriptor) {
        auto b = *static_cast<T*>(getDecoration(descriptor._raw));
        return *static_cast<T*>(getDecoration(descriptor._raw));
    }

    /**
     * Same as the non-const form above, but returns a const result.
     */
    template <typename T>
    const T& getDecoration(DecorationDescriptorWithType<T> descriptor) const {
        return *static_cast<const T*>(getDecoration(descriptor._raw));
    }

private:
    const DecorationRegistry<DecoratedType>* const _registry;
    const std::unique_ptr<unsigned char[]> _decorationData;
};


template <typename DecoratedType>
class DecorationRegistry {
    DecorationRegistry(const DecorationRegistry&) = delete;
    DecorationRegistry& operator=(const DecorationRegistry&) = delete;

public:
    DecorationRegistry() {
        std::cout<<"111"<<std::endl;
    }

    /**
     * Declares a decoration of type T, constructed with T's default constructor, and
     * returns a descriptor for accessing that decoration.
     *
     * NOTE: T's destructor must not throw exceptions.
     */
    template <typename T>
    auto declareDecoration() {
        std::is_nothrow_destructible<T>::value;
    //     MONGO_STATIC_ASSERT_MSG(std::is_nothrow_destructible<T>::value,
    //                             "Decorations must be nothrow destructible");
        return
            typename DecorationContainer<DecoratedType>::template DecorationDescriptorWithType<T>(
                std::move(declareDecoration(
                    sizeof(T), std::alignment_of<T>::value, &constructAt<T>, &destroyAt<T>)));
    }

    size_t getDecorationBufferSizeBytes() const {
        return _totalSizeBytes;
    }

    /**
     * Constructs the decorations declared in this registry on the given instance of
     * "decorable".
     *
     * Called by the DecorationContainer constructor. Do not call directly.
     */
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

        for (; iter != cend(_decorationInfo); ++iter) {
            iter->constructor(container->getDecoration(iter->descriptor));
        }

        cleanup.dismiss();
    }

    /**
     * Destroys the decorations declared in this registry on the given instance of "decorable".
     *
     * Called by the DecorationContainer destructor.  Do not call directly.
     */
    void destroy(DecorationContainer<DecoratedType>* const container) const noexcept try {
        std::for_each(_decorationInfo.rbegin(), _decorationInfo.rend(), [&](auto&& decoration) {
            decoration.destructor(container->getDecoration(decoration.descriptor));
        });
    } catch (...) {
        std::terminate();
    }

private:
    /**
     * Function that constructs (initializes) a single instance of a decoration.
     */
    using DecorationConstructorFn = void (*)(void*);

    /**
     * Function that destroys (deinitializes) a single instance of a decoration.
     */
    using DecorationDestructorFn = void (*)(void*);

    struct DecorationInfo {
        DecorationInfo() {}
        DecorationInfo(
            typename DecorationContainer<DecoratedType>::DecorationDescriptor inDescriptor,
            DecorationConstructorFn inConstructor,
            DecorationDestructorFn inDestructor)
            : descriptor(std::move(inDescriptor)),
              constructor(std::move(inConstructor)),
              destructor(std::move(inDestructor)) {}

        typename DecorationContainer<DecoratedType>::DecorationDescriptor descriptor;
        DecorationConstructorFn constructor;
        DecorationDestructorFn destructor;
    };

    using DecorationInfoVector = std::vector<DecorationInfo>;

    template <typename T>
    static void constructAt(void* location) {
        new (location) T();
    }

    template <typename T>
    static void destroyAt(void* location) {
        static_cast<T*>(location)->~T();
    }

    /**
     * Declares a decoration with given "constructor" and "destructor" functions,
     * of "sizeBytes" bytes.
     *
     * NOTE: "destructor" must not throw exceptions.
     */
    typename DecorationContainer<DecoratedType>::DecorationDescriptor declareDecoration(
        const size_t sizeBytes,
        const size_t alignBytes,
        const DecorationConstructorFn constructor,
        const DecorationDestructorFn destructor) {
        const size_t misalignment = _totalSizeBytes % alignBytes;
        if (misalignment) {
            _totalSizeBytes += alignBytes - misalignment;
        }
        typename DecorationContainer<DecoratedType>::DecorationDescriptor result(_totalSizeBytes);
        _decorationInfo.push_back(DecorationInfo(result, constructor, destructor));
        _totalSizeBytes += sizeBytes;
        return result;
    }

    DecorationInfoVector _decorationInfo;
    size_t _totalSizeBytes{sizeof(void*)};
};

template <typename D>
class Decorable {
    Decorable(const Decorable&) = delete;
    Decorable& operator=(const Decorable&) = delete;

public:
    template <typename T>
    class Decoration {
    public:
        Decoration() = delete;

        T& operator()(D& d) const {
            return static_cast<Decorable&>(d)._decorations.getDecoration(this->_raw);
        }

        T& operator()(D* const d) const {
            return (*this)(*d);
        }

        const T& operator()(const D& d) const {
            return static_cast<const Decorable&>(d)._decorations.getDecoration(this->_raw);
        }

        const T& operator()(const D* const d) const {
            return (*this)(*d);
        }

        const D* owner(const T* const t) const {
            return static_cast<const D*>(getOwnerImpl(t));
        }

        D* owner(T* const t) const {
            return static_cast<D*>(getOwnerImpl(t));
        }

        const D& owner(const T& t) const {
            return *owner(&t);
        }

        D& owner(T& t) const {
            return *owner(&t);
        }

    private:
        const Decorable* getOwnerImpl(const T* const t) const {
            return *reinterpret_cast<const Decorable* const*>(
                reinterpret_cast<const unsigned char* const>(t) - _raw._raw._index);
        }

        Decorable* getOwnerImpl(T* const t) const {
            return const_cast<Decorable*>(getOwnerImpl(const_cast<const T*>(t)));
        }

        friend class Decorable;

        explicit Decoration(
            typename DecorationContainer<D>::template DecorationDescriptorWithType<T> raw)
            : _raw(std::move(raw)) {}

        typename DecorationContainer<D>::template DecorationDescriptorWithType<T> _raw;
    };

    template <typename T>
    static Decoration<T> declareDecoration() {
        return Decoration<T>(getRegistry()->template declareDecoration<T>());
    }

protected:
    Decorable() : _decorations(this, getRegistry()) {}
    ~Decorable() = default;

private:
    static DecorationRegistry<D>* getRegistry() {
        static DecorationRegistry<D>* theRegistry = new DecorationRegistry<D>();
        return theRegistry;
    }

    DecorationContainer<D> _decorations;
};

static int numConstructedAs;
static int numDestructedAs;
class B1 {
public:
    B1() {

    }
    // B1() = delete;
    B1(int a){
        std::cout<<a<<std::endl;
    }
};
class A {
public:
    A() : value(0) {
        std::cout<<"-------"<<std::endl;
        ++numConstructedAs;
    }
    ~A() {
        ++numDestructedAs;
    }
    int getValue() {
        return this->value;
    }
    int value; // 4
    bool v2; // 1
    float v3; // 8
    bool v4; // 1
};

class ThrowA {
public:
    ThrowA() : value(0) {
    }

    int value;
};

class MyDecorable : public Decorable<MyDecorable> {};


template <typename Derived>
class Base
{
public:
    void Interface()  
    {  
        std::cout <<"come from Interface"<<std::endl;      
        // 转换为子类指针，编译期将绑定至子类方法  
        static_cast<Derived*>(this)->Implementation(); 
    }  
             
    static void StaticInterface()  
    {  
        // 编译期将绑定至子类方法  
        std::cout <<"come from StaticInterface"<<std::endl;
        Derived::StaticImplementation();  
    }  

    void Implementation()
    {
        std::cout <<"Base Implementation"<<std::endl;
        return;
    }
    static void StaticImplementation()
    {
        std::cout << "Base StaticImplementation"<<std::endl;
        return;
    }
};

class Derived1 : public Base<Derived1>
{
public:
    static void StaticImplementation(){
        std::cout << "StaticImplementation from Derived1"<<std::endl;
        return;
    }

};

class Derived2 : public Base<Derived2>
{
public:
    static void Implementation(){
        std::cout <<"Implementation from Derived2"<<std::endl;
        return;
    }

};

static A getA() {
    static A a1;
    return a1;
}
class A3{
    char a;
};
class A2{
public:
    int a;
    int a1;
    int a2;
    char b;
    short c;
    double d;
    double e;
    A a3;
    // A3 a4;
    // A3 a5;


};
struct C
{
    char a;
    int b;
    double c;
    short d;
};
int main(){
    std::cout<<alignof(A2)<< " "<< sizeof(A2) << " "<< sizeof(A)<<std::endl;
    std::cout<<alignof(C)<< " "<< sizeof(C)<<std::endl; // 8 24
    getA();
    getA();
    getA();
    getA();
    const auto dd1 = MyDecorable::declareDecoration<A>();
    const auto dd2 = MyDecorable::declareDecoration<A>();
    const auto dd4 = MyDecorable::declareDecoration<A>();
    const auto dd3 = MyDecorable::declareDecoration<int>();
    // const auto dd5 = MyDecorable::declareDecoration<C>();
    // const auto dd6 = MyDecorable::declareDecoration<float>();
    const auto dd7 = MyDecorable::declareDecoration<B1>();

    MyDecorable decorable1;
    std::cout<< "numConstructedAs: "<< numConstructedAs<<std::endl;
    dd1(decorable1).value = 1;
    
    std::cout<<dd1(decorable1).getValue()<<std::endl;

    dd2(decorable1).value = 2;
    
    std::cout<<dd1(decorable1).getValue()<<std::endl;
    std::cout<<dd2(decorable1).getValue()<<std::endl;

    std::cout << "***********************************" << std::endl;
    Derived1 derive1;
    Derived2 derive2;
    derive1.Implementation();
    derive1.StaticImplementation();
    derive2.Implementation();
    derive2.StaticImplementation();
    std::cout << "***********************************" << std::endl << std::endl;

    Base<Derived1> base_derive1;
    Base<Derived2> base_derive2;
    base_derive1.Implementation();
    base_derive1.StaticImplementation();
    base_derive2.Implementation();
    base_derive2.StaticImplementation();
    std::cout << "***********************************" << std::endl << std::endl;

    base_derive1.StaticInterface();
    base_derive1.Interface();
    base_derive2.StaticInterface();
    base_derive2.Interface();
    std::cout << "***********************************" << std::endl << std::endl;

    return 0;

}