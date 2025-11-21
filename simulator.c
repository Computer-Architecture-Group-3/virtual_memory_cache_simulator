#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#define FILE_NUM 3  // you must accept 1 to 3 trace files
#define PAGE_SIZE 4096
#define VA_PAGES_PER_PROC (512ULL * 1024ULL)

// prototypes
void parse_command_line(int argc, char* argv[], Config* config);

static int is_pow2_ull(unsigned long long x) {
  return x && ((x & (x - 1)) == 0);
}
// Struc for milestone 2

typedef struct {
  unsigned long long vpn;  // virtual page number
  unsigned long long ppn;  // physical page number
} MapEntry;

typedef struct {
  MapEntry* arr;  // array of entries
  size_t used;    // number of valid entries
  size_t cap;     // capacity of the array
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

// Linear search for vpn in page table
static long pt_find(PageTable* pt, unsigned long long vpn) {
  for (size_t i = 0; i < pt->used; i++) {
    if (pt->arr[i].vpn == vpn) {
      return (long)i;
    }
  }
  return -1;  // not found
}
// add a new mapping to the page table
static void pt_push(PageTable* pt, unsigned long long vpn,
                    unsigned long long ppn) {
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

// line 1 parsing
int parse_eip_line(const char* line, unsigned long long* addr, int* len) {
  // Expected format: EIP (XX): <hex>
  if (strncmp(line, "EIP", 3) != 0) return 0;

  const char* p = strchr(line, '(');
  if (!p) return 0;
  *len = atoi(p + 1);  // decimal length

  p = strchr(line, ':');
  if (!p) return 0;

  unsigned long long v = strtoull(p + 1, NULL, 16);

  *addr = v;
  return 1;
}

// line 2 parsing
int parse_dst_src_line(const char* line, unsigned long long* dst,
                       int* dst_valid, unsigned long long* src,
                       int* src_valid) {
  if (strncmp(line, "dstM:", 5) != 0) return 0;

  char dst_addr[16], dst_data[16];
  char src_addr[16], src_data[16];

  sscanf(line, "dstM: %s %s srcM: %s %s", dst_addr, dst_data, src_addr,
         src_data);

  // no access for "-------"
  *dst_valid = strcmp(dst_data, "--------") != 0;
  *src_valid = strcmp(src_data, "--------") != 0;

  // hex to string
  if (*dst_valid) *dst = strtoull(dst_addr, NULL, 16);
  if (*src_valid) *src = strtoull(src_addr, NULL, 16);

  return 1;
}

int main(int argc, char* argv[]) {
  Config* config;

  int cache_size = 0;                //-s KB
  int block_size = 0;                //-b
  int associativity = 0;             //-a
  char replacement_policy[15] = "";  // -r
  int physical_mem = 0;              // -p MB
  double physical_mem_used = 0;      // -u
  int instruction = -1;              //-n
  char* filenames[FILE_NUM];         //-f
  int fileCount = 0;

  if (argc < 2) {
    printf(
        "Usage: VMCacheSim.exe -s <cacheKB> -b <blocksize> -a <associativity> "
        "-r <rr/rnd> -p <physmemMB> -u<mem used> -f <file1> -f <file2>... ");
    return 1;
  }

  // parse command line arguments
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-s") == 0) {  // cache size
      cache_size = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-b") == 0) {  // block size
      block_size = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-a") == 0) {  // associativity
      associativity = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-r") == 0) {
      strcpy(replacement_policy, argv[++i]);  // replacement policy
      if (strcmp(replacement_policy, "rr") == 0) {
        strcpy(replacement_policy, "Round Robin");
      } else if (strcmp(replacement_policy, "rnd") == 0) {
        strcpy(replacement_policy, "Random");
      }
    } else if (strcmp(argv[i], "-p") == 0) {  // physical memory
      physical_mem = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-u") == 0) {  // physical mem used by OS
      physical_mem_used = atoll(argv[++i]);
    } else if (strcmp(argv[i], "-n") == 0) {  // instructions / time slice
      instruction = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-f") == 0) {  // trace file name
      if (fileCount < FILE_NUM) {
        filenames[fileCount++] = argv[++i];
      }
    }
  }

  // validating inputs
  if (cache_size < 8 || cache_size > 8192) {
    printf("Error: Cache size (-s) must be between 8KB and 8192KB.\n ");
    return 1;
  }
  if (block_size < 8 || block_size > 64) {
    printf("Error: Block size (-b) must be between 8 bytes and 64 bytes.\n ");
    return 1;
  }
  if (!(associativity == 1 || associativity == 2 || associativity == 4 ||
        associativity == 8 || associativity == 16)) {
    printf("Error: Associativity (-a) must be 1, 2, 4, 8, 16.\n");
    return 1;
  }
  if (strcmp(replacement_policy, "Round Robin") != 0 &&
      strcmp(replacement_policy, "Random") != 0) {
    printf("Error: Replacement policy (-r) must be rr or rnd.\n");
    return 1;
  }
  if (physical_mem < 128 || physical_mem > 4096) {
    printf("Error: Physical memory (-p) must be between 128MB and 4096MB.\n ");
    return 1;
  }
  if (physical_mem_used < 0 || physical_mem_used > 100) {
    printf(
        "Error: Physical memory used (-u) must be between 0%% and 100%%.\n ");
    return 1;
  }
  if (instruction != -1 && instruction < 1) {
    printf("Error: Instruction (-n) must be >=1 or -1 for max.\n");
    return 1;
  }
  if (fileCount < 1 || fileCount > 3) {
    printf("Error: There must be 1 to 3 files using -f.\n");
    return 1;
  }

  printf("Cache Simulator - CS 3853 - Team #03\n\n");
  printf("Trace File(s):\n");
  for (int i = 0; i < fileCount; i++) {
    printf("\t%s\n", filenames[i]);
  }
  printf("\n***** Cache Input Parameters *****\n\n");
  printf("Cache Size:\t\t\t\t%d KB\n", cache_size);
  printf("Block Size:\t\t\t\t%d bytes\n", block_size);
  printf("Associativity:\t\t\t\t%d\n", associativity);
  printf("Replacement Policy:\t\t\t%s\n", replacement_policy);
  printf("Physical Memory:\t\t\t%d MB\n", physical_mem);
  printf("Physical Memory Used by System:\t\t%.1lf%%\n", physical_mem_used);
  printf("Instructions / Time Slice:\t\t%d\n", instruction);

  // Calculate cache parameters
  int num_blocks = (cache_size * 1024) / block_size;  // 1024 to convert to
                                                      // bytes
  int num_rows = num_blocks / associativity;
  int index_bits = (int)log2(num_rows);

  // tag bit calculation
  int offset = log2(block_size);  // used to help find tag bits
  double phys_mem_bits = log2(pow(2, 20) * physical_mem);
  int tag_size = phys_mem_bits - offset - index_bits;

  int overhead_per_row = associativity * (tag_size + 1);
  int total_overhead =
      ceil((double)num_rows * overhead_per_row / 8.0);  // divide by 8 for bytes
  unsigned long long phys_bytes = (unsigned long long)physical_mem
                                  << 20;  // MB -> bytes

  int implementation_memory = (cache_size * 1024) + total_overhead;
  double implementaion_memory_kb = implementation_memory / 1024;

  double cost = implementaion_memory_kb *
                0.07;  // not sure if the 0.07 is a constant value

  printf("\n***** Cache Calculated Values *****\n\n");
  printf("Total # Blocks:\t\t\t\t%d\n", num_blocks);
  printf("Tag Size:\t\t\t\t%d bits\n", tag_size);
  printf("Index Size:\t\t\t\t%d bits\n", index_bits);
  printf("Total # Rows:\t\t\t\t%d\n", num_rows);
  printf("Overhead Size:\t\t\t\t%d bytes\n", total_overhead);
  printf("Implementation Memory Size:\t\t%.2lf KB (%d bytes)\n",
         implementaion_memory_kb, implementation_memory);
  printf("Cost:\t\t\t\t\t$%.2lf @ $0.07 per KB", cost);

  // Physical Memory Calculations
  unsigned long long phys_pages = phys_bytes / 4096ULL;
  unsigned long long system_pages =
      (unsigned long long)(phys_pages * (physical_mem_used / 100.0));
  int pte_bits =
      1 +
      (int)log2(
          phys_pages);  // valid bit + number of bits to address physical pages
  unsigned long long va_pages_per_proc = 512ULL * 1024ULL;
  unsigned long long total_pt_bits = va_pages_per_proc *
                                     (unsigned long long)fileCount *
                                     (unsigned long long)pte_bits;
  unsigned long long total_pt_bytes = total_pt_bits / 8ULL;
  unsigned long long user_pages =
      (phys_pages > system_pages) ? (phys_pages - system_pages) : 0ULL;

  printf("\n\n***** Physical Memory Calculated Values *****\n\n");
  printf("Number of Physical Pages :\t\t%llu\n", phys_pages);
  printf("Number of Pages for System:\t\t%llu \n", system_pages);
  printf("Size of Page Table Entry:\t\t%d bits \n", pte_bits);
  printf("Total Ram for Page Tables:\t\t%llu bytes \n", total_pt_bytes);

  // Milestone 2: Virtual Memory Simulation

  // open files for reading
  FILE* fp[FILE_NUM] = {0};
  for (int i = 0; i < fileCount; i++) {
    fp[i] = fopen(filenames[i], "r");
    if (!fp[i]) {
      fprintf(stderr, "Warning: cannot open %s — skipping this file.\n",
              filenames[i]);
    }
  }

  // initialize page tables
  PageTable pt[FILE_NUM];
  for (int i = 0; i < fileCount; i++) {
    pt_init(&pt[i]);
  }

  // Counters for VM simulation
  unsigned long long page_table_hits = 0;
  unsigned long long pages_from_free = 0;
  unsigned long long total_page_faults = 0;
  unsigned long long virtual_pages_mapped = 0;

  // Physical page allocator state
  unsigned long long free_ppn_left =
      user_pages;                   // how many physical pages are free
  unsigned long long next_ppn = 0;  // next physical page number to allocate

  char line1[256], line2[256];

  if (instruction >= 0) {
    // read each file until EOF
    for (int i = 0; i < fileCount; i++) {
      if (!fp[i]) continue;

      while (1) {
        // read both lines until the end of the file
        if (!fgets(line1, sizeof(line1), fp[i])) break;
        if (!fgets(line2, sizeof(line2), fp[i])) break;

        // Parse line 1 (EIP)
        unsigned long long eip_addr;
        int eip_len;
        if (parse_eip_line(line1, &eip_addr, &eip_len)) {
          // Process all bytes in this instruction fetch
          for (int b = 0; b < eip_len; b++) {
            unsigned long long a = eip_addr + b;  // accessed byte
            unsigned long long vpn = a >> 12;

            // check if mapping exists
            long idx = pt_find(&pt[i], vpn);
            if (idx >= 0) {
              page_table_hits++;
            } else {  // new mapping required
              if (free_ppn_left > 0) {
                pt_push(&pt[i], vpn, next_ppn++);
                free_ppn_left--;
                pages_from_free++;
              } else {  // no free ppns left
                total_page_faults++;
              }
            }
            virtual_pages_mapped++;
          }
        }

        // Parse line 2 (dstM/srcM)
        unsigned long long dst_addr, src_addr;
        int dst_valid, src_valid;

        if (parse_dst_src_line(line2, &dst_addr, &dst_valid, &src_addr,
                               &src_valid)) {
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
          }

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
          }
        }
      }
    }
  }

  // Output final Milestone 2 stats
  printf("\n***** VIRTUAL MEMORY SIMULATION RESULTS *****\n\n");
  printf("Physical Pages Used By SYSTEM: %lu\n", system_pages);
  printf("Pages Available to User: %lu\n\n", user_pages);

  printf("Virtual Pages Mapped: %lu\n", virtual_pages_mapped);
  printf("\t------------------------------\n");
  printf("\tPage Table Hits: %lu\n", page_table_hits);
  printf("\tPages from Free: %lu\n", pages_from_free);
  printf("\tTotal Page Faults: %lu\n\n", total_page_faults);

  printf("Page Table Usage Per Process:\n");
  printf("------------------------------\n");

  for (int i = 0; i < fileCount; i++) {
    unsigned long long used = pt[i].used;
    double pct = (100.0 * used) / (double)VA_PAGES_PER_PROC;

    unsigned long long pte_bytes = (pte_bits + 7) / 8;
    unsigned long long wasted = (VA_PAGES_PER_PROC - used) * pte_bytes;

    printf("[%d] %s:\n", i, filenames[i]);
    printf("Used Page Table Entries: %llu ( %.2f%% )\n", used, pct);
    printf("Page Table Wasted: %llu bytes\n\n", wasted);
  }

  for (int i = 0; i < fileCount; i++) {
    pt_free(&pt[i]);
  }

  return 0;
}

// parse command line arguments
void parse_command_line(int argc, char* argv[], Config* config) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-s") == 0) {  // cache size
      config->cache.cache_size = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-b") == 0) {  // block size
      config->cache.block_size = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-a") == 0) {  // associativity
      config->cache.associativity = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-r") == 0) {  // replacement policy
      if (strcmp(argv[++i], "rr") == 0) {
        config->cache.policy = RR;
      } else if (strcmp(argv[i], "rnd") == 0) {
        config->cache.policy = RND;
      }
    } else if (strcmp(argv[i], "-p") == 0) {  // physical memory
      config->vmemory.physical_memory = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-u") == 0) {  // physical mem used by OS
      config->vmemory.physical_memory_used = atoll(argv[++i]);
    } else if (strcmp(argv[i], "-n") == 0) {  // instructions / time slice
      config->instruction = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-f") == 0) {  // trace file name
      if (config->fileCount < FILE_NUM) {
        config->filenames[config->fileCount++] = argv[++i];
      }
    }
  }
}
