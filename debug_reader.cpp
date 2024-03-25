#include "debug_reader.h"

#include "config.h"
#include "datastructs.h"

#include "pin.H"

#include <set>
#include <string>
#include <unordered_map>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static std::unordered_map<std::string, std::set<std::pair<std::string, std::size_t>>> g_fnargs{};

extern std::string g_application_name;
extern unsigned    g_max_threads;

int             *ptr_types     = NULL;
struct function *functions     = NULL;
size_t           num_ptr_types = 0;
size_t           num_fn        = 0;

void print_fnargmap()
{
    // generate filename
    constexpr size_t FILENAME_SIZE = 256u;
    char             csv_filename[FILENAME_SIZE];
    snprintf(csv_filename, FILENAME_SIZE, "pincpt-%s-%04ut-fnargs.csv", g_application_name.c_str(), g_max_threads);

    FILE *csv_out = fopen(csv_filename, "w");
    fprintf(csv_out, "%s", "region,var,ds\n");

    for (auto &kv : g_fnargs) {
        // std::cout << kv.first << ":\n    ";
        for (auto &pair : kv.second) {
            // std::cout << pair.first << ": ds=" << pair.second << '\n';
            fprintf(csv_out, "%s,%s,%zu\n", kv.first.c_str(), pair.first.c_str(), pair.second);
        }
    }
    fclose(csv_out);
}

static char *last_token(char *line, const char *delim)
{
    strtok(line, delim);
    char *token, *prev = NULL;
    while ((token = strtok(NULL, delim)) != NULL) {
        prev = token;
    }
    return prev;
}

bool is_ptr_type(int type, int *ptr_types, size_t num_ptr_types)
{
    for (size_t i = 0; i < num_ptr_types; i++) {
        if (type == ptr_types[i]) {
            return true;
        }
    }
    return false;
}

void read_debug(const char *file_name)
{
    // objdump functions/variables
    const char *objdump = "objdump -g -Wi --dwarf-depth=3";
    int         size    = snprintf(NULL, 0, "%s %s", objdump, file_name) + 1;
    char       *cmd     = (char *)malloc(size);
    size                = snprintf(cmd, size, "%s %s", objdump, file_name);
    //   puts(cmd);
    FILE *stream = popen(cmd, "r");
    free(cmd);

    // parse functions/variables
    char   *line = NULL;
    size_t  len  = 0;
    ssize_t nread;
    bool    get_function  = false;
    bool    get_parameter = false;
    // bool get_type = false;
    while ((nread = getline(&line, &len, stream)) != -1) {
        if (strncmp(line, "    ", 4) != 0) {
            get_function  = false;
            get_parameter = false;
            // get_type = false;
        }

        if (strstr(line, "DW_TAG_pointer_type")) {
            num_ptr_types++;
            ptr_types = (int *)realloc(ptr_types, sizeof(int) * num_ptr_types);
            [[maybe_unused]] int ret =
                sscanf(line, " <1><%x>: Abbrev Number: %*d (DW_TAG_pointer_type) ", &ptr_types[num_ptr_types - 1]);
            assert(ret == 1);
        }

        if (strstr(line, "DW_TAG_restrict_type")) {
            num_ptr_types++;
            ptr_types = (int *)realloc(ptr_types, sizeof(int) * num_ptr_types);
            [[maybe_unused]] int ret =
                sscanf(line, " <1><%x>: Abbrev Number: %*d (DW_TAG_restrict_type) ", &ptr_types[num_ptr_types - 1]);
            assert(ret == 1);
        }

        //   if(sscanf(line, " "))
        if (strstr(line, "DW_TAG_subprogram")) {
            num_fn++;
            functions = (struct function *)realloc(functions, sizeof(struct function) * num_fn);

            functions[num_fn - 1].num_parameters = 0;
            functions[num_fn - 1].parameters     = NULL;
            get_function                         = true;
        }

        if (strstr(line, "DW_TAG_formal_parameter")) {
            functions[num_fn - 1].num_parameters++;
            functions[num_fn - 1].parameters = (struct parameter *)realloc(
                functions[num_fn - 1].parameters, sizeof(struct parameter) * functions[num_fn - 1].num_parameters);
            get_parameter = true;
        }

        if (strstr(line, "DW_AT_name")) {
            if (get_function) {
                functions[num_fn - 1].name = strdup(last_token(line, " "));
                strtok(functions[num_fn - 1].name, "\n");
                eprintf("function: %s\n", functions[num_fn - 1].name);
            }

            if (get_parameter) {
                functions[num_fn - 1].parameters[functions[num_fn - 1].num_parameters - 1].name =
                    strdup(last_token(line, " "));
                strtok(functions[num_fn - 1].parameters[functions[num_fn - 1].num_parameters - 1].name, "\n");
                eprintf("    parameter: %s\n",
                        functions[num_fn - 1].parameters[functions[num_fn - 1].num_parameters - 1].name);
            }
        }

        if (strstr(line, "DW_AT_type")) {
            int                  type = -1;
            [[maybe_unused]] int ret  = sscanf(last_token(line, " "), "<%x>", &type);
            assert(ret == 1);
            // printf("type: %d\n", type);
            if (get_function) {
                functions[num_fn - 1].type = type;
            }
            if (get_parameter) {
                functions[num_fn - 1].parameters[functions[num_fn - 1].num_parameters - 1].type = type;
                eprintf("        type: %x %s\n", type, is_ptr_type(type, ptr_types, num_ptr_types) ? "(pointer)" : "");
            }
        }
    }
    pclose(stream);
    free(line);
}

// needs -O1 or lower because of isra?
void Inspector(ADDRINT *addr, char *fn_name, char *var_name)
{
    // printf("[%s] %s: %p\n", fn_name, var_name, (void *)*addr);
    size_t i = 0;
    for (Datastruct &ds : Datastruct::datastructs) {
        // ds.print();
        if (*addr >= (ADDRINT)ds.address && *addr < (ADDRINT)ds.address + ds.nbytes && !ds.is_freed) {
            // printf("    data object: %d\n", i);
            // g_fnargs[std::pair<std::string, std::string>{fn_name, var_name}].insert(i);
            g_fnargs[fn_name].insert({var_name, i});
            // ds.print();
        }
        i++;
    }
    fflush(stdout);
}

void inspect_ptr_args(RTN rtn, std::string fname2)
{
    char *fname = strdup(fname2.c_str());
    char *dot   = strchr(fname, '.');
    if (dot) {
        *dot = '\0';
    }

    eprintf("search: %s\n", fname);
    if (fname == nullptr) {
        eprintf("call with nullptr\n");
        return;
    }
    for (size_t f = 0; f < num_fn; f++) {
        if (functions[f].name == nullptr) {
            // printf("!! nullptr\n"); // TODO!
            continue;
        }
        if (PIN_UndecorateSymbolName(std::string{fname}, UNDECORATION_NAME_ONLY) !=
            PIN_UndecorateSymbolName(std::string{functions[f].name}, UNDECORATION_NAME_ONLY)) {
            continue;
        }

        eprintf("found: %s\n", functions[f].name);

        for (size_t p = 0; p < functions[f].num_parameters; p++) {
            // printf("RTN_InsertCall(rtn);\n");
            eprintf("name: %s", functions[f].parameters[p].name);
            eprintf(" type: %x", functions[f].parameters[p].type);
            if (is_ptr_type(functions[f].parameters[p].type, ptr_types, num_ptr_types)) {
                // printf("RTN_InsertCall(rtn);\n");
                eprintf(" is ptr type");
                RTN_InsertCall(rtn,
                               IPOINT_BEFORE,
                               AFUNPTR(Inspector),
                               IARG_FUNCARG_ENTRYPOINT_REFERENCE,
                               p,
                               IARG_PTR,
                               functions[f].name,
                               IARG_PTR,
                               functions[f].parameters[p].name,
                               IARG_END);
            }
            eprintf("\n");
        }
        break;
    }

    free(fname);
}
