#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FILE_NUM 3              // Accept 1 to 3 trace files
#define PAGE_SIZE 4096
#define VA_PAGES_PER_PROC (512ULL * 1024ULL)

// PAGE TABLE STRUCTS (Milestone 2)

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
        MapEntry* tmp = (MapEntry*)realloc(pt->arr, new_cap * sizeof(MapEntry));
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

/* Touch one virtual page: update stats + maybe map from free */
static void vm_touch_page(PageTable* pt,
                          unsigned long long vpn,
                          unsigned long long* page_table_hits,
                          unsigned long long* pages_from_free,
                          unsigned long long* total_page_faults,
                          unsigned long long* virtual_pages_mapped,
                          unsigned long long* free_ppn_left,
                          unsigned long long* next_ppn) {
    long idx = pt_find(pt, vpn);
    if (idx >= 0) {
        (*page_table_hits)++;
    } else {
        if (*free_ppn_left > 0) {
            pt_push(pt, vpn, *next_ppn);
            (*next_ppn)++;
            (*free_ppn_left)--;
            (*pages_from_free)++;
        } else {
            (*total_page_faults)++;
        }
    }
    (*virtual_pages_mapped)++;
}

// Translate VA -> PA if mapped. Return 1 if OK, 0 if unmapped 
static int vm_translate(PageTable* pt,
                        unsigned long long vaddr,
                        unsigned long long* paddr_out) {
    unsigned long long vpn = vaddr >> 12;
    unsigned long long offset = vaddr & (PAGE_SIZE - 1);
    long idx = pt_find(pt, vpn);
    if (idx < 0) return 0;
    unsigned long long ppn = pt->arr[idx].ppn;
    *paddr_out = (ppn << 12) | offset;
    return 1;
}

// TRACE PARSING HELPERS 

// Parse EIP line
int parse_eip_line(const char* line, unsigned long long* addr, int* len) {
    if (strncmp(line, "EIP", 3) != 0) return 0;
    
    char len_str[3] = { line[5], line[6], '\0' };
    *len = atoi(len_str);
    
    char addr_str[9];
    strncpy(addr_str, line + 10, 8);
    addr_str[8] = '\0';
    
    unsigned long long val = strtoull(addr_str, NULL, 16);
    if (val > 0x7FFFFFFF) return 0;
    *addr = val;
    
    return 1;
}

// Parse dst/src memory line
int parse_dst_src_line(const char* line,
                       unsigned long long* dst_addr, int* dst_valid,
                       unsigned long long* src_addr, int* src_valid) {
    const char* p;
    *dst_addr = *src_addr = 0;
    *dst_valid = *src_valid = 0;

    // dstM
    p = strstr(line, "dstM:");
    if (p) {
        p += 5;
        while (*p == ' ' || *p == '\t') p++;
        const char* src_start = strstr(p, "srcM:");
        int len = src_start ? (int)(src_start - p) : (int)strlen(p);
        char buf[64];
        if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
        strncpy(buf, p, len);
        buf[len] = '\0';

        if (!strstr(buf, "--------")) {
            unsigned long long val = strtoull(buf, NULL, 16);
            if (val <= 0x7FFFFFFF) {
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

        if (!strstr(buf, "--------")) {
            unsigned long long val = strtoull(buf, NULL, 16);
            if (val <= 0x7FFFFFFF) {
                *src_addr = val;
                *src_valid = 1;
            }
        }
    }

    return 1;
}

// CACHE SIM STRUCTS (Milestone 3)

typedef enum {
    POLICY_RR = 0,
    POLICY_RND = 1
} ReplacementPolicy;

typedef struct {
    int cache_size_kb;
    int block_size;
    int associativity;
    ReplacementPolicy policy;

    int num_blocks;
    int num_sets;
    int offset_bits;
    int index_bits;
    int tag_bits;

    // stats
    unsigned long long accesses;
    unsigned long long hits;
    unsigned long long misses;
    unsigned long long compulsory_misses;
    unsigned long long conflict_misses;

    unsigned long long instruction_bytes;
    unsigned long long srcdst_bytes;
    unsigned long long total_cycles;
    unsigned long long total_instructions;

    // arrays 
    unsigned int *rr_next;          // one per set
    unsigned long long *tags;       // [num_sets * ways]
    unsigned char *valid;           // [num_sets * ways]
} CacheSim;

static void cache_sim_init(CacheSim *cs,
                           int cache_size_kb,
                           int block_size,
                           int associativity,
                           ReplacementPolicy policy) {
    if (!cs) return;

    cs->cache_size_kb = cache_size_kb;
    cs->block_size = block_size;
    cs->associativity = associativity;
    cs->policy = policy;

    cs->num_blocks = (cache_size_kb * 1024) / block_size;
    cs->num_sets = cs->num_blocks / associativity;
    cs->offset_bits = (int)log2(block_size);
    cs->index_bits = (int)log2(cs->num_sets);
    cs->tag_bits = 32 - cs->offset_bits - cs->index_bits; // assume 32-bit PA

    cs->accesses = 0;
    cs->hits = 0;
    cs->misses = 0;
    cs->compulsory_misses = 0;
    cs->conflict_misses = 0;
    cs->instruction_bytes = 0;
    cs->srcdst_bytes = 0;
    cs->total_cycles = 0;
    cs->total_instructions = 0;

    size_t nlines = (size_t)cs->num_sets * (size_t)associativity;
    cs->tags = (unsigned long long *)malloc(nlines * sizeof(unsigned long long));
    cs->valid = (unsigned char *)calloc(nlines, sizeof(unsigned char));
    cs->rr_next = (unsigned int *)calloc(cs->num_sets, sizeof(unsigned int));

    if (!cs->tags || !cs->valid || !cs->rr_next) {
        fprintf(stderr, "Error: cache_sim_init out of memory.\n");
        exit(1);
    }
}

static void cache_sim_free(CacheSim *cs) {
    if (!cs) return;
    free(cs->tags);
    free(cs->valid);
    free(cs->rr_next);
    cs->tags = NULL;
    cs->valid = NULL;
    cs->rr_next = NULL;
}

// One cache access for ONE block 
static void cache_access_block(CacheSim *cs, unsigned long long phys_addr) {
    unsigned long long block_num = phys_addr / (unsigned long long)cs->block_size;
    int set_index = (int)(block_num % (unsigned long long)cs->num_sets);
    unsigned long long tag = block_num / (unsigned long long)cs->num_sets;

    int base = set_index * cs->associativity;

    cs->accesses++;

    // check for hit 
    for (int way = 0; way < cs->associativity; way++) {
        int idx = base + way;
        if (cs->valid[idx] && cs->tags[idx] == tag) {
            cs->hits++;
            cs->total_cycles += 1; // 1 cycle for cache hit
            return;
        }
    }

    // miss 
    cs->misses++;

    // cost to fill this cache block from memory (bus 32-bit) 
    int words_per_block = (cs->block_size + 3) / 4; // ceil(block_size/4)
    cs->total_cycles += 4 * words_per_block;        // 4 cycles per memory read

    // find victim 
    int victim = -1;
    for (int way = 0; way < cs->associativity; way++) {
        int idx = base + way;
        if (!cs->valid[idx]) {
            victim = idx;
            cs->compulsory_misses++;
            break;
        }
    }

    if (victim == -1) {
        cs->conflict_misses++;
        if (cs->policy == POLICY_RR) {
            unsigned int pos = cs->rr_next[set_index] % (unsigned int)cs->associativity;
            victim = base + (int)pos;
            cs->rr_next[set_index] = (pos + 1U) % (unsigned int)cs->associativity;
        } else {
            int way = rand() % cs->associativity;
            victim = base + way;
        }
    }

    cs->valid[victim] = 1;
    cs->tags[victim] = tag;
}

// Access a range [phys_addr, phys_addr + len - 1], may touch multiple blocks 
static void cache_access_range(CacheSim *cs,
                               unsigned long long phys_addr,
                               int len) {
    unsigned long long first_block =
        phys_addr / (unsigned long long)cs->block_size;
    unsigned long long last_block =
        (phys_addr + (unsigned long long)len - 1ULL) /
        (unsigned long long)cs->block_size;

    for (unsigned long long b = first_block; b <= last_block; b++) {
        unsigned long long block_addr =
            b * (unsigned long long)cs->block_size;
        cache_access_block(cs, block_addr);
    }
}

//=====MAIN=====

int main(int argc, char *argv[]) {
    int cache_size = 0;
    int block_size = 0;
    int associativity = 0;
    char replacement_policy_str[32] = "";
    ReplacementPolicy policy = POLICY_RR;
    int physical_mem = 0;
    double physical_mem_used = 0.0;
    int instruction_limit = -1;
    char *filenames[FILE_NUM];
    int fileCount = 0;

    if (argc < 2) {
        printf("Usage: VMCacheSim.exe -s <cacheKB> -b <blocksize> -a <associativity> "
               "-r <rr/rnd> -p <physmemMB> -u <mem used> -f <file1> -f <file2>...\n");
        return 1;
    }

    // parse command line 
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            cache_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0) {
            block_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-a") == 0) {
            associativity = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0) {
            char *opt = argv[++i];
            if (strcmp(opt, "rr") == 0) {
                policy = POLICY_RR;
                strcpy(replacement_policy_str, "Round Robin");
            } else if (strcmp(opt, "rnd") == 0 || strcmp(opt, "RND") == 0) {
                policy = POLICY_RND;
                strcpy(replacement_policy_str, "Random");
            } else {
                printf("Error: Replacement policy (-r) must be rr or rnd.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-p") == 0) {
            physical_mem = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-u") == 0) {
            physical_mem_used = atof(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0) {
            instruction_limit = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-f") == 0 && fileCount < FILE_NUM) {
            filenames[fileCount++] = argv[++i];
        }
    }

    // validate inputs 
    if (cache_size < 8 || cache_size > 8192) {
        printf("Error: Cache size (-s) must be between 8KB and 8192KB.\n");
        return 1;
    }
    if (block_size < 8 || block_size > 64) {
        printf("Error: Block size (-b) must be between 8 bytes and 64 bytes.\n");
        return 1;
    }
    if (!(associativity == 1 || associativity == 2 || associativity == 4 ||
          associativity == 8 || associativity == 16)) {
        printf("Error: Associativity (-a) must be 1, 2, 4, 8, 16.\n");
        return 1;
    }
    if (physical_mem < 128 || physical_mem > 4096) {
        printf("Error: Physical memory (-p) must be between 128MB and 4096MB.\n");
        return 1;
    }
    if (physical_mem_used < 0 || physical_mem_used > 100) {
        printf("Error: Physical memory used (-u) must be between 0%% and 100%%.\n");
        return 1;
    }
    if (instruction_limit != -1 && instruction_limit < 1) {
        printf("Error: Instruction (-n) must be >=1 or -1 for max.\n");
        return 1;
    }
    if (fileCount < 1 || fileCount > 3) {
        printf("Error: There must be 1 to 3 files using -f.\n");
        return 1;
    }

    srand((unsigned int)time(NULL));

    /* ========== MILESTONE #1: Input + Calculated values ========== */
    printf("Cache Simulator - CS 3853 - Team #03\n\n");
    printf("Trace File(s):\n");
    for (int i = 0; i < fileCount; i++)
        printf("\t%s\n", filenames[i]);

    printf("\n***** Cache Input Parameters *****\n\n");
    printf("Cache Size:\t\t\t\t%d KB\n", cache_size);
    printf("Block Size:\t\t\t\t%d bytes\n", block_size);
    printf("Associativity:\t\t\t\t%d\n", associativity);
    printf("Replacement Policy:\t\t\t%s\n", replacement_policy_str);
    printf("Physical Memory:\t\t\t%d MB\n", physical_mem);
    printf("Physical Memory Used by System:\t\t%.1f%%\n", physical_mem_used);
    printf("Instructions / Time Slice:\t\t%d\n", instruction_limit);

    int num_blocks = (cache_size * 1024) / block_size;
    int num_rows = num_blocks / associativity;
    int index_bits = (int)log2(num_rows);
    int offset_bits = (int)log2(block_size);
    double phys_mem_bits = log2(pow(2.0, 20.0) * (double)physical_mem);
    int tag_size = (int)(phys_mem_bits - offset_bits - index_bits);

    int overhead_per_row_bits = associativity * (tag_size + 1); /* tag + valid */
    int total_overhead =
        (int)ceil((double)num_rows * (double)overhead_per_row_bits / 8.0);
    unsigned long long phys_bytes = ((unsigned long long)physical_mem) << 20;

    int implementation_memory = (cache_size * 1024) + total_overhead;
    double implementation_memory_kb = implementation_memory / 1024.0;
    double cost = implementation_memory_kb * 0.07;

    printf("\n***** Cache Calculated Values *****\n\n");
    printf("Total # Blocks:\t\t\t\t%d\n", num_blocks);
    printf("Tag Size:\t\t\t\t%d bits\n", tag_size);
    printf("Index Size:\t\t\t\t%d bits\n", index_bits);
    printf("Total # Rows:\t\t\t\t%d\n", num_rows);
    printf("Overhead Size:\t\t\t\t%d bytes\n", total_overhead);
    printf("Implementation Memory Size:\t\t%.2f KB (%d bytes)\n",
           implementation_memory_kb, implementation_memory);
    printf("Cost:\t\t\t\t\t$%.2f @ $0.07 per KB\n", cost);

    unsigned long long phys_pages = phys_bytes / PAGE_SIZE;
    unsigned long long system_pages =
        (unsigned long long)(phys_pages * (physical_mem_used / 100.0));
    unsigned long long user_pages =
        (phys_pages > system_pages) ? (phys_pages - system_pages) : 0;
    int pte_bits = 1 + (int)ceil(log2((double)phys_pages));
    unsigned long long total_pt_bits =
        VA_PAGES_PER_PROC * (unsigned long long)fileCount *
        (unsigned long long)pte_bits;
    unsigned long long total_pt_bytes = total_pt_bits / 8ULL;

    printf("\n***** Physical Memory Calculated Values *****\n\n");
    printf("Number of Physical Pages:       \t%llu\n", phys_pages);
    printf("Number of Pages for System:     \t%llu\n", system_pages);
    printf("Size of Page Table Entry:       \t%d bits\n", pte_bits);
    printf("Total RAM for Page Table(s):    \t%llu bytes\n", total_pt_bytes);

    /* ========== MILESTONE #2 + #3: VM + Cache simulation ========== */

    FILE *fp[FILE_NUM] = {0};
    for (int i = 0; i < fileCount; i++) {
        fp[i] = fopen(filenames[i], "r");
        if (!fp[i]) {
            fprintf(stderr, "Warning: cannot open %s — skipping this file.\n",
                    filenames[i]);
        }
    }

    PageTable pt[FILE_NUM];
    for (int i = 0; i < fileCount; i++) {
        pt_init(&pt[i]);
    }

    CacheSim cache;
    cache_sim_init(&cache, cache_size, block_size, associativity, policy);

    unsigned long long page_table_hits = 0;
    unsigned long long pages_from_free = 0;
    unsigned long long total_page_faults = 0;
    unsigned long long virtual_pages_mapped = 0;

    unsigned long long free_ppn_left = user_pages;
    unsigned long long next_ppn = 0;

    char line1[256], line2[256];

    for (int i = 0; i < fileCount; i++) {
        if (!fp[i])
            continue;

        int instructions_seen = 0;

        while (fgets(line1, sizeof(line1), fp[i])) {
            if (line1[0] == '\n' || line1[0] == '\r' || line1[0] == '\0')
                continue;

            unsigned long long eip_addr = 0;
            int eip_len = 0;
            if (!parse_eip_line(line1, &eip_addr, &eip_len)) {
                fprintf(stderr, "Warning: invalid EIP line: %s", line1);
                continue;
            }

            if (!fgets(line2, sizeof(line2), fp[i])) {
                break;
            }
            if (line2[0] == '\n' || line2[0] == '\r' || line2[0] == '\0') {
                continue;
            }

            instructions_seen++;
            cache.total_instructions++;
            cache.instruction_bytes += (unsigned long long)eip_len;

            // simple time-slice: stop if over limit 
            if (instruction_limit != -1 && instructions_seen > instruction_limit) {
                break;
            }

            // VM: touch instruction pages 
            unsigned long long first_vpn = eip_addr >> 12;
            unsigned long long last_vpn =
                (eip_addr + (unsigned long long)eip_len - 1ULL) >> 12;
            for (unsigned long long vpn = first_vpn; vpn <= last_vpn; vpn++) {
                vm_touch_page(&pt[i], vpn,
                              &page_table_hits,
                              &pages_from_free,
                              &total_page_faults,
                              &virtual_pages_mapped,
                              &free_ppn_left,
                              &next_ppn);
            }

            // parse data line 
            unsigned long long dst_addr, src_addr;
            int dst_valid, src_valid;
            parse_dst_src_line(line2, &dst_addr, &dst_valid,
                               &src_addr, &src_valid);

            if (dst_valid && dst_addr != 0) {
                unsigned long long vpn = dst_addr >> 12;
                vm_touch_page(&pt[i], vpn,
                              &page_table_hits,
                              &pages_from_free,
                              &total_page_faults,
                              &virtual_pages_mapped,
                              &free_ppn_left,
                              &next_ppn);
            }

            if (src_valid && src_addr != 0) {
                unsigned long long vpn = src_addr >> 12;
                vm_touch_page(&pt[i], vpn,
                              &page_table_hits,
                              &pages_from_free,
                              &total_page_faults,
                              &virtual_pages_mapped,
                              &free_ppn_left,
                              &next_ppn);
            }

            // ===== CACHE PART ===== 

            // EIP fetch 
            unsigned long long paddr_eip;
            if (vm_translate(&pt[i], eip_addr, &paddr_eip)) {
                cache_access_range(&cache, paddr_eip, eip_len);
            }
            cache.total_cycles += 2; // execute instruction 

            // dstM: write 4 bytes 
            if (dst_valid && dst_addr != 0) {
                unsigned long long paddr_dst;
                if (vm_translate(&pt[i], dst_addr, &paddr_dst)) {
                    cache_access_range(&cache, paddr_dst, 4);
                }
                cache.total_cycles += 1; // effective address 
                cache.srcdst_bytes += 4;
            }

            // srcM: read 4 bytes 
            if (src_valid && src_addr != 0) {
                unsigned long long paddr_src;
                if (vm_translate(&pt[i], src_addr, &paddr_src)) {
                    cache_access_range(&cache, paddr_src, 4);
                }
                cache.total_cycles += 1; // effective address 
                cache.srcdst_bytes += 4;
            }
        }
    }

    // add 100 cycles per page fault 
    cache.total_cycles += 100ULL * total_page_faults;

    for (int i = 0; i < fileCount; i++) {
        if (fp[i])
            fclose(fp[i]);
    }

    /* ========== PRINT MILESTONE #2 RESULTS (VM) ========== */
    printf("\n***** VIRTUAL MEMORY SIMULATION RESULTS *****\n\n");
    printf("Physical Pages Used By SYSTEM: %llu\n", system_pages);
    printf("Pages Available to User: %llu\n\n", user_pages);

    printf("Virtual Pages Mapped: %llu\n", virtual_pages_mapped);
    printf("\t------------------------------\n");
    printf("\tPage Table Hits: %llu\n", page_table_hits);
    printf("\tPages from Free: %llu\n", pages_from_free);
    printf("\tTotal Page Faults: %llu\n\n", total_page_faults);
    for (int i = 0; i < fileCount; i++) {
        unsigned long long used = pt[i].used;
        double pct = (100.0 * (double)used) / (double)VA_PAGES_PER_PROC;
        double wasted = ((double)VA_PAGES_PER_PROC - (double)used) *
                        (double)pte_bits / 8.0;

        printf("[%d] %s:\n", i, filenames[i]);
        printf("\tUsed Page Table Entries: %llu ( %.2f%% )\n", used, pct);
        printf("\tPage Table Wasted: %.0f bytes\n\n", wasted);
    }

    // PRINT MILESTONE #3 RESULTS  
    printf(" CACHE SIMULATION RESULTS:\n\n");
    printf("Total Cache Accesses:\t%llu (%llu addresses)\n",
           cache.accesses,
           (unsigned long long)(cache.total_instructions +
                                (cache.srcdst_bytes / 4)));
    printf(" Instruction Bytes:\t%llu\n", cache.instruction_bytes);
    printf(" SrcDst Bytes:\t%llu\n", cache.srcdst_bytes);

    printf("Cache Hits:\t\t%llu\n", cache.hits);
    printf("Cache Misses:\t\t%llu\n", cache.misses);
    printf("Compulsory Misses:\t%llu\n", cache.compulsory_misses);
    printf(" Conflict Misses:\t%llu\n", cache.conflict_misses);

    double hit_rate =
        (cache.accesses > 0)
            ? (100.0 * (double)cache.hits / (double)cache.accesses)
            : 0.0;
    double miss_rate = 100.0 - hit_rate;

    printf("\nACHE HIT & MISS RATE: \n");
    printf("Hit  Rate:\t\t%.4f%%\n", hit_rate);
    printf("Miss Rate:\t\t%.4f%%\n", miss_rate);

    double cpi =
        (cache.total_instructions > 0)
            ? ((double)cache.total_cycles /
               (double)cache.total_instructions)
            : 0.0;
    printf("CPI:\t\t\t%.2f Cycles/Instruction (%llu)\n",
           cpi, cache.total_cycles);

    // unused cache space/blocks 
    unsigned long long unused_blocks =
        (cache.num_blocks > cache.compulsory_misses)
            ? (cache.num_blocks - cache.compulsory_misses)
            : 0;
    double overhead_per_block =
        (cache.num_blocks > 0)
            ? ((double)total_overhead / (double)cache.num_blocks)
            : 0.0;
    double unused_kb =
        ((double)unused_blocks *
         ((double)block_size + overhead_per_block)) / 1024.0;
    double waste = unused_kb * 0.07;

    printf("Unused Cache Space:\t%.2f KB / %.2f KB = %.2f%%  Waste: $%.2f/chip\n",
           unused_kb,
           implementation_memory_kb,
           (implementation_memory_kb > 0.0)
               ? (100.0 * unused_kb / implementation_memory_kb)
               : 0.0,
           waste);
    printf("Unused Cache Blocks:\t%llu / %d\n",
           unused_blocks, cache.num_blocks);

    // cleanup 
    for (int i = 0; i < fileCount; i++) {
        pt_free(&pt[i]);
    }
    cache_sim_free(&cache);

    return 0;
}
