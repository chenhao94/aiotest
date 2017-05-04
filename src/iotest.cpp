#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>
#include <string>
#include <memory>
#include <new>
#include <chrono>

#include "iotest.hpp"

using namespace std;

size_t thread_num = 1;
size_t testType;
size_t workload;
size_t FILE_SIZE = 1 << 31;
size_t READ_SIZE = 1 << 12;
size_t WRITE_SIZE = 1 << 12;
size_t IO_ROUND = 1 << 10;
size_t SYNC_RATE = 1;
size_t WAIT_RATE = 1;

bool SINGLE_FILE = false;

thread_local int RandomWrite::tid = -1;

#ifdef __linux__
io_context_t LibAIOWrite::io_cxt;
#endif

