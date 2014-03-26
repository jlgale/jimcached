/* -*-c++-*- */

#include <ostream>

class tcp_server;
tcp_server *
tcp_server_new(cache &c, std::ostream &log, char *iface,
               int memcache_port, int listen_backlog,
               io_service_pool &pool);
void tcp_server_delete(tcp_server *tcp);
