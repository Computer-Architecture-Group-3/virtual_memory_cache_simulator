#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[]){

  
  
  //parse command line arguments
  for(int i = 1; i < argc; i++){
    if(strcmp(argv[i], "-s") == 0){             //cache size
      //TODO
    }else if(strcmp(argv[i], "-b") == 0){       //block size 
      //TODO
    }else if(strcmp(argv[i], "-a") == 0){       //associativity
      //TODO 
    }else if(strcmp(argv[i], "-r") == 0){       //replacement policy
      //TODO
    }else if(strcmp(argv[i], "-p") == 0){       //physical memory
      //TODO
    }else if(strcmp(argv[i], "-u") == 0){       //physical mem used by OS
      //TODO
    }else if(strcmp(argv[i], "-n") == 0){       //instructions / time slice
      //TODO
    }else if(strcmp(argv[i], "-f") == 0){       //trace file name
      //TODO
    }
  }

  printf("Test\n");

  return 0;
}
