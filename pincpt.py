import warnings
import sys
import argparse
from numpy.lib.arraysetops import isin, unique
import pandas as pd

pd.options.mode.chained_assignment = None  # default='warn'
warnings.filterwarnings("ignore", category=DeprecationWarning)
warnings.filterwarnings("ignore", category=FutureWarning)

NO_DATASTRUCT = '0'
#NO_DATASTRUCT = 0

""" output settings """

#verbose_data_objects = False
verbose_data_objects = True

#verbose_function_arguments = False
verbose_function_arguments = True

# min. ratio of total l1 or l2 cache misses in a region to include that region in output
REGION_THRESHOLD = 0.01
# REGION_THRESHOLD = 0.0

# min. cache miss reduction to include in output
REDUCTION_THRESHOLD = 0.01
# REDUCTION_THRESHOLD = 0.0

# min. ratio of cache misses to data accesses
CACHE_MISS_THRESHOLD = 0.0

SEPARATOR = '========================================================'

""" cache settings """

# cache line size
MEMBLOCKLEN = 256

# cache capacity in bytes
L1capacity = 64 * 1024
L2capacity = 8192 * 1024

# number of cache ways
L1ways = 4
L2ways = 16

mins_l1 = [c * L1capacity / L1ways for c in range(L1ways)]
mins_l2 = [c * L2capacity / L2ways for c in range(L2ways)]

"""
fill dict with total number of memory accesses in each region
"""
def access_in_region(df):
    air = dict()
    for region in df['region'].unique():
        df0 = df[(df['data_object'] == NO_DATASTRUCT)
                 & (df['region'] == region)]
        air[region] = sum(df0['partition0']) + sum(df0['partition1'])
    return air


def misses_nosc(df, level):
    min = 0
    if level == 1:
        min = L1capacity
    if level == 2:
        min = L2capacity
    df0 = df[df['min_dist'] >= min]
    return sum(df0['partition0']) + sum(df0['partition1'])


def misses_sc(df, level, nways):
    # print(f"level={level} ways={nways}")
    min_1 = 0
    min_2 = 0
    if level == 1:
        min_1 = mins_l1[nways]
        min_2 = mins_l1[L1ways - nways]
        assert (mins_l1[nways] + mins_l1[L1ways - nways] == L1capacity)
    if level == 2:
        min_1 = mins_l2[nways]
        min_2 = mins_l2[L2ways - nways]
        assert (mins_l2[nways] + mins_l2[L2ways - nways] == L2capacity)
    df1 = df[df['min_dist'] >= min_1]
    df2 = df[df['min_dist'] >= min_2]
    miss1 = sum(df1['partition0'])
    miss2 = sum(df2['partition1'])
    return miss1 + miss2


def print_datastructs(df):
    # print data structures
    for ds in df['data_object'].unique():
        if isinstance(ds, str) and ds[0] != '[' and ds[0] != NO_DATASTRUCT:
            item = df[df['data_object'] == ds].iloc[0]
            print(
                f"data object nr: {ds} {item['nbytes'] / 1024} kbytes allocated in line: { item['line'] }, file: {item['file_name'].split('/')[-1]}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="table")
    parser.add_argument("-f", "--file", type=str,
                        default=None, help="csv file")
    args = parser.parse_args()

    df = pd.read_csv(args.file)

    MEMBLOCKLEN = df['cachelinesize'].unique()
    threads = df['threads'].unique()

    assert(len(MEMBLOCKLEN) == 1)
    assert(len(threads) == 1)

    MEMBLOCKLEN = MEMBLOCKLEN[0]
    threads = threads[0]

    # rescale buckets to bytes
    df['min_dist'] = df['min_dist'] * MEMBLOCKLEN # // 1024

    df_total = df.loc[(df['region'] == 'main') & (
        df['data_object'] == NO_DATASTRUCT)]
    # print(df_total)
    l1miss_total = misses_nosc(df_total, 1)
    l2miss_total = misses_nosc(df_total, 2)

    print(SEPARATOR)
    print(f'  file: {args.file}')
    #print(f'total accesses: {access_counts}')
    print(f'    l1 misses (total): {l1miss_total}')
    print(f'    l2 misses (total): {l2miss_total}')
    print(SEPARATOR)

    region_access_dict = access_in_region(df)
    access_counts = region_access_dict['main']

    """
    for r in df['region'].unique():
        if region_access_dict[r] <= access_counts * REGION_THRESHOLD:
            print(f"drop region {r}")
            df = df[df['region'] != r]
    """

    # get # sequential access?

    # print(mins_l1, mins_l2)
    recommendations = pd.DataFrame(columns=[
                                   'region', # code region (function etc.)
                                   'data_object', # data object to isolate 
                                   'cache_level',
                                   'nways', # number of cache ways for isolated data object
                                   'miss_sc', # cache misses with sector cache
                                   'miss_nosc', # cache misses without sector cache
                                   ])

    for r in df['region'].unique():

        l1_cache_misses = sys.maxsize
        l2_cache_misses = sys.maxsize
        min_cache_misses = sys.maxsize

        l1_recommendation = None
        l2_recommendation = None
        datastruct1 = None
        datastruct2 = None

        dfg = df[(df['region'] == r) & (df['data_object'] == NO_DATASTRUCT)]
        l1_misses_nosc = misses_nosc(dfg, 1)
        l2_misses_nosc = misses_nosc(dfg, 2)

        if region_access_dict[r] == 0:
            continue

        if l1_misses_nosc / l1miss_total < REGION_THRESHOLD and l2_misses_nosc / l2miss_total < REGION_THRESHOLD:
            continue

        for ds in df['data_object'].unique():
            dfr = df[(df['region'] == r) & (df['data_object'] == ds)]

            best = sys.maxsize
            n = 0

            for n1 in range(1, L1ways):
                cache_misses = misses_sc(dfr, 1, n1)

                if l1_misses_nosc / region_access_dict[r] < CACHE_MISS_THRESHOLD:
                    break

                if l1_misses_nosc == 0:
                    break

                ratio = (l1_misses_nosc - cache_misses) / l1_misses_nosc
                # print(ratio)
                if cache_misses < l1_cache_misses and ratio > REDUCTION_THRESHOLD:
                    # if True:
                    l1_recommendation = n1
                    l1_cache_misses = cache_misses
                    datastruct1 = ds

                if cache_misses < best and ratio > REDUCTION_THRESHOLD:
                    best = cache_misses
                    n = n1

            best = sys.maxsize
            n = 0

            for n2 in range(2, L2ways - 1):
            #for n2 in range(4, L2ways - 3):
                cache_misses = misses_sc(dfr, 2, n2)
                # print(f'ds {ds} ways {n2} misses {cache_misses}')

                if l2_misses_nosc / region_access_dict[r] < CACHE_MISS_THRESHOLD:
                    break

                if l2_misses_nosc == 0:
                    break

                ratio = (l2_misses_nosc - cache_misses) / l2_misses_nosc
                if cache_misses < l2_cache_misses and ratio > REDUCTION_THRESHOLD:
                    l2_recommendation = n2
                    l2_cache_misses = cache_misses
                    datastruct2 = ds

                # find "cold" data struct (TODO)
                if cache_misses < best and ratio > REDUCTION_THRESHOLD:
                    best = cache_misses
                    n = n2

            # if n == 2:
            #     print(f'l2: cold datastruct {ds} in region {r}')
        # """
        # print L1
        if datastruct1 != None:
            # if True:
            #print('L1D:', r, datastruct1, l1_cache_misses, l1_recommendation, l1_cache_misses / l1_cache_misses_nosc)
            # 'region': 'data_object', 'cache_level', 'nways', 'miss_sc', 'miss_nosc', 'ratio'
            recommendations = recommendations.append({
                'region': r,
                'data_object': datastruct1,
                'cache_level': 1,
                'nways': l1_recommendation,
                'miss_sc': l1_cache_misses,
                'miss_nosc': l1_misses_nosc
            }, ignore_index=True)
        # """
        # print L2
        if datastruct2 != None:
            #print('L2:', r, datastruct2, l2_cache_misses, l2_recommendation, l2_cache_misses / l2_cache_misses_nosc)
            """        
            recommendations = pd.concat([recommendations, pd.DataFrame({
                'region': r,
                'data_object': datastruct2,
                'cache_level': 2,
                'nways': l2_recommendation,
                'miss_sc': l2_cache_misses,
                'miss_nosc': l2_misses_nosc,
                'ratio': l2_cache_misses / l2_misses_nosc
                },)]) # , ignore_index=True)
            """
            
            recommendations = recommendations.append({
                'region': r,
                'data_object': datastruct2,
                'cache_level': 2,
                'nways': l2_recommendation,
                'miss_sc': l2_cache_misses,
                'miss_nosc': l2_misses_nosc
                # }.items())], ignore_index=True)
            }, ignore_index=True)
    # print(recommendations.to_latex())

    def f0(x):
        return x

    def f1(x):
        return '%.0f' % x

    def f2(x):
        x = 100 * x
        return '%.2f' % x

    recommendations.columns = ['region', 'object', 'cache level', 'nways', 'misses sc', 'misses nosc']
    recommendations['reduction [%]'] = 100.0 * (1.0 - recommendations['misses sc'] / recommendations['misses nosc'])

    recommendations = recommendations.sort_values(
        by=['misses nosc'], ascending=False)

    dfm = pd.read_csv(args.file.replace('.csv', '-fnargs.csv'))

    def get_aka(dfm, region, ds):
        dfm0 = dfm[(dfm['region'] == region) & (dfm['ds'] == ds)]
        for _, r in dfm0.iterrows():
            # print(r)
            print(f'    {region}: isolate object \"{r["var"]}\" (aka object number {ds})')

    # print(recommendations.to_latex(formatters=[f0, f0, f0, f0, f0, f0, f2, f2], index=False))
    for _, r in recommendations.iterrows():
        #print(r)
        print(pd.DataFrame(r))
        for tok in r['object'].split():
            a = 0
            try:
                a = int(tok)
                get_aka(dfm, r['region'], a)
            except:
                pass
        print(SEPARATOR)
    # print(recommendations)


    if verbose_data_objects:
        print_datastructs(df)
    
    if verbose_function_arguments:
        print(dfm)

    exit()
