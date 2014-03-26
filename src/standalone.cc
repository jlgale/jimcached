#include "buffer.h"
#include "cache.h"
#include "session.h"

#include <algorithm>
#include <cstring>
#include <cassert>

static constexpr size_t max_bytes = 128 * 1024 * 1024;

int main(int argc, char **argv)
{
  std::ostream &log = std::cerr;
  boost::asio::io_service io_service;
  boost::asio::posix::stream_descriptor input(io_service, STDIN_FILENO);
  boost::asio::posix::stream_descriptor output(io_service, STDOUT_FILENO);
  descriptor_stream inputs(input);
  descriptor_stream outputs(output);
  
  cache c(max_bytes);
  std::unique_ptr<Session, decltype(&session_delete)>
    s(session_new(io_service, c, inputs, outputs, log, "jimcache> "),
      session_delete);
  io_service.post([&]() {
      session_interact(*s, [&](){
          io_service.stop();
        });
    });
  io_service.run();
  return 0;
}
