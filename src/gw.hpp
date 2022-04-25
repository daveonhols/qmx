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

      std::string read_login(tcp::socket& client) {
	asio::error_code ignored_error;
	asio::streambuf sbuf;
	int pwd_len = asio::read_until(client, sbuf, '\00', ignored_error);
	std::istream input(&sbuf);
	std::ostringstream ss;
	ss << input.rdbuf();
	return ss.str();
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
    }   
  }
  
  namespace exception {
    class srceof : public std::exception {};
    class dsteof : public std::exception {};
  }

  
  namespace worker {
    class connection_pool; // circular dependency between worker and pool containing it.
    
    class connection {

    public:
      connection(asio::io_context& io, std::string h, int p) : io(io), host(h), port(p) {
	connect(); 
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
      owned_connection(connection&& ww, connection_pool& oo) : conn(std::move(ww)), owner(oo), live(true){ }
      ~owned_connection();
      owned_connection(owned_connection&& other) : conn(std::move(other.conn)), owner(other.owner), live(true) {
	other.live=false; // if moved out of, we are not responsible for putting back worker to owning pool
      }
      owned_connection(const owned_connection&) = delete;
      owned_connection& operator=(const owned_connection&) = delete;
      owned_connection& operator=(owned_connection &&other) = delete;
      tcp::socket& socket() {
	tcp::socket& res = conn.socket();
	return res;
 
      }
    private:
      bool live;
      connection conn;
      connection_pool& owner;  
    };

    using worker_provider = std::function<qmx::worker::owned_connection ()>; // we will have a lambda matching this signature to get a conn from pool when we need it.

    class connection_pool {
    private:
      std::deque<qmx::worker::connection> workers{};
      std::mutex mutex;
      std::condition_variable ready;
    public:
      void add(connection&& w) {
	workers.push_back(std::move(w));
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

      void done(connection w) {
	std::unique_lock<std::mutex> guard(mutex);
	workers.push_back(std::move(w));
	ready.notify_one();
      }

      worker_provider getter() {
	return [this]() -> owned_connection {
		 owned_connection owned(std::move(take()), *this);
		 return owned;
	       };
      }  
    };
  }

  namespace io {

    std::vector<char> read_some(tcp::socket& src) {
      std::vector<char> bb(65536);
      asio::error_code err;
      int this_read = src.read_some(asio::buffer(bb, bb.size()), err);
      if(err == asio::error::eof) {
	throw exception::srceof();
      }
      bb.resize(this_read);
      return bb;
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
      asio::write(to, asio::buffer(start, start.size()));
      size_t to_write = msg_len - read;
      while(to_write > 0) {
	std::vector<char> next_chunk = read_some(from);
	asio::write(to, asio::buffer(next_chunk, next_chunk.size()));
	to_write-=next_chunk.size();
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
	util::kdb::write_capability(client);    
	std::string user_pass = util::kdb::read_login(client);
        
	while(true) {
	  std::optional<worker::owned_connection> reply_to;
	  try {
	    std::vector<char> chunk =  io::read_some(client); // block until client starts sending
	    reply_to.emplace(provide()); // take a worker from the pool *after* client query comes in.
	    io::transfer_remaining(client, (*reply_to).socket(), chunk);
	  } catch (exception::srceof const& e) {
	    std::cerr << "client eof ... (went away)" << std::endl;
	    break;
	  }
	  io::transfer_remaining((*reply_to).socket(), client, io::read_some((*reply_to).socket()));
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
	: io(io)
	, acceptor(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
	, prov(p)
      {}

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
