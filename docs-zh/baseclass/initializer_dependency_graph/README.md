# InitializerDependencyGraph
`Initializer`的依赖管理类。这个类最主要的作用的就是解决初始化组件先后顺序。比如`A`依赖`B`,那么在执行`A`的时候需要先执行
`B`。代码主要在[initializer_dependency_graph.h](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/base/initializer_dependency_graph.h)和[initializer_dependency_graph.cpp](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/base/initializer_dependency_graph.cpp)
## 头文件
- InitializerDependencyNode:  `Initializer`节点类
- InitializerDependencyGraph: `Initializers`管理类
```cpp
/**
 * 创建 initializer时候调用的函数
 */
typedef std::function<Status(InitializerContext*)> InitializerFunction;

/**
 * 释放 initializer时候调用的函数
 */
typedef std::function<Status(DeinitializerContext*)> DeinitializerFunction;

// ====================InitializerDependencyNode
class InitializerDependencyNode {
    friend class InitializerDependencyGraph;
public:
    bool isInitialized() const {
        return initialized;
    }
    void setInitialized(bool value) {
        initialized = value;
    };

    InitializerFunction const& getInitializerFunction() const {
        return initFn;
    }
    DeinitializerFunction const& getDeinitializerFunction() const {
        return deinitFn;
    }

private:
    /**
     * 对应InitializerFunction
     */
    InitializerFunction initFn;
    /**
     * 对应DeinitializerFunction
     */
    DeinitializerFunction deinitFn;
    /**
     * 依赖的Initializer 名称
     */
    stdx::unordered_set<std::string> prerequisites;
    /**
     * 是否已经执行过initFn
     */
    bool initialized{false};
};

// ====================InitializerDependencyGraph
class InitializerDependencyGraph {
    InitializerDependencyGraph(const InitializerDependencyGraph&) = delete;
    InitializerDependencyGraph& operator=(const InitializerDependencyGraph&) = delete;

public:
    InitializerDependencyGraph();
    ~InitializerDependencyGraph();
    /**
     * 添加Initializer
     */
    Status addInitializer(std::string name,
                          InitializerFunction initFn,
                          DeinitializerFunction deinitFn,
                          std::vector<std::string> prerequisites,
                          std::vector<std::string> dependents);
    /**
     * 按name获取Initializer
     */                      
    InitializerDependencyNode* getInitializerNode(const std::string& name);
    /**
     * 按依赖进行排序
     */
    Status topSort(std::vector<std::string>* sortedNames) const;

private:
    typedef stdx::unordered_map<std::string, InitializerDependencyNode> NodeMap;
    typedef NodeMap::value_type Node;
    /**
     * Initializer的节点map
     */
    NodeMap _nodes;
};
```
## 添加
添加`Initializer`的做法比较另类，看起来比较费劲。先看宏定义[init.h](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/base/init.h)。可以重点看下`MONGO_INITIALIZER_GENERAL`的宏定义，在使用宏的基本在各个.h或者.cpp中(如db.cpp)，大家搜一下就能发现了。
```cpp
/**
 * 空的依赖节点 => ()
 */
#define MONGO_NO_PREREQUISITES ()

/**
 * 空的被依赖节点 => ()
 */
#define MONGO_NO_DEPENDENTS ()

/**
 * 默认的依赖节点 =>  (default)
 */
#define MONGO_DEFAULT_PREREQUISITES (::mongo::defaultInitializerName().c_str())


/**
 * 多个参数
 */
#define MONGO_INITIALIZER_STRIP_PARENS_(...) __VA_ARGS__

/**
 * 定义了生成固定前缀的宏
 * 如: MONGO_INITIALIZER_FUNCTION_NAME_(TEST)  预编译后就变成了MONGO_INITIALIZER_FUNCTION_NAME_TEST
 */
#define MONGO_INITIALIZER_FUNCTION_NAME_(NAME) _mongoInitializerFunction_##NAME

/**
 * MONGO_INITIALIZER_GENERAL 这个宏比较难以容易  我们拆分一下
 * 1 ::mongo::Status MONGO_INITIALIZER_FUNCTION_NAME_(NAME)(::mongo::InitializerContext*);
 *    定义了一个函数(可能是约束的是下面的代码)
 *    返回值为mongo::Status 
 *    名称为_mongoInitializerFunction_##NAME
 *    参数只有一个为mongo::InitializerContext*
 * 2 namespace{  mongo::GlobalInitializerRegisterer _mongoInitializerRegisterer_##NAME(...)  } 
 *    定义了一个匿名的 防止和外部的命名空间的变量重名了
 *    创建了名称为_mongoInitializerRegisterer_##NAME的GlobalInitializerRegisterer对象
 *    传的参数有四个依次为MONGO_INITIALIZER_FUNCTION_NAME_(NAME)、null、PREREQUISITES、DEPENDENTS
 * 3 替换宏为mongo::Status MONGO_INITIALIZER_FUNCTION_NAME_(NAME)
 * 如：
 * MONGO_INITIALIZER_GENERAL(main, ("pre"), ("dep"))(mongo::InitializerContext* context) {
 *   printf("=========");
 *   return Status::OK;
 * }
 * 预编译之后就变成了
 * mongo::Status _mongoInitializerFunction_main(mongo::InitializerContext* context){
 *  printf("=========");
 *  return Status::OK;
 * }
 * 同时在运行阶段会new一个_mongoInitializerRegisterer_main, 类型为GlobalInitializerRegisterer的变量
 */
#define MONGO_INITIALIZER_GENERAL(NAME, PREREQUISITES, DEPENDENTS)                        \
    ::mongo::Status MONGO_INITIALIZER_FUNCTION_NAME_(NAME)(::mongo::InitializerContext*); \
    namespace {                                                                           \
    ::mongo::GlobalInitializerRegisterer _mongoInitializerRegisterer_##NAME(              \
        std::string(#NAME),                                                               \
        mongo::InitializerFunction(MONGO_INITIALIZER_FUNCTION_NAME_(NAME)),               \
        mongo::DeinitializerFunction(nullptr),                                            \
        std::vector<std::string>{MONGO_INITIALIZER_STRIP_PARENS_ PREREQUISITES},          \
        std::vector<std::string>{MONGO_INITIALIZER_STRIP_PARENS_ DEPENDENTS});            \
    }                                                                                     \
    ::mongo::Status MONGO_INITIALIZER_FUNCTION_NAME_(NAME)

/**
 * 不需要function的定义 
 * 如: MONGO_INITIALIZER_GROUP(main, ("pre"), ("dep"))
 */
#define MONGO_INITIALIZER_GROUP(NAME, PREREQUISITES, DEPENDENTS)                               \
    MONGO_INITIALIZER_GENERAL(NAME, PREREQUISITES, DEPENDENTS)(::mongo::InitializerContext*) { \
        return ::mongo::Status::OK();                                                          \
    }

/**
 * 空的依赖节点和空的被依赖节点, 只有name
 */
#define MONGO_INITIALIZER(NAME) \
    MONGO_INITIALIZER_WITH_PREREQUISITES(NAME, MONGO_DEFAULT_PREREQUISITES)

/**
 * 自定义依赖节点和被依赖节点
 */
#define MONGO_INITIALIZER_WITH_PREREQUISITES(NAME, PREREQUISITES) \
    MONGO_INITIALIZER_GENERAL(NAME, PREREQUISITES, MONGO_NO_DEPENDENTS)

```
上面的宏定义中用到了`GlobalInitializerRegisterer`， 简单看下构造函数部分，类定义部分比较简单不细说了。两个文件[global_initializer_registerer.h](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/base/global_initializer_registerer.h)和[global_initializer_registerer.cpp](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/base/global_initializer_registerer.cpp)。

```cpp
/**
 * 构造函数主要就是调用到了addInitializer方法
 * getGlobalInitializer函数在global_initializer.cpp中定义，用static定义了个静态的局部变量，所以这里Initializer的对象只有一个theGlobalInitializer。
 * 具体代码如下
 * Initializer& getGlobalInitializer() {
 *   static Initializer theGlobalInitializer;
 *   return theGlobalInitializer;
 *}
 * 
 */
GlobalInitializerRegisterer::GlobalInitializerRegisterer(std::string name,
                                                         InitializerFunction initFn,
                                                         DeinitializerFunction deinitFn,
                                                         std::vector<std::string> prerequisites,
                                                         std::vector<std::string> dependents) {
    Status status = getGlobalInitializer().getInitializerDependencyGraph().addInitializer(
        std::move(name),
        std::move(initFn),
        std::move(deinitFn),
        std::move(prerequisites),
        std::move(dependents));

    if (Status::OK() != status) {
        std::cerr << "Attempt to add global initializer failed, status: " << status << std::endl;
        ::abort();
    }
}
```
addInitializer实现。`prerequisites`为执行当前节点必须执行完里面节点，`dependents`为执行被依赖节点必须执行完当前节点。
```cpp
Status InitializerDependencyGraph::addInitializer(std::string name,
                                                  InitializerFunction initFn,
                                                  DeinitializerFunction deinitFn,
                                                  std::vector<std::string> prerequisites,
                                                  std::vector<std::string> dependents) {
    if (!initFn)
        return Status(ErrorCodes::BadValue, "Illegal to supply a NULL function");

    InitializerDependencyNode & newNode = _nodes[name];
    if (newNode.initFn) {
        return Status(ErrorCodes::Error(50999), name);
    }

    newNode.initFn = std::move(initFn);
    newNode.deinitFn = std::move(deinitFn);
    // 把该节点的依赖节点复制到prerequisites prerequisites是个数组
    for (size_t i = 0; i < prerequisites.size(); ++i) {
        newNode.prerequisites.insert(prerequisites[i]);
    }
    // 找到关联的节点 使当前节点为被依赖节点的依赖
    for (size_t i = 0; i < dependents.size(); ++i) {
        _nodes[dependents[i]].prerequisites.insert(name);
    }

    return Status::OK();
}
```

## topSort
说完添加部分的代码，再说下排序部分。代码都在[initializer_dependency_graph.cpp](https://github.com/jsfrog/mongo/blob/v4.4/src/mongo/base/initializer_dependency_graph.cpp)中。

我们先举个例子大概说一下要完成的事。
|节点  |关联节点 |依赖节点|
|---- |----    |----   |
|node2|  空    | node0 |
|node0|  空    |     空 |
|node3| node2  | node0 |

处理的结果如下
```js
_nodes = {
    node3: {
        prerequisites: ["node0"]
    },
    node0: {
        prerequisites: []
    },
    node2: {
        prerequisites: ["node0","node3"]
    }
}
sortedNames = ["node0", "node3", "node2"];
```
### 算法描述
- 数组分为`sorted`、`unsorted`、`stack`三部分
- 有两个指针分别指向数组的下标，`unsortedBegin`指向`unsorted`的左侧，`unsortedEnd`指向`unsorted`的右侧
- 初始时，`unsortedBegin`为数组起始位置，`unsortedEnd`为结束位置
- 当发现依赖的节点属于`sorted`，那么就不处理
- 当发现依赖的节点属于`unsorted`，那么就把依赖的节点放到`stack`区域
- 当所有的依赖节点都处理完成了，那么就把节点放到`sorted`区域
- 最终只有`sorted`区域

### 实现代码
```cpp
namespace mongo {

Status InitializerDependencyGraph::topSort(std::vector<std::string>* sortedNames) const try {
    /**
     * 在Node又包了一层Element
     */
    struct Element {
        // 节点名称
        const std::string& name() const {
            return node->first;
        }
        // 节点
        const Node* node;
        // 依赖的节点列表
        std::vector<Element*> children;
        // 依赖节点的指针列表
        std::vector<Element*>::iterator membership;
    };
    std::vector<Element> elementsStore;
    std::vector<Element*> elements;
    auto swapPositions = [](Element& a, Element& b) {
        using std::swap;
        swap(*a.membership, *b.membership);
        swap(a.membership, b.membership);
    };
    // 增加容量 大小为_nodes的长度
    elementsStore.reserve(_nodes.size());
    // 遍历_nodes 同时创建Element 插入到elementsStore [Element{node:Node*, children: []， membership： null}]
    std::transform(
        _nodes.begin(), _nodes.end(), std::back_inserter(elementsStore), [](const Node& n) {
            uassert(ErrorCodes::BadValue,
                    "No implementation provided for initializer {}"_format(n.first),
                    n.second.initFn);
            return Element{&n};
        });

    {
        // 以name为key value为对应Element的指针地址
        StringMap<Element*> byName;
        // 先按elementsStore进行byName初始化
        for (Element& e : elementsStore)
            byName[e.name()] = &e;
        for (Element& element : elementsStore) {
            const auto& prereqs = element.node->second.prerequisites;
            // 遍历prereqs 把依赖的node指针添加到children中
            std::transform(prereqs.begin(),
                           prereqs.end(),
                           std::back_inserter(element.children),
                           [&](StringData childName) {
                               auto iter = byName.find(childName);
                               uassert(ErrorCodes::BadValue,
                                       "Initializer {} depends on missing initializer {}"_format(
                                           element.node->first, childName),
                                       iter != byName.end());
                               return iter->second;
                           });
        }
    }
    // 此时elementsStore [Element{node:Node*, children: [Element*， Element*]， membership: null}]
    // 增加容量 大小为_nodes的长度
    elements.reserve(_nodes.size());
    // 复制elementsStore中每个element的指针到elements
    // 此时elements [Element*, Element*]
    std::transform(elementsStore.begin(),
                   elementsStore.end(),
                   std::back_inserter(elements),
                   [](auto& e) { return &e; });

    // 准备好了数据后 接下来就是随机打乱elements 和 children的顺序
    {
        std::random_device slowSeedGen;
        std::mt19937 generator(slowSeedGen());
        std::shuffle(elements.begin(), elements.end(), generator);
        for (Element* e : elements)
            std::shuffle(e->children.begin(), e->children.end(), generator);
    }
    // 初始化membership membership可以理解为每个element在elements中的位置
    //  那么此时 
    // elementsStore [Element{node:Node*, children: [Element*， Element*]， membership: indexElements}]
    // elements [Element*, Element*]
    for (auto iter = elements.begin(); iter != elements.end(); ++iter)
        (*iter)->membership = iter;
    // 未排序的第一个元素 默认elements中第一个element地址
    auto unsortedBegin = elements.begin();
    // 未排序的最后一个元素 elements中最后一个element的下一个地址  这里是0x21
    auto unsortedEnd = elements.end();
    // 遍历所有element
    while (unsortedBegin != elements.end()) {

        if (unsortedEnd == elements.end()) {
            // 说明已经到结尾了  --unsortedEnd就是往前移动一位 
            // 移动以后unsortedEnd指向的就是elements的最后一个元素
            --unsortedEnd;
        }
        auto top = unsortedEnd;
        auto& children = (*top)->children;
        if (!children.empty()) {
            // 弹出children最后一个element 
            Element* picked = children.back();
            children.pop_back();
            // picked在sorted的区域内 则continue
            if (picked->membership < unsortedBegin)
                continue;
            // picked在unsorted的右边 抛出异常
            if (picked->membership >= unsortedEnd) {
                sortedNames->clear();
                throwGraphContainsCycle(unsortedEnd, elements.end(), *sortedNames);
            }
            // 交换picked和unsortedEnd前一个 因为此时的picked属于unsorted区域 
            swapPositions(**--unsortedEnd, *picked);
            continue;
        }
        // 交换unsortedEnd和unsortedBegin  因为此时的unsortedEnd children为空了
        swapPositions(**unsortedEnd++, **unsortedBegin++);
    }
    sortedNames->clear();
    sortedNames->reserve(_nodes.size());
    // 复制elements中的名称
    std::transform(elements.begin(),
                   elements.end(),
                   std::back_inserter(*sortedNames),
                   [](const Element* e) { return e->name(); });
    return Status::OK();
} catch (const DBException& ex) {
    return ex.toStatus();
}

} 
```
### 模拟排序时候的实例 仅供参考
```text

// elements:        [ sorted | unsorted | stack ]
//          unsortedBegin => [          )  <= unsortedEnd
[
    {node: "node3", children: [0x42e4d0], membership:  0x42e480 } => 0x42e480,
    {node: "node2", children: [0x42e4d0, 0x42e480], membership:  0x42e4a8 } => 0x42e4a8,
    {node: "node0", children: [], membership:  0x42e4d0 } => 0x42e4d0,
]
第一次:
    elements=[0x42e480， 0x42e4a8, 0x42e4d0]
    unsortedBegin=0x42e480  top=unsortedEnd=0x42e4d0
    swap 
        swapPositions(**unsortedEnd++, **unsortedBegin++)
        0x42e4d0 <=> 0x42e480
    elements=[0x42e4d0, 0x42e4a8, 0x42e480]
第二次
    elements=[0x42e4d0, 0x42e4a8, 0x42e480]
    unsortedBegin=0x42e4a8  top=unsortedEnd=0x42e480
    picked=0x42e4d0
    对比index unsortedBegin > picked  什么也不做, 因为unsortedBegin的是排好顺序的
第三次
    elements=[0x42e4d0, 0x42e4a8, 0x42e480]
    unsortedBegin=0x42e4a8  top=unsortedEnd=0x42e480
    swap 
        swapPositions(**unsortedEnd++, **unsortedBegin++)
        0x42e480 <=> 0x42e4a8
    elements=[0x42e4d0, 0x42e480, 0x42e4a8]
第四次
    elements=[0x42e4d0, 0x42e480, 0x42e4a8]
    unsortedBegin=0x42e4a8  top=unsortedEnd=0x42e4a8
    picked=0x42e4d0
    对比index unsortedBegin > picked  什么也不做, 因为unsortedBegin的是排好顺序的

第五次
    elements=[0x42e4d0, 0x42e480, 0x42e4a8]
    unsortedBegin=0x42e4a8  top=unsortedEnd=0x42e4a8
    swap 
        swapPositions(**unsortedEnd++, **unsortedBegin++)
        0x42e4a8 <=> 0x42e4a8
第六次
    unsortedBegin=0x20 退出循环 


```