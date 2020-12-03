#include <cstdio>
#include <TLSF.h>
#include <set>
#include <random>


int main() {
    ugi::tlsf::Allocator allocator(1024*256);
    
    std::set<uint32_t> allocations;

    std::default_random_engine randEngine;
    std::uniform_int_distribution<uint32_t> randRange(96, 1024);

    while(true) {
        uint32_t randSize = randRange(randEngine);
        randSize = (randSize + 15)&~(15);
        uint32_t rst = allocator.alloc(randSize);
        if(rst == 0xffffffff) {
            break;
        }
        allocations.insert(rst);
    }

    while(!allocations.empty()) {
        auto size = allocations.size();
        std::uniform_int_distribution<uint32_t> range(0, size-1);
        auto position = range(randEngine);
        auto iter = allocations.begin();
        for(size_t i = 0; i<position; ++i) {
            ++iter;
        }
        allocator.free(*iter);
        allocations.erase(iter);
    }
    printf("hello,world!");
    return 0;
}