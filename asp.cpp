#include <vector>
#include <iostream>
#include <asio.hpp>
#include <optional>
#include <deque>
#include <unordered_map>


using asio::ip::tcp;

#include "poc2.hpp"

class worker_pool {
private:
  std::deque<worker> workers{};
  std::mutex mutex;
  std::condition_variable ready;
public:
  void add(worker&& w) {
    workers.push_back(std::move(w));
  }

  worker take() {
    std::unique_lock<std::mutex> guard(mutex);
    while(workers.size() == 0) {
      ready.wait(guard);
    }
    worker res = std::move(workers.front());
    workers.pop_front();
    return res;
  }

  void done(worker w) {
    std::unique_lock<std::mutex> guard(mutex);
    workers.push_back(std::move(w));
    ready.notify_one();
  }

  worker_provider getter() {
    return [this]() -> owned_worker {
      owned_worker owned(std::move(take()), *this);
      return owned;
    };
  }  
};

owned_worker::~owned_worker() { 
  if(live) {
    owner.done(std::move(w));
  }
}

class session : public std::enable_shared_from_this<session> {
public:

  session(asio::ip::tcp::socket&& socket, worker_provider sp)
    : client(std::move(socket))
    , provide(sp){
  }
  
  void exec() {
    write_capability(client);    
    std::string user_pass = get_login(client);
    
    wrapped_socket w_c(client);
    wrapped_socket_provider w_s_p(w_c);
    
    while(true) {
      std::optional<owned_worker> reply_to;
      try {
        reply_to.emplace(std::move(msg_transfer<owned_worker_provider, owned_worker>(client, owned_worker_provider(provide))));
      } catch (srceof const& e) {
	std::cerr << "client eof ... (went away)" << std::endl;
	break;
      }
      msg_transfer<wrapped_socket_provider, wrapped_socket>((*reply_to).get(), w_s_p);          
    }
  }

private:
  asio::ip::tcp::socket client;
  asio::streambuf streambuf;
  worker_provider provide;
};

class server {

public:
  server(asio::io_context& io, std::uint16_t port, worker_provider p)
    : io(io)
    , acceptor(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
    , prov(p)
  {}

  void async_accept() {
    socket.emplace(io);

    acceptor.async_accept(*socket, [&] (asio::error_code error) {
	auto new_session = std::make_shared<session>(std::move(*socket), prov);
	std::thread run_session([session = std::move(new_session)](){ session->exec(); });
	run_session.detach();
	//int id = new_session->session_id();
	//threads.emplace(id,
        //  [session = std::move(new_session),this](){
	//    int s_id = session->exec();
	//    dead_threads.push_back(s_id);
	//	});
	async_accept();
      });
  }

private:
  worker_provider prov;
  asio::io_context& io;
  asio::ip::tcp::acceptor acceptor;
  std::unordered_map<int, std::thread> threads;
  std::vector<int> dead_threads;
  std::optional<asio::ip::tcp::socket> socket;  
};


int main(int argc, char*argv[]) {
  asio::io_context io;
  worker w1(io, "127.0.0.1",8833);
  worker w2(io, "127.0.0.1",8844);
  worker_pool p;
  p.add(std::move(w1));
  p.add(std::move(w2));
  server srv(io, 8811, p.getter());
  srv.async_accept();
  io.run();
  return 0;  
}
