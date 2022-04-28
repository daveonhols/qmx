#include <vector>
#include <iostream>
#include <asio.hpp>
#include <optional>
#include <deque>
#include <unordered_map>


using asio::ip::tcp;

#include "gw.hpp"

qmx::worker::owned_connection::~owned_connection() { 
  if(live) {
    owner.done(std::move(conn));
  }
}

// worker => connection, connection_pool, owned_connection
// error => srceof, dsteof
// event => onclient onquery
// session => session
// util = conn_kdb


int main(int argc, char*argv[]) {
  asio::io_context io;
  qmx::worker::connection w1(io, "127.0.0.1",8833);
  qmx::worker::connection w2(io, "127.0.0.1",8844);
  qmx::worker::connection_pool p;
  p.add(std::move(w1));
  p.add(std::move(w2));
  qmx::server::server srv(io, 8811, p.getter());
  srv.async_accept();
  io.run();
  return 0;  
}

