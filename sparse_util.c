#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

#define FILE_SIZE (1<<30)
#define START_STRING "$$start of file$$"
#define START_LEN strlen(START_STRING)
#define END_STRING "$$end of file$$"
#define END_LEN strlen(END_STRING)

int create_sparse_file(const char *filename, off_t size) {
  int fd = open(filename, O_RDWR|O_CREAT, 0755);
  if (fd < 0) {
    perror("Could not create file");
    return -1;
    
  }

  if (write(fd, START_STRING, START_LEN) < START_LEN) {
    perror("Could not write START_STRING");
    close(fd);
    return -1;
  }

  if (lseek(fd, size - END_LEN, SEEK_SET) < 0) {
    perror("Unable to seek at end");
    close(fd);
    return -1;
  }

  if (write(fd, END_STRING, END_LEN) < END_LEN) {
    perror("Could not write END_STRING");
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

void print_file_data(int fd, long from, long size) {
  char *map = mmap(0, size, PROT_READ, MAP_SHARED, fd, from);
  int i;
  for (i = 0; i < size; i++) {
    printf("%c", map[i]);
  }
  printf("\n");
  munmap(map, size);
}

int print_holes(const char *filename, int verbose) {
  int fd = open(filename, O_RDONLY);
  struct stat buf;
  long data = 0;
  long hole = 0;

  if (fd < 0) {
    perror("Could not open file");
    return -1;
  }

  if (fstat(fd, &buf) < 0) {
    perror("Could not get file info");
    close(fd);
    return -1;
  }
  printf("size %ld blocksize %ld blockcount %ld\n", buf.st_size, buf.st_blksize, buf.st_blocks);

  do  {
    data = lseek(fd, hole, SEEK_DATA);
    hole = lseek(fd, data, SEEK_HOLE);

    if (data < 0)
      break;

    if (hole < 0) {
      hole = lseek(fd, 0, SEEK_END);
      printf("intra\n");
    }
    printf("data = %ld, hole = %ld\n", data, hole);

    if (verbose)
      print_file_data(fd, data, hole - data);
  } while(1);

  close(fd);

  return 0;
}

void generate_char_block(void *block, size_t size, char c) {
  memset(block, c, size);
}

void generate_rand_block(void *block, size_t size, char c) {
  int i;
  for (i = 0; i < size / (sizeof(int) / sizeof(char)); i++) {
    ((int *)block)[i] =  rand();
  }
}

int scrub_sparse_file(const char *filename, void (*generate_block)(void*, size_t, char), char c) {
  int fd = open(filename, O_RDWR);  
  char *blk;
  struct stat buf;
  off_t blk_size, file_size;
  int ret = 0;
  off_t data = 0, hole = 0;

  if (fd < 0) {
    perror("Could not open file");
    ret = -1;
    goto exit;
  }

  if (fstat(fd, &buf) < 0) {
    perror("Could not get file info");
    ret = -1;
    goto exit_close_fd;
  }

  blk_size = buf.st_blksize;
  file_size = buf.st_size;
  printf("Detected block size %ld file size %ld\n", blk_size, file_size);

  blk = malloc(blk_size);
  if (blk < 0) {
    perror("Could not allocate block mem\n");
    ret = -1;
    goto exit_close_fd;
  }

  generate_block(blk, blk_size, c);

  do  {
    int num_writes = 0;
    int i;

    data = lseek(fd, hole, SEEK_DATA);
    hole = lseek(fd, data, SEEK_HOLE);
    printf("data %ld hole %ld\n", data, hole);

    if (data < 0)
      break;

    if (hole < 0) {
      hole = lseek(fd, 0, SEEK_END);
      printf("Reposition hole at the end of the file %ld", hole);
    }

    num_writes = (hole - data) / blk_size;
    if ((hole - data) % blk_size != 0) {
      printf("Found a hole that is not a multiple of block_size\n data=%ld hole=%ld\n", data, hole);
      num_writes ++;
    }

    lseek(fd, data, SEEK_SET);
    for (i = 0 ; i < num_writes; i++) {
      /* TODO handle the partial write case or do it with mmap */
      if (write(fd, blk, blk_size) < 0) {
	perror("could not zero a block");
	goto exit_free_close_fd;
      }
    }

    /* this should not be needed we are at the start of the first hole */
    lseek(fd, hole, SEEK_SET);
  } while(1);

  fsync(fd);

 exit_free_close_fd:
  free(blk);
 exit_close_fd:
  close(fd);

 exit:
  return ret;
}

void print_usage(const char* name) {
  printf("Usage: %s [-e -c -p -v] filename [char_to_fill_data]\n", name);
    printf("   -e erase sparse file, replace data with char of zeros if no char provided\n");
    printf("   -r erase sparse file, replace data random values\n");
    printf("   -c create a spare file with data at the end and begining with size 0x%x\n", FILE_SIZE);  
    printf("   -p print sparse file information\n");
    printf("   -v print sparse file information and data as string\n");
}

int main(int argc, char *argv[]) {
  if ((argc != 3) && (argc != 4)) {
    print_usage(argv[0]);

    return -1;
  }

  if (strcmp(argv[1], "-e") == 0) {
    char c = 0;
    if (argc == 4)
      c = argv[3][0];

    return scrub_sparse_file(argv[2], generate_char_block, c);
  }
  else if (strcmp(argv[1], "-r") == 0) {
    return scrub_sparse_file(argv[2], generate_rand_block, 0);
  }
  else if (strcmp(argv[1], "-c") == 0) {
    return create_sparse_file(argv[2], FILE_SIZE);
  }
  else if (strcmp(argv[1], "-p") == 0) {
    return print_holes(argv[2], 0);
  }
  else if (strcmp(argv[1], "-v") == 0) {
    return print_holes(argv[2], 1);
  }
  else {
    print_usage(argv[0]);
  }

  return 0;
}
