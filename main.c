#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]){

  int cache_size = 0;
  int block_size = 0;
  int associativity = 0;
  char replacement_policy[4];
  int physical_mem = 0;
  int physical_mem_used = 0;
  
  //parse command line arguments
  for(int i = 1; i < argc; i++){
    if(strcmp(argv[i], "-s") == 0){             //cache size
      cache_size = atoi(argv[++i]);
    }else if(strcmp(argv[i], "-b") == 0){       //block size 
      block_size = atoi(argv[++i]);
    }else if(strcmp(argv[i], "-a") == 0){       //associativity
      associativity = atoi(argv[++i]);
    }else if(strcmp(argv[i], "-r") == 0){       //replacement policy
      strcpy(replacement_policy, argv[++i]);
    }else if(strcmp(argv[i], "-p") == 0){       //physical memory
      physical_mem = atoi(argv[++i]);
    }else if(strcmp(argv[i], "-u") == 0){       //physical mem used by OS
      physical_mem_used = atoi(argv[++i]);
    }else if(strcmp(argv[i], "-n") == 0){       //instructions / time slice
      //TODO
    }else if(strcmp(argv[i], "-f") == 0){       //trace file name
      //TODO
    }
  }

  printf("Cache Simulator - CS 3853 - Team #03\n\n");
  printf("Trace File(s):\n\n"); //TODO
  printf("***** Cache Input Parameters *****\n\n");
  printf("Cache Size:\t\t%d KB\n", cache_size);
  printf("Block Size:\t\t%d bytes\n", block_size);
  printf("Associativity:\t\t%d\n", associativity);
  printf("Replacement Policy:\t\t%s\n", replacement_policy);


  return 0;
}


