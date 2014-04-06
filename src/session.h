/* -*-c++-*- */
#include <functional>

#include <boost/asio.hpp>
#include "stream.h"

// base session class
typedef std::function<void ()> session_done;
class session
{
public:
  virtual ~session() { }
  virtual void interact(session_done done) = 0;
};

session *text_session_new(boost::asio::io_service &io_service,
                          class cache &c, stream &in, stream &out,
                          std::ostream &log, const char *prompt);
session *binary_session_new(boost::asio::io_service &io_service,
                            class cache &c, stream &in, stream &out,
                            std::ostream &log);
