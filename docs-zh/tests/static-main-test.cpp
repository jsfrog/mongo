#include <iostream>
using namespace std;

class ThreadSafetyContext final {

public:
    ThreadSafetyContext();
    void forbidMultiThreading() noexcept;
    static ThreadSafetyContext* getThreadSafetyContext() noexcept;
};

ThreadSafetyContext::ThreadSafetyContext() {
    cout<<"===="<<endl;
}
ThreadSafetyContext* ThreadSafetyContext::getThreadSafetyContext() noexcept {
    static auto safetyContext = new ThreadSafetyContext();  // Intentionally leaked
    cout<<"========="<< safetyContext<<endl;
    return safetyContext;
}
void ThreadSafetyContext::forbidMultiThreading() noexcept {
    cout<<"===="<<endl;
}
int val = 10;
void a() {
    static int* a = &val;
    cout<<a<< "========a"<<endl;
}

int main() {

    auto p = ThreadSafetyContext::getThreadSafetyContext();
    auto p1 = ThreadSafetyContext::getThreadSafetyContext();
    cout<<p<<"========"<<p1<<endl;
    a();
    a();
    a();
    a();
}