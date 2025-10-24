#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FILE_NUM 3 //you must accept 1 to 3 trace files
static int is_pow2_ull(unsigned long long x){
  return x && ((x & (x - 1)) == 0);
}

int main(int argc, char* argv[]){

  int cache_size = 0; //-s 
  int block_size = 0; //-b
  int associativity = 0; //-a
  char replacement_policy[15]; // -r 
  int physical_mem = 0; // -p
  double physical_mem_used = 0; // -u
  int instruction = 0; //-n
  char* filenames [FILE_NUM];
  int fileCount= 0;


  /*restrictions for command line stuff need to be added*/
  
  //parse command line arguments
  for(int i = 1; i < argc; i++){
    if(strcmp(argv[i], "-s") == 0){             //cache size
      cache_size = atoi(argv[++i]);
    }else if(strcmp(argv[i], "-b") == 0){       //block size 
      block_size = atoi(argv[++i]);
    }else if(strcmp(argv[i], "-a") == 0){       //associativity
      associativity = atoi(argv[++i]);
    }else if(strcmp(argv[i], "-r") == 0){    
      strcpy(replacement_policy, argv[++i]);   //replacement policy
      if(strcmp(replacement_policy,"rr") == 0){
        strcpy(replacement_policy,"Round Robin");
      }else if(strcmp(replacement_policy,"rnd") == 0){
        strcpy(replacement_policy,"Random");
      }
    }else if(strcmp(argv[i], "-p") == 0){       //physical memory
      physical_mem = atoi(argv[++i]);
    }else if(strcmp(argv[i], "-u") == 0){       //physical mem used by OS
      physical_mem_used = atoi(argv[++i]);
    }else if(strcmp(argv[i], "-n") == 0){       //instructions / time slice
      instruction = atoi(argv[++i]);
    }else if(strcmp(argv[i], "-f") == 0){       //trace file name
       if(i + 1 < argc && argv[i + 1][0] != '-'){
        if(fileCount < FILE_NUM){
          filenames[fileCount++] = argv[++i];
        }else {break;}
       }
      
    }
  }

  printf("Cache Simulator - CS 3853 - Team #03\n\n");
  printf("Trace File(s):\n");
  for(int i = 0; i < fileCount; i++){
  printf("%s\n", filenames[i]);
 
}
  printf("\n***** Cache Input Parameters *****\n\n");
  printf("Cache Size:\t\t\t\t%d KB\n", cache_size);
  printf("Block Size:\t\t\t\t%d bytes\n", block_size);
  printf("Associativity:\t\t\t\t%d\n", associativity);
  printf("Replacement Policy:\t\t\t%s\n", replacement_policy);
  printf("Physical Memory:\t\t\t%d\n", physical_mem);
  printf("Physical Memory Used by System:\t\t%.1lf%%\n", physical_mem_used); 
  printf("Instructions / Time Slice:\t\t%d\n", instruction);
  // Calculate cache parameters
  int num_blocks = (cache_size*1024)/block_size; //convert to bytes

  int num_rows = num_blocks / associativity;

  int index_bits = (int)log2(num_rows);
  
  //tag bit calculation
  int offset = log2(block_size); // used to help find tag bits
  double phys_mem_bits = log2(pow(2,20) * physical_mem);
  int tag_size = phys_mem_bits - offset - index_bits; 

  int overhead_per_row = associativity * (tag_size + 1);
  int total_overhead = num_rows * overhead_per_row / 8; //divide by 8 for bytes
  
  int implementation_memory =(cache_size*1024) + total_overhead;
  double implementaion_memory_kb = implementation_memory / 1024;
 
  double cost = implementaion_memory_kb * 0.07; //not sure if the 0.07 is a constant value

  printf("\n***** Cache Calculated Values *****\n\n");
  printf("Total # Blocks:\t\t\t\t%d\n", num_blocks);
  printf("Tag Size:\t\t\t\t%d bits\n",tag_size);
  printf("Index Size:\t\t\t\t%d bits\n", index_bits);
  printf("Total # Rows:\t\t\t\t%d\n", num_rows);
  printf("Overhead Size:\t\t\t\t%d bytes\n", total_overhead);
  printf("Implementation Memory Size:\t\t%.2lf KB (%d bytes)\n",implementaion_memory_kb, implementation_memory);
  printf("Cost:\t\t\t\t\t%.2lf @ $0.07 per KB",cost);
  
  //Physical Memory Calculations
  unsigned long long phys_pages = phys_bytes / 4096;
  unsigned long long system_pages = (unsiegned long long )(phys_pages * (physical_mem_used / 100.0));
  int pte_bits = 1 + log2(phys_pages); //valid bit + number of bits to address physical pages
  unsigned long  long va_pages_per_proc * fileCount * pte_bits;
  unsigned long long total_pt_bytes = total_pt_bits / 8;

  printf("\n\n***** Physical Memory Calculated Values *****\n\n");
  printf("Number of Physical Pages :\t\t%llu\n", phys_pages);
  printf("Number of Pages for System:\t\t%llu\n( %.2f *llu = %llu)\n", system_pages, physical_mem_used/100.0, phys_pages, system_pages);
  printf("Size of Page Table Entry:\t\t\t%d bits\n (1 valid bit + %d bits for physical page number)\n", pte_bits, pte_bits - 1);
  printf("Total Ram for Page Tables:\t\t\t%llu bytes\n", total_pt_bytes,fileCount, pte_bits);
  return 0;
  
}


