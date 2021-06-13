#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <iostream>
#include <fstream>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>


#include <thread>


#include "AltelReader.hh"

#ifndef DEBUG_PRINT
#define DEBUG_PRINT 0
#endif
#define debug_print(fmt, ...)                                           \
  do { if (DEBUG_PRINT) std::fprintf(stdout, fmt, ##__VA_ARGS__); } while (0)

#ifndef INFO_PRINT
#define INFO_PRINT 0
#endif
#define info_print(fmt, ...)                                           \
  do { if (INFO_PRINT) std::fprintf(stdout, fmt, ##__VA_ARGS__); } while (0)

#define HEADER_BYTE  (0x5a)
#define FOOTER_BYTE  (0xa5)

std::string AltelReader::binToHexString(const char *bin, int len){
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

std::string AltelReader::hexToBinString(const char *hex, int len){
  if(len%2){
    throw;
  }
  size_t blen  = len/2;
  const unsigned char* data = (const unsigned char*)(hex);
  std::string s(blen, ' ');
  for (int i = 0; i < blen; ++i){
    unsigned char d0 = data[2*i];
    unsigned char d1 = data[2*i+1];
    unsigned char v0;
    unsigned char v1;
    if(d0>='0' && d0<='9'){
      v0 = d0-'0';
    }
    else if(d0>='a' && d0<='f'){
      v0 = d0-'a'+10;
    }
    else if(d0>='A' && d0<='F'){
      v0 = d0-'A'+10;
    }
    else{
      std::fprintf(stderr, "wrong hex string\n");
      throw;
    }
    if(d1>='0' && d1<='9'){
      v1 = d1-'0';
    }
    else if(d1>='a' && d1<='f'){
      v1 = d1-'a'+10;
    }
    else if(d1>='A' && d1<='F'){
      v1 = d1-'A'+10;
    }
    else{
      std::fprintf(stderr, "wrong hex string\n");
      throw;
    }
    s[i]= (v0<<4) + v1;
  }
  return s;
}

std::string AltelReader::binToHexString(const std::string& bin){
  return binToHexString(bin.data(), bin.size());
}

std::string AltelReader::hexToBinString(const std::string& hex){
  return hexToBinString(hex.data(), hex.size());
}

size_t AltelReader::dumpBrokenData(int fd_rx){
  size_t len_total = 0;
  uint32_t buf_word;
  char * p_buf = reinterpret_cast<char*>(&buf_word);

  while(read(fd_rx, p_buf, 4)>0){
    len_total ++;
    if(buf_word == 0xa5 || buf_word>>8 == 0xa5 || buf_word>>16 == 0xa5 || buf_word>>24 == 0xa5){
      // reach pack end
      return len_total;
    }
  }
  return len_total;
}


AltelReader::~AltelReader(){
  Close();
}

AltelReader::AltelReader(){
  m_fd = 0;
  m_file_path = "/dev/axidmard";
};


bool AltelReader::Open(){
  m_fd = open(m_file_path.c_str(), O_RDONLY | O_NONBLOCK);
  if(!m_fd)
    return false;
  return true;
}

void AltelReader::Close(){
  if(!m_fd)
    return;

  close(m_fd);
  m_fd = 0;
}

std::vector<std::shared_ptr<altel::TelEvent>> AltelReader::Read(size_t size_max_pkg,
                                                                const std::chrono::milliseconds &timeout_idle,
                                                                const std::chrono::milliseconds &timeout_total){
  std::chrono::system_clock::time_point tp_timeout_total = std::chrono::system_clock::now() + timeout_total;
  std::vector<std::shared_ptr<altel::TelEvent>> pkg_v;
  while(1){
    auto pkg = Read(timeout_idle);
    if(pkg){
      pkg_v.push_back(pkg);
      if(pkg_v.size()>=size_max_pkg){
        break;
      }
    }
    else{
      break;
    }
    if(std::chrono::system_clock::now() > tp_timeout_total){
      break;
    }
  }
  return pkg_v;
}


std::string AltelReader::readPack(int fd_rx, const std::chrono::milliseconds &timeout_idel){ //timeout_read_interval
  // std::fprintf(stderr, "-");
  size_t size_buf_min = 16;
  size_t size_buf = size_buf_min;
  std::string buf(size_buf, 0);
  size_t size_filled = 0;
  std::chrono::system_clock::time_point tp_timeout_idel;
  bool can_time_out = false;
  int read_len_real = 0;
  while(size_filled < size_buf){
    read_len_real = read(fd_rx, &buf[size_filled], size_buf-size_filled);
    if(read_len_real>0){
      // debug_print(">>>read  %d Bytes \n", read_len_real);
      size_filled += read_len_real;
      can_time_out = false;
      if(size_buf == size_buf_min  && size_filled >= size_buf_min){
	uint8_t header_byte =  buf.front();
	uint32_t w1 = *reinterpret_cast<const uint32_t*>(buf.data()+4);
	// uint8_t rsv = (w1>>20) & 0xf;

	uint32_t size_payload = (w1 & 0xfffff);
	// std::cout<<" size_payload "<< size_payload<<std::endl;
	if(header_byte != HEADER_BYTE){
	  std::fprintf(stderr, "ERROR<%s>: wrong header of data frame, skip\n", __func__);
	  std::fprintf(stderr, "RawData_TCP_RX:\n%s\n", binToHexString(buf).c_str());
	  std::fprintf(stderr, "<");
	  //TODO: skip broken data
	  dumpBrokenData(fd_rx);
	  size_buf = size_buf_min;
	  size_filled = 0;
	  can_time_out = false;
	  continue;
	}
	size_buf += size_payload;
	size_buf &= -4; // aligment 32bits, tail 32bits might be cutted.
	if(size_buf > 300){
	  size_buf = 300;
	}
	buf.resize(size_buf);
      }
    }
    else if (read_len_real== 0 || (read_len_real < 0 && errno == EAGAIN)){ // empty readback, read again
      if(!can_time_out){
	can_time_out = true;
	tp_timeout_idel = std::chrono::system_clock::now() + timeout_idel;
      }
      else{
	if(std::chrono::system_clock::now() > tp_timeout_idel){
	  if(size_filled == 0){
	    // debug_print("INFO<%s>: no data receving.\n",  __func__);
	    return std::string();
	  }
	  //TODO: keep remain data, nothrow
	  std::fprintf(stderr, "ERROR<%s>: timeout error of incomplete data reading \n", __func__ );
	  std::fprintf(stderr, "=");
	  return std::string();
	  // throw;
	}
      }
      // std::this_thread::sleep_for(std::chrono::microseconds(10));
      continue;
    }
    else{
      std::fprintf(stderr, "ERROR<%s>: read(...) returns error code %d\n", __func__,  errno);
      throw;
    }
  }
  uint32_t w_end = *reinterpret_cast<const uint32_t*>(&buf.back()-3);

  if(w_end != FOOTER_BYTE && (w_end>>8)!= FOOTER_BYTE && (w_end>>16)!= FOOTER_BYTE && (w_end>>24)!= FOOTER_BYTE ){
    std::fprintf(stderr, "ERROR<%s>:  wrong footer of data frame\n", __func__);
    std::fprintf(stderr, ">");
    std::fprintf(stderr, "dumpping data Hex:\n%s\n", binToHexString(buf).c_str());
    return std::string();
    //throw;
  }
  // std::fprintf(stdout, "dumpping data Hex:\n%s\n", binToHexString(buf).c_str());
  return buf;
}


std::shared_ptr<altel::TelEvent> AltelReader::Read(const std::chrono::milliseconds &timeout_idle){ //timeout_read_interval
  auto buf = readPack(m_fd, timeout_idle);
  if(buf.empty()){
    return nullptr;
  }
  return createTelEvent(buf);
}

std::string AltelReader::LoadFileToString(const std::string& path){
  std::ifstream ifs(path);
  if(!ifs.good()){
    std::fprintf(stderr, "ERROR<%s>: unable to load file<%s>\n", __func__, path.c_str());
    throw;
  }
  std::string str;
  str.assign((std::istreambuf_iterator<char>(ifs) ),
             (std::istreambuf_iterator<char>()));
  return str;
}

std::string AltelReader::readRawPack(const std::chrono::milliseconds &timeout_idel){ //timeout_read_interval
  return readPack(m_fd, timeout_idel);
}


std::shared_ptr<altel::TelEvent> AltelReader::createTelEvent(const std::string& raw){

  uint32_t runN = 0;
  uint32_t eventN = 0;
  uint32_t triggerN = 0;
  uint32_t deviceN = 0;
  std::vector<altel::TelMeasRaw> alpideMeasRaws;

  const uint8_t* p_raw_beg = reinterpret_cast<const uint8_t *>(raw.data());
  const uint8_t* p_raw = p_raw_beg;
  if(raw.size()<16){
    std::fprintf(stderr, "raw data length is less than 16\n");
    throw;
  }
  if( *p_raw_beg!=0x5a){
    std::fprintf(stderr, "package header/trailer mismatch, head<%hhu>\n", *p_raw_beg);
    throw;
  }
  p_raw++; //header
  p_raw++; //resv
  p_raw++; //resv

  uint8_t deviceId = *p_raw;
  deviceN=*p_raw;

  debug_print(">>deviceId %hhu\n", deviceId);
  p_raw++; //deviceId

  uint32_t len_payload_data = *reinterpret_cast<const uint32_t*>(p_raw) & 0x00ffffff;
  uint32_t len_pack_expected = (len_payload_data + 16) & -4;
  if( len_pack_expected  != raw.size()){
    std::fprintf(stderr, "raw data length does not match to package size\n");
    std::fprintf(stderr, "payload_len = %u,  package_size = %zu\n",
                 len_payload_data, raw.size());
    throw;
  }
  p_raw += 4;

  uint32_t triggerId = *reinterpret_cast<const uint16_t*>(p_raw);
  debug_print(">>triggerId %u\n", triggerId);
  triggerN  = *reinterpret_cast<const uint16_t*>(p_raw);

  p_raw += 4;

  const uint8_t* p_payload_end = p_raw_beg + 12 + len_payload_data -1;
  if( *(p_payload_end+1) != 0xa5 ){
    std::fprintf(stderr, "package header/trailer mismatch, trailer<%hu>\n", *(p_payload_end+1) );
    throw;
  }

  uint8_t l_frame_n = -1;
  uint8_t l_region_id = -1;
  while(p_raw <= p_payload_end){
    char d = *p_raw;
    if(d & 0b10000000){
      debug_print("//1     NOT DATA\n");
      if(d & 0b01000000){
        debug_print("//11    EMPTY or REGION HEADER or BUSY_ON/OFF\n");
        if(d & 0b00100000){
          debug_print("//111   EMPTY or BUSY_ON/OFF\n");
          if(d & 0b00010000){
            debug_print("//1111  BUSY_ON/OFF\n");
            p_raw++;
            continue;
          }
          debug_print("//1110  EMPTY\n");
          uint8_t chip_id = d & 0b00001111;
          l_frame_n++;
          p_raw++;
          d = *p_raw;
          uint8_t bunch_counter_h = d;
          p_raw++;
          continue;
        }
        debug_print("//110   REGION HEADER\n");
        l_region_id = d & 0b00011111;
        debug_print(">>region_id %hhu\n", l_region_id);
        p_raw++;
        continue;
      }
      debug_print("//10    CHIP_HEADER/TRAILER or UNDEFINED\n");
      if(d & 0b00100000){
        debug_print("//101   CHIP_HEADER/TRAILER\n");
        if(d & 0b00010000){
          debug_print("//1011  TRAILER\n");
          uint8_t readout_flag= d & 0b00001111;
          p_raw++;
          continue;
        }
        debug_print("//1010  HEADER\n");
        uint8_t chip_id = d & 0b00001111;
        l_frame_n++;
        p_raw++;
        d = *p_raw;
        uint8_t bunch_counter_h = d;
        p_raw++;
        continue;
      }
      debug_print("//100   UNDEFINED\n");
      p_raw++;
      continue;
    }
    else{
      debug_print("//0     DATA\n");
      if(d & 0b01000000){
        debug_print("//01    DATA SHORT\n"); // 2 bytes
        uint8_t encoder_id = (d & 0b00111100)>> 2;
        uint16_t addr = (d & 0b00000011)<<8;
        p_raw++;
        d = *p_raw;
        addr += *p_raw;
        p_raw++;

        uint16_t y = addr>>1;
        uint16_t x = (l_region_id<<5)+(encoder_id<<1)+((addr&0b1)!=((addr>>1)&0b1));
        debug_print("[%hu, %hu, %hhu]\n", x, y, deviceId);
        alpideMeasRaws.emplace_back(x, y, deviceN, triggerN);
        continue;
      }
      debug_print("//00    DATA LONG\n"); // 3 bytes
      uint8_t encoder_id = (d & 0b00111100)>> 2;
      uint16_t addr = (d & 0b00000011)<<8;
      p_raw++;
      d = *p_raw;
      addr += *p_raw;
      p_raw++;
      d = *p_raw;
      uint8_t hit_map = (d & 0b01111111);
      p_raw++;
      uint16_t y = addr>>1;
      uint16_t x = (l_region_id<<5)+(encoder_id<<1)+((addr&0b1)!=((addr>>1)&0b1));
      debug_print("[%hu, %hu, %hhu] ", x, y, deviceId);
      alpideMeasRaws.emplace_back(x, y, deviceN, triggerN);

      for(int i=1; i<=7; i++){
        if(hit_map & (1<<(i-1))){
          uint16_t addr_l = addr + i;
          uint16_t y = addr_l>>1;
          uint16_t x = (l_region_id<<5)+(encoder_id<<1)+((addr_l&0b1)!=((addr_l>>1)&0b1));
          debug_print("[%hu, %hu, %hhu] ", x, y, deviceId);
          alpideMeasRaws.emplace_back(x, y, deviceN, triggerN);
        }
      }
      debug_print("\n");
      continue;
    }
  }

  auto alpideMeasHits = altel::TelMeasHit::clustering_UVDCus(alpideMeasRaws,
                                                             0.02924,
                                                             0.02688,
                                                             -0.02924*(1024-1)*0.5,
                                                             -0.02688*(512-1)*0.5);

  std::shared_ptr<altel::TelEvent> telev;
  telev.reset(new altel::TelEvent(runN, eventN, deviceN, triggerN));
  telev->measRaws().insert(telev->measRaws().end(), alpideMeasRaws.begin(), alpideMeasRaws.end());
  telev->measHits().insert(telev->measHits().end(), alpideMeasHits.begin(), alpideMeasHits.end());
  return telev;
}

