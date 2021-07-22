
#include <unistd.h>

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <iostream>
#include <thread>

#include "TcpConnection.hh"

TcpConnection::TcpConnection(int sockfd, FunProcessMessage recvFun,  FunSendDeamon sendFun, void* pobj){
  m_sockfd = sockfd;
  if(m_sockfd>=0){
    m_isAlive = true;
    if(recvFun)
      m_fut = std::async(std::launch::async, &TcpConnection::threadConnRecv, this, recvFun, pobj);
    if(sendFun)
      m_fut_send = std::async(std::launch::async, sendFun, pobj, this);
  }
  printf("TcpConnnection construction done\n");
}

TcpConnection::~TcpConnection(){
  printf("TcpConnnection deconstructing\n");
  if(m_fut.valid()){
    m_isAlive = false;
    m_fut.get();
  }
  if(m_fut_send.valid()){
    m_fut_send.get();
  }
  closeSocket(m_sockfd);
  printf("TcpConnnection deconstruction done\n");
}

TcpConnection::operator bool() const {
  return m_isAlive;
}

uint64_t TcpConnection::threadConnRecv(FunProcessMessage processMessage, void* pobj){
  msgpack::unpacker unp;
  msgpack::object_handle oh;

  timeval tv_timeout;
  tv_timeout.tv_sec = 0;
  tv_timeout.tv_usec = 10;
  fd_set fds;
  m_isAlive = true;
  std::printf("listenning on the client connection recv\n");
  while (m_isAlive){
    FD_ZERO(&fds);
    FD_SET(m_sockfd, &fds);
    FD_SET(0, &fds);
    if(!select(m_sockfd+1, &fds, NULL, NULL, &tv_timeout) || !FD_ISSET(m_sockfd, &fds) ){
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    unp.reserve_buffer(4096);
    int count = recv(m_sockfd, unp.buffer(), (unsigned int)(unp.buffer_capacity()), MSG_WAITALL);
    if(count== 0 && errno != EWOULDBLOCK && errno != EAGAIN){
      m_isAlive = false; // closed connection
      std::printf("connection is closed by remote peer\n");
      break;
    }
    if(count == 0){
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    unp.buffer_consumed(count);
    while (unp.next(oh)){
      int re = (*processMessage)(pobj, this, oh);
      if(re < 0){
        std::fprintf(stderr, "error: processMessage return error \n");
        m_isAlive = false;
        break;
      }
    }
    if(unp.nonparsed_size() > 4096) {
      m_isAlive = false;
      std::fprintf(stderr, "error: nonparsed buffer of message is too large\n");
      break;
    }
  }
  m_isAlive = false;
  printf("threadConnRecv exited\n");
  return 0;
}

int TcpConnection::processMessageClientTest(void* ,void* pconn, msgpack::object_handle& oh){
  msgpack::object msg = oh.get();
  unique_zone& life = oh.zone();
  std::cout << "client processMessage: " << msg << std::endl;
  return 1;
}


int TcpConnection::processMessageServerTest(void* ,void* pconn, msgpack::object_handle& oh){
  msgpack::object msg = oh.get();
  unique_zone& life = oh.zone();
  std::cout << "server processMessage: " << msg << std::endl;
  return 1;
}


void TcpConnection::sendRaw(const char* buf, size_t len){
  const char* remain_bufptr = buf;
  size_t remain_len = len;
  while (remain_len > 0 && m_isAlive){
    int written = write(m_sockfd, remain_bufptr, remain_len);
    if (written < 0) {
      fprintf(stderr, "ERROR writing to the TCP socket. errno: %d\n", errno);
      if (errno == EPIPE) {
        m_isAlive = false;
        break;
      }
    }
    else{
      remain_bufptr += written;
      remain_len -= written;
    }
  }
}

std::unique_ptr<TcpConnection> TcpConnection::connectToServer(const std::string& host,  short int port, FunProcessMessage recvFun, FunSendDeamon sendFun, void* pobj){
  auto now = std::chrono::system_clock::now();
  auto now_c = std::chrono::system_clock::to_time_t(now);
  printf("AsyncTcpClientConn is running...\n");

  int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sockfd < 0)
    fprintf(stderr, "ERROR opening socket");

  sockaddr_in serv_addr;
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  serv_addr.sin_addr.s_addr = inet_addr(host.c_str());
  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
    if(errno != EINPROGRESS){
      std::fprintf(stderr, "ERROR<%s>: unable to start TCP connection, error code %i \n", __func__, errno);
    }
    if(errno == 29){
      std::fprintf(stderr, "ERROR<%s>: TCP open timeout \n", __func__);
    }
    printf("\nConnection Failed \n");
    closeSocket(sockfd);
    return nullptr;
  }
  setupSocket(sockfd);
  return std::make_unique<TcpConnection>(sockfd, recvFun, sendFun, pobj);
}

int TcpConnection::createServerSocket(short int port){
  int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sockfd < 0)
    fprintf(stderr, "ERROR opening socket");

  setupSocket(sockfd);

  sockaddr_in serv_addr;
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(port);
  if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
    fprintf(stderr, "ERROR on binding. errno=%d\n", errno);
    closeSocket(sockfd);
    return -1;
  }
  listen(sockfd, 1);
  return sockfd;
}

std::unique_ptr<TcpConnection> TcpConnection::waitForNewClient(int sockfd, const std::chrono::milliseconds &timeout, FunProcessMessage recvFun, FunSendDeamon sendFun, void* pobj){
  sockaddr_in cli_addr;
  socklen_t clilen = sizeof(cli_addr);
  std::chrono::system_clock::time_point  tp_timeout = std::chrono::system_clock::now() + timeout;
  int sockfd_conn = -1;
  while(1){
    sockfd_conn = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen); //wait for the connection
    if(sockfd_conn>=0){ // got new sockfd;
      break;
    }
    else{
      if(std::chrono::system_clock::now() > tp_timeout){
        break;
      }
      if((errno == EAGAIN  || errno == EWOULDBLOCK)){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }
      else{
        std::fprintf(stderr, "server socket error\n");
        break;
      }
    }
  }

  if(sockfd_conn<0){
    return nullptr;
  }
  else{
    printf("new connection from %03d.%03d.%03d.%03d\n",
         (cli_addr.sin_addr.s_addr & 0xFF), (cli_addr.sin_addr.s_addr & 0xFF00) >> 8,
         (cli_addr.sin_addr.s_addr & 0xFF0000) >> 16, (cli_addr.sin_addr.s_addr & 0xFF000000) >> 24);
    setupSocket(sockfd_conn);
    return std::make_unique<TcpConnection>(sockfd_conn, recvFun, sendFun, pobj);
  }
}

void TcpConnection::setupSocket(int& sockfd) {
  /// Set non-blocking mode
  int iof = fcntl(sockfd, F_GETFL, 0);
  if (iof != -1)
    fcntl(sockfd, F_SETFL, iof | O_NONBLOCK);

  /// Allow the socket to rebind to an address that is still shutting down
  /// Useful if the server is quickly shut down then restarted
  int one = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

  /// Periodically send packets to keep the connection open
  setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);

  /// Try to send any remaining data when a socket is closed
  linger ling;
  ling.l_onoff = 1; ///< Enable linger mode
  ling.l_linger = 1; ///< Linger timeout in seconds
  setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &ling, sizeof ling);
}

int TcpConnection::createSocket(){
  return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

void TcpConnection::closeSocket(int& sockfd){
  if(sockfd>=0){
    close(sockfd);
    sockfd = -1;
  }
}


std::string TcpConnection::binToHexString(const char *bin, int len){
  constexpr char hexmap[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  const unsigned char* data = (const unsigned char*)(bin);
  std::string s(len * 2, ' ');
  for (int i = 0; i < len; ++i) {
    s[2 * i]     = hexmap[(data[i] & 0xF0) >> 4];
    s[2 * i + 1] = hexmap[data[i] & 0x0F];
  }
  return s;
}



