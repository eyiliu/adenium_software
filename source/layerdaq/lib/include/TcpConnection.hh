#pragma once

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <string>
#include <future>
#include <memory>
#include <chrono>

#include <msgpack.hpp>
typedef msgpack::unique_ptr<msgpack::zone> unique_zone;

class TcpConnection;

//callback
typedef int (*FunProcessMessage)(void* pobj, void* pconn,  msgpack::object_handle&);
typedef int (*FunSendDeamon)(void* pobj, void*pconn);


struct NetMsg{
  enum Type: uint16_t {data, daqcmd, daqinit, daqstart, daqstop};
  uint16_t type;
  uint16_t device;
  uint32_t address;
  uint32_t value;
  std::vector<char> bin;
  MSGPACK_DEFINE(type, device, address, value, bin);
};

class TcpConnection{
public:
  TcpConnection() = delete;
  TcpConnection(const TcpConnection&) =delete;
  TcpConnection& operator=(const TcpConnection&) =delete;
  TcpConnection(int sockfd, FunProcessMessage recvFun, FunSendDeamon sendFun, void* pobj);

  ~TcpConnection();

  operator bool() const;

  //forked thread
  uint64_t threadConnRecv(FunProcessMessage recvFun, void* pobj);

  void sendRaw(const char* buf, size_t len);

  static int createSocket();
  static void setupSocket(int& sockfd);
  static void closeSocket(int& sockfd);

  //client side
  static std::unique_ptr<TcpConnection> connectToServer(const std::string& host,  short int port, FunProcessMessage recvFun, FunSendDeamon sendFun, void* pobj);
  //server side
  static int createServerSocket(short int port);
  static std::unique_ptr<TcpConnection> waitForNewClient(int sockfd, const std::chrono::milliseconds &timeout, FunProcessMessage recvFun, FunSendDeamon sendFun, void* pobj);

  //test
  static int processMessageServerTest(void* pobj, void* pconn, msgpack::object_handle& oh);
  static int processMessageClientTest(void* pobj, void* pconn, msgpack::object_handle& oh);

  static std::string binToHexString(const char *bin, int len);
private:
  std::future<uint64_t> m_fut;
  std::future<int> m_fut_send;
  bool m_isAlive{false};
  int m_sockfd{-1};
};
