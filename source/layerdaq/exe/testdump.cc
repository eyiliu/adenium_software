#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <future>
#include <thread>

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

// #include "TelEvent.hpp"


#include "getopt.h"

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::size_t;

#define HEADER_BYTE 0x5a
#define FOOTER_BYTE 0xa5

#ifndef DEBUG_PRINT
#define DEBUG_PRINT 0
#endif
#define debug_print(fmt, ...)                                           \
  do { if (DEBUG_PRINT) std::fprintf(stdout, fmt, ##__VA_ARGS__); } while (0)


static sig_atomic_t g_done = 0;

std::atomic<size_t> ga_unexpectedN = 0;
std::atomic<size_t> ga_dataFrameN = 0;
std::atomic<uint16_t>  ga_lastTriggerId = 0;

uint64_t AsyncWatchDog(){
  auto tp_run_begin = std::chrono::system_clock::now();
  auto tp_old = tp_run_begin;
  size_t st_old_dataFrameN = 0;
  size_t st_old_unexpectedN = 0;

  while(!g_done){
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto tp_now = std::chrono::system_clock::now();
    std::chrono::duration<double> dur_period_sec = tp_now - tp_old;
    std::chrono::duration<double> dur_accu_sec = tp_now - tp_run_begin;
    double sec_period = dur_period_sec.count();
    double sec_accu = dur_accu_sec.count();

    size_t st_unexpectedN = ga_unexpectedN;
    size_t st_dataFrameN = ga_dataFrameN;
    uint16_t st_lastTriggerId= ga_lastTriggerId;

    double st_hz_pack_accu = st_dataFrameN / sec_accu;
    double st_hz_pack_period = (st_dataFrameN-st_old_dataFrameN) / sec_period;

    tp_old = tp_now;
    st_old_dataFrameN= st_dataFrameN;
    st_old_unexpectedN = st_unexpectedN;
    std::fprintf(stdout, "                  ev_accu(%8.2f hz) ev_trans(%8.2f hz) last_id(%8.2hu)\r",st_hz_pack_accu, st_hz_pack_period, st_lastTriggerId);
    std::fflush(stdout);
  }
  std::fprintf(stdout, "\n\n");
  return 0;
}



namespace{

  std::string binToHexString(const char *bin, int len){
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

  std::string hexToBinString(const char *hex, int len){
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

  std::string binToHexString(const std::string& bin){
    return binToHexString(bin.data(), bin.size());
  }

  std::string hexToBinString(const std::string& hex){
    return hexToBinString(hex.data(), hex.size());
  }

  size_t dumpBrokenData(int fd_rx){
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

  std::string readPack(int fd_rx, const std::chrono::milliseconds &timeout_idel){ //timeout_read_interval
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
	    std::fprintf(stdout, "RawData_TCP_RX:\n%s\n", binToHexString(buf).c_str());
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
        continue;
      }
      else{
        std::fprintf(stderr, "ERROR<%s>: read(...) returns error code %d\n", __func__,  errno);
        throw;
      }
    }
    uint32_t w_end = *reinterpret_cast<const uint32_t*>(&buf.back()-3);

    if(w_end != FOOTER_BYTE && (w_end>>8)!= FOOTER_BYTE && (w_end>>16)!= FOOTER_BYTE && (w_end>>24)!= FOOTER_BYTE ){
      // std::fprintf(stderr, "ERROR<%s>:  wrong footer of data frame\n", __func__);
      std::fprintf(stderr, ">");
      // std::fprintf(stderr, "dumpping data Hex:\n%s\n", binToHexString(buf).c_str());
      return std::string();
      //throw;
    }
    // std::fprintf(stdout, "dumpping data Hex:\n%s\n", StringToHexString(buf).c_str());
    return buf;
  }

  void fromRaw(const std::string &raw){
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
          std::fprintf(stdout, "[%hu, %hu, %hhu]\n", x, y, deviceId);
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

        for(int i=1; i<=7; i++){
          if(hit_map & (1<<(i-1))){
            uint16_t addr_l = addr + i;
            uint16_t y = addr_l>>1;
            uint16_t x = (l_region_id<<5)+(encoder_id<<1)+((addr_l&0b1)!=((addr_l>>1)&0b1));
            debug_print("[%hu, %hu, %hhu] ", x, y, deviceId);
          }
        }
        debug_print("\n");
        continue;
      }
    }
    return;
  }
}




static const std::string help_usage = R"(
Usage:
  -help                        help message
  -verbose                     verbose flag
  -rawPrint                    print data by hex format in terminal
  -decode                      decode the raw data
  -tidCheck                    check the trigger TID continuous
  -rawFile        <path>       path of raw file to save
  -exitTime       <n>          exit after n seconds (0=NoLimit, default 10)
examples:
#1. save data and print
testdump -rawPrint -rawFile test.dat

#2. save data only
testdump -rawFile test.dat

#3. print only
testdump -rawPrint

#4. print, exit after 60 seconds
testdump -rawPrint -exitTime 60

)";

int main(int argc, char *argv[]) {
  signal(SIGINT, [](int){g_done+=1;});

  std::string rawFilePath;
  std::string ipAddressStr;
  int exitTimeSecond = 10;
  bool do_rawPrint = false;
  bool do_decode = false;
  bool do_tidCheck = false;
  int do_verbose = 0;
  {////////////getopt begin//////////////////
    struct option longopts[] = {{"help",      no_argument, NULL, 'h'},//option -W is reserved by getopt
                                {"verbose",   no_argument, NULL, 'v'},//val
                                {"decode",    no_argument, NULL, 'd'},
                                {"tidCheck",  no_argument, NULL, 'c'},
                                {"rawPrint",  no_argument, NULL, 's'},
                                {"rawFile",   required_argument, NULL, 'f'},
                                {"exitTime",  required_argument, NULL, 'e'},
                                {0, 0, 0, 0}};

    if(argc == 1){
      std::fprintf(stderr, "%s\n", help_usage.c_str());
      std::exit(1);
    }
    int c;
    int longindex;
    opterr = 1;
    while ((c = getopt_long_only(argc, argv, "-", longopts, &longindex)) != -1) {
      // // "-" prevents non-option argv
      // if(!optopt && c!=0 && c!=1 && c!=':' && c!='?'){ //for debug
      //   std::fprintf(stdout, "opt:%s,\targ:%s\n", longopts[longindex].name, optarg);;
      // }
      switch (c) {
      case 'f':
        rawFilePath = optarg;
        break;
      case 'e':
        exitTimeSecond = std::stoi(optarg);
        break;
      case 's':
        do_rawPrint = true;
        break;
      case 'c':
        do_tidCheck = true;
        break;
      case 'd':
        do_decode = true;
        break;
        // help and verbose
      case 'v':
        do_verbose=1;
        //option is set to no_argument
        if(optind < argc && *argv[optind] != '-'){
          do_verbose = std::stoul(argv[optind]);
          optind++;
        }
        break;
      case 'h':
        std::fprintf(stdout, "%s\n", help_usage.c_str());
        std::exit(0);
        break;
        /////generic part below///////////
      case 0:
        // getopt returns 0 for not-NULL flag option, just keep going
        break;
      case 1:
        // If the first character of optstring is '-', then each nonoption
        // argv-element is handled as if it were the argument of an option
        // with character code 1.
        std::fprintf(stderr, "%s: unexpected non-option argument %s\n",
                     argv[0], optarg);
        std::exit(1);
        break;
      case ':':
        // If getopt() encounters an option with a missing argument, then
        // the return value depends on the first character in optstring:
        // if it is ':', then ':' is returned; otherwise '?' is returned.
        std::fprintf(stderr, "%s: missing argument for option %s\n",
                     argv[0], longopts[longindex].name);
        std::exit(1);
        break;
      case '?':
        // Internal error message is set to print when opterr is nonzero (default)
        std::exit(1);
        break;
      default:
        std::fprintf(stderr, "%s: missing getopt branch %c for option %s\n",
                     argv[0], c, longopts[longindex].name);
        std::exit(1);
        break;
      }
    }
  }/////////getopt end////////////////


  std::fprintf(stdout, "\n");
  std::fprintf(stdout, "rawPrint:  %d\n", do_rawPrint);
  std::fprintf(stdout, "rawFile:   %s\n", rawFilePath.c_str());
  std::fprintf(stdout, "\n");

  // if (rawFilePath.empty() && !do_rawPrint) {
  //   std::fprintf(stderr, "ERROR: neither rawPrint or rawFile is set.\n\n");
  //   std::fprintf(stderr, "%s\n", help_usage.c_str());
  //   std::exit(1);
  // }

  std::FILE *fp = nullptr;
  if(!rawFilePath.empty()){
    std::filesystem::path filepath(rawFilePath);
    std::filesystem::path path_dir_output = std::filesystem::absolute(filepath).parent_path();
    std::filesystem::file_status st_dir_output =
      std::filesystem::status(path_dir_output);
    if (!std::filesystem::exists(st_dir_output)) {
      std::fprintf(stdout, "Output folder does not exist: %s\n\n",
                   path_dir_output.c_str());
      std::filesystem::file_status st_parent =
        std::filesystem::status(path_dir_output.parent_path());
      if (std::filesystem::exists(st_parent) &&
          std::filesystem::is_directory(st_parent)) {
        if (std::filesystem::create_directory(path_dir_output)) {
          std::fprintf(stdout, "Create output folder: %s\n\n", path_dir_output.c_str());
        } else {
          std::fprintf(stderr, "Unable to create folder: %s\n\n", path_dir_output.c_str());
          throw;
        }
      } else {
        std::fprintf(stderr, "Unable to create folder: %s\n\n", path_dir_output.c_str());
        throw;
      }
    }

    std::filesystem::file_status st_file = std::filesystem::status(filepath);
    if (std::filesystem::exists(st_file)) {
      std::fprintf(stderr, "File < %s > exists.\n\n", filepath.c_str());
      throw;
    }

    fp = std::fopen(filepath.c_str(), "w");
    if (!fp) {
      std::fprintf(stderr, "File opening failed: %s \n\n", filepath.c_str());
      throw;
    }
  }

  std::fprintf(stdout, " connecting to %s\n", "/dev/axidmard");
  int fd_rx = open("/dev/axidmard", O_RDONLY | O_NONBLOCK);
  if(!fd_rx){
    std::fprintf(stdout, " connection fail\n");
    throw;
  }
  std::fprintf(stdout, " connected\n");

  size_t unexpectedN = 0;
  size_t dataFrameN = 0;

  std::chrono::system_clock::time_point tp_timeout_exit  = std::chrono::system_clock::now() + std::chrono::seconds(exitTimeSecond);

  bool isFirstEvent = true;
  uint16_t firstId = 0;
  uint16_t lastId = 0;


  std::future<uint64_t> fut_async_watch;
  fut_async_watch = std::async(std::launch::async, &AsyncWatchDog);
  while(!g_done){
    if(exitTimeSecond && std::chrono::system_clock::now() > tp_timeout_exit){
      std::fprintf(stdout, "run %d seconds, nornal exit\n", exitTimeSecond);
      break;
    }


    auto df_pack = readPack(fd_rx, std::chrono::seconds(1));
    // auto df = rd->Read(std::chrono::seconds(1));
    if(df_pack.empty()){
      // std::fprintf(stdout, "Data reveving timeout\n");
      // std::fprintf(stdout, "!!!!!you might need to start alpide with rbcp tool (rbcp_main) \n");
      // std::fprintf(stdout, "!!!!!you might need to trigger alpide \n");
      continue;
    }
    else if(df_pack.size()<16){
      std::fprintf(stdout, "ERROR, too small pack size\n");
    }
    if(do_rawPrint){
      std::fprintf(stdout, "-- DataFrame #%d, DeviceId #%hhu,  TriggerId #%hu, PayloadLen %u\n",
                   dataFrameN, df_pack[3],
                   *reinterpret_cast<const uint16_t*>(df_pack.data() + 8),
                   *reinterpret_cast<const uint32_t*>(df_pack.data() + 4));
      std::fprintf(stdout, "RawData_TCP_RX:\n%s\n", binToHexString(df_pack).c_str());
      fromRaw(df_pack);
      std::fflush(stdout);
    }

    uint16_t triggerId =  *reinterpret_cast<const uint16_t*>(df_pack.data() + 8);
    if(isFirstEvent){
      lastId = triggerId -1;
      isFirstEvent = false;
    }
    uint16_t expectedId = lastId +1;

    if(triggerId != expectedId ){
      if(do_tidCheck){
        std::fprintf(stdout, "expected #%hu, got #%hu,  lost #%hu\n",  expectedId, triggerId, triggerId-expectedId);
      }
      unexpectedN ++;
    }

    if(fp){
      std::fwrite(df_pack.data(), 1, df_pack.size(), fp);
      std::fflush(fp);
    }
    dataFrameN ++;
    lastId = triggerId;

    ga_dataFrameN = dataFrameN;
    ga_unexpectedN = unexpectedN;
    ga_lastTriggerId = lastId;
  }

  std::fprintf(stdout, "dataFrameN #%zu, unexpectedN #%zu\n",  dataFrameN, unexpectedN);

  close(fd_rx);
  if(fp){
    std::fflush(fp);
    std::fclose(fp);
  }

  g_done= 1;
  if(fut_async_watch.valid())
    fut_async_watch.get();

  return 0;
}

