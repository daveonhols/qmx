#include <thread>
#include <chrono>

namespace qmx {
  namespace util {
    namespace kdb {      
      tcp::socket get_socket(asio::io_context& io, std::string ip, int port) {
	asio::error_code err;
	asio::ip::address a_w = asio::ip::make_address(ip);
	asio::ip::basic_endpoint<tcp> e_w(a_w, port);
	tcp::socket s_w(io);
	std::vector<asio::ip::basic_endpoint<tcp>> eps_w{e_w};
	asio::connect(s_w, eps_w, err);
	std::vector<unsigned char> lgg{0x61, 0x62, 0x63, 0x3A, 0x61, 0x62, 0x63, 0x03, 0x00};
	asio::write(s_w, asio::buffer(lgg, lgg.size()), err);
	std::vector<unsigned char> cap{0x00};
	s_w.read_some(asio::buffer(cap, cap.size()));
	return s_w;    
      }
        
      void write_capability(tcp::socket& sock, unsigned char cap = 0x03) {
	asio::error_code ignored_error;
	std::vector<unsigned char> cap_resp{cap};
	asio::write(sock, asio::buffer(cap_resp), ignored_error);
      }

      std::string read_login(tcp::socket& client, std::vector<char>& write_extra) {
	asio::streambuf sbuf;
	// read_until can occasionally read past the delimiter
	// we need to hold onto those extra bytes as they are part of the user query
	// they are passed back out via ref write_extra
	int pwd_len = asio::read_until(client, sbuf, '\00');
	std::istream input(&sbuf);
	std::string pwd(pwd_len, ' ');
	input.read(&pwd[0], pwd_len);
	std::ostringstream remaining;
	remaining << input.rdbuf();
	std::string str_extra = remaining.str();
	std::copy(str_extra.begin(), str_extra.end(), std::back_inserter(write_extra));
	return pwd;
      }

      // note msg may be incomplete first read but will be len > 8
      // maybe doesn't work from Windows (endian stuffs)
      size_t get_msg_length(std::vector<char> msg) {
	int b_shift = 0;
	size_t len = 0;
	for(int i = 4; i < 8; ++i) {
	  len += ((unsigned char)msg[i] << b_shift);
          b_shift += 8;
	}
	return len;
      }

      std::vector<unsigned char> signal_bytes(std::string signal) {
	// it's going to be simpler if we don't signal more than 255 chars
	if(signal.length() > 240) {
	  signal = signal.substr(0, 240);
	}
	char len = 10 + signal.length() & 0xff;
	std::vector<unsigned char> msg{1, 2, 0, 0, 0, 0, 0, 0, 128};
	msg[4] = len;
	std::copy(signal.begin(), signal.end(), std::back_inserter(msg));
	msg.push_back(0);
	return msg;
      }      
    }   
  }
  
  namespace exception {
    class srceof : public std::exception {};
    class dsteof : public std::exception {};
    class srcbrokenpipe : public std::exception {};
    class dstbrokenpipe : public std::exception {};    
  }

  
  namespace worker {
    class connection_pool; // circular dependency between worker and pool containing it.
    
    class connection {

    public:
      connection(asio::io_context& io, std::string h, int p) : io(io), host(h), port(p) {
	//connect(); 
      }

      void connect() {
	sock = util::kdb::get_socket(io, host, port);
      }

      tcp::socket& socket() {
	return *sock;
      }
    private:
      asio::io_context& io;
      std::string host;
      int port;
      std::optional<tcp::socket> sock;  
    };

    // a connection is taken from the pool and owned fot the duration of a query by the session handling the query
    // encapsulate this ownership and lifecycle so that the connection can be automatically returned to pool when done    
    class owned_connection {

    public:
      owned_connection(connection&& ww, connection_pool& oo) :failed(false), conn(std::move(ww)), owner(oo){ }
      ~owned_connection();
      owned_connection(owned_connection&& other) : failed(other.failed), conn(std::move(other.conn)), owner(other.owner)  {
	other.conn = std::nullopt;
      }
      owned_connection(const owned_connection&) = delete;
      owned_connection& operator=(const owned_connection&) = delete;
      owned_connection& operator=(owned_connection &&other) = delete;
      tcp::socket& socket() {
	tcp::socket& res = conn->socket();
	return res;
      }
      void fail() {
        failed = true;
	conn->socket().close();
      }
    private:
      bool failed;
      std::optional<connection> conn{};
      connection_pool& owner;  
    };

    // we will have a lambda on a thread / timer that occassionaly tries to bring failed workers back to life.
    using recover_failed = std::function<void ()>;
    // we will have a lambda matching this signature to get a conn from pool when we need it.
    using worker_provider = std::function<qmx::worker::owned_connection ()>; 

    class connection_pool {
    private:
      std::deque<qmx::worker::connection> workers{};
      std::deque<qmx::worker::connection> failed{};
      std::mutex mutex;
      std::condition_variable ready;
    public:
      void add(connection&& w) {
	// connections go into failed on start up
	// means we can have one connect / fail / recover loop on the timer
	failed.push_back(std::move(w));
      }

      connection take() {
	std::unique_lock<std::mutex> guard(mutex);
	while(workers.size() == 0) {
	  ready.wait(guard);
	}
	connection res = std::move(workers.front());
	workers.pop_front();
	return res;
      }

      recover_failed get_recover_fn() {
	using namespace std::chrono_literals;
	return [this]() {
		 while(true) {		   
		   std::this_thread::sleep_for(2000ms);
		   std::unique_lock<std::mutex> guard(mutex);
		   int to_recover = failed.size();
		   int recovered = 0;
		   while(to_recover > 0) {
		     connection recover = std::move(failed.front());
		     failed.pop_front();
     		     to_recover--;
		     try{
		       recover.connect();
		     } catch (std::system_error& err) {
		       failed.push_back(std::move(recover));
		       continue;
		     }
		     workers.push_back(std::move(recover));
		     recovered++;
		   }
		   if(recovered > 0) {
		     ready.notify_one();
		   }
		 }
	       };
      }

      void done(connection c) {
	std::unique_lock<std::mutex> guard(mutex);
	workers.push_back(std::move(c));
	ready.notify_one();
      }

      void fail(connection c) {
	std::unique_lock<std::mutex> guard(mutex);
	failed.push_back(std::move(c));
      }

      worker_provider getter() {
	return [this]() -> owned_connection {
		 owned_connection owned(take(), *this);
		 return owned;
	       };
      }  
    };
  }

  namespace io {

    std::vector<char> read_some(tcp::socket& src) {
      std::vector<char> bb(65536);
      asio::error_code err;
      int this_read = 0;
      try{
        this_read = src.read_some(asio::buffer(bb, bb.size()));	
      } catch (std::system_error& err) {
	if(0 == std::string(err.what()).compare(std::string("read_some: End of file"))) {
	  throw exception::srceof();
	}	
      }
      bb.resize(this_read);
      return bb;
    }

    void drain_socket(tcp::socket& s, size_t num) {
      std::vector<char> buff(65536);
      while(num > 0) {
        num -= s.read_some(asio::buffer(buff, buff.size()));
	// do nothing, we are just clearing the socket for this message
      }
    }

    void transfer_remaining(tcp::socket& from, tcp::socket& to, std::vector<char> start) {
      size_t read = start.size();
      size_t header_required = (read < 8) ? read - 8 : 0;
      // high probability we don't go through here as we get > 8 bytes in first read      
      while (header_required > 0) {
	std::vector<char> more = read_some(from);
	read += more.size();
	header_required = (read < 8) ? read - 8 : 0;
	start.insert(start.end(), more.begin(), more.end());
      }
      size_t msg_len = qmx::util::kdb::get_msg_length(start);
      size_t remaining = msg_len - read;
      try {
        asio::write(to, asio::buffer(start, start.size()));      
	while(remaining > 0) {
	  std::vector<char> next_chunk = read_some(from);
          remaining-=next_chunk.size();
	  asio::write(to, asio::buffer(next_chunk, next_chunk.size()));
	}
      }catch (std::system_error& err) {      
	if(0 == std::string(err.what()).compare(std::string("read_some: End of file"))) {  
	  throw exception::dsteof();
	}
	if(0 == std::string(err.what()).compare(std::string("write: Broken pipe"))) {
	  drain_socket(from, remaining);  // write failed but we need to make sure nothing pending on read side.
	  throw exception::dstbrokenpipe();
	}
	if(0 == std::string(err.what()).compare(std::string("write: Connection reset by peer"))) {
	  drain_socket(from, remaining);  // write failed but we need to make sure nothing pending on read side.
	  throw exception::dstbrokenpipe();
	}
	throw err;
      }
    }
  }

  namespace session {
    class session : public std::enable_shared_from_this<session> {
    public:

      session(asio::ip::tcp::socket&& socket, qmx::worker::worker_provider sp)
	: client(std::move(socket))
	, provide(sp){
      }
  
      void exec() {
        using namespace std::chrono_literals;
	std::vector<char> buff{};
	util::kdb::write_capability(client);    
	std::string user_pass = util::kdb::read_login(client, buff);
        
	while(true) {
	  std::optional<worker::owned_connection> reply_to;
	  try {
	    if(buff.size() > 0) {
	    }
	    std::vector<char> chunk = (buff.size() > 0) ? buff : io::read_some(client); // block until client starts
	    reply_to.emplace(provide()); // take a worker from the pool *after* client query comes in.
	    io::transfer_remaining(client, (*reply_to).socket(), chunk);
	  } catch (exception::srceof const& e) {
	    break;
	  }
	  // send response back from worker to client
	  // src is worker, dst is client 
	  try {
	    io::transfer_remaining((*reply_to).socket(), client, io::read_some((*reply_to).socket()));
	  }
	  // data coming back from worker to client,
	  // if the client goes away, data copy won't finish,
	  // bytes are left hanging on worker outbound socket
	  // we need to clear them up.
	  catch (exception::dstbrokenpipe const& e) {
	    // client went away, we drained the worker already, nothing else to do.
	  }
	  catch (exception::srceof const& e) {
	    // worker went away
	    (*reply_to).fail();
	    std::vector<unsigned char> error = util::kdb::signal_bytes("Worker Closed");
	    asio::write(client, asio::buffer(error, error.size()));
	  }
	  buff.clear();
	}
      }

    private:
      asio::ip::tcp::socket client;
      asio::streambuf streambuf;
      worker::worker_provider provide;
    };
  }

  namespace server {
    class server {

    public:
      server(asio::io_context& io, std::uint16_t port, worker::worker_provider p)
	: prov(p)
	, io(io)
	, acceptor(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))

      {

      }

      void async_accept() {
	socket.emplace(io);

	acceptor.async_accept(*socket, [&] (asio::error_code error) {
					 auto new_session = std::make_shared<session::session>(std::move(*socket), prov);
					 std::thread run_session([session = std::move(new_session)](){ session->exec(); });
					 run_session.detach();
					 async_accept();
				       });
      }

    private:
      worker::worker_provider prov;
      asio::io_context& io;
      asio::ip::tcp::acceptor acceptor;
      std::unordered_map<int, std::thread> threads;
      std::optional<asio::ip::tcp::socket> socket;  
    };
  }
}
