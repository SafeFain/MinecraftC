#include "threading/ThreadPool.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace std::chrono_literals;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main() {
    try {
        ThreadPool pool(1);
        std::promise<void> entered, release;
        auto gate=release.get_future().share();
        auto active=pool.enqueue([&]{entered.set_value();gate.wait();return 17;});
        require(entered.get_future().wait_for(2s)==std::future_status::ready,"worker starts bounded blocking task");
        require(pool.pendingCount()==0 && !pool.idle(),"active work is not confused with an empty queue");
        std::promise<void> waiting;
        auto idle=std::async(std::launch::async,[&]{waiting.set_value();pool.waitIdle();});
        waiting.get_future().wait();
        require(idle.wait_for(50ms)==std::future_status::timeout,"waitIdle waits for active work even when no tasks are queued");
        std::vector<int> order;
        for(int priority:{-3,8,2}) pool.enqueuePriority([&,priority]{order.push_back(priority);},priority);
        release.set_value();
        require(active.get()==17,"future delivers task result");
        require(idle.wait_for(2s)==std::future_status::ready,"waitIdle completes after drain"); idle.get();
        require(order==std::vector<int>({8,2,-3}),"single worker drains higher priority work first");
        auto error=pool.enqueue([]()->int{throw std::runtime_error("task fault");});
        bool failed=false;try{error.get();}catch(const std::runtime_error&){failed=true;}
        require(failed && pool.enqueue([]{return 23;}).get()==23,"task exception reaches future and worker remains usable");
        pool.waitIdle();
        std::promise<void> destructorEntered, destructorRelease;
        auto destructorGate=destructorRelease.get_future().share();
        auto owned=std::make_unique<ThreadPool>(1);
        auto blocked=owned->enqueue([&]{destructorEntered.set_value();destructorGate.wait();return 1;});
        destructorEntered.get_future().wait();
        auto queued=owned->enqueue([]{return 29;});
        std::promise<void> destroying;
        auto destroyed=std::async(std::launch::async,[owned=std::move(owned),&destroying]()mutable{destroying.set_value();owned.reset();});
        destroying.get_future().wait();
        require(destroyed.wait_for(50ms)==std::future_status::timeout,"destructor waits for outstanding work");
        destructorRelease.set_value();
        require(destroyed.wait_for(2s)==std::future_status::ready,"destructor drains and joins workers"); destroyed.get();
        require(blocked.get()==1 && queued.get()==29,"destructor preserves queued future results");
        std::cout<<"Thread pool priority, idle barrier, exceptions and shutdown passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
