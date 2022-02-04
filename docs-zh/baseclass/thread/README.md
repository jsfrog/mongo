# Thread
`mongo-server`中有比较多的线程，下面主要大概说下每个线程的作用。
## 异常处理机制
在开始多线程之前先说说异常处理机制，Linux提供的`core dump`机制就是一种黑匣子（core文件就是黑匣子文件）。但是core文件并非在所有场景都适用，因为core文件是程序崩溃时的内存映像，如果程序使用的内存空间比较大，那产生的core文件也将会非常大，在64bit的操作系统中，该现象更为显著。但是，其实我们定位程序崩溃的原因一般只需要程序挂掉之前的堆栈信息、内存信息等就足够了。所以有的时候没有必要使用系统自带的core文件机制。和`Node.js`中的`process.on`进行异常处理的功能比较类似。[参考文章](https://blog.csdn.net/andylauren/article/details/69360629)


### sigaction
使用该函数可以改变程序默认的信号处理函数。第一个参数signum指明我们想要改变其信号处理函数的信号值。注意，这里的信号不能是SIGKILL和SIGSTOP。这两个信号的处理函数不允许用户重写，因为它们给超级用户提供了终止程序的方法（ SIGKILL and SIGSTOP cannot be caught, blocked, or ignored）。
第二个和第三个参数是一个struct sigaction的结构体，该结构体在<signal.h>中定义，用来描述信号处理函数。如果act不为空，则其指向信号处理函数。如果oldact不为空，则之前的信号处理函数将保存在该指针中。如果act为空，则之前的信号处理函数不变。我们可以通过将act置空，oldact非空来获取当前的信号处理函数。
```cpp
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

```

### sigemptyset
函数sigemptyset初始化由set指向的信号集，清除其中所有的信号。
```cpp
int sigemptyset(sigset_t *set);
```

### sigaltstack
该函数的作用就是在在堆中为函数分配一块区域，作为该函数的栈使用。所以，虽然将系统默认的栈空间用尽了，但是当调用我们的信号处理函数时，使用的栈是它实现在堆中分配的空间，而不是系统默认的栈，所以它仍旧可以正常工作。
```cpp
int sigaltstack(const stack_t *ss, stack_t *oss);

typedef struct {
   void  *ss_sp;     /* Base address of stack */
   int    ss_flags;  /* Flags */
   size_t ss_size;   /* Number of bytes in stack */
} stack_t;

```

要想创建一个新的可替换信号栈，`ss_flags`必须设置为0，`ss_sp`和`ss_size`分别指明可替换信号栈的起始地址和栈大小。系统定义了一个常数`SIGSTKSZ`，该常数对极大多数可替换信号栈来说都可以满足需求，`MINSIGSTKSZ`规定了可替换信号栈的最小值。
如果想要禁用已存在的一个可替换信号栈，可将`ss_flags`设置为`SS_DISABLE`。
而sigaltstack第一个参数为创建的新的可替换信号栈，第二个参数可以设置为NULL，如果不为NULL的话，将会将旧的可替换信号栈的信息保存在里面。函数成功返回0，失败返回-1.


### 总结
使用可替换信号栈的步骤如下：
在内存中分配一块区域作为可替换信号栈
使用sigaltstack()函数通知系统可替换信号栈的存在和内存地址
使用sigaction()函数建立信号处理函数的时候，通过将sa_flags设置为SA_ONSTACK来告诉系统信号处理函数将在可替换信号栈上面运行。
> 关于`sigaltstack`的使用可以参考[sigalt-main-test.cpp](https://github.com/jsfrog/mongo/blob/v4.4/doc-zh/tests/sigalt-main-test.cpp)

## 多线程
`mongo`中的多线程实际对`std::thread`进行了一些封装，如：增加了子线程异常处理的机制、改名操作等。

### SigAltStackController
这个类主要功能就是在每个子线程中都增加了一块区域，大小为64KiB。代码在[thread.h](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/stdx/thread.h),
```cpp
class SigAltStackController {
public:
// __linux__和__FreeBSD__系统才有信号处理函数
#if MONGO_HAS_SIGALTSTACK
    // 利用Guard的构造函数和析构函数完成信号处理函数的安装和卸载
    auto makeInstallGuard() const {
        struct Guard {
            explicit Guard(const SigAltStackController& controller) : _controller(controller) {
                // 构造函数对应安装
                _controller._install();
            }

            ~Guard() {
                // 析构函数对应卸载
                _controller._uninstall();
            }

            const SigAltStackController& _controller;
        };
        return Guard{*this};
    }

private:
    static size_t _getStackSize() {
        // kMongoMinSignalStackSize 对应的大小是65535字节 
        // MINSIGSTKSZ 这里是2048
        // 所以_getStackSize返回的是65535字节 = 64KiB
        static const std::size_t kMinSigStkSz = MINSIGSTKSZ;
        return std::max(kMongoMinSignalStackSize, kMinSigStkSz);
    }

    void _install() const {
        stack_t ss = {};
        ss.ss_sp = _stackStorage.get();
        ss.ss_flags = 0;
        ss.ss_size = _getStackSize();
        if (sigaltstack(&ss, nullptr)) {
            abort();
        }
    }

    void _uninstall() const {
        stack_t ss = {};
        ss.ss_flags = SS_DISABLE;
        if (sigaltstack(&ss, nullptr)) {
            abort();
        }
    }

    // 在mongo/util/stacktrace_test实验得出64Kib比较合适 
    static constexpr std::size_t kMongoMinSignalStackSize = std::size_t{64} << 10;

    std::unique_ptr<std::byte[]> _stackStorage = std::make_unique<std::byte[]>(_getStackSize());

#else   
    auto makeInstallGuard() const {
        struct Guard {
            ~Guard() {}  // needed to suppress 'unused variable' warnings.
        };
        return Guard{};
    }
#endif
};

```
### 构造函数
`stdx::thread`中继承了`std::thread`，完成的主要功能是在构造函数中实例化了`SigAltStackController`。代码在[thread.h](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/stdx/thread.h),
```cpp
class thread : private ::std::thread {  // NOLINT
public:
    using ::std::thread::id;                  // NOLINT
    using ::std::thread::native_handle_type;  // NOLINT

    thread() noexcept = default;

    ~thread() noexcept = default;
    thread(const thread&) = delete;
    thread(thread&& other) noexcept = default;
    thread& operator=(const thread&) = delete;
    thread& operator=(thread&& other) noexcept = default;

    template <class Function,
              class... Args,
              std::enable_if_t<!std::is_same_v<thread, std::decay_t<Function>>, int> = 0>
    explicit thread(Function f, Args&&... args) noexcept
        : ::std::thread::thread(
              // 注意[]中定义的是形参
              [
                  sigAltStackController = support::SigAltStackController(),
                  f = std::move(f),
                  pack = std::make_tuple(std::forward<Args>(args)...)
              ]() mutable noexcept {
                  // 这才是匿名函数的执行方法
#if defined(_WIN32)
                  ::std::set_terminate(  // NOLINT
                      ::mongo::stdx::TerminateHandlerDetailsInterface::dispatch);
#endif
                  ThreadSafetyContext::getThreadSafetyContext()->onThreadCreate();
                  auto sigAltStackGuard = sigAltStackController.makeInstallGuard();
                  return std::apply(std::move(f), std::move(pack));
              }) {
    }

    using ::std::thread::get_id;                // NOLINT
    using ::std::thread::hardware_concurrency;  // NOLINT
    using ::std::thread::joinable;              // NOLINT
    using ::std::thread::native_handle;         // NOLINT

    using ::std::thread::detach;  // NOLINT
    using ::std::thread::join;    // NOLINT

    void swap(thread& other) noexcept {
        this->::std::thread::swap(other);  // NOLINT
    }
};
```
### 异常处理
在启动函数`mongoDbMain`中调用了`setupSignalHandlers` =>`setupSynchronousSignalHandlers`=>`setupStackTraceSignalAction` =>`initialize`。代码在[stacktrace_threads.cpp](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/util/stacktrace_threads.cpp),

```cpp
void initialize(int signal) {
    stateSingleton->setSignal(signal);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    // 对应的handler函数
    sa.sa_sigaction = [](int, siginfo_t* si, void*) { stateSingleton->action(si); };
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    if (sigaction(signal, &sa, nullptr) != 0) {
        int savedErr = errno;
        LOGV2_FATAL(31376,
                    "Failed to install sigaction for signal {signal}: {error}",
                    "Failed to install sigaction for signal",
                    "signal"_attr = signal,
                    "error"_attr = strerror(savedErr));
    }
}

void State::action(siginfo_t* si) {
    switch (si->si_code) {
        case SI_USER:
        case SI_QUEUE:
            // Received from outside. Forward to signal processing thread if there is one.
            if (int sigTid = _processingTid.load(std::memory_order_acquire); sigTid != -1)
                tgkill(getpid(), sigTid, si->si_signo);
            break;
        case SI_TKILL:
            // 
            // Users should call the toplevel printAllThreadStacks function.
            // An SI_TKILL could be a raise(SIGUSR2) call, and we ignore those.
            // Received from the signal processing thread.
            // Submit this thread's backtrace to the results stack.
            if (ThreadBacktrace* msg = acquireBacktraceBuffer(); msg != nullptr) {
                msg->tid = gettid();
                msg->size = rawBacktrace(msg->addrs, msg->capacity);
                postBacktrace(msg);
            }
            break;
    }
}
```

### 修改线程名
- `windows`没有长度限制
- `mac`最大为MAXTHREADNAMESIZE，超过以...代替
- `linux`最大15，超出则取前七位+.+后七位

```cpp
void setThreadName(StringData name) {
    invariant(mongoInitializersHaveRun);
    threadNameStorage = name.toString();
    threadName = threadNameStorage;

#if defined(_WIN32)
    setWindowsThreadName(GetCurrentThreadId(), threadNameStorage.c_str());
#elif defined(__APPLE__)
    std::string threadNameCopy = threadNameStorage;
    if (threadNameCopy.size() > MAXTHREADNAMESIZE) {
        threadNameCopy.resize(MAXTHREADNAMESIZE - 4);
        threadNameCopy += "...";
    }
    int error = pthread_setname_np(threadNameCopy.c_str());
    if (error) {
        LOGV2(23102,
              "Ignoring error from setting thread name: {error}",
              "Ignoring error from setting thread name",
              "error"_attr = errnoWithDescription(error));
    }
#elif defined(__linux__) && defined(MONGO_CONFIG_HAVE_PTHREAD_SETNAME_NP)
    // linux 在主线程中不支持修改名称 会影响pgrep/pkill
    if (getpid() != syscall(SYS_gettid)) {
        int error = 0;
        if (threadName.size() > 15) {
            std::string shortName = str::stream()
                << threadName.substr(0, 7) << '.' << threadName.substr(threadName.size() - 7);
            error = pthread_setname_np(pthread_self(), shortName.c_str());
        } else {
            error = pthread_setname_np(pthread_self(), threadName.rawData());
        }

        if (error) {
            LOGV2(23103,
                  "Ignoring error from setting thread name: {error}",
                  "Ignoring error from setting thread name",
                  "error"_attr = errnoWithDescription(error));
        }
    }
#endif
}

```
### 创建子线程
detach: 从 thread 对象分离执行的线程，允许执行独立地持续。一旦线程退出，则释放所有分配的资源
```cpp
stdx::thread(signalProcessingThread, rotate).detach();
```

## 子线程(部分) 后期再逐渐加上
### SignalHandler
信号监听处理线程
### SignalHandler
重复休眠指定的毫秒数，并唤醒存储当前时间。
### OCSPMangerHttp
OCSP 证书管理相关的线程。
### conxxx
客户端连接的线程。

