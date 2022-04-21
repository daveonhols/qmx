//#include <vector>
//#include <iostream>

//#include <asio.hpp>


//using asio::ip::tcp;

tcp::socket conn_worker(asio::io_context& io, std::string ip, int port) {
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

class worker {

public:
  worker(asio::io_context& io, std::string h, int p) : io(io), host(h), port(p) {
    connect(); 
  }
  void connect() {
    sock = conn_worker(io, host, port);
  }

  tcp::socket& get() {
    return *sock;
  }
private:
  asio::io_context& io;
  std::string host;
  int port;
  std::optional<tcp::socket> sock;  
};

class worker_pool;
class owned_worker {

public:
  owned_worker(worker&& ww, worker_pool& oo) : w(std::move(ww)), owner(oo), live(true){ }
  ~owned_worker();
  owned_worker(owned_worker&& other) : w(std::move(other.w)), owner(other.owner), live(true) {
    other.live=false; // if moved out of, we are not responsible for putting back worker to owning pool
  }
  owned_worker(const owned_worker&) = delete;
  owned_worker& operator=(const owned_worker&) = delete;
  owned_worker& operator=(owned_worker &&other) = delete;
  worker& getw() {return w;}
  tcp::socket& get() {
    tcp::socket& res = w.get();
    //    char buf2[22] = {1,0,0,0,21,0,0,0,10,0,7,0,0,0,115,104,111,119,32,96,103};
    //asio::write(res, asio::buffer(buf2, 21));
    return res;
 
  }
private:
  bool live;
  worker w;
  worker_pool& owner;  
};

using worker_provider = std::function<owned_worker ()>;

class owned_worker_provider {
public:
  owned_worker_provider(worker_provider& o) : wp(o) {}
  owned_worker get() {
    owned_worker res = std::move(wp());
    return res;
  }
private:
  worker_provider& wp;  
};

class wrapped_socket {
public:
  wrapped_socket(tcp::socket& sock) : s(sock) {}
  tcp::socket& get() { return s;}
private:
  tcp::socket& s;
};

class wrapped_socket_provider {
public:
  wrapped_socket_provider(wrapped_socket w) : ws(w) {}
  wrapped_socket get() { return ws;}
private:
  wrapped_socket ws;
};

class srceof : public std::exception {};
class dsteof : public std::exception {};

// T will either be a class where get returns an owned worker from pool, for clean up
// or T will be a class where get returns a simple 

template<class T, class R>
R msg_transfer(tcp::socket& src, T sop) {
  asio::streambuf sbuf;
  std::istream input(&sbuf);
  asio::error_code src_error;
  asio::error_code sink_error;

  // wait for new user query
  int msg_len = asio::read(src, sbuf, asio::transfer_exactly(8),src_error);
  if(src_error == asio::error::eof) {
    throw srceof();    
  }
  // get back end worker socket only after user query comes in.!!
  // t is owned worker provider, t.get is owned worker, t.get.get is socket
  // t is client psas through provider, t.get is client psas through, t.get.get is socket
  R socket_owner = std::move(sop.get());
  tcp::socket& sink = socket_owner.get();

  int len = 0;
  int b_shift = 0;
  std::vector<char> full_msg{};
  full_msg.reserve(65536);
  char buf[8];
  input.read(buf, 8);

  for(int i = 0; i < msg_len; ++i) {
    full_msg.push_back(buf[i]);
    if(i < 4) {
      continue;
    }    
    len += ((unsigned char)buf[i] << b_shift);
    b_shift += 8;
  }
  sbuf.consume(len);
  int to_read = len - 8; // we already got 8
  int buf_size = 8;
  while(to_read > 0) {
    std::vector<char> bb(65536);
    int this_read = src.read_some(asio::buffer(bb, bb.size()));
    buf_size += this_read;
    to_read-=this_read;

    full_msg.insert(full_msg.end(), bb.begin(), bb.end());
    int wrote = asio::write(sink, asio::buffer(full_msg, buf_size));
    full_msg.clear();
    buf_size = 0;
  }
  return socket_owner;
}

void write_capability(tcp::socket& sock, unsigned char cap = 0x03) {
    asio::error_code ignored_error;
    std::vector<unsigned char> cap_resp{cap};
    asio::write(sock, asio::buffer(cap_resp), ignored_error);
}

std::string get_login(tcp::socket& client) {

  asio::error_code ignored_error;
  asio::streambuf sbuf;
  int pwd_len = asio::read_until(client, sbuf, '\00', ignored_error);
  std::istream input(&sbuf);
  std::ostringstream ss;
  ss << input.rdbuf();
  return ss.str();

}

/*
int main (int argc, char*argv[]) {

  asio::io_context io;     
  tcp::socket w1 = conn_worker(io, "127.0.0.1", 8833);

  tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 8811));
  tcp::socket listener(io);
  acceptor.accept(listener);
  write_capability(listener);

  std::string user_pass = get_login(listener);
  std::cout << user_pass << std::endl;
  
  while(true) {
    msg_transfer(listener, w1);
    msg_transfer(w1, listener);
  }

  return 0;
  
}
*/
