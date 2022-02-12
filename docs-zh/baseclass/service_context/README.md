# ServiceContext
`ServiceContext`是服务的上下文的类，如表示一个mongod、mongos服务。一个`ServiceContext`对象可以有多个客户端，每个客户端连接也有自己的`OperationContexts`。
## 类定义
`ServiceContext`继承与`Decorable`类，更多关于`Decorable`可以看之前的文章。
```cpp
class ServiceContext final : public Decorable<ServiceContext> {
}
```

## 成员属性
### _mutex


