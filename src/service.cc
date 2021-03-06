#include "buffer.h"
#include "cache.h"
#include "service.h"
#include <cassert>
#include "log.h"

auto service::next() -> time_type
{
  time_type tt;
  int res = clock_gettime(clock, &tt);
  assert(res == 0);             // XXX
  tt.tv_sec += service_period_sec;
  return tt;
}

void service::run()
{
  gc_lock();
  log << INFO << "starting collect" << std::endl;
  c.collect();
  log << INFO << "collect complete" << std::endl;
  gc_unlock();
}

void service::loop()
{
  while (running) {
    time_type nxt = next();
    run();
    clock_nanosleep(clock, TIMER_ABSTIME, &nxt, NULL);
  }
}

void service::entry()
{
  cpu_init();
  loop();
  cpu_exit();
}

service::service(cache &c, std::ostream &log)
  : c(c), log(log), running(true), worker(std::bind(&service::entry, this))
{
}

service::~service()
{
  // XXX - lame
  running = false;
  worker.join();
}
