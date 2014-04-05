#include "buffer.h"
#include "cache.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <unistd.h>

static cache *cc = nullptr;
static int collect_usec = 1000;
static int nthreads = 1;
static int inserts = 100;
static int nkeys = 100;
static bool stopping = false;
static constexpr size_t max_bytes = 16 * 1024 * 1024;

static void
collect_worker()
{
  cpu_init();
  gc_checkpoint();
  while (!stopping) {
    usleep(collect_usec);
    cc->collect();
    //gc_flush();
    gc_checkpoint();
  }
  gc_exit();
}

static void
insert_worker(int id)
{
  cpu_init();
  gc_checkpoint();
  char kstr[64], vstr[64];
  for (int i = 0; i < inserts; ++i) {
    snprintf(kstr, sizeof(kstr), "%ld", random() % nkeys);
    buf k(kstr, (int)strlen(kstr));
    snprintf(vstr, sizeof(vstr), "%d", id * 1000000 + i);
    mem *vmem = mem_alloc(strlen(vstr));
    memcpy(vmem->data, vstr, vmem->size);
    rope r(vmem, vmem);
    cache_error_t err = cc->set(k, 0, 0, r);
    if (err != cache_error_t::stored) {
      // poo happens
      // XXX - freed by set on error?
      //mem_free(vmem);
    }
    gc_checkpoint();
  }
  gc_exit();
}

static void
insert_test()
{
  std::thread *threads[nthreads];
  for (int i = 0; i < nthreads; ++i)
    threads[i] = new std::thread(insert_worker, i);
  for (int i = 0; i < nthreads; ++i) {
    threads[i]->join();
    delete threads[i];
  }
}

static void
usage()
{
  printf("loadtest [options]\n"
         "\n"
         "  -c periodically collect\n"
         "  -k number of keys\n"
         "  -n number of inserts per thread\n"
         "  -t number of threads\n");
  exit(1);
}

int main(int argc, char** argv)
{
  int ch;
  bool collect = false;
  std::thread *collector = nullptr;
  while ((ch = getopt(argc, argv, "c:k:n:t:")) != -1) {
    switch (ch) {
    case 'c':
      collect_usec = atoi(optarg);
      collect = true;
      break;
    case 'k':
      nkeys = atoi(optarg);
      break;
    case 'n':
      inserts = atoi(optarg);
      break;
    case 't':
      nthreads = atoi(optarg);
      break;
    case '?':
    default:
      usage();
    }
  }
  argc -= optind;
  argv += optind;
  cc = new cache(max_bytes);
  if (collect)
    collector = new std::thread(collect_worker);
  insert_test();
  if (collect) {
    stopping = true;
    collector->join();
    delete collector;
  }
  gc_finish();
  delete cc;
}
