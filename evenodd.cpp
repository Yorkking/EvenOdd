#include <cstddef>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define PATH_MAX_LEN 60

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)                                                   \
  (byte & 0x80 ? '1' : '0'), (byte & 0x40 ? '1' : '0'),                        \
      (byte & 0x20 ? '1' : '0'), (byte & 0x10 ? '1' : '0'),                    \
      (byte & 0x08 ? '1' : '0'), (byte & 0x04 ? '1' : '0'),                    \
      (byte & 0x02 ? '1' : '0'), (byte & 0x01 ? '1' : '0')

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

enum RC {
  SUCCESS = 1,
  FILE_NOT_EXIST,
  CANNOT_CREAT_FILE,
  WRITE_COMPLETE,
};

struct stat st = {0};

void usage() {
  printf("./evenodd write <file_name> <p>\n");
  printf("./evenodd read <file_name> <save_as>\n");
  printf("./evenodd repair <number_erasures> <idx0> ...\n");
}

/*
 * Please encode the input file with EVENODD code
 * and store the erasure-coded splits into corresponding disks
 * For example: Suppose "file_name" is "testfile", and "p" is 5. After your
 * encoding logic, there should be 7 splits, "testfile_0", "testfile_1",
 * ..., "testfile_6", stored in 7 diffrent disk folders from "disk_0" to
 * "disk_6".
 */

/*
 * caculte the xor value and save to lhs
 */
void symbolXor(char *lhs, const char *rhs, size_t symbol_size) {
  for (size_t i = 0; i < symbol_size; i++) {
    lhs[i] = lhs[i] ^ rhs[i];
  }
}

void caculateXor(char *pre_row_parity, char *pre_diag, char *cur_data,
                 size_t symbol_size, int num, int col_idx) {
  for (int i = 0; i < num; i++) {
    int diag_idx = (i + col_idx) % num;
    symbolXor(pre_diag + diag_idx * symbol_size, cur_data + i * symbol_size,
              symbol_size);
    symbolXor(pre_row_parity + i * symbol_size, cur_data + i * symbol_size,
              symbol_size);
  }
}

int write_left_file(const char *path, const char *filename, int p, int i,
                    char *buffer, size_t last_size) {
  char output_path[PATH_MAX_LEN];

  // open file
  sprintf(output_path, "./disk_%d/%s_%d.left", i, filename, p);
  int col_fd = open(output_path, O_CREAT | O_WRONLY, S_IRWXU);
  if (col_fd < 0) {
    perror("Error: can't create file");
    return -1;
  }
  size_t size = write(col_fd, buffer, last_size);
  if (size != last_size) {
    perror("Error: left, write don't completely");
    // return RC::WRITE_COMPLETE;
    close(col_fd);
    return -1;
  }
  close(col_fd);
  return col_fd;
}

int write_col_file(const char *path, const char *filename, int p, int i,
                   char *buffer, size_t write_size, bool flag = false) {
  char output_path[PATH_MAX_LEN];
  sprintf(output_path, "./disk_%d", i);
  if (stat(output_path, &st) == -1) {
    mkdir(output_path, 0700); // mode 0700 = read,write,execute only for owner
  }
  // open file
  sprintf(output_path, "./disk_%d/%s_%d", i, filename, p);
  int col_fd;
  if (flag) {
    col_fd = open(output_path, O_APPEND | O_WRONLY);
  } else
    col_fd = open(output_path, O_CREAT | O_WRONLY, S_IRWXU);
  if (col_fd < 0) {
    perror("Error: can't create file");
    return -1;
  }
  size_t size = write(col_fd, buffer, write_size);
  if (size != write_size) {
    perror("Error: col, write don't completely");
    // return RC::WRITE_COMPLETE;
    close(col_fd);
    return -1;
  }
  close(col_fd);
  return col_fd;
}

RC encode(const char *path, int p) {
  size_t fd = open(path, O_RDONLY);
  if (fd < 0) {
    return RC::FILE_NOT_EXIST;
  }
  struct stat stat_;
  fstat(fd, &stat_);

  // file size in bytes
  size_t file_size = stat_.st_size;

  size_t symbol_size = file_size / ((p - 1) * (p));
  // last symbol left, this will add to the tail of row parity directory
  size_t last_size = file_size - symbol_size * (p - 1) * p;
  // TODO: if buffer_size > 4UL * 1024 * 1024 * 1024 Bytes

  size_t buffer_size_ = (p - 1) * symbol_size;
  // TODO: search a the smallest buffer_size_ % 4K == 0 and >= buffer_size_
  size_t buffer_size = buffer_size_;

  // use this buffer to save row parity
  char *buffers[2];
  for (int i = 0; i < 2; i++) {
    buffers[i] = new char[buffer_size + last_size];
    memset(buffers[i], 0, buffer_size + last_size);
  }
  size_t file_offset = 0;
  file_offset += read(fd, buffers[0], buffer_size);

  const char *filename = basename(path);
  int col_fd = write_col_file(path, filename, p, 0, buffers[0], buffer_size);

  // use this diag_buffer to save diagonal parity
  char *diag_buffer = new char[p * symbol_size];
  memccpy(diag_buffer, buffers[0], 0, buffer_size);
  memset(diag_buffer + symbol_size * (p - 1), 0, symbol_size);
  int select_idx = 1;

  for (int i = 1; i < p; i++) {
    int read_size = read(fd, buffers[select_idx], buffer_size);
    file_offset += read_size;
    // create part file and write date
    int col_fd =
        write_col_file(path, filename, p, i, buffers[select_idx], buffer_size);
    // save middle result
    caculateXor(buffers[(select_idx + 1) % 2], diag_buffer, buffers[select_idx],
                symbol_size, p - 1, i);
    select_idx = (select_idx + 1) % 2;
  }

  // write row parity
  col_fd =
      write_col_file(path, filename, p, p, buffers[select_idx], buffer_size);
  // write diag parity file

  for (int i = 0; i < p - 1; i++) {
    symbolXor(diag_buffer + i * symbol_size,
              diag_buffer + (p - 1) * symbol_size, symbol_size);
  }
  col_fd = write_col_file(path, filename, p, p + 1, diag_buffer, buffer_size);

  // left file, just duplicate it as: filename_left in p and p+1 disk.
  if (last_size > 0) {
    read(fd, buffers[0], last_size);
    // disk p
    write_col_file(path, filename, p, p - 1, buffers[0], last_size, true);
    write_left_file(path, filename, p, p, buffers[0], last_size);
    write_left_file(path, filename, p, p + 1, buffers[0], last_size);
  }
  close(fd);
  for (int i = 0; i < 2; i++) {
    delete[] buffers[i];
  }
  delete[] diag_buffer;

  // fsync all col files
  return RC::SUCCESS;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return -1;
  }

  char *op = argv[1];
  if (strcmp(op, "write") == 0) {

    if (argc < 4) {
      usage();
      return -1;
    }
    char *file_path = argv[2];
    int p = atoi(argv[3]);
    // myWrite(file_path, p);
    // 按列划分下来。
    RC error_code = encode(file_path, p);

  } else if (strcmp(op, "read") == 0) {
    /*
     * Please read the file specified by "file_name", and store it as a file
     * named "save_as" in the local file system.
     * For example: Suppose "file_name" is "testfile" (which we have encoded
     * before), and "save_as" is "tmp_file". After the read operation, there
     * should be a file named "tmp_file", which is the same as "testfile".
     */
  } else if (strcmp(op, "repair") == 0) {
    /*
     * Please repair failed disks. The number of failures is specified by
     * "num_erasures", and the index of disks are provided in the command
     * line parameters.
     * For example: Suppose "number_erasures" is 2, and the indices of
     * failed disks are "0" and "1". After the repair operation, the data
     * splits in folder "disk_0" and "disk_1" should be repaired.
     */
  } else {
    printf("Non-supported operations!\n");
  }
  return 0;
}

void myWrite(char *file_path, int p) {
  if (p <= 0) {
    printf("p should greater than 0 but p = %d", p);
    return;
  }

  /* init */
  // 假定输入为大小 (p-1)*p, 即有 p 列，每列有 p-1 个元素
  char **data = (char **)malloc(sizeof(char *) * p);
  for (int i = 0; i < p; i++) {
    data[i] = (char *)malloc(sizeof(char) * (p - 1));
    memset(data[i], 0, p - 1);
  }
  char *row_parity = (char *)malloc(sizeof(char) * (p - 1));
  char *diagonal_parity = (char *)malloc(sizeof(char) * (p - 1));

  /* read input file */
  FILE *input = fopen(file_path, "rb");
  if (input == NULL) {
    printf("can't find the file %s", file_path);
    return;
  }
  int num = 0;
  while (num < p && fread(data[num], sizeof(char), p - 1, input)) {
    num++;
  }
  fclose(input);

  // check input data
  // for(int i = 0;i < p;i++){
  //     printf("column %d:", i);
  //     for(int j = 0;j < p-1;j++){
  //         printf(BYTE_TO_BINARY_PATTERN",", BYTE_TO_BINARY(data[i][j]));
  //     }
  //     printf("\n");
  // }

  /* encoding */
  int S = 0;
  for (int i = 0; i < p; i++) {
    for (int j = 0; j < p - 1; j++) {
      row_parity[j] ^= data[i][j];
      if (i + j == p - 1) {
        S ^= data[i][j];
      } else if (i + j < p - 1) {
        diagonal_parity[i + j] ^= data[i][j];
      } else {
        diagonal_parity[i + j - p] ^= data[i][j];
      }
    }
  }
  for (int i = 0; i < p - 1; i++) {
    diagonal_parity[i] ^= S;
  }

  // just for test: output encoding column
  printf("row parity:");
  for (int i = 0; i < p - 1; i++) {
    printf(BYTE_TO_BINARY_PATTERN ",", BYTE_TO_BINARY(row_parity[i]));
  }
  printf("\ndiagonal parity:");
  for (int i = 0; i < p - 1; i++) {
    printf(BYTE_TO_BINARY_PATTERN ",", BYTE_TO_BINARY(diagonal_parity[i]));
  }
  printf("\n");

  /* write to disk */
  char *filename = basename(file_path);
  FILE *output;
  char output_path[PATH_MAX_LEN];
  size_t write_count = 0;
  for (int i = 0; i < p + 2; i++) {
    // create directory if not exist
    sprintf(output_path, "./disk/disk_%d", i);
    if (stat(output_path, &st) == -1) {
      mkdir(output_path, 0700); // mode 0700 = read,write,execute only for owner
    }

    // open file
    sprintf(output_path, "./disk/disk_%d/%s_%d", i, filename, i);
    output = fopen(output_path, "wb");
    if (!output) {
      perror("Error in creating file");
      exit(1);
    }

    // write file
    char *ptr;
    if (i < p) {
      ptr = data[i];
    } else if (i == p) {
      ptr = row_parity;
    } else {
      ptr = diagonal_parity;
    }
    write_count += fwrite(ptr, sizeof(char), p - 1, output);
    fclose(output);
  }

  printf("Totally write %ld elements\n", write_count);

  for (int i = 0; i < p; i++)
    free(data[i]);
  free(data);
  free(row_parity);
  free(diagonal_parity);
}
