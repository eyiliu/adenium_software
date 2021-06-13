
#include <regex>
#include <iostream>

#include "Layer.hh"

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

using namespace altel;

Layer::~Layer(){
  m_conn.reset();

  m_is_async_watching = false;
  if(m_fut_async_watch.valid())
    m_fut_async_watch.get();
}

void Layer::start(){
  m_vec_ring_ev.clear();
  m_vec_ring_ev.resize(m_size_ring);
  m_count_ring_write = 0;
  m_count_ring_read = 0;
  m_hot_p_read = m_size_ring -1; // tail

  m_tg_expected = 0;
  m_flag_wait_first_event = true;

  m_st_n_tg_ev_now =0;
  m_st_n_ev_input_now =0;
  m_st_n_ev_output_now =0;
  m_st_n_ev_bad_now =0;
  m_st_n_ev_overflow_now =0;
  m_st_n_tg_ev_begin = 0;

  std::stringstream ssbuf;
  std::string strbuf;
  NetMsg daqMsg{NetMsg::daqstart, 0, 0, 0, {}};
  ssbuf.str(std::string());
  msgpack::pack(ssbuf, daqMsg);
  strbuf = ssbuf.str();
  m_isDataAccept= true;
  m_conn->sendRaw(strbuf.data(), strbuf.size());

  if(!m_is_async_watching){
    m_fut_async_watch = std::async(std::launch::async, &Layer::AsyncWatchDog, this);
  }
}

void Layer::stop(){
  m_isDataAccept= false;

  std::stringstream ssbuf;
  std::string strbuf;
  NetMsg daqMsg{NetMsg::daqstop, 0, 0, 0, {}};
  ssbuf.str(std::string());
  msgpack::pack(ssbuf, daqMsg);
  strbuf = ssbuf.str();
  m_conn->sendRaw(strbuf.data(), strbuf.size());

  m_is_async_watching = false;
  if(m_fut_async_watch.valid())
    m_fut_async_watch.get();
}

void Layer::init(){
  m_isDataAccept= false;
  m_conn =  TcpConnection::connectToServer(m_host,  m_port, reinterpret_cast<FunProcessMessage>(&Layer::perConnProcessRecvMesg), nullptr, this);

  std::stringstream ssbuf;
  std::string strbuf;
  NetMsg daqMsg{NetMsg::daqinit, 0, 0, 0, {}};
  ssbuf.str(std::string());
  msgpack::pack(ssbuf, daqMsg);
  strbuf = ssbuf.str();
  m_conn->sendRaw(strbuf.data(), strbuf.size());

}

int Layer::perConnProcessRecvMesg(void* pconn, msgpack::object_handle& oh){ // IMPROVE IT AS A RING
  if(!m_isDataAccept){
    std::cout<< "msg is dropped"<<std::endl;
    return 0;
  }

  msgpack::object msg = oh.get();
  unique_zone& life = oh.zone();

  NetMsg netmsg;
  try{
    netmsg = std::move(msg.as<NetMsg>());
  }
  catch(...){
    std::cout<< "msg parsing error"<<std::endl;
    return 0;
  }

  // std::cout<< "bin "<<TcpConnection::binToHexString(netmsg.bin.data(),netmsg.bin.size())<<std::endl;
  if(netmsg.type!=NetMsg::Type::data){
    std::cout<< "unknown msg type"<<std::endl;
  }

  std::string datastr(netmsg.bin.data(),netmsg.bin.size());
  auto df = altel::Layer::createTelEvent(datastr);
  if(!df){
    std::cout<< "fatal error: fail to create telev"<<std::endl;
    return 0;
  }

  m_st_n_ev_input_now ++;
  uint64_t next_p_ring_write = m_count_ring_write % m_size_ring;
  if(next_p_ring_write == m_hot_p_read){
    // buffer full, permanent data lose
    m_st_n_ev_overflow_now ++;
    return 0;
  }
  uint16_t tg_l16 = 0xffff & df->clkN();
  // std::cout<< "id "<< tg_l16 <<std::endl;
  if(m_flag_wait_first_event){
    m_flag_wait_first_event = false;
    m_extension = df->detN() ;
    m_tg_expected = tg_l16;
    m_st_n_tg_ev_begin = m_tg_expected;
  }
  if(tg_l16 != (m_tg_expected & 0xffff)){
    // std::cout<<(tg_expected & 0x7fff)<< " " << tg_l16<<"\n";
    uint32_t tg_guess_0 = (m_tg_expected & 0xffff0000) + tg_l16;
    uint32_t tg_guess_1 = (m_tg_expected & 0xffff0000) + 0x10000 + tg_l16;
    if(tg_guess_0 > m_tg_expected && tg_guess_0 - m_tg_expected < 20000){
      // std::cout<< "missing trigger, expecting : provided "<< (m_tg_expected & 0xffff) << " : "<< tg_l16<<" ("<< m_extension <<") \n";
      m_tg_expected =tg_guess_0;
    }
    else if (tg_guess_1 > m_tg_expected && tg_guess_1 - m_tg_expected < 20000){
      // std::cout<< "missing trigger, expecting : provided "<< (m_tg_expected & 0xffff) << " : "<< tg_l16<<" ("<< m_extension <<") \n";
      m_tg_expected =tg_guess_1;
    }
    else{
      std::cout<< "broken trigger ID, expecting : provided "<< (m_tg_expected & 0xffff) << " : "<< tg_l16<<" ("<<df->detN() <<") \n";
      m_tg_expected ++;
      m_st_n_ev_bad_now ++;
      // permanent data lose
      return 0;
    }
  }
  //TODO: fix tlu firmware, mismatch between modes AIDA start at 1, EUDET start at 0
  df->clkN() = m_tg_expected;
  m_st_n_tg_ev_now = m_tg_expected;

  m_vec_ring_ev[next_p_ring_write] = df;
  m_count_ring_write ++;
  m_tg_expected ++;

  return 1;
}

std::string  Layer::GetStatusString(){
  std::unique_lock<std::mutex> lk(m_mtx_st);
  return m_st_string;
}

TelEventSP& Layer::Front(){
  if(m_count_ring_write > m_count_ring_read) {
    uint64_t next_p_ring_read = m_count_ring_read % m_size_ring;
    m_hot_p_read = next_p_ring_read;
    // keep hot read to prevent write-overlapping
    return m_vec_ring_ev[next_p_ring_read];
  }
  else{
    return m_ring_end;
  }
}

void Layer::PopFront(){
  if(m_count_ring_write > m_count_ring_read) {
    uint64_t next_p_ring_read = m_count_ring_read % m_size_ring;
    m_hot_p_read = next_p_ring_read;
    // keep hot read to prevent write-overlapping
    m_vec_ring_ev[next_p_ring_read].reset();
    m_count_ring_read ++;
  }
}

uint64_t Layer::Size(){
  return  m_count_ring_write - m_count_ring_read;
}

void Layer::ClearBuffer(){
  m_count_ring_write = m_count_ring_read;
  m_vec_ring_ev.clear();
}

uint64_t Layer::AsyncWatchDog(){
  std::chrono::system_clock::time_point m_tp_old;
  std::chrono::system_clock::time_point m_tp_run_begin;

  m_tp_run_begin = std::chrono::system_clock::now();
  m_tp_old = m_tp_run_begin;
  m_is_async_watching = true;

  m_st_n_tg_ev_old =0;
  m_st_n_ev_input_old = 0;
  m_st_n_ev_bad_old =0;
  m_st_n_ev_overflow_old = 0;

  while(m_is_async_watching){
    std::this_thread::sleep_for(std::chrono::seconds(1));
    uint64_t st_n_tg_ev_begin = m_st_n_tg_ev_begin;
    uint64_t st_n_tg_ev_now = m_st_n_tg_ev_now;
    uint64_t st_n_ev_input_now = m_st_n_ev_input_now;
    uint64_t st_n_ev_bad_now = m_st_n_ev_bad_now;
    uint64_t st_n_ev_overflow_now = m_st_n_ev_overflow_now;

    // time
    auto tp_now = std::chrono::system_clock::now();
    std::chrono::duration<double> dur_period_sec = tp_now - m_tp_old;
    std::chrono::duration<double> dur_accu_sec = tp_now - m_tp_run_begin;
    double sec_period = dur_period_sec.count();
    double sec_accu = dur_accu_sec.count();

    // period
    uint64_t st_n_tg_ev_period = st_n_tg_ev_now - m_st_n_tg_ev_old;
    uint64_t st_n_ev_input_period = st_n_ev_input_now - m_st_n_ev_input_old;
    uint64_t st_n_ev_bad_period = st_n_ev_bad_now - m_st_n_ev_bad_old;
    uint64_t st_n_ev_overflow_period = st_n_ev_overflow_now - m_st_n_ev_overflow_old;

    // ratio
    //double st_output_vs_input_accu = st_n_ev_input_now? st_ev_output_now / st_ev_input_now : 1;
    double st_bad_vs_input_accu = st_n_ev_input_now? 1.0 * st_n_ev_bad_now / st_n_ev_input_now : 0;
    double st_overflow_vs_input_accu = st_n_ev_input_now? 1.0 *  st_n_ev_overflow_now / st_n_ev_input_now : 0;
    double st_input_vs_trigger_accu = st_n_ev_input_now? 1.0 * st_n_ev_input_now / (st_n_tg_ev_now - st_n_tg_ev_begin + 1) : 1;
    //double st_output_vs_input_period = st_ev_input_period? st_ev_output_period / st_ev_input_period : 1;
    double st_bad_vs_input_period = st_n_ev_input_period? 1.0 * st_n_ev_bad_period / st_n_ev_input_period : 0;
    double st_overflow_vs_input_period = st_n_ev_input_period? 1.0 *  st_n_ev_overflow_period / st_n_ev_input_period : 0;
    double st_input_vs_trigger_period = st_n_tg_ev_period? 1.0 *  st_n_ev_input_period / st_n_tg_ev_period : 1;

    // hz
    double st_hz_tg_accu = (st_n_tg_ev_now - st_n_tg_ev_begin + 1) / sec_accu ;
    double st_hz_input_accu = st_n_ev_input_now / sec_accu ;

    double st_hz_tg_period = st_n_tg_ev_period / sec_period ;
    double st_hz_input_period = st_n_ev_input_period / sec_period ;

    std::string st_string_new =
      Layer::FormatString("L<%u> event(%d)/trigger(%d - %d)=Ev/Tr(%.4f) dEv/dTr(%.4f) tr_accu(%.2f hz) ev_accu(%.2f hz) tr_period(%.2f hz) ev_period(%.2f hz)",
                          m_extension, st_n_ev_input_now, st_n_tg_ev_now, st_n_tg_ev_begin, st_input_vs_trigger_accu, st_input_vs_trigger_period,
                          st_hz_tg_accu, st_hz_input_accu, st_hz_tg_period, st_hz_input_period
        );

    {
      std::unique_lock<std::mutex> lk(m_mtx_st);
      m_st_string = std::move(st_string_new);
    }

    //write to old
    m_st_n_tg_ev_old = st_n_tg_ev_now;
    m_st_n_ev_input_old = st_n_ev_input_now;
    m_st_n_ev_bad_old = st_n_ev_bad_now;
    m_st_n_ev_overflow_old = st_n_ev_overflow_now;
    m_tp_old = tp_now;
  }
  return 0;
}

std::shared_ptr<altel::TelEvent> Layer::createTelEvent(const std::string& raw){
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
