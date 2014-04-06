#include <functional>
#include <boost/asio.hpp>
#include "stream.h"

typedef std::function<void ()> binary_done;

class Binary;
Binary *binary_new(boost::asio::io_service &io_service,
                   class cache &c, stream &in, stream &out,
                   std::ostream &log, const char *prompt);
void binary_interact(Binary &session, binary_done done);
void binary_delete(Binary *);
