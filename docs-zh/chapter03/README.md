# 源码入门
看源码是个比较费力气的事，可能挺多之前是javascript开发，对c/c++接触比较少，所以这个章节会大概说下mongo项目中用到的一些难理解的一些c++代码。然后再介绍下项目结构和怎么去断点调试。

## 分支
基于mongodb`4.4`分支,

### 目录结构
```text
├── build                   编译相关的文件
├── docs                    文档 building.md 需要关注下
├── etc
├── jstests                 单元测试
├── pytests
├── rpm
├── site_scons
├── src                     源码文件
├──── mongo                 mongo源码
├──────── base
├──────── bson
├──────── client
├──────── crypto
├──────── db               mongo server主要目录
├──────── dbtest
├──────── embedded
├──────── executor
├──────── idl
├──────── install
├──────── logger
├──────── platform
├──────── s
├──────── scripting
├──────── shell
├──────── stdx
├──────── tools
├──────── transport
├──────── unittest
├──────── util
├──────── watchdog
├──────── watchdog
├──── third_party           第三方库源码
```

## building
我这里是linux系统。
- 系统: debian 10.6
- vscode: 1.61.1
- python: 3.7.3
- mongodb: 4.4
- gcc: 8.3.0
- pip: 20.x
### 安装依赖
```shell
sudo apt install  libcurl4-openssl-dev
sudo apt install liblzma-dev
cd mongo
# 安装mongod mongo mongos 修改了源码也需要重新编译
# 也可以加 -j 4 指定开启4个线程同时编译 如果不指定会用本机可以的最大线程数
python3 buildscripts/scons.py install-core -j 4
```
### 修改配置
我这里加了.vscode和config目录，只要修改mongodb.conf的dbPath和systemLog.path为你的本地路径就可以了。


## c++
代码里有大量的c++11写法，让没做过c++开发的会比较懵，所以我觉得还是有必要单独列出来。当然了，我们不深入到语法具体的实现，大概了解它的逻辑就可以了。

### .h .cpp
.h一般是定义类有哪些方法和属性，.cpp一般是实现对应的方法。所以你看到有.h文件，一般会有个同名的.cpp文件
### 定义类的方式
struct: 结构体。一般在比较早的代码或内部类里会比较多
```cpp
struct SpecificStats {
    virtual ~SpecificStats() {}

    /**
     * Make a deep copy.
     */
    virtual SpecificStats* clone() const = 0;

    virtual uint64_t estimateObjectSizeInBytes() const = 0;
};
```

class: 类。这个就太多了，比较好理解。在c中没有class的概念。
```cpp
class CommandInterface {
public:
    virtual ~CommandInterface() = default;
    virtual StringData sensitiveFieldName() const = 0;
    virtual void snipForLogging(mutablebson::Document* cmdObj) const = 0;
    virtual StringData getName() const = 0;
    virtual NamespaceString ns() const = 0;
    virtual bool redactArgs() const = 0;
};
```
### 类的实例化
- new: 比较常用了
- BSONObjBuilder cursorResult: 调用BSONObjBuilder无参构造函数

### 访问对象或者类
- A->B: 则A为指向类、结构的指针，如exec->getNext() 
- A.B: 则A为对象或结构体，如responseBuilder.append(next)。和第一个区别一个是指针，一个是对象
- A::B : 表示作用域A中的名称B，A可以是名字空间、类、结构。如FindCommon::haveSpaceForNext()。一般是访问类的静态函数或静态属性

### 类型转换
- static_cast: 静态转换，编译时检查。static_cast<bool>(cursor) cursor转为bool
- dynamic_cast: 动态转换，会检查类型。dynamic_cast<UpdateObjectNode*>(child)  child 转为UpdateObjectNode*
- const_cast: 用于修改类型的const或volatile属性。const_cast<char*>(ConstView::view2ptr());
- reinterpret_cast: 允许将任何指针转换为任何其他指针类型。如reinterpret_cast<uint64_t>(&_idx)

### 智能指针
- unique_ptr: 实现独占式，保证同一时间只有一个智能指针指向该对象。如std::unique_ptr<Impl> _impl;
- shared_ptr: 实现共享式，多个智能指针可以指向相同对象，该对象和其相关资源会在“最后一个引用被销毁”时候释放。如std::shared_ptr<CommandInvocation> invocation
- weak_ptr: 不控制对象生命周期的智能指针, 如std::weak_ptr<QueueType> _weakQueue;

### noexcept
该关键字告诉编译器，函数中不会发生异常,这有利于编译器对程序做更多的优化。如果在运行时，noexecpt函数向外抛出了异常（如果函数内部捕捉了异常并完成处理，这种情况不算抛出异常），程序会直接终止，调用std::terminate()函数，该函数内部会调用std::abort()终止程序。

### atomic
在c++11标准里，引入了原子操作，它表示在多个线程访问同一个全局资源的时候，能够确保所有其他的线程都不在同一时间内访问相同的资源。它提供了多种原子操作数据类型，如`atomic_bool`,`atomic_int`,`atomic_char`，同时也可可以自定义类型`atomic<T>`。

### =delete、=default
`delete`禁止使用该函数，`default`默认函数。

### 类型
- std::is_integral<T>::value: 判断`T`是否为整数类型。
- std::is_same<T， U>::value: 判断`T` 与 `U` 指名同一类型（考虑 const/volatile 限定）
- std::is_unsigned<T>::value: 判断`T`是否为无符号类型。

### using
- 申明命名空间
- 在子类中对基类成员进行声明，可恢复基类的防控级别
- 使用using起别名

### 其他std方法
- std::transform: 在指定的范围内应用于给定的操作,并将结果存储在指定的另一个范围内。
- std::back_inserter: 尾部插入迭代器。
- std::front_inserter: 首部插入迭代器。
- std::inserter: 指定位置插入迭代器。


## mongo源码中比较常见的写法
### MONGO_likely与MONGO_unlikely
`likely`中文解释是可能发生，unlikely不可能发生的。代码在文件`/path/mongo/src/mongo/platform/compiler_gcc.h`中。将`__builtin_expect`指令封装为`MONGO_likely`和`MONGO_unlikely`两个宏。简单来说`__builtin_expect`作用是允许程序员将最有可能执行的分支告诉编译器，通过这种方式，编译器在编译过程中，会将可能性更大的代码紧跟着起面的代码，从而减少指令跳转带来的性能上的下降。
```cpp
#define MONGO_likely(x) static_cast<bool>(__builtin_expect(static_cast<bool>(x), 1))
#define MONGO_unlikely(x) static_cast<bool>(__builtin_expect(static_cast<bool>(x), 0))
```

### invariant
源码里有大量的`invariant`判断，主要用来判断表达式是否成立。它是一个宏定义，代码集中在文件`/path/mongo/src/mongo/util/invariant.h`中
```cpp
// MONGO_expand和BOOST_PP_OVERLOAD先不了解
#define invariant(...) \
    MONGO_expand(MONGO_expand(BOOST_PP_OVERLOAD(MONGO_invariant_, __VA_ARGS__))(__VA_ARGS__))

// ============一个参数
// invariant(condition)  那么会判断condition是否为true， 比如非空为true，大于0为true。
// 并且 condition也可以是表达式， 比如 invariant(this == _stack->pop()), 那么就是相等就是true
#define MONGO_invariant_1(Expression) \
    ::mongo::invariantWithLocation((Expression), #Expression, __FILE__, __LINE__)

template <typename T>
// 如果表达式不满足 则会输出日志 调用的是invariantFailed
inline void invariantWithLocation(const T& testOK,
                                  const char* expr,
                                  const char* file,
                                  unsigned line) {
    if (MONGO_unlikely(!testOK)) {
        ::mongo::invariantFailed(expr, file, line);
    }
}
// ============一个参数

// ============两个参数
// invariant(condition, "hello")，和一个参数的作用一致。第二个入参可以增加自定义的message输出到日志中
#define MONGO_invariant_2(Expression, contextExpr)                                           \
    ::mongo::invariantWithContextAndLocation((Expression),                                   \
                                             #Expression,                                    \
                                             [&]() -> std::string { return (contextExpr); }, \
                                             __FILE__,                                       \
                                             __LINE__)

// 如果表达式不满足 则会输出日志 调用的是invariantFailedWithMsg
template <typename T, typename ContextExpr>
inline void invariantWithContextAndLocation(
    const T& testOK, const char* expr, ContextExpr&& contextExpr, const char* file, unsigned line) {
    if (MONGO_unlikely(!testOK)) {
        ::mongo::invariantFailedWithMsg(expr, contextExpr(), file, line);
    }
}
// ============两个参数
```
断言代码在`/path/mongo/src/mongo/util/assert_util.cpp`文件中，记住断言失败了，会调用`std::abort`终止程序。
```cpp
// 输出例子: {"t":{"$date":"2021-02-09T02:33:45.907+01:00"},"s":"F",  "c":"-",        "id":23079,   "ctx":"listener","msg":"Invariant failure","attr":{"expr":"_isShutdown","file":"src/mongo/db/client_out_of_line_executor.cpp","line":58}}
invariantFailed(const char* expr,
                                             const char* file,
                                             unsigned line) noexcept {
    LOGV2_FATAL_CONTINUE(23079,
                         "Invariant failure {expr} {file} {line}",
                         "Invariant failure",
                         "expr"_attr = expr,
                         "file"_attr = file,
                         "line"_attr = line);
    breakpoint();
    LOGV2_FATAL_CONTINUE(23080, "\n\n***aborting after invariant() failure\n\n");
    std::abort();
}
// 输入例子: {"t":{"$date":"2021-10-25T16:24:03.520+07:00"},"s":"F", "c":"-", "id":23081, "ctx":"conn66","msg":"Invariant failure","attr":{"expr":"batchStatus == ErrorCodes::TimeseriesBucketCleared || batchStatus.isA<ErrorCategory::Interruption>()","msg":"Unexpected error when aborting time-series batch: WriteConflict: WriteConflict error: this operation conflicted with another operation. Please retry your operation or multi-document transaction.","file":"src/mongo/db/timeseries/bucket_catalog.cpp","line":381}}
MONGO_COMPILER_NOINLINE void invariantFailedWithMsg(const char* expr,
                                                    const std::string& msg,
                                                    const char* file,
                                                    unsigned line) noexcept {
    LOGV2_FATAL_CONTINUE(23081,
                         "Invariant failure {expr} {msg} {file} {line}",
                         "Invariant failure",
                         "expr"_attr = expr,
                         "msg"_attr = msg,
                         "file"_attr = file,
                         "line"_attr = line);
    breakpoint();
    LOGV2_FATAL_CONTINUE(23082, "\n\n***aborting after invariant() failure\n\n");
    std::abort();
}
```