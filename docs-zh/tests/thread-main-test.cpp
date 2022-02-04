#include <thread>

#include <iostream>
#include <type_traits>
#include <cstdint>
#include <signal.h>

#define MONGO_HAS_SIGALTSTACK 1

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
        std::cout<<"install"<<std::endl;
        stack_t ss = {};
        ss.ss_sp = _stackStorage.get();
        ss.ss_flags = 0;
        ss.ss_size = _getStackSize();
        if (sigaltstack(&ss, nullptr)) {
            abort();
        }
    }

    void _uninstall() const {
        std::cout<<"uninstall"<<std::endl;
        stack_t ss = {};
        ss.ss_flags = SS_DISABLE;
        if (sigaltstack(&ss, nullptr)) {
            abort();
        }
    }

    // Signal stack consumption was measured in mongo/util/stacktrace_test.
    // 64 kiB is 4X our worst case, so that should be enough.
    //   .                                    signal handler action
    //   .  --use-libunwind : ----\       =============================
    //   .  --dbg=on        : -\   \      minimal |  print  | backtrace
    //   .                     =   =      ========|=========|==========
    //   .                     N   N :      4,344 |   7,144 |     5,096
    //   .                     Y   N :      4,424 |   7,528 |     5,160
    //   .                     N   Y :      4,344 |  13,048 |     7,352
    //   .                     Y   Y :      4,424 |  13,672 |     8,392
    //   ( https://jira.mongodb.org/secure/attachment/233569/233569_stacktrace-writeup.txt )
    static constexpr std::size_t kMongoMinSignalStackSize = std::size_t{64} << 10;

    std::unique_ptr<std::byte[]> _stackStorage = std::make_unique<std::byte[]>(_getStackSize());

#else   // !MONGO_HAS_SIGALTSTACK
    auto makeInstallGuard() const {
        struct Guard {
            ~Guard() {}  // needed to suppress 'unused variable' warnings.
        };
        return Guard{};
    }
#endif  // !MONGO_HAS_SIGALTSTACK
};

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

    /**
     * As of C++14, the Function overload for std::thread requires that this constructor only
     * participate in overload resolution if std::decay_t<Function> is not the same type as thread.
     * That prevents this overload from intercepting calls that might generate implicit conversions
     * before binding to other constructors (specifically move/copy constructors).
     *
     * NOTE: The `Function f` parameter must be taken by value, not reference or forwarding
     * reference, as it is used on the far side of the thread launch, and this ctor has to properly
     * transfer ownership to the far side's thread.
     */
    template <class Function,
              class... Args,
              std::enable_if_t<!std::is_same_v<thread, std::decay_t<Function>>, int> = 0>
    explicit thread(Function f, Args&&... args) noexcept
        : ::std::thread::thread(  // NOLINT
              [
                  sigAltStackController = SigAltStackController(),
                  f = std::move(f),
                  pack = std::make_tuple(std::forward<Args>(args)...)
              ]() mutable noexcept {
                  // ThreadSafetyContext::getThreadSafetyContext()->onThreadCreate();
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
void createThread() {
  std::cout<<"============"<<std::endl;
  std::cout<<"[TID:"<<std::this_thread::get_id()<<"]"<<std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(10));
}
int main() {
    thread(createThread).detach();

    
    while (true)
    {
        /* code */
    }
    
    
    return 0;
}