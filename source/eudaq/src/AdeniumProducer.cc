#include "eudaq/Producer.hh"

#include <list>
#include <iostream>
#include <chrono>
#include <thread>

#include "Telescope.hh"

#define MAGIC_EUDET_PIVOT (9152)

template<typename ... Args>
static std::string FormatString( const std::string& format, Args ... args ){
  std::size_t size = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1;
  std::unique_ptr<char[]> buf( new char[ size ] );
  std::snprintf( buf.get(), size, format.c_str(), args ... );
  return std::string( buf.get(), buf.get() + size - 1 );
}


namespace altel{
  class AdeniumProducer : public eudaq::Producer {
  public:
    using eudaq::Producer::Producer;
    ~AdeniumProducer() override {};
    void DoInitialise() override;
    void DoConfigure() override;
    void DoStartRun() override;
    void DoStopRun() override;
    void DoTerminate() override;
    void DoReset() override;
    void DoStatus() override;

    static const uint32_t m_id_factory = eudaq::cstr2hash("AdeniumProducer");
    static const uint32_t m_id1_factory = eudaq::cstr2hash("AltelNiProducer");

    void RunLoop() override;
  private:
    bool m_exit_of_run;
    std::unique_ptr<altel::Telescope> m_tel;

    std::atomic<uint64_t> m_tg_n_begin;
    std::atomic<uint64_t> m_tg_n_last;

    std::chrono::system_clock::time_point m_tp_run_begin;

    uint64_t m_st_n_tg_old;
    std::chrono::system_clock::time_point m_st_tp_old;
  };
}

namespace{
  auto _reg_ = eudaq::Factory<eudaq::Producer>::
    Register<altel::AdeniumProducer, const std::string&, const std::string&>(altel::AdeniumProducer::m_id_factory);

  auto _reg1_ = eudaq::Factory<eudaq::Producer>::
    Register<altel::AdeniumProducer, const std::string&, const std::string&>(altel::AdeniumProducer::m_id1_factory);
}


void altel::AdeniumProducer::DoInitialise(){
  m_tel.reset(new altel::Telescope("builtin", "builtin"));
  m_tel->Init();
}

void altel::AdeniumProducer::DoConfigure(){
  //do nothing here
}

void altel::AdeniumProducer::DoStartRun(){
  m_tel->Start_no_tel_reading();
}

void altel::AdeniumProducer::DoStopRun(){
  m_tel->Stop();
  m_exit_of_run = true;
}

void altel::AdeniumProducer::DoReset(){
  m_tel.reset();
}

void altel::AdeniumProducer::DoTerminate(){
  std::terminate();
}


void altel::AdeniumProducer::RunLoop(){
  m_tp_run_begin = std::chrono::system_clock::now();
  auto tp_start_run = std::chrono::steady_clock::now();
  m_exit_of_run = false;
  bool is_first_event = true;
  while(!m_exit_of_run){
    auto telev = m_tel->ReadEvent();
    if(!telev){
      std::this_thread::sleep_for(std::chrono::microseconds(100));
      continue;
    }

    uint64_t trigger_n = telev->clkN();

    auto ev_eudaq = eudaq::Event::MakeUnique("NiRawDataEvent");
    ev_eudaq->SetTriggerN(trigger_n);

    std::map<uint32_t,  std::vector<std::shared_ptr<altel::TelMeasHit>>> map_layer_measHits;

    std::map<uint32_t,  std::vector<uint32_t>> map_layer_ni_pixels;

    for(auto& mh: telev->measHits()){
      if(!mh){
        continue;
      }
      uint32_t detN = mh->detN();
      map_layer_measHits[detN].push_back(mh);
    }

    std::vector<uint32_t>  ni_block;
    ni_block.reserve(200);

    for(uint32_t detN = 0; detN<6; detN++){
      auto& mhs = map_layer_measHits[detN];
      size_t npixel_on_detN = 0;
      for(auto& mh : mhs){
        npixel_on_detN += mh->measRaws().size();
      }
      ni_block.push_back(detN); // header
      if(ni_block.size()==1){
        ni_block.push_back(MAGIC_EUDET_PIVOT | ((trigger_n & 0xff)<<16)); // pivot, tid
      }

      ni_block.push_back(0);//fcnt
      ni_block.push_back( (npixel_on_detN &0xffff) | ((npixel_on_detN &0xffff)<<16) );//lent and lent_h

      for(auto &mh : mhs){
        for(auto &mr : mh->measRaws()){
          //col-x-u:    [11+2+16-1:2+16-1]
          //row-y-v:    [11+4-1:4-1]
          //nstate:     [4-1:0]
          //morepixel:  [2+16-1:16-1]
          // [x31, x30, x29, x28, col-x-u, morepixel[1:0]=00, x15, row-y-v, nstate[3:0]=0001]
          ni_block.push_back( ((mr.u()&0x7ff)<<(2+16)) | ((mr.v()&0x7ff)<<4) | 0x1 ); // pixel
        }
      }
      ni_block.push_back(0);//layer tail0;
      // ni_block.push_back(0);//layer tail1;
      // ni_block.push_back(0);//layer tail3;
    }
    ev_eudaq->AddBlock(0, ni_block);
    ev_eudaq->AddBlock(1, ni_block);

    SendEvent(std::move(ev_eudaq));
    if(is_first_event){
      is_first_event = false;
      m_tg_n_begin = trigger_n;
    }
    m_tg_n_last = trigger_n;
  }
}

void altel::AdeniumProducer::DoStatus() {
  if(m_exit_of_run){
    auto tp_now = std::chrono::system_clock::now();
    m_st_tp_old = tp_now;
    m_st_n_tg_old = 0;
  }

  if (!m_exit_of_run) {
    auto tp_now = std::chrono::system_clock::now();
    std::chrono::duration<double> dur_period_sec = tp_now - m_st_tp_old;
    std::chrono::duration<double> dur_accu_sec = tp_now - m_tp_run_begin;
    uint64_t st_n_tg_now = m_tg_n_last;
    uint64_t st_n_tg_begin = m_tg_n_begin;

    double sec_accu = dur_accu_sec.count();
    double sec_period = dur_period_sec.count();

    double st_hz_tg_accu = (st_n_tg_now - st_n_tg_begin) / sec_accu ;
    double st_hz_tg_period = (st_n_tg_now - m_st_n_tg_old) / sec_period ;

    SetStatusTag("TriggerID(latest:first)", FormatString("%u:%u", st_n_tg_now, st_n_tg_begin));
    SetStatusTag("TriggerHz(per:avg)", FormatString("%.1f:%.1f", st_hz_tg_period, st_hz_tg_accu));
    // SetStatusTag("EventHz(per,avg)", std::to_string());
    // SetStatusTag("Cluster([layer:avg:per])", std::to_string());

    m_st_tp_old = tp_now;
    m_st_n_tg_old = st_n_tg_now;
  }
}
