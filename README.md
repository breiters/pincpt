# Pin Tool for Reuse Distance Calculation of Data Objects

This [Pin](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-dynamic-binary-instrumentation-tool.html) tool computes reuse distances w.r.t. data objects and generates reuse distance histograms. The results can be used to determine code regions where cache partitioning can reduce the number of occurring cache misses. The tool currently is configured with cache parameters and the cache partitioning facility (sector cache) of the A64FX processor.

## Build

Install Pin into your home, and set PIN_ROOT and PATH
to be able to run the "pin" binary.

Run `make` in folder pincpt.


## Run

In pincpt: `pin -t obj-intel64/pincpt.so -- <prog> <args>`

**Note:** you need to compile with debug information and disable certain interprocedural compiler optimizations and function inlining in your target program for `pincpt` to work correctly (to associate data objects to variable names of functions and to attribute cache misses to functions). With GCC, you need to set `-g -fno-inline -fno-ipa-sra -fno-ipa-cp` (tested w/ v11.4).

## Reuse Analysis

Running the `pincpt` tool generates two csv-files: `pincpt-<prog>-Nt.csv` and `pincpt-<prog>-Nt-fnargs.csv`, where N is the number of threads in the program. To analyze the reuse histograms, run `python pincpt.py -f pincpt-<prog>-Nt.csv`.

Possible output running a dense matrix-vector multiply (`dmvm`) program:

```bash
========================================================
  file: pincpt-dmvm-0001t.csv
    l1 misses (total): 783729
    l2 misses (total): 469731
========================================================
...
========================================================
region              dmvm
cache level            1
nways                  1
misses sc         312571
misses nosc       626490
reduction [%]  50.107584
    dmvm: isolate object "y" (aka object number 3)
    dmvm: isolate object "a" (aka object number 1)
========================================================
```

The output indicates that using cache partitioning of the L1 data cache with 1 cache way allocated to the data objects `y` and `a` (remaining cache space allocated to the remaining data objects) in the function `dmvm` decreases the number of L1 cache misses by ~50% (from 626490 to 312571).

## References

For further information, please read the corresponding [publication](https://link.springer.com/chapter/10.1007/978-3-031-29927-8_35).
