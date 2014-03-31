#include "log.h"

#include <boost/iostreams/stream_buffer.hpp>
#include <boost/iostreams/device/null.hpp>

int verbosity = 0;

static boost::iostreams::stream_buffer<boost::iostreams::null_sink>
null_buf{boost::iostreams::null_sink()};
std::ostream null_log(&null_buf);
