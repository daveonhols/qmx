#include <vector>
#include <iostream>
#include <asio.hpp>
#include <optional>
#include <deque>
#include <unordered_map>

#include "spdlog/spdlog.h"
#include "cxxopts.hpp"

#include "gw.hpp"

qmx::worker::owned_connection::~owned_connection() { 
  if(conn) {
    if(failed) {
      owner.fail(std::move(*conn));
    } else {      
      owner.done(std::move(*conn));
    }
  }
}

// worker => connection, connection_pool, owned_connection
// error => srceof, dsteof
// event => onclient onquery
// session => session
// util = conn_kdb

void set_log_level(std::string option) {

  if(0 == std::string("info").compare(option)) {
    spdlog::set_level(spdlog::level::info);
  }
  if(0 == std::string("error").compare(option)) {
    spdlog::set_level(spdlog::level::err);
  }
  if(0 == std::string("warn").compare(option)) {
    spdlog::set_level(spdlog::level::warn);
  }
  if(0 == std::string("critical").compare(option)) {
    spdlog::set_level(spdlog::level::critical);
  }
  if(0 == std::string("debug").compare(option)) {
    spdlog::set_level(spdlog::level::debug);
  }  
}

int main(int argc, char*argv[]) {

  spdlog::set_level(spdlog::level::debug);
  
  cxxopts::Options options("cpp-gateway", "Query gateway for Kdb");

  options.add_options()
    ("l,loglevel", "Log Level [info|error|warn|critical|debug]", cxxopts::value<std::string>())
    ("p,port", "Port to listen on ", cxxopts::value<int>())
    ("h,help", "Show usage")
    ("w,workers", "Comma separated list of worker host:port[,h:p ... ]", cxxopts::value<std::vector<std::string>>());

  auto parsed_opts = options.parse(argc, argv);

  if(parsed_opts.arguments().size() == 0) {
    spdlog::info(options.help());
    exit(EXIT_SUCCESS);
  }
  
  if(parsed_opts.count("loglevel") > 0) {
    spdlog::info("Log level found");
    std::string loglevel = parsed_opts["loglevel"].as<std::string>();    
  }

  if (parsed_opts.count("help")) {
    spdlog::info(options.help());
    exit(EXIT_SUCCESS);
  }

  std::optional<int> port{};
  if(parsed_opts.count("port") > 0) {
    port = parsed_opts["port"].as<int>();
  }
  if(!port) {
    throw std::invalid_argument("must specify a port for listening");
  }
  
  std::vector<std::string> worker_cfgs{};
  if(parsed_opts.count("workers") > 0) {
    worker_cfgs = parsed_opts["workers"].as<std::vector<std::string>>();
  }
  if(0==worker_cfgs.size()) {
    throw std::invalid_argument("must specify worker host:port[,h:p] list.");
  }
  
  asio::io_context io;
  
  qmx::worker::connection_pool p;
  std::thread run_recover(&qmx::worker::connection_pool::health_check, &p);

  for(auto& worker_cfg : worker_cfgs) {
    spdlog::debug("Adding worker config :: " + worker_cfg);
    p.add(qmx::worker::connection::build(io, worker_cfg));
  }

  spdlog::debug("Listening on port :: {0:d}", *port);
  
  qmx::server::server srv(io, *port, p.get_worker_provider());
  srv.async_accept();
  io.run();
  return 0;  
}

