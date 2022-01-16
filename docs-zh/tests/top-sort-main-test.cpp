#include <algorithm>
#include <iostream>
#include <iostream>
#include <iterator>
#include <random>
#include <sstream>
#include <set>  
#include <unordered_map>
#include <assert.h>

class InitializerDependencyNode {
public:
    bool isInitialized() const {
        return initialized;
    }
    void setInitialized(bool value) {
        initialized = value;
    };
    // InitializerFunction const& getInitializerFunction() const {
    //     return initFn;
    // }
    // DeinitializerFunction const& getDeinitializerFunction() const {
    //     return deinitFn;
    // }

public:
    // InitializerFunction initFn;
    // DeinitializerFunction deinitFn;
    std::set<std::string> prerequisites;
    bool initialized{false};
};

template <typename Iter>
void throwGraphContainsCycle(Iter first, Iter last, std::vector<std::string>& names) {
    std::string desc = "Cycle in dependency graph: ";
    std::transform(first, last, std::back_inserter(names), [](auto& e) { return e->name(); });
    names.push_back((*first)->name());  // Tests awkwardly want first element to be repeated.
    // strAppendJoin(desc, " -> ", names);
}
typedef std::unordered_map<std::string, InitializerDependencyNode> NodeMap;
typedef NodeMap::value_type Node;

NodeMap nodeMap;

void addInitializer(NodeMap &_nodes ,std::string name,
                                                  std::vector<std::string> prerequisites,
                                                  std::vector<std::string> dependents) {
    
    InitializerDependencyNode& newNode = _nodes[name];
    

    for (size_t i = 0; i < prerequisites.size(); ++i) {
        newNode.prerequisites.insert(prerequisites[i]);
    }

    for (size_t i = 0; i < dependents.size(); ++i) {
        _nodes[dependents[i]].prerequisites.insert(name);
    }
}

int topSort(NodeMap _nodes , std::vector<std::string>* sortedNames) {
    
    // Topological sort via repeated depth-first traversal.
    // All nodes must have an initFn before running topSort, or we return BadValue.
    struct Element {
        const std::string& name() const {
            return node->first;
        }
        const Node* node;
        std::vector<Element*> children;
        std::vector<Element*>::iterator membership;  // Position of this in `elements`.
    };

    std::vector<Element> elementsStore;
    std::vector<Element*> elements;

    // Swap the pointers in the `elements` vector that point to `a` and `b`.
    // Update their 'membership' data members to reflect the change.
    auto swapPositions = [](Element& a, Element& b) {
        using std::swap;
        swap(*a.membership, *b.membership);
        swap(a.membership, b.membership);
    };

    elementsStore.reserve(_nodes.size());
    std::transform(
        _nodes.begin(), _nodes.end(), std::back_inserter(elementsStore), [](const Node& n) {
            return Element{&n};
        });

    // Wire up all the child relationships by pointer rather than by string names.
    {
        std::unordered_map<std::string, Element*> byName;
        for (Element& e : elementsStore)
            byName[e.name()] = &e;
        for (Element& element : elementsStore) {
            const auto& prereqs = element.node->second.prerequisites;
            std::transform(prereqs.begin(),
                           prereqs.end(),
                           std::back_inserter(element.children),
                           [&](std::string childName) {
                               auto iter = byName.find(childName);
                               assert(iter != byName.end());
                               return iter->second;
                           });
        }
    }

    elements.reserve(_nodes.size());
    std::transform(elementsStore.begin(),
                   elementsStore.end(),
                   std::back_inserter(elements),
                   [](auto& e) { return &e; });

    // Shuffle the inputs to improve test coverage of undeclared dependencies.
    // {
    //     std::random_device slowSeedGen;
    //     std::mt19937 generator(slowSeedGen());
    //     std::shuffle(elements.begin(), elements.end(), generator);
    //     for (Element* e : elements)
    //         std::shuffle(e->children.begin(), e->children.end(), generator);
    // }

    // Initialize all the `membership` iterators. Must only happen after shuffle.
    for (auto iter = elements.begin(); iter != elements.end(); ++iter)
        (*iter)->membership = iter;

    // The `elements` sequence is divided into 3 regions:
    // elements:        [ sorted | unsorted | stack ]
    //          unsortedBegin => [          )  <= unsortedEnd
    // Each element of the stack region is a prerequisite of its neighbor to the right. Through
    // 'swapPositions' calls and boundary increments, elements will transition from unsorted to
    // stack to sorted. The unsorted region shinks to ultimately become an empty region on the
    // right. No other moves are permitted.
    auto unsortedBegin = elements.begin();
    auto unsortedEnd = elements.end();

    while (unsortedBegin != elements.end()) {
        if (unsortedEnd == elements.end()) {
            // The stack is empty but there's more work to do. Grow the stack region to enclose
            // the rightmost unsorted element. Equivalent to pushing it.
            --unsortedEnd;
        }
        auto top = unsortedEnd;
        auto& children = (*top)->children;
        if (!children.empty()) {
            Element* picked = children.back();
            children.pop_back();
            if (picked->membership < unsortedBegin)
                continue;
            if (picked->membership >= unsortedEnd) {  // O(1) cycle detection
                sortedNames->clear();
                throwGraphContainsCycle(unsortedEnd, elements.end(), *sortedNames);
            }
            swapPositions(**--unsortedEnd, *picked);  // unsorted push to stack
            continue;
        }
        swapPositions(**unsortedEnd++, **unsortedBegin++);  // pop from stack to sorted
    }
    sortedNames->clear();
    sortedNames->reserve(_nodes.size());
    std::transform(elements.begin(),
                   elements.end(),
                   std::back_inserter(*sortedNames),
                   [](const Element* e) { return e->name(); });
    return 1;
}

template<typename T>
std::ostream & operator<<(std::ostream & os, std::vector<T> vec)
{
    os<<"{";
    std::copy(vec.begin(), vec.end(), std::ostream_iterator<T>(os,", "));
    os<<"}";
    return os;
}
int main() {
    /**
     * 
        |节点  |被依赖节点 |依赖节点|
        |---- |----    |----   |
        |node2|  空    | node0 |
        |node0|  空    |     空 |
        |node3| node2  | node0 |
        
     * 
     */

    std::vector<std::string> node3Dep;
    // node3Dep.push_back("node2");

    std::vector<std::string> node3Pre;
    // node3Pre.push_back("node0");



    std::vector<std::string> node0Dep;
    // node0Dep.push_back("node2");
    std::vector<std::string> node0Pre;

    std::vector<std::string> node2Dep;
    
    std::vector<std::string> node2Pre;
    // node2Pre.push_back("node0");
    
    addInitializer(nodeMap, "node3", node3Pre, node3Dep);
    addInitializer(nodeMap, "node2", node2Pre, node2Dep);
    addInitializer(nodeMap, "node0", node0Pre, node0Dep);


    std::vector<std::string> names;
    topSort(nodeMap, &names);
    std::cout<<names<<std::endl;
    return 1;
} 


// #include <cstdio>
// #include <iostream>
// #include <cstdio>
// #include <iostream>
// #include <vector>
// #include <list>
// #include <map>
// #include <set>
// #include <string>
// #include <algorithm>
// #include <functional>
// #include <memory>
// typedef std::function<int(int)> InitializerFunction;
// class A {
//   public:
//   A(int a) {
//     std::cout<<"11111111"<<std::endl;
//     std::cout<<"11111111"<<std::endl;
//   }
// };
// #define MONGO_INITIALIZER_GENERAL(NAME, PREREQUISITES, DEPENDENTS)                        \
//     int MONGO_INITIALIZER_FUNCTION_NAME_(NAME)(char); \
// 	namespace {A _mongoInitializerRegisterer_##NAME(1);} \
//     int MONGO_INITIALIZER_FUNCTION_NAME_(NAME)
// #define MONGO_INITIALIZER_FUNCTION_NAME_(NAME) _mongoInitializerFunction_##NAME
// MONGO_INITIALIZER_GENERAL(ForkServer, ("EndStartupOptionHandling"), ("default"))
// (int context) {
//     printf("=========");
//     return context;
// }
// int main()
// {
//     // A a(nullptr);
//     std::cout<<"22222"<<"==="<<std::endl;

// }