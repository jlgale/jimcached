#include "buffer.h"
#include "cache.h"
#include "pool.h"
#include "tcp.h"
#include "config.h"
#include "service.h"
#include "utils.h"
#include "log.h"

#include <algorithm>
#include <cstring>
#include <cassert>
#include <unistd.h>
#include <err.h>
#include <thread>

static char *iface = nullptr;
static int tcp_port = 11211;
static int listen_backlog = 1024;
static bool daemonize = false;
static int max_memory_mb = 64;
static int num_threads = 4;

using std::cout;
using std::endl;

void usage()
{
  static const char *usage_msg[] = {
    PACKAGE_STRING,
    "-p <num> TCP port number to listen on (default: 11211)",
    "-d       run as a daemon",
    "-m <num> max memory to use for items in megabytes (default: 64 MB)",
    "-c <num> max simultaneous connections (default: 1024)",
    "-v       verbose (print errors/warnings while in event loop)",
    "-vv      very verbose (also print client commands/reponses)",
    "-vvv     extremely verbose (also print internal state transitions)",
    "-h       print this help and exit",
    "-t <num> number of threads to use (default: 4)",
    NULL
  };
  for (int i = 0; usage_msg[i]; ++i)
    cout << usage_msg[i] << endl;
}

void parse_commandline(int argc, char **argv)
{
  int ch;
  while ((ch = getopt(argc, argv, "p:dm:c:vht:")) != -1) {
    switch (ch) {
    case 'p':
      tcp_port = atoi(optarg);
      break;
    case 'd':
      daemonize = true;
      break;
    case 'm':
      max_memory_mb = atoi(optarg);
      break;
    case 'c':
      listen_backlog = atoi(optarg);
      break;
    case 'v':
      verbosity++;
      break;
    case 'h':
      usage();
      exit(0);
    case 't':
      num_threads = atoi(optarg);
      break;
    case '?':
    default:
      fprintf(stderr, "Illegal argument \"%c\"\n", ch);
      exit(2);
    }
  }
}



int main(int argc, char **argv)
{
  parse_commandline(argc, argv);
  if (daemonize) {
    if (daemon(0, 0))
      err(1, NULL);
  }
  cache c((size_t)max_memory_mb * 1024 * 1024);
  io_service_pool io_pool(num_threads);
  service s(c, std::clog);
  std::unique_ptr<tcp_server, decltype(&tcp_server_delete)> tcp
    (tcp_server_new(c, std::clog, iface, tcp_port, listen_backlog, io_pool),
     tcp_server_delete);
  io_pool.run();
}
