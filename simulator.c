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

// Check if x is power of 2
static int is_pow2_ull(unsigned long long x) {
    return x && ((x & (x - 1)) == 0);
}

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
        char* src_start = strstr(p, "srcM:");
        int len = src_start ? (src_start - p) : strlen(p);
        char buf[64];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        strncpy(buf, p, len);
        buf[len] = '\0';

        if (!strchr(buf, '-')) {
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
typedef enum{
    RP_RR, //Round robin
    RP_RANDOM //RANDOM
} ReplacementPolicy;

typedef struct {
    int valid;
    unsigned long long tag;
} CacheLine;

typedef struct {
    CacheLine* lines;
    int next_victim;
} CacheSet;

typedef struct {
    int s; // bit index
    int b; //bit offset
    int E; //lines per set
    int S; //number of set
    ReplacementPolicy policy;
    CacheSet* set;
    unsigned long long hits;
    unsigned long long misses;
    unsigned long long evictions;
    
} Cache;

static void cache_init(Cache* c, int s, int b, int E, ReplacementPolicy policy_{
    c->s = s;
    c->b = b;
    c->E = E;
    c->S = 1 << s;
    c->policy = policy;
    c->hits = c->misses = c->evictions = 0;
    c->sets = (CacheSet*)malloc(sizeof(CacheSet) * c->S);
    if (!c->sets) {
        fprintf(stderr, "Error: malloc for cache sets failed.\n");
        exit(1);
    }

    for (int i = 0; i < c->S; i++) {
        c->sets[i].lines = (CacheLine*)malloc(sizeof(CacheLine) * E);
        if (!c->sets[i].lines) {
            fprintf(stderr, "Error: malloc for cache lines failed.\n");
            exit(1);
        }
        c->sets[i].next_victim = 0;
        for (int j = 0; j < E; j++) {
            c->sets[i].lines[j].valid = 0;
            c->sets[i].lines[j].tag = 0;
        }
    }
})
typedef struct {
    int cache_size_kb;
    int block_size;
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
    printf("Physical Memory Used by System:\t%.1lf%%\n", physical_mem_used);
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

    // --- Virtual Memory Simulation ---
    FILE* fp[FILE_NUM] = {0};
    for (int i = 0; i < fileCount; i++) {
        fp[i] = fopen(filenames[i], "r");
        if (!fp[i]) fprintf(stderr, "Warning: cannot open %s — skipping this file.\n", filenames[i]);
    }

    PageTable pt[FILE_NUM];
    for (int i = 0; i < fileCount; i++) pt_init(&pt[i]);

    unsigned long long page_table_hits = 0, pages_from_free = 0, total_page_faults = 0, virtual_pages_mapped = 0;
    unsigned long long free_ppn_left = user_pages, next_ppn = 0;

    char line1[256], line2[256];

    for (int i = 0; i < fileCount; i++) {
        if (!fp[i]) continue;

        while (fgets(line1, sizeof(line1), fp[i])) {
            if (line1[0] == '\n' || line1[0] == '\r' || line1[0] == '\0') continue;

            unsigned long long eip_addr;
            int eip_len;
            if (!parse_eip_line(line1, &eip_addr, &eip_len)) {
                fprintf(stderr, "Warning: invalid EIP line: %s", line1);
                continue;
            }

            if (!fgets(line2, sizeof(line2), fp[i])) break;
            if (line2[0] == '\n' || line2[0] == '\r' || line2[0] == '\0') continue;

            // Process EIP line
            unsigned long long first_vpn = eip_addr >> 12;
            unsigned long long last_vpn = (eip_addr + eip_len - 1) >> 12;
            for (unsigned long long vpn = first_vpn; vpn <= last_vpn; vpn++) {
                long idx = pt_find(&pt[i], vpn);
                if (idx >= 0) page_table_hits++;
                else {
                    if (free_ppn_left > 0) { pt_push(&pt[i], vpn, next_ppn++); free_ppn_left--; pages_from_free++; }
                    else total_page_faults++;
                }
                virtual_pages_mapped++;
            }

            // Process dst/src line
            unsigned long long dst_addr, src_addr;
            int dst_valid, src_valid;
            parse_dst_src_line(line2, &dst_addr, &dst_valid, &src_addr, &src_valid);

            if (dst_valid) {
                unsigned long long vpn = dst_addr >> 12;
                long idx = pt_find(&pt[i], vpn);
                if (idx >= 0) page_table_hits++;
                else { if (free_ppn_left > 0) { pt_push(&pt[i], vpn, next_ppn++); free_ppn_left--; pages_from_free++; } else total_page_faults++; }
                virtual_pages_mapped++;
            }
            if (src_valid) {
                unsigned long long vpn = src_addr >> 12;
                long idx = pt_find(&pt[i], vpn);
                if (idx >= 0) page_table_hits++;
                else { if (free_ppn_left > 0) { pt_push(&pt[i], vpn, next_ppn++); free_ppn_left--; pages_from_free++; } else total_page_faults++; }
                virtual_pages_mapped++;
            }
        }
    }

    // --- Output final VM results ---
    printf("\n***** VIRTUAL MEMORY SIMULATION RESULTS *****\n\n");
    printf("Physical Pages Used By SYSTEM: %llu\n", system_pages);
    printf("Pages Available to User: %llu\n\n", user_pages);
    printf("Virtual Pages Mapped: %llu\n", virtual_pages_mapped);
    printf("\t------------------------------\n");
    printf("\tPage Table Hits: %llu\n", page_table_hits);
    printf("\tPages from Free: %llu\n", pages_from_free);
    printf("\tTotal Page Faults: %llu\n\n", total_page_faults);

    printf("Page Table Usage Per Process:\n------------------------------\n");
    for (int i = 0; i < fileCount; i++) {
        unsigned long long used = pt[i].used;
        double pct = (100.0 * used) / (double)VA_PAGES_PER_PROC;
        double wasted = (double)(VA_PAGES_PER_PROC - used) * (double)pte_bits / 8.0;

        printf("[%d] %s:\n", i, filenames[i]);
        printf("\tUsed Page Table Entries: %llu (%.2f%%)\n", used, pct);
        printf("\tPage Table Wasted: %.0f bytes\n\n", wasted);
    }

    for (int i = 0; i < fileCount; i++) pt_free(&pt[i]);
    return 0;
}

