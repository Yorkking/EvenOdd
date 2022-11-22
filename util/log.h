#pragma once
#include <bits/types/struct_timeval.h>
#include <cstddef>
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>

enum LOG_LEVEL {
  LOG_LEVEL_ERR = 1,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_LAST
};

int __log_output__(LOG_LEVEL level, const char *file_name, int line,
                   const char *f, ...);

#define LOG_ERROR(fmt, ...)                                                    \
  LOG_OUTPUT(LOG_LEVEL::LOG_LEVEL_ERR, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)                                                     \
  LOG_OUTPUT(LOG_LEVEL::LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)                                                    \
  LOG_OUTPUT(LOG_LEVEL::LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

#define LOG_OUTPUT(level, fmt, ...)                                            \
  do {                                                                         \
    __log_output__(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__);             \
  } while (0)

class State {
public:
  float xor_cost = 0.0f;

  void xor_start() { gettimeofday(&_start, NULL); }
  void xor_end() {
    gettimeofday(&_end, NULL);
    float time =
        (_end.tv_sec - _start.tv_sec) + (_end.tv_usec - _start.tv_usec) / 1e6;
    xor_cost += time;
  }
  void start() { gettimeofday(&pro_start, NULL); }
  void end() { gettimeofday(&pro_end, NULL); }

  void print() {
    float time = (pro_end.tv_sec - pro_start.tv_sec) +
                 (pro_end.tv_usec - pro_start.tv_usec) / 1e6;
    LOG_DEBUG("total cost: %f, xor: %f", time, xor_cost);
  }

private:
  struct timeval pro_start;
  struct timeval pro_end;
  struct timeval _start;
  struct timeval _end;
};