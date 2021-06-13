#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <future>

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>


#include "getopt.h"

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::size_t;

#ifndef DEBUG_PRINT
#define DEBUG_PRINT 1
#endif
#define debug_print(fmt, ...)                                           \
  do { if (DEBUG_PRINT) std::fprintf(stdout, fmt, ##__VA_ARGS__); } while (0)


static sig_atomic_t g_done = 0;

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
      std::cout<<binToHexString(&d, 1)<<std::endl;
      
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
  -help                    help message
  -hex <HEXRAW>            hex string input 



)";

int main(int argc, char *argv[]) {
  std::string hexraw;
  int do_verbose = 0;
  {////////////getopt begin//////////////////
    struct option longopts[] = {{"help",      no_argument, NULL, 'h'},//option -W is reserved by getopt
                                {"verbose",   no_argument, NULL, 'v'},//val
                                {"hex",   required_argument, NULL, 'f'},
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
	hexraw = optarg;
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
  std::fprintf(stdout, "hexraw:  %s\n", hexraw.c_str());
  std::fprintf(stdout, "\n");

  std::string bin = hexToBinString(hexraw);
  std::cout<<"decode  "<<binToHexString(bin)<<std::endl;

  fromRaw(bin);
  
  return 0;
}

