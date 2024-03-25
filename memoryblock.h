#pragma once

#include <list>
#include <vector>

typedef void *Addr;

struct MemoryBlock {
    void print() {};
    uint bucket{0};
};

using StackIterator = std::list<MemoryBlock>::iterator;

struct IteratorContainer {
    // IteratorContainer(Addr addr, size_t ds_num) : addr_{addr}, ds_num_{ds_num} {}
    Addr                       addr_;
    size_t                     ds_num_;
    std::vector<StackIterator> iterators_;
};
