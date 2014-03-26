//
// io_service_pool.cpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "pool.h"
#include "cpu.h"
#include "gc.h"
#include <stdexcept>
#include <thread>
#include <boost/iterator/transform_iterator.hpp>

namespace {

// Check garbage collection every so many milliseconds
const int gc_wakeup_ms = 500;

void
thread_timer(boost::asio::deadline_timer *timer)
{
  timer->expires_from_now(boost::posix_time::milliseconds(gc_wakeup_ms));
  gc_lock();
  gc_unlock();
  timer->async_wait(std::bind(thread_timer, timer));
}

std::thread
thread_constructor(boost::asio::io_service &io)
{
  return std::thread([&]() {
      cpu_init();
      boost::asio::deadline_timer timer(io);
      thread_timer(&timer);
      io.run();
      cpu_exit();
      timer.cancel();           // XXX - racey?
    });
}

}

using boost::make_transform_iterator;

io_service_pool::io_service_pool(size_t pool_size)
  : io_services_(pool_size),
    work_(io_services_.begin(), io_services_.end()),
    next_io_service_(0) {}

void io_service_pool::run()
{
  std::vector<std::thread>
    threads(make_transform_iterator(io_services_.begin(), thread_constructor),
            make_transform_iterator(io_services_.end(), thread_constructor));

  for (std::thread &t : threads)
    t.join();
}

void io_service_pool::stop()
{
  std::clog << "stop!" << std::endl;
  // Explicitly stop all io_services.
  for (boost::asio::io_service &io : io_services_)
    io.stop();
}

boost::asio::io_service& io_service_pool::get_io_service()
{
  // Use a round-robin scheme to choose the next io_service to use.
  size_t idx = (next_io_service_++) % io_services_.size();
  return io_services_[idx];
}
