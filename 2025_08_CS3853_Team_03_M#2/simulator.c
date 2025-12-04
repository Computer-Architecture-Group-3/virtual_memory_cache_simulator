#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_NUM 3              // Accept 1 to 3 trace files
#define PAGE_SIZE 4096
#define VA_PAGES_PER_PROC (512ULL * 1024ULL)

// Check if x is power of 2
static int is_pow2_ull(unsigned long long x) {
    return x && ((x & (x - 1)) == 0);
}



// --- Structurtures for Milestone 3
typedef struct {
    unsigned long long tag;
    int valid;
} CacheLine;

typedef struct {
    CacheLine* lines;   // associativity lines per set
} CacheSet;


typedef struct {
    CacheSet* sets;
    int num_sets;
    int associativity;
    int block_size;
    char replacement_policy[16];
    int* rr_counters;   // round robin pointer
} Cache;

// --- Structures for Milestone 2 ---
typedef struct {
    unsigned long long vpn;    // virtual page number
    unsigned long long ppn;    // physical page number
} MapEntry;

typedef struct {
    MapEntry* arr;             // array of entries
    size_t used;               // number of valid entries
    size_t cap;                // capacity of the array
} PageTable;

// Initialize page table
static void pt_init(PageTable* pt) {
    pt->arr = NULL;
    pt->used = 0;
    pt->cap = 0;
}

// Free page table memory
static void pt_free(PageTable* pt) {
    free(pt->arr);
    pt->arr = NULL;
    pt->used = 0;
    pt->cap = 0;
}

// Linear search for VPN in page table
static long pt_find(PageTable* pt, unsigned long long vpn) {
    for (size_t i = 0; i < pt->used; i++) {
        if (pt->arr[i].vpn == vpn)
            return (long)i;
    }
    return -1; // not found
}

// Add a new mapping to the page table
static void pt_push(PageTable* pt, unsigned long long vpn, unsigned long long ppn) {
    if (pt->used == pt->cap) {
        size_t new_cap = (pt->cap == 0) ? 1 : pt->cap * 2;
        MapEntry* tmp = realloc(pt->arr, new_cap * sizeof(MapEntry));
        if (!tmp) {
            fprintf(stderr, "Error: Memory allocation failed in pt_push.\n");
            exit(1);
        }
        pt->arr = tmp;
        pt->cap = new_cap;
    }
    pt->arr[pt->used].vpn = vpn;
    pt->arr[pt->used].ppn = ppn;
    pt->used++;
}

// Parse EIP line
int parse_eip_line(const char* line, unsigned long long* addr, int* len) {
    if (strncmp(line, "EIP", 3) != 0) return 0;
    
    char len_str[3] = { line[5], line[6], '\0' };
    *len = atoi(len_str);
    
    char addr_str[9];
    strncpy(addr_str, line + 10, 8);
    addr_str[8] = '\0';
    
    unsigned long long val = strtoull(addr_str, NULL, 16);
    if (val > 0x7FFFFFFF) return 0; //can change parameter but works for now
    *addr = val;
    
    return 1;
}

// Parse dst/src memory line
int parse_dst_src_line(const char* line, unsigned long long* dst_addr, int* dst_valid,unsigned long long* src_addr, int* src_valid) {
    
    const char* p;
    *dst_addr = *src_addr = 0;
    *dst_valid = *src_valid = 0;

    // dstM
    p = strstr(line, "dstM:");
    if (p) {
        p += 5;
        while (*p == ' ' || *p == '\t') p++;
        char* src_start = strstr(p, "srcM:");
        int len = src_start ? (src_start - p) : strlen(p);
        char buf[64];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        strncpy(buf, p, len);
        buf[len] = '\0';

        if (!strchr(buf, '-')) {
            unsigned long long val = strtoull(buf, NULL, 16);
            if (val <= 0x7FFFFFFF) {//change hex
                *dst_addr = val;
                *dst_valid = 1;
            }
        }
    }

    // srcM
    p = strstr(line, "srcM:");
    if (p) {
        p += 5;
        while (*p == ' ' || *p == '\t') p++;
        char buf[64];
        strncpy(buf, p, sizeof(buf)-1);
        buf[sizeof(buf)-1] = '\0';

        if (!strchr(buf, '-')) {
            unsigned long long val = strtoull(buf, NULL, 16);
            if (val <= 0x7FFFFFFF) {
                *src_addr = val;
                *src_valid = 1;
            }
        }
    }

    return 1;
}


int cache_rows_spanned(unsigned long long addr, int bytes, int block_size) {
    unsigned long long first = addr / block_size;
    unsigned long long last  = (addr + bytes - 1) / block_size;
    return (int)(last - first + 1);
}

void cache_init(Cache* cache, int cache_size_kb, int block_size, int associativity, const char* policy) {
    int total_bytes = cache_size_kb * 1024;
    int num_blocks = total_bytes / block_size;
    int num_sets = num_blocks / associativity;

    cache->num_sets = num_sets;
    cache->associativity = associativity;
    cache->block_size = block_size;
    strcpy(cache->replacement_policy, policy);

    cache->sets = malloc(sizeof(CacheSet) * num_sets);
    cache->rr_counters = calloc(num_sets, sizeof(int));

    for (int i = 0; i < num_sets; i++) {
        cache->sets[i].lines = calloc(associativity, sizeof(CacheLine));
    }
}

typedef enum { HIT=1, MISS_COMPULSORY, MISS_CONFLICT } CacheResult;//for readability

CacheResult cache_access(Cache* cache, unsigned long long addr) {
    unsigned long long block_addr = addr / cache->block_size;
    unsigned long long index = block_addr % cache->num_sets;
    unsigned long long tag = block_addr / cache->num_sets;

    CacheSet* set = &cache->sets[index];

    // Check for hit
    for (int i = 0; i < cache->associativity; i++) {
        if (set->lines[i].valid && set->lines[i].tag == tag) {
            return HIT; 
        }
    }

    // MISS 
    int way;
    int first_empty = -1;
    for (int i = 0; i < cache->associativity; i++) {
        if (!set->lines[i].valid) { first_empty = i; break; }
    }

    if (first_empty >= 0) {
        // Compulsory miss
        way = first_empty;
        set->lines[way].tag = tag;
        set->lines[way].valid = 1;
        return MISS_COMPULSORY;
    }

    // Conflict miss
    if (strcmp(cache->replacement_policy, "Round Robin") == 0) {
        way = cache->rr_counters[index];
        cache->rr_counters[index] = (way + 1) % cache->associativity;
    } else {
        way = rand() % cache->associativity;
    }

    set->lines[way].tag = tag;
    set->lines[way].valid = 1;
    return MISS_CONFLICT;
}



int main(int argc, char* argv[]) {
    int cache_size = 0;                  // -s KB
    int block_size = 0;                  // -b bytes
    int associativity = 0;               // -a
    char replacement_policy[15] = "";    // -r
    int physical_mem = 0;                // -p MB
    double physical_mem_used = 0;        // -u
    int instruction = -1;                // -n
    char* filenames[FILE_NUM];           // -f
    int fileCount = 0;

    if (argc < 2) {
        printf("Usage: VMCacheSim.exe -s <cacheKB> -b <blocksize> -a <associativity> "
               "-r <rr/rnd> -p <physmemMB> -u<mem used> -f <file1> -f <file2>... ");
        return 1;
    }

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) cache_size = atoi(argv[++i]);
        else if (strcmp(argv[i], "-b") == 0) block_size = atoi(argv[++i]);
        else if (strcmp(argv[i], "-a") == 0) associativity = atoi(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0) {
            strcpy(replacement_policy, argv[++i]);
            if (strcmp(replacement_policy, "rr") == 0) strcpy(replacement_policy, "Round Robin");
            else if (strcmp(replacement_policy, "rnd") == 0) strcpy(replacement_policy, "Random");
        }
        else if (strcmp(argv[i], "-p") == 0) physical_mem = atoi(argv[++i]);
        else if (strcmp(argv[i], "-u") == 0) physical_mem_used = atoll(argv[++i]);
        else if (strcmp(argv[i], "-n") == 0) instruction = atoi(argv[++i]);
        else if (strcmp(argv[i], "-f") == 0 && fileCount < FILE_NUM) filenames[fileCount++] = argv[++i];
    }

    // Validate inputs
    if (cache_size < 8 || cache_size > 8192) { printf("Error: Cache size (-s) must be between 8KB and 8192KB.\n"); return 1; }
    if (block_size < 8 || block_size > 64) { printf("Error: Block size (-b) must be between 8 bytes and 64 bytes.\n"); return 1; }
    if (!(associativity == 1 || associativity == 2 || associativity == 4 || associativity == 8 || associativity == 16)) {
        printf("Error: Associativity (-a) must be 1, 2, 4, 8, 16.\n"); return 1;
    }
    if (strcmp(replacement_policy, "Round Robin") != 0 && strcmp(replacement_policy, "Random") != 0) {
        printf("Error: Replacement policy (-r) must be rr or rnd.\n"); return 1;
    }
    if (physical_mem < 128 || physical_mem > 4096) { printf("Error: Physical memory (-p) must be between 128MB and 4096MB.\n"); return 1; }
    if (physical_mem_used < 0 || physical_mem_used > 100) { printf("Error: Physical memory used (-u) must be between 0%% and 100%%.\n"); return 1; }
    if (instruction != -1 && instruction < 1) { printf("Error: Instruction (-n) must be >=1 or -1 for max.\n"); return 1; }
    if (fileCount < 1 || fileCount > 3) { printf("Error: There must be 1 to 3 files using -f.\n"); return 1; }

    // --- Display Configuration ---
    printf("Cache Simulator - CS 3853 - Team #03\n\n");
    printf("Trace File(s):\n");
    for (int i = 0; i < fileCount; i++) printf("\t%s\n", filenames[i]);

    printf("\n***** Cache Input Parameters *****\n\n");
    printf("Cache Size:\t\t\t%d KB\n", cache_size);
    printf("Block Size:\t\t\t%d bytes\n", block_size);
    printf("Associativity:\t\t\t%d\n", associativity);
    printf("Replacement Policy:\t\t%s\n", replacement_policy);
    printf("Physical Memory:\t\t%d MB\n", physical_mem);
    printf("Percent Memory Used by System:\t%.1lf%%\n", physical_mem_used);
    printf("Instructions / Time Slice:\t%d\n", instruction);

    // --- Cache Calculations ---
    int num_blocks = (cache_size * 1024) / block_size;
    int num_rows = num_blocks / associativity;
    int index_bits = (int)log2(num_rows);
    int offset = log2(block_size);
    double phys_mem_bits = log2(pow(2, 20) * physical_mem);
    int tag_size = phys_mem_bits - offset - index_bits;

    int overhead_per_row = associativity * (tag_size + 1);
    int total_overhead = (int)ceil((double)num_rows * overhead_per_row / 8.0);
    unsigned long long phys_bytes = (unsigned long long)physical_mem << 20;

    int implementation_memory = (cache_size * 1024) + total_overhead;
    double implementation_memory_kb = implementation_memory / 1024.0;
    double cost = implementation_memory_kb * 0.07;

    printf("\n***** Cache Calculated Values *****\n\n");
    printf("Total # Blocks:\t\t\t%d\n", num_blocks);
    printf("Tag Size:\t\t\t%d bits\n", tag_size);
    printf("Index Size:\t\t\t%d bits\n", index_bits);
    printf("Total # Rows:\t\t\t%d\n", num_rows);
    printf("Overhead Size:\t\t\t%d bytes\n", total_overhead);
    printf("Implementation Memory Size:\t%.2lf KB (%d bytes)\n", implementation_memory_kb, implementation_memory);
    printf("Cost:\t\t\t\t$%.2lf @ $0.07 per KB\n", cost);

    // --- Physical Memory Calculations ---
    unsigned long long phys_pages = phys_bytes / PAGE_SIZE;
    unsigned long long system_pages = (unsigned long long)(phys_pages * (physical_mem_used / 100.0));
    int pte_bits = 1 + (int)ceil(log2(phys_pages));
    unsigned long long total_pt_bits = VA_PAGES_PER_PROC * fileCount * pte_bits;
    unsigned long long total_pt_bytes = total_pt_bits / 8;
    unsigned long long user_pages = (phys_pages > system_pages) ? (phys_pages - system_pages) : 0;

    printf("\n***** Physical Memory Calculated Values *****\n\n");
    printf("Number of Physical Pages :\t%llu\n", phys_pages);
    printf("Number of Pages for System:\t%llu\n", system_pages);
    printf("Size of Page Table Entry:\t%d bits\n", pte_bits);
    printf("Total Ram for Page Tables:\t%llu bytes\n", total_pt_bytes);

    unsigned long long total_cache_accesses = 0;
    unsigned long long total_instruction_bytes = 0;
    unsigned long long total_srcdst_bytes = 0;
    unsigned long long cache_hits = 0;
    unsigned long long cache_misses = 0;
    unsigned long long compulsory_misses = 0;
    unsigned long long conflict_misses = 0;
    unsigned long long num_instructions = 0;
    double total_cycles = 0.0;

    int mem_reads_per_miss = (block_size + 3) /4;
    int miss_cost = 4 * mem_reads_per_miss;

    Cache cache;
    cache_init(&cache, cache_size, block_size, associativity, replacement_policy);

    // --- Virtual Memory Simulation ---
FILE* fp[FILE_NUM] = {0};
for (int i = 0; i < fileCount; i++) {
    fp[i] = fopen(filenames[i], "r");
    if (!fp[i])
        fprintf(stderr, "Warning: cannot open %s - skipping this file.\n", filenames[i]);
}

PageTable pt[FILE_NUM];
for (int i = 0; i < fileCount; i++)
    pt_init(&pt[i]);

unsigned long long page_table_hits = 0, pages_from_free = 0, total_page_faults = 0, virtual_pages_mapped = 0;
unsigned long long free_ppn_left = user_pages, next_ppn = 0;

char line1[256], line2[256];

for (int i = 0; i < fileCount; i++) {
    if (!fp[i]) continue;

    while (fgets(line1, sizeof(line1), fp[i])) {

        if (line1[0] == '\n' || line1[0] == '\r' || line1[0] == '\0')
            continue;

        unsigned long long eip_addr;
        int eip_len;

        if (!parse_eip_line(line1, &eip_addr, &eip_len)) {
            fprintf(stderr, "Warning: invalid EIP line: %s", line1);
            continue;
        }

        total_instruction_bytes += eip_len;
        num_instructions++;

        if (!fgets(line2, sizeof(line2), fp[i]))
            break;

        if (line2[0] == '\n' || line2[0] == '\r' || line2[0] == '\0')
            continue;

        // Process EIP line
        unsigned long long first_vpn = eip_addr >> 12;
        unsigned long long last_vpn  = (eip_addr + eip_len - 1) >> 12;

        for (unsigned long long vpn = first_vpn; vpn <= last_vpn; vpn++) {

            long idx = pt_find(&pt[i], vpn);

            if (idx >= 0)
                page_table_hits++;
            else {
                if (free_ppn_left > 0) {
                    pt_push(&pt[i], vpn, next_ppn++);
                    free_ppn_left--;
                    pages_from_free++;
                } else {
                    total_page_faults++;
                }
            }

            virtual_pages_mapped++;
        }

        int spans = cache_rows_spanned(eip_addr, eip_len, block_size);

        // Update total cache accesses 
        total_cache_accesses += spans;
        total_cycles += 2;

        for (int k = 0; k < spans; k++) {
            unsigned long long row_addr = eip_addr + k * block_size;
            CacheResult result = cache_access(&cache, row_addr);

            if (result == HIT) {
                cache_hits++;
                total_cycles += 1;
            } else {
                cache_misses++;
                total_cycles += miss_cost;

                if (result == MISS_COMPULSORY)
                    compulsory_misses++;
                else if (result == MISS_CONFLICT)
                    conflict_misses++;
            }
        }

        // Process dst/src line
        unsigned long long dst_addr, src_addr;
        int dst_valid, src_valid;

        parse_dst_src_line(line2, &dst_addr, &dst_valid, &src_addr, &src_valid);

        if (dst_valid) total_srcdst_bytes += 4;
        if (src_valid) total_srcdst_bytes += 4;

        // ---- DST ACCESS ----
        if (dst_valid) {

            unsigned long long vpn = dst_addr >> 12;
            long idx = pt_find(&pt[i], vpn);

            if (idx >= 0)
                page_table_hits++;
            else {
                if (free_ppn_left > 0) {
                    pt_push(&pt[i], vpn, next_ppn++);
                    free_ppn_left--;
                    pages_from_free++;
                } else {
                    total_page_faults++;
                }
            }

            virtual_pages_mapped++;
            total_cycles += 1;

            int spans = cache_rows_spanned(dst_addr, 4, block_size);
            total_cache_accesses += spans;

            for (int k = 0; k < spans; k++) {
                unsigned long long row_addr = dst_addr + k * block_size;
                CacheResult result = cache_access(&cache, row_addr);

                if (result == HIT) {
                    cache_hits++;
                    total_cycles += 1;
                } else {
                    cache_misses++;
                    total_cycles += miss_cost;

                    if (result == MISS_COMPULSORY)
                        compulsory_misses++;
                    else if (result == MISS_CONFLICT)
                        conflict_misses++;
                }
            }
        }

        // ---- SRC ACCESS ----
        if (src_valid) {

            unsigned long long vpn = src_addr >> 12;
            long idx = pt_find(&pt[i], vpn);

            if (idx >= 0)
                page_table_hits++;
            else {
                if (free_ppn_left > 0) {
                    pt_push(&pt[i], vpn, next_ppn++);
                    free_ppn_left--;
                    pages_from_free++;
                } else {
                    total_page_faults++;
                }
            }

            virtual_pages_mapped++;
            total_cycles += 1;

            int spans = cache_rows_spanned(src_addr, 4, block_size);
            total_cache_accesses += spans;

            for (int k = 0; k < spans; k++) {
                unsigned long long row_addr = src_addr + k * block_size;
                CacheResult result = cache_access(&cache, row_addr);

                if (result == HIT) {
                    cache_hits++;
                    total_cycles += 1;
                } else {
                    cache_misses++;
                    total_cycles += miss_cost;

                    if (result == MISS_COMPULSORY)
                        compulsory_misses++;
                    else if (result == MISS_CONFLICT)
                        conflict_misses++;
                }
            }
        }
    }
}
//penalty
    total_cycles += total_page_faults * 100;

    // --- Output final VM results ---
    printf("\n***** VIRTUAL MEMORY SIMULATION RESULTS *****\n\n");
    printf("Physical Pages Used By SYSTEM:  %llu\n", system_pages);
    printf("Pages Available to User:  \t%llu\n\n", user_pages);
    printf("Virtual Pages Mapped:\t\t%llu\n", virtual_pages_mapped);
    printf("\t------------------------------\n");
    printf("\tPage Table Hits: \t%llu\n", page_table_hits);
    printf("\tPages from Free: \t%llu\n", pages_from_free);
    printf("\tTotal Page Faults: \t%llu\n\n", total_page_faults);

    printf("Page Table Usage Per Process:\n------------------------------\n\n");
    for (int i = 0; i < fileCount; i++) {
        unsigned long long used = pt[i].used;
        double pct = (100.0 * used) / (double)VA_PAGES_PER_PROC;
        double wasted = (double)(VA_PAGES_PER_PROC - used) * (double)pte_bits / 8.0;

        printf("[%d] %s:\n", i, filenames[i]);
        printf("\tUsed Page Table Entries: %llu (%.2f%%)\n", used, pct);
        printf("\tPage Table Wasted: %.0f bytes\n\n", wasted);
    }

//free
    for (int i = 0; i < fileCount; i++) pt_free(&pt[i]);
    
 
 double hit_rate = ((double)cache_hits / (double)(total_cache_accesses)) * 100.0;
double miss_rate = 100.0 - hit_rate;


double cpi = total_cycles / (double)num_instructions;

 
unsigned long long total_blocks = num_blocks;
unsigned long long used_blocks = compulsory_misses;
unsigned long long unused_blocks = total_blocks - used_blocks;

double overhead_per_block =((double)overhead_per_row / associativity) / 8.0;  
double unused_kb = (unused_blocks * (cache.block_size + overhead_per_block)) / 1024.0;
double total_cache_kb = (cache_size) + (total_overhead / 1024.0);
double waste = unused_kb * 0.07;


printf("\n***** CACHE SIMULATION RESULTS *****\n\n");
 printf("Total Cache Accesses:\t\t%llu\t(%llu addresses)\n", total_cache_accesses, virtual_pages_mapped);
printf("---Instruction Bytes:\t\t%llu\n", total_instruction_bytes);
printf("--- SrcDst Bytes:\t\t%llu\n", total_srcdst_bytes);
printf("Cache Hits:\t\t\t%llu\n",cache_hits);
printf("Cache Misses:\t\t\t%llu\n",cache_misses);
printf("--- Compulsory Misses:\t \t%llu", compulsory_misses);
printf("\n--- Conflict Misses:\t \t%llu", conflict_misses);
printf("\n\n***** ***** CACHE HIT & MISS RATE: ***** *****\n\n");
printf("Hit Rate:\t\t\t%.4lf%%\n", hit_rate);
printf("Miss Rate:\t\t\t%.4lf%%\n", miss_rate);
printf("CPI:\t\t\t\t%.2f Cycles/Instruction (%llu)\n",cpi, num_instructions);


printf("Unused Cache Space:\t\t%.2lf KB / %.2lf KB = %.2lf%%  Waste: $%.2lf\n",unused_kb, total_cache_kb,implementation_memory_kb, waste);
printf("Unused Cache Blocks:\t\t%llu / %llu\n", unused_blocks, total_blocks);
return 0;
}
}
