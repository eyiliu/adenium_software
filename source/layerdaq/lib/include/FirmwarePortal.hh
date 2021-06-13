#ifndef _FIRMWARE_PORTAL_HH_
#define _FIRMWARE_PORTAL_HH_

#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <ctime>
#include <regex>
#include <map>
#include <vector>
#include <utility>
#include <algorithm>

#include "mysystem.hh"
#include "myrapidjson.h"

class FirmwarePortal{
public:
  FirmwarePortal(const std::string &json_str_options);

  void SetFirmwareRegister(const std::string& name, uint64_t value);
  void SendFirmwareCommand(const std::string& name);
  uint64_t GetFirmwareRegister(const std::string& name);

  void SetAlpideRegister(const std::string& name, uint64_t value);
  void SendAlpideCommand(const std::string& name);
  void SendAlpideBroadcast(const std::string& name);
  uint64_t GetAlpideRegister(const std::string& name);  

  
  void SetRegionRegister(uint64_t region, const std::string& name, uint64_t value);
  void BroadcastRegionRegister(const std::string& name, uint64_t value);
  uint64_t GetRegionRegister(uint64_t region, const std::string& name);

  
  void SetPixelRegister(uint64_t x, uint64_t y, const std::string& name, uint64_t value); //page 70 manual
  void SetPixelRegisterFullColumn(uint64_t x, const std::string& name, uint64_t value);
  void SetPixelRegisterFullRow(uint64_t y, const std::string& name, uint64_t value);
  void SetPixelRegisterFullChip(const std::string& name, uint64_t value);
  
  void InjectPulse();// test only, to be removed
  

  void fw_init();
  void fw_setid(uint32_t);
  void fw_start();
  void fw_stop();

  const std::string& DeviceUrl();
  
  static std::string LoadFileToString(const std::string& path);

private:
  void  WriteWord(uint64_t address, uint64_t value);
  uint64_t ReadWord(uint64_t address);
  
  std::string m_alpide_ip_addr;

public:
  template<typename ... Args>
  static std::string FormatString( const std::string& format, Args ... args ){
    std::size_t size = snprintf( nullptr, 0, format.c_str(), args ... ) + 1;
    std::unique_ptr<char[]> buf( new char[ size ] ); 
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 );
  }

  template<typename ... Args>
  static std::size_t FormatPrint(std::ostream &os, const std::string& format, Args ... args ){
    std::size_t size = snprintf( nullptr, 0, format.c_str(), args ... ) + 1;
    std::unique_ptr<char[]> buf( new char[ size ] ); 
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    std::string formated_string( buf.get(), buf.get() + size - 1 );
    os<<formated_string<<std::flush;
    return formated_string.size();
  }

  template<typename ... Args>
  static std::size_t DebugFormatPrint(std::ostream &os, const std::string& format, Args ... args ){
    return 0;
    std::size_t size = snprintf( nullptr, 0, format.c_str(), args ... ) + 1;
    std::unique_ptr<char[]> buf( new char[ size ] ); 
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    std::string formated_string( buf.get(), buf.get() + size - 1 );
    os<<formated_string<<std::flush;
    return formated_string.size();
  }
  
  template<typename T>
  static const std::string Stringify(const T& o){
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    o.Accept(writer);
    return sb.GetString();
  }

  template<typename T>
  static void PrintJson(const T& o){
    rapidjson::StringBuffer sb;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> w(sb);
    o.Accept(w);
    std::fwrite(sb.GetString(), 1, sb.GetSize(), stdout);
  }

  rapidjson::CrtAllocator m_jsa;
  rapidjson::GenericValue<rapidjson::UTF8<char>, rapidjson::CrtAllocator> m_js_conf;
  rapidjson::GenericValue<rapidjson::UTF8<char>, rapidjson::CrtAllocator> m_js_reg_cmd;  
  rapidjson::Document m_json;
};


#endif

  
