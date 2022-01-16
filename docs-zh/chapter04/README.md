# 启动流程
接下来就是源码的第一篇了，主要看下mongodb server的启动流程。

## 先启动起来
发现启动了多个线程。mongod是server线程，connx是client连接线程，其他的后面慢慢接触。
![线程图](./images/线程图.png)
## 寻找main
一般都是`int main()`, 发现在db.cpp下面
```cpp
// 启动入口
int main(int argc, char* argv[], char** envp) {
    int exitCode = mongo::mongoDbMain(argc, argv, envp);
    mongo::quickExit(exitCode);
}

int mongoDbMain(int argc, char* argv[], char** envp) {
    // 单线程保证
    ThreadSafetyContext::getThreadSafetyContext()->forbidMultiThreading();
    // 注册退出函数
    registerShutdownTask(shutdownTask);
    // 处理信号量
    setupSignalHandlers();
    // 生成随机数？
    srand(static_cast<unsigned>(curTimeMicros64()));

    // 执行初始化
    Status status = mongo::runGlobalInitializers(argc, argv, envp);
    if (!status.isOK()) {
        LOGV2_FATAL_OPTIONS(
            20574,
            logv2::LogOptions(logv2::LogComponent::kControl, logv2::FatalMode::kContinue),
            "Error during global initialization: {error}",
            "Error during global initialization",
            "error"_attr = status);
        quickExit(EXIT_FAILURE);
    }
    // 匿名函数 生成ServiceContext对象
    auto* service = [] {
        try {
            auto serviceContextHolder = ServiceContext::make();
            auto* serviceContext = serviceContextHolder.get();
            setGlobalServiceContext(std::move(serviceContextHolder));

            return serviceContext;
        } catch (...) {
            auto cause = exceptionToStatus();
            LOGV2_FATAL_OPTIONS(
                20575,
                logv2::LogOptions(logv2::LogComponent::kControl, logv2::FatalMode::kContinue),
                "Error creating service context: {error}",
                "Error creating service context",
                "error"_attr = redact(cause));
            quickExit(EXIT_FAILURE);
        }
    }();
    // 给service设置各种属性
    setUpCollectionShardingState(service);
    setUpCatalog(service);
    setUpReplication(service);
    setUpObservers(service);
    service->setServiceEntryPoint(std::make_unique<ServiceEntryPointMongod>(service));

    ErrorExtraInfo::invariantHaveAllParsers();

    startupConfigActions(std::vector<std::string>(argv, argv + argc));
    cmdline_utils::censorArgvArray(argc, argv);
    // 检查pid文件是否有权限
    if (!initializeServerGlobalState(service))
        quickExit(EXIT_FAILURE);
    // 安全检查
    if (!initializeServerSecurityGlobalState(service))
        quickExit(EXIT_FAILURE);

    // 允许多线程
    ThreadSafetyContext::getThreadSafetyContext()->allowMultiThreading();

    // Per SERVER-7434, startSignalProcessingThread must run after any forks (i.e.
    // initializeServerGlobalState) and before the creation of any other threads
    // 启动 SignalHandler 线程 主要监听信号量 触发对应的事件
    startSignalProcessingThread();

    ReadWriteConcernDefaults::create(service, readWriteConcernDefaultsCacheLookupMongoD);
    // 箭头端口并启动线程
    ExitCode exitCode = initAndListen(service, serverGlobalParams.port);
    exitCleanly(exitCode);
    return 0;
}


```



