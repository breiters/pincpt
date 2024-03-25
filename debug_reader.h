#pragma once
/*
    DW_AT_location:


    DW_OP_fbreg -32
    A variable which is entirely stored -32 bytes from the stack frame base


    DW_OP_reg3 DW_OP_piece 4 DW_OP_reg10 DW_OP_piece 2
    A variable whose first four bytes reside in register 3 and whose next two
   bytes reside in register 10

*/

// #define eprintf(fmt, ...) fprintf(stderr, fmt, ...)

#include <cstddef>

struct parameter {
    int   type;
    char *name;
};

struct function {
    int               type;
    char             *name;
    size_t            num_parameters;
    struct parameter *parameters;
};

class DebugReader
{
public:
    void read_debug(const char *file_name);

    
};

extern void read_debug(const char *file_name);