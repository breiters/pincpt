#pragma once

#include <string>
#include <vector>
#include <iostream>

enum DATASTRUCTS : size_t { RD_NO_DATASTRUCT = 0u };
typedef void *Addr;

class Datastruct
{
public:
    Datastruct() : address{(void *)0x0}, nbytes{0UL}, col{0U}, line{0U}, is_freed{false} {};
    void *address;
    size_t nbytes;
    int col;
    int line;
    unsigned long access_count{0};
    bool is_freed{false};
    std::string allocator;
    std::string file_name;

    inline void print(void)
    {
        std::cout << "============================\n";
        std::cout << "bytes allocated by " << allocator << "(): " << nbytes << '\n';
        std::cout << "in: " << file_name << " line: ?" << line << "? col: " << col
             << '\n';
        std::cout << "located at address: " << std::hex << address << std::dec << '\n';
        std::cout << "============================\n";
    }
    // inline void print(void) {}

    static std::vector<Datastruct> datastructs;
    static std::vector<std::vector<int>> indices_of;


    static size_t datastruct_num(Addr addr);
    static void register_datastruct(Datastruct &ds);

private:
    static void combine(size_t ds_num);
};
