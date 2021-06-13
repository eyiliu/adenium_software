#include <unistd.h>
#include <vector>
#include <iostream>
#include <chrono>

#include "TcpServer.hh"
#include "TcpConnection.hh"

TcpServer::TcpServer(short int port, uint16_t deviceid, std::vector<std::pair<uint16_t, uint16_t>> hots){
  m_hots = hots;
  m_deviceid = deviceid;
  m_rd.Open();
  m_isActive = true;
  m_fut = std::async(std::launch::async, &TcpServer::threadClientMananger, this, port);
}

TcpServer::~TcpServer(){
  if(m_fut.valid()){
    m_isActive = false;
    m_fut.get();
  }
}

uint64_t TcpServer::threadClientMananger(short int port){
  m_isActive = true;
  int sockfd = TcpConnection::createServerSocket(port);
  if(sockfd<0){
    printf("unable to create server listenning socket\n");
    m_isActive = false;
  }
  std::unique_ptr<TcpConnection> conn;

  while(m_isActive){
    if(conn && !(*conn)){
      conn.reset();
    }
    if(!conn){
      auto new_conn = TcpConnection::waitForNewClient(sockfd, std::chrono::seconds(1),
                                                      reinterpret_cast<FunProcessMessage>(&TcpServer::perConnProcessMessage),
                                                      reinterpret_cast<FunSendDeamon>(&TcpServer::perConnSendDeamon), this);
      if(new_conn){
        conn = std::move(new_conn);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  if(sockfd>=0){
    TcpConnection::closeSocket(sockfd);
  }
  printf("server is closed\n");
  return 0;
}


int TcpServer::perConnProcessMessage(void* pconn, msgpack::object_handle &oh){
  msgpack::object msg = oh.get();
  unique_zone& life = oh.zone();
  NetMsg netmsg = msg.as<NetMsg>();
  switch(netmsg.type){
  case NetMsg::Type::data :{
    break;
  }
  case NetMsg::Type::daqinit :{
    std::cout<< "datatking init"<<std::endl;
    m_fw.fw_init();
    m_fw.fw_setid(m_deviceid);
    std::cout<< "masking hots"<<std::endl;
    for(auto& hot_xy: m_hots ){
      m_fw.SetPixelRegister(hot_xy.first, hot_xy.second, "MASK_EN", true);
    }
    std::cout<< "masking hots done"<<std::endl;
    break;
  }
  case NetMsg::Type::daqstart :{
    m_fw.fw_start();
    std::cout<< "datatking start"<<std::endl;
    break;
  }
  case NetMsg::Type::daqstop :{
    m_fw.fw_stop();
    std::cout<< "datatking stop"<<std::endl;
    break;
  }
  default:
    std::cout<< "unknown msg type"<<std::endl;
  }
  return 0;
}

int TcpServer::perConnSendDeamon(void  *pconn){
  TcpConnection* conn = reinterpret_cast<TcpConnection*>(pconn);
  std::stringstream ssbuf;
  std::string strbuf;
  while((*conn)){
    std::string dataraw = m_rd.readRawPack(std::chrono::milliseconds(1000));
    if(dataraw.empty()){
      continue;
    }

    // auto telev = AltelReader::createTelEvent(dataraw);
    
    NetMsg dataMsg{NetMsg::data, 0, 0, 0, {std::vector<char>(dataraw.begin(), dataraw.end())}};
    ssbuf.str(std::string());
    msgpack::pack(ssbuf, dataMsg);
    strbuf = ssbuf.str();

    conn->sendRaw(strbuf.data(), strbuf.size());
    
    // std::stringstream ssbuf;
    // std::string strbuf;
    // msgpack::packer<std::stringstream> pk(ssbuf);
    // pk.pack_map(1);
    // const std::string msgkey_data("data");
    // pk.pack_str(msgkey_data.size());
    // pk.pack_str_body(msgkey_data.data(), msgkey_data.size());
    // pk.pack_bin(dataraw.size());
    // pk.pack_bin_body(dataraw.data(), dataraw.size());
    // strbuf = ssbuf.str();
    // conn->sendRaw(strbuf.data(), strbuf.size());
  }
  return 0;
}
