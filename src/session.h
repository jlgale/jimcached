/* -*-c++-*- */
#include <functional>

#include <boost/asio.hpp>
#include "stream.h"

typedef std::function<void ()> session_done;

class Session;
Session *session_new(boost::asio::io_service &io_service,
                     class cache &c, stream &in, stream &out,
                     std::ostream &log, const char *prompt);
void session_interact(Session &session, session_done done);
void session_delete(Session *);
