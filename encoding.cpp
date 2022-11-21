#include "encoding.h"

#include <cstddef>
#include <future>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <vector>

#include "log.h"

#include "util/ThreadPool.h"

#ifndef PATH_MAX_LEN
#define PATH_MAX_LEN 512
#endif

// #define __USE_THREADPOOL__
#define THREAD_NUM 12

static off_t MAX_BUFFER_SIZE = 1UL * 1024 * 1024;
static State state;

void setBufferSize(off_t buffer_size_) { MAX_BUFFER_SIZE = buffer_size_; }
/*
 * caculte the xor value and save to lhs
 */
void symbolXor(char *lhs, const char *rhs, off_t symbol_size) {
  state.xor_start();
  for (off_t i = 0; i < symbol_size; i++) {
    // printf("xor: %x ^ %x = %x \n", lhs[i], rhs[i], lhs[i] ^ rhs[i]);
    lhs[i] = lhs[i] ^ rhs[i];
  }
  state.xor_end();
}

void symbolXor(const char *lhs, const char *rhs, char *dst, off_t symbol_size) {
  for (off_t i = 0; i < symbol_size; i++) {
    dst[i] = lhs[i] ^ rhs[i];
  }
}

void caculateXor(char *pre_row_parity, char *pre_diag, char *cur_data,
                 off_t symbol_size, int p, int col_idx) {
  for (int i = 0; i < p - 1; i++) {
    int diag_idx = (i + col_idx) % p;
    symbolXor(pre_diag + diag_idx * symbol_size, cur_data + i * symbol_size,
              symbol_size);
    symbolXor(pre_row_parity + i * symbol_size, cur_data + i * symbol_size,
              symbol_size);
  }
}

int write_remaining_file(const char *filename, int p, int i, char *buffer,
                         off_t last_size) {
  char output_path[PATH_MAX_LEN];

  // open file
  sprintf(output_path, "./disk_%d/%s.remaining", i, filename);
  int col_fd = open(output_path, O_CREAT | O_WRONLY, S_IRWXU);
  if (col_fd < 0) {
    perror("Error: can't create file");
    return -1;
  }
  off_t size = write(col_fd, buffer, last_size);
  if (size != last_size) {
    perror("Error: remaining, write don't completely");
    // return RC::WRITE_COMPLETE;
    close(col_fd);
    return -1;
  }
  close(col_fd);
  return col_fd;
}

int write_col_file(const char *filename, int p, int i, char *buffer,
                   off_t write_size, bool flag = false) {
  char output_path[PATH_MAX_LEN];
  struct stat st;
  sprintf(output_path, "./disk_%d", i);
  if (stat(output_path, &st) == -1) {
    mkdir(output_path, 0700); // mode 0700 = read,write,execute only for owner
  }
  // open file
  sprintf(output_path, "./disk_%d/%s", i, filename);
  int col_fd;
  if (flag) {
    col_fd = open(output_path, O_APPEND | O_WRONLY);
  } else {
    col_fd = open(output_path, O_CREAT | O_WRONLY, S_IRWXU);
    off_t size_ = write(col_fd, (void *)(&p), sizeof(p));
    if (size_ != sizeof(p)) {
      perror("Error: col, write don't completely");
      // return RC::WRITE_COMPLETE;
      close(col_fd);
      return -1;
    }
  }
  if (col_fd < 0) {
    perror("Error: can't create file");
    return -1;
  }
  off_t size = write(col_fd, buffer, write_size);
  if (size != write_size) {
    perror("Error: col, write don't completely");
    // return RC::WRITE_COMPLETE;
    close(col_fd);
    return -1;
  }
  close(col_fd);
  return col_fd;
}

RC bigFileEncode() { return RC::SUCCESS; }

RC partEncode(int fd, off_t offset, off_t encode_size,
              const char *save_filename, int p) {

  if (fd < 0) {
    return RC::FILE_NOT_EXIST;
  }
  // file size in bytes
  off_t file_size = encode_size;
  off_t symbol_size = file_size / ((p - 1) * (p));

  // last symbol remaining, this will add to the tail of row parity directory
  off_t last_size = file_size - symbol_size * (p - 1) * p;

  off_t buffer_size_ = (p - 1) * symbol_size;
  if (buffer_size_ > MAX_BUFFER_SIZE) {
    LOG_ERROR("buffer need: %llu: ", buffer_size_);
    return RC::BUFFER_OVERFLOW;
  }
  // TODO: search the smallest buffer_size % 4K == 0 and >= buffer_size_
  off_t buffer_size = buffer_size_;

  char *buffer = new char[buffer_size + last_size];
  char *col_buffer = new char[buffer_size];
  char *diag_buffer = new char[p * symbol_size];

  if (!(buffer && col_buffer && diag_buffer)) {
    LOG_INFO("new buffer fails");
    return RC::BUFFER_OVERFLOW;
  }

  memset(col_buffer, 0, buffer_size);
  memset(diag_buffer, 0, p * symbol_size);
  const char *filename = basename(save_filename);
  off_t file_offset = offset;
  for (int i = 0; i < p; i++) {
    int read_size = pread(fd, buffer, buffer_size, file_offset);
    if (read_size != buffer_size) {
      perror("Error: ");
      LOG_INFO("Error: can't read");
      return RC::WRITE_COMPLETE;
    }

    file_offset += read_size;
    int col_fd = write_col_file(filename, p, i, buffer, buffer_size);
    caculateXor(col_buffer, diag_buffer, buffer, symbol_size, p, i);
  }

  // write row parity
  int col_fd = write_col_file(filename, p, p, col_buffer, buffer_size);
  // write diag parity file
  for (int i = 0; i < p - 1; i++) {
    symbolXor(diag_buffer + i * symbol_size,
              diag_buffer + (p - 1) * symbol_size, symbol_size);
  }
  col_fd = write_col_file(filename, p, p + 1, diag_buffer, buffer_size);

  // remaining file, just duplicate it as: filename_remaning in p and p+1 disk.
  if (last_size > 0) {
    pread(fd, buffer, last_size, file_offset);
    // write remaining file to the tail of file in disk p-1
    write_col_file(filename, p, p - 1, buffer, last_size, true);

    write_remaining_file(filename, p, p, buffer, last_size);
    write_remaining_file(filename, p, p + 1, buffer, last_size);
  }
  delete[] buffer;
  buffer = nullptr;
  delete[] col_buffer;
  col_buffer = nullptr;
  delete[] diag_buffer;
  diag_buffer = nullptr;

  return RC::SUCCESS;
}

RC thread_partEncode(int fd, off_t offset, off_t encode_size,
                     const char *file_name, int p, int _idx) {

  if (fd < 0) {
    return RC::FILE_NOT_EXIST;
  }
  // file size in bytes
  off_t file_size = encode_size;
  off_t symbol_size = file_size / ((p - 1) * (p));

  // last symbol remaining, this will add to the tail of row parity directory
  off_t last_size = file_size - symbol_size * (p - 1) * p;

  off_t buffer_size_ = (p - 1) * symbol_size;
  if (buffer_size_ > MAX_BUFFER_SIZE) {
    LOG_ERROR("buffer need: %llu: ", buffer_size_);
    return RC::BUFFER_OVERFLOW;
  }
  // TODO: search the smallest buffer_size % 4K == 0 and >= buffer_size_
  off_t buffer_size = buffer_size_;

  char *buffer = new char[buffer_size + last_size];
  char *col_buffer = new char[buffer_size];
  char *diag_buffer = new char[p * symbol_size];

  if (!(buffer && col_buffer && diag_buffer)) {
    LOG_INFO("new buffer fails");
    return RC::BUFFER_OVERFLOW;
  }
  char save_filename[PATH_MAX_LEN];
  sprintf(save_filename, "%s.%d", file_name, _idx);
  memset(col_buffer, 0, buffer_size);
  memset(diag_buffer, 0, p * symbol_size);
  const char *filename = basename(save_filename);
  off_t file_offset = offset;
  for (int i = 0; i < p; i++) {
    int read_size = pread(fd, buffer, buffer_size, file_offset);
    if (read_size != buffer_size) {
      perror("Error: ");
      LOG_INFO("Error: can't read");
      return RC::WRITE_COMPLETE;
    }

    file_offset += read_size;
    int col_fd = write_col_file(filename, p, i, buffer, buffer_size);
    caculateXor(col_buffer, diag_buffer, buffer, symbol_size, p, i);
  }

  // write row parity
  int col_fd = write_col_file(filename, p, p, col_buffer, buffer_size);
  // write diag parity file
  for (int i = 0; i < p - 1; i++) {
    symbolXor(diag_buffer + i * symbol_size,
              diag_buffer + (p - 1) * symbol_size, symbol_size);
  }
  col_fd = write_col_file(filename, p, p + 1, diag_buffer, buffer_size);

  // remaining file, just duplicate it as: filename_remaning in p and p+1 disk.
  if (last_size > 0) {
    pread(fd, buffer, last_size, file_offset);
    // write remaining file to the tail of file in disk p-1
    write_col_file(filename, p, p - 1, buffer, last_size, true);

    write_remaining_file(filename, p, p, buffer, last_size);
    write_remaining_file(filename, p, p + 1, buffer, last_size);
  }
  delete[] buffer;
  buffer = nullptr;
  delete[] col_buffer;
  col_buffer = nullptr;
  delete[] diag_buffer;
  diag_buffer = nullptr;

  return RC::SUCCESS;
}

/*
 * Please encode the input file with EVENODD code
 * and store the erasure-coded splits into corresponding disks
 * For example: Suppose "file_name" is "testfile", and "p" is 5. After your
 * encoding logic, there should be 7 splits, "testfile_0", "testfile_1",
 * ..., "testfile_6", stored in 7 diffrent disk folders from "disk_0" to
 * "disk_6".
 */
RC encode(const char *path, int p) {
  state.start();

  RC rc = RC::SUCCESS;
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("error, can't open file");
    return RC::FILE_NOT_EXIST;
  }
  struct stat stat_;
  fstat(fd, &stat_);

  // file size in bytes
  size_t file_size = stat_.st_size;
  const char *filename = basename(path);
  char save_filename[PATH_MAX_LEN];

  int split_num = file_size / (MAX_BUFFER_SIZE * p);

#ifndef __USE_THREADPOOL__
  for (int i = 0; i < split_num; i++) {
    sprintf(save_filename, "%s.%d", filename, i);
    rc = partEncode(fd, i * (MAX_BUFFER_SIZE * p), (MAX_BUFFER_SIZE * p),
                    save_filename, p);
    if (rc != RC::SUCCESS) {
      close(fd);
      LOG_ERROR("error,");
      return rc;
    }
  }
#endif

#ifdef __USE_THREADPOOL__
  {
    int thread_num = THREAD_NUM;
    ThreadPool pool(thread_num);
    std::vector<std::future<RC>> results;
    for (int i = 0; i < split_num; i++) {
      // sprintf(save_filename, "%s.%d", filename, i);
      results.emplace_back(pool.enqueue(thread_partEncode, fd,
                                        i * (MAX_BUFFER_SIZE * p),
                                        (MAX_BUFFER_SIZE * p), filename, p, i));
    }
    for (auto &&result : results) {
      rc = result.get();
      // LOG_DEBUG("%d", rc);
      if (rc != RC::SUCCESS) {
        close(fd);
        LOG_ERROR("error,");
        return rc;
      }
    }
  }
#endif

  off_t last_part_size = file_size % (MAX_BUFFER_SIZE * p);
  if (last_part_size != 0) {
    // if (split_num == 0) {
    //   sprintf(save_filename, "%s", filename);
    // } else
    sprintf(save_filename, "%s.%d", filename, split_num);
    rc = partEncode(fd, split_num * (MAX_BUFFER_SIZE * p), last_part_size,
                    save_filename, p);
    if (rc != RC::SUCCESS) {
      close(fd);
      LOG_ERROR("error: %d", rc);
      return rc;
    }
  }
  close(fd);

  state.end();
  state.print();

  return rc;
}

/*
 * just read from 0 ~ p-1 to recover file
 */
RC basicRead(const char *path, const char *save_as) {
  const char *filename = basename(path);
  char output_path[PATH_MAX_LEN];
  int write_fd = open(save_as, O_CREAT | O_WRONLY, S_IRWXU);
  if (write_fd < 0) {
    perror("Error:  can't create file !");
    return RC::FILE_NOT_EXIST;
  }
  struct stat st = {0};
  // try to read from ./disk_0/filename
  sprintf(output_path, "./disk_%d", 0);
  if (stat(output_path, &st) == -1) {
    perror("Error: directory doesn't exist !");
    return RC::FILE_NOT_EXIST;
  }
  sprintf(output_path, "./disk_%d/%s", 0, filename);
  int fd = open(output_path, O_RDONLY);
  int p = 0;
  int read_size_ = read(fd, (void *)(&p), sizeof(p));
  if (read_size_ != sizeof(p)) {
    perror("can't read p!");
    return RC::FILE_NOT_EXIST;
  }

  int buffer_size = 4 * 1024 * 1024;
  char *buffer = new char[buffer_size];
  int tmp = 0;
  for (int i = 0; i < p; i++) {
    if (i != 0) {
      sprintf(output_path, "./disk_%d", i);
      if (stat(output_path, &st) == -1) {
        perror("Error: directory doesn't exist !");
        return RC::FILE_NOT_EXIST;
      }
      sprintf(output_path, "./disk_%d/%s", i, filename);
      fd = open(output_path, O_RDONLY);
      if (fd < 0) {
        perror("Error: can't open such file!");
        return RC::FILE_NOT_EXIST;
      }
      int read_size_ = read(fd, (void *)(&tmp), sizeof(tmp));
      if (read_size_ != sizeof(tmp)) {
        perror("can't read p!");
        return RC::FILE_NOT_EXIST;
      }
      if (tmp != p) {
        printf("p: %d %d\n", tmp, p);
        perror("Error: p saved diff!");
        return RC::FILE_NOT_EXIST;
      }
    }
    int read_size = 0;
    do {
      read_size = read(fd, buffer, buffer_size);
      if (read_size < 0) {
        perror("can't read");
      }
      int write_size = write(write_fd, buffer, read_size);
      if (write_size < 0) {
        perror("can't write!");
      }
      // printf("read size: %d, write size: %d\n", read_size, write_size);
    } while (read_size == buffer_size);
    close(fd);
  }
  close(write_fd);
  delete[] buffer;

  return RC::SUCCESS;
}

RC seqXor(const char *path, std::vector<int> &xor_idxs, int p, char *col_buffer,
          char *diag_buffer, int symbol_size) {

  // TODO: if buffer_size_ > 4UL * 1024 * 1024 * 1024 Bytes
  off_t buffer_size_ = (p - 1) * symbol_size;

  // TODO: search the smallest buffer_size % 4K == 0 and >= buffer_size_
  off_t buffer_size = buffer_size_;
  char *buffer = new char[buffer_size];
  memset(buffer, 0, buffer_size);
  const char *filename = basename(path);

  off_t file_offset = 0;
  char output_path[PATH_MAX_LEN];

  int num = xor_idxs.size();
  for (int i = 0; i < num; i++) {
    int idx = xor_idxs[i];

    sprintf(output_path, "./disk_%d/%s", idx, filename);

    printf("read file:%s\n", output_path);

    int fd = open(output_path, O_RDONLY);
    if (fd < 0) {
      perror("can't open file");
      return RC::FILE_NOT_EXIST;
    }
    int save_p = 0;
    int read_size = read(fd, (void *)(&save_p), sizeof(save_p));
    if (read_size != sizeof(save_p)) {
      perror("can't read file");
      return RC::FILE_NOT_EXIST;
    }
    file_offset += read_size;

    read_size = read(fd, buffer, buffer_size);
    if (read_size != buffer_size) {
      perror("can't read file");
      return RC::FILE_NOT_EXIST;
    }
    file_offset += read_size;
    caculateXor(col_buffer, diag_buffer, buffer, symbol_size, p, idx);
    close(fd);
  }
  delete[] buffer;
  return RC::SUCCESS;
}

/*
 * failIdx is increasing
 */
RC repairSingleFile(const char *filename, int *fail_idxs, int num, int p) {

  //
  char output_path[PATH_MAX_LEN];
  sprintf(output_path, "./disk_%d/%s", 0, filename);
  int _fd = open(output_path, O_RDONLY);
  struct stat stat_;
  fstat(_fd, &stat_);
  off_t file_size_ = stat_.st_size;
  off_t symbol_size = (file_size_ - sizeof(int)) / (p - 1);
  // printf("file size: %lu symbol size: %lu\n", file_size_, symbol_size);

  if (num > 2 || num <= 0) {
    return RC::CANNOT_REPAIR;
  }
  if (num == 1) {
    // situation 1: 1 disk fails
    int fail_idx = fail_idxs[0];

  } else {
    // situation 2: 2 disks fail

    if (fail_idxs[0] >= p) {
      // p, p+1 fail, just redo encode

    } else if (fail_idxs[1] == p) {
      // p fails aka row parity fails, another is in disk_{0- (p-1)}

      std::vector<int> xor_idxs;
      for (int i = 0; i < p; i++) {
        if (i != fail_idxs[0] && i != fail_idxs[1])
          xor_idxs.push_back(i);
      }
      // caculate syndrome

      char *diag_buffer = new char[symbol_size * (p + 1)];
      char *col_buffer = new char[symbol_size * p];
      char *diag_parity = new char[symbol_size * (p - 1)];

      memset(diag_buffer, 0, (p + 1) * symbol_size);
      memset(col_buffer, 0, (p)*symbol_size);
      memset(diag_parity, 0, symbol_size * (p - 1));

      sprintf(output_path, "./disk_%d/%s", p + 1, filename);
      int diag_fd = open(output_path, O_RDONLY);
      int tmp_p;
      read(diag_fd, (void *)(&tmp_p), sizeof(tmp_p));
      read(diag_fd, diag_parity, symbol_size * (p - 1));
      close(diag_fd);

      RC rc =
          seqXor(filename, xor_idxs, p, col_buffer, diag_buffer, symbol_size);
      if (rc != RC::SUCCESS) {
        delete[] diag_parity;
        delete[] col_buffer;
        delete[] diag_buffer;
        return rc;
      }

      // syndrome is stored in {diag_buffer + (p)*symbol_size}
      if (fail_idxs[0] != 0) {
        int special_idx = (fail_idxs[0] + p - 1) % p;
        symbolXor(diag_buffer + special_idx * symbol_size,
                  diag_parity + special_idx * symbol_size,
                  diag_buffer + (p)*symbol_size, symbol_size);
      } else {
        memcpy(diag_buffer + (p)*symbol_size,
               diag_buffer + (p - 1) * symbol_size, symbol_size);
      }
      // recover col_file fail_idxs[0]
      for (int i = 0; i < p - 1; i++) {
        int diag_idx = (i + fail_idxs[0]) % p;
        if (diag_idx != (p - 1)) {
          symbolXor(diag_parity + diag_idx * symbol_size,
                    diag_buffer + diag_idx * symbol_size,
                    col_buffer + i * symbol_size, symbol_size);
          symbolXor(col_buffer + i * symbol_size, diag_buffer + (p)*symbol_size,
                    symbol_size);
        } else {
          symbolXor(diag_buffer + diag_idx * symbol_size,
                    diag_buffer + (p)*symbol_size, col_buffer + i * symbol_size,
                    symbol_size);
        }
      }
      write_col_file(filename, p, fail_idxs[0], col_buffer,
                     symbol_size * (p - 1));

      printf("repair symbol size:%lu size: %lu \n", symbol_size,
             symbol_size * (p - 1));
      delete[] diag_parity;
      delete[] col_buffer;
      delete[] diag_buffer;

    } else if (fail_idxs[1] == p + 1) {
      printf("repair symbol size:%lu size: %lu , p is %d\n", symbol_size,
             symbol_size * (p - 1), p);
      // p+1 fails aka diagonal parity fails, another is in disk_{1 - (p-1)}
      std::vector<int> xor_idxs;
      for (int i = 0; i < p + 1; i++) {
        if (i != fail_idxs[0] && i != fail_idxs[1])
          xor_idxs.push_back(i);
      }

      char *diag_buffer = new char[symbol_size * (p + 1)];
      char *col_buffer = new char[symbol_size * p];

      memset(diag_buffer, 0, (p + 1) * symbol_size);
      memset(col_buffer, 0, (p)*symbol_size);

      RC rc =
          seqXor(filename, xor_idxs, p, col_buffer, diag_buffer, symbol_size);
      if (rc != RC::SUCCESS) {
        delete[] col_buffer;
        delete[] diag_buffer;
        return rc;
      }
      write_col_file(filename, p, fail_idxs[0], col_buffer,
                     symbol_size * (p - 1));

      printf("repair symbol size:%lu size: %lu \n", symbol_size,
             symbol_size * (p - 1));
      // < p
    }
  }

  return RC::SUCCESS;
}