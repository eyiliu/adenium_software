#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <string>

#include "FirmwarePortal.hh"

static const std::string altel_reg_cmd_list_content =
#include "altel_reg_cmd_list_json.hh"
  ;

FirmwarePortal::FirmwarePortal(const std::string &json_str){
  m_alpide_ip_addr = "";
  if(json_str == "builtin"){
    m_json.Parse(altel_reg_cmd_list_content.c_str());
  }
  else{
    m_json.Parse(json_str.c_str());
  }
  if(m_json.HasParseError()){
    fprintf(stderr, "JSON parse error: %s (at string positon %lu)", rapidjson::GetParseError_En(m_json.GetParseError()), m_json.GetErrorOffset());
    throw;
  }
}

const std::string& FirmwarePortal::DeviceUrl(){
  return m_alpide_ip_addr;
}

void  FirmwarePortal::WriteWord(uint64_t address, uint64_t value){
  DebugFormatPrint(std::cout, "INFO<%s>: %se( address=%#016llx ,  value=%#016llx )\n", __func__, __func__, address, value);
    
  int fd;
  if ((fd = open("/dev/mem", O_RDWR|O_SYNC)) < 0 ) {
    printf("Error opening file. \n");
    exit(-1);
  }
  
  off_t phy_addr = address;
  size_t len = 1;
  size_t page_size = getpagesize();  
  off_t offset_in_page = phy_addr & (page_size - 1);

  size_t mapped_size = page_size * ( (offset_in_page + len)/page_size + 1);
  
  void *map_base = mmap(NULL,
			mapped_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			fd,
			phy_addr & ~(off_t)(page_size - 1));

  if (map_base == MAP_FAILED){
    perror("Memory mapped failed\n");
    exit(-1);
  }

  char* virt_addr = (char*)map_base + offset_in_page;
  uint32_t* virt_addr_u32 = reinterpret_cast<uint32_t*>(virt_addr);
  *virt_addr_u32 = (uint32_t)value;

  close(fd);
};

uint64_t FirmwarePortal::ReadWord(uint64_t address){
  DebugFormatPrint(std::cout, "ReadWord( address=%#016x)\n", address);

  int fd;
  if ((fd = open("/dev/mem", O_RDWR|O_SYNC)) < 0 ) {
    printf("Error opening file. \n");
    exit(-1);
  }
  
  off_t phy_addr = address;
  size_t len = 1;
  size_t page_size = getpagesize();  
  off_t offset_in_page = phy_addr & (page_size - 1);

  size_t mapped_size = page_size * ( (offset_in_page + len)/page_size + 1);
  
  void *map_base = mmap(NULL,
			mapped_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			fd,
			phy_addr & ~(off_t)(page_size - 1));

  if (map_base == MAP_FAILED){
    perror("Memory mapped failed\n");
    exit(-1);
  }

  char* virt_addr = (char*)map_base + offset_in_page;
  uint32_t* virt_addr_u32 = reinterpret_cast<uint32_t*>(virt_addr);
  uint32_t reg_value = *virt_addr_u32;  

  close(fd);
  
  DebugFormatPrint(std::cout, "ReadWord( address=%#016x) return value=%#016x\n", address, reg_value);
  return reg_value;
};


void FirmwarePortal::SetFirmwareRegister(const std::string& name, uint64_t value){
  DebugFormatPrint(std::cout, "INFO<%s>: %s( name=%s ,  value=%#016llx )\n", __func__, __func__, name.c_str(), value);
  static const std::string array_name("FIRMWARE_REG_LIST_V4");
  auto& json_array = m_json[array_name];
  if(json_array.Empty()){
    FormatPrint(std::cerr, "ERROR<%s>:   unable to find array<%s>\n", __func__, array_name.c_str());
    throw;
  }
  bool flag_found_reg = false;
  for(auto& json_reg: json_array.GetArray()){
    if( json_reg["name"] != name )
      continue;
    auto& json_addr = json_reg["address"];
    if(json_addr.IsString()){
      uint64_t address = std::stoull(json_reg["address"].GetString(), 0, 16);
      WriteWord(address, value);
    }
    else if(json_addr.IsArray()){
      auto& json_bytes = json_reg["bytes"];
      auto& json_words = json_reg["words"];
      if(!json_bytes.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   bytes<%s> is not an int\n", __func__, Stringify(json_bytes).c_str());
	throw;
      }
      if(!json_words.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   words<%s> is not an int\n", __func__, Stringify(json_words).c_str());
	throw;
      }
      uint64_t n_bytes = json_bytes.GetUint64();
      uint64_t n_words = json_words.GetUint64();
      if((!n_bytes)||(!n_words)||(n_bytes%n_words)){
	FormatPrint(std::cerr, "ERROR<%s>: incorrect bytes<%u> or words<%u>\n" , __func__, json_bytes.GetUint64(), json_words.GetUint64());
	throw;
      }
      uint64_t n_bits_per_word = 8*n_bytes/n_words;
      
      auto& json_endian = json_reg["endian"];
      bool flag_is_little_endian;
      if(json_endian=="LE"){
	flag_is_little_endian = true;
      }
      else if(json_endian=="BE"){
	flag_is_little_endian = false;
      }
      else{
	FormatPrint(std::cerr, "ERROR<%s>: unknown endian<%s>\n", __func__, Stringify(json_endian).c_str());
	throw;
      }

      if(n_words != json_addr.Size()){
	FormatPrint(std::cerr, "ERROR<%s>: address<%s> array's size does not match the word number which is %u\n", __func__ , Stringify(json_addr).c_str(), n_words);
	throw;
      }
      uint64_t i=0;
      for(auto& name_in_array: json_addr.GetArray()){
	if(!name_in_array.IsString()){
	  FormatPrint(std::cerr, "ERROR<%s>: name<%s> is not a string value\n", __func__, Stringify(name_in_array).c_str());
	  throw;
	}
	std::string name_in_array_str = name_in_array.GetString();
	uint64_t sub_value;
	if(flag_is_little_endian){
	  size_t f = 8*sizeof(value)-n_bits_per_word*(i+1);
	  size_t b = 8*sizeof(value)-n_bits_per_word;
	  sub_value = (value<<f)>>b;
	  // DebugFormatPrint(std::cout, "INFO<%s>: %s value=%#016x (<<%z)  (>>%z) sub_value=%#016llx \n", __func__, "LE", value, f, b, sub_value);
	}
	else{
	  size_t f = 8*sizeof(value)-n_bits_per_word*(n_words-i);
	  size_t b = 8*sizeof(value)-n_bits_per_word;
	  sub_value = (value<<f)>>b;
	  // DebugFormatPrint(std::cout, "INFO<%s>: %s value=%#016x (<<%z)  (>>%z) sub_value=%#016llx \n", __func__, "BE", value, f, b, sub_value);
	}
	SetFirmwareRegister(name_in_array_str, sub_value);
	i++;
      }
    }
    else{
      FormatPrint(std::cerr, "ERROR<%s>: unknown address format<%s>\n", __func__, Stringify(json_addr).c_str());
      throw;
    }
    flag_found_reg = true;
    break;
  }
  if(!flag_found_reg){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find register<%s> in array<%s>\n", __func__, name.c_str(), array_name.c_str());
    throw;
  }
}

void FirmwarePortal::SetAlpideRegister(const std::string& name, uint64_t value){
  DebugFormatPrint(std::cout, "INFO<%s>: %s( name=%s ,  value=%#016llx )\n", __func__, __func__, name.c_str(), value);
  static const std::string array_name("CHIP_REG_LIST");
  auto& json_array = m_json[array_name];
  if(json_array.Empty()){
    FormatPrint(std::cerr, "ERROR<%s>:   unable to find array<%s>\n", __func__, array_name.c_str());
    throw;
  }   
  
  bool flag_found_reg = false;
  for(auto& json_reg: json_array.GetArray()){
    if( json_reg["name"] != name )
      continue;
    auto& json_addr = json_reg["address"];
    if(json_addr.IsString()){
      uint64_t address = std::stoull(json_reg["address"].GetString(), 0, 16);
      SetFirmwareRegister("DET_WRITE_DATA", value);
      SetFirmwareRegister("DET_WRITE_ADDR", address);
    }
    else if(json_addr.IsArray()){
      auto& json_bytes = json_reg["bytes"];
      auto& json_words = json_reg["words"];
      if(!json_bytes.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   bytes<%s> is not an int\n", __func__, Stringify(json_bytes).c_str());
	throw;
      }
      if(!json_words.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   words<%s> is not an int\n", __func__, Stringify(json_words).c_str());
	throw;
      }
      uint64_t n_bytes = json_bytes.GetUint64();
      uint64_t n_words = json_words.GetUint64();
      if((!n_bytes)||(!n_words)||(n_bytes%n_words)){
	FormatPrint(std::cerr, "ERROR<%s>: incorrect bytes<%u> or words<%u>\n" , __func__, json_bytes.GetUint64(), json_words.GetUint64());
	throw;
      }
      uint64_t n_bits_per_word = 8*n_bytes/n_words;

      auto& json_endian = json_reg["endian"];
      bool flag_is_little_endian;
      if(json_endian=="LE"){
	flag_is_little_endian = true;
      }
      else if(json_endian=="BE"){
	flag_is_little_endian = false;
      }
      else{
	FormatPrint(std::cerr, "ERROR<%s>: unknown endian<%s>\n", __func__, Stringify(json_endian).c_str());
	throw;
      }
      
      if(n_words != json_addr.Size()){
	FormatPrint(std::cerr, "ERROR<%s>: address<%s> array's size does not match the word number which is %u\n", __func__ ,Stringify(json_addr).c_str(), n_words);
	throw;
      }
      uint64_t i=0;
      for(auto& name_in_array: json_addr.GetArray() ){
	if(!name_in_array.IsString()){
	  FormatPrint(std::cerr, "ERROR<%s>: name<%s> is not a string value\n", __func__, Stringify(name_in_array).c_str());
	  throw;
	}
	std::string name_in_array_str = name_in_array.GetString();
	uint64_t sub_value;
	if(flag_is_little_endian){
	  uint64_t f = (8*sizeof(value)-n_bits_per_word*(i+1));
	  uint64_t b = (8*sizeof(value)-n_bits_per_word);
	  sub_value = (value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s value=%#016x << %u  >>%u sub_value=%#016x \n", __func__, "LE", value, f, b, sub_value);
	}
	else{
	  uint64_t f = (8*sizeof(value)-n_bits_per_word*(n_words-i));
	  uint64_t b = (8*sizeof(value)-n_bits_per_word);
	  sub_value = (value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s value=%#016x << %u  >>%u sub_value=%#016x \n", __func__, "BE", value, f, b, sub_value);
	}
	SetAlpideRegister(name_in_array_str, sub_value);
	i++;  
      }
    }
    else{
      FormatPrint(std::cerr, "ERROR<%s>: unknown address format<%s>\n", __func__, Stringify(json_addr).c_str());
      throw;
    }
    flag_found_reg = true;
    break;
  }
  if(!flag_found_reg){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find register<%s> in array<%s>\n", __func__, name.c_str(), array_name.c_str());
    throw;
  }
}

void FirmwarePortal::SendFirmwareCommand(const std::string& name){
  DebugFormatPrint(std::cout, "INFO<%s>:  %s( name=%s )\n", __func__, __func__, name.c_str());
  static const std::string array_name("FIRMWARE_CMD_LIST_V4");
  auto& json_array = m_json[array_name];
  if(json_array.Empty()){
    FormatPrint(std::cerr, "ERROR<%s>:   unable to find array<%s>\n", __func__, array_name.c_str());
    throw;
  }
  bool flag_found_cmd = false;
  for(auto& json_cmd: json_array.GetArray()){
    if( json_cmd["name"] != name )
      continue;
    auto& json_value = json_cmd["value"];
    if(!json_value.IsString()){
      FormatPrint(std::cerr, "ERROR<%s>:   command value<%s> is not a string\n", __func__, Stringify(json_value).c_str());
      throw;
    }
    uint64_t cmd_value = std::stoull(json_value.GetString(),0,16);
    SetFirmwareRegister("DAQ_CMD", cmd_value);
    flag_found_cmd = true;
  }
  if(!flag_found_cmd){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find command<%s> in array<%s>\n", __func__, name.c_str(), array_name.c_str());
    throw;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void FirmwarePortal::SendAlpideCommand(const std::string& name){
  DebugFormatPrint(std::cout, "INFO<%s>:  %s( name=%s )\n", __func__, __func__, name.c_str());
  static const std::string array_name("CHIP_CMD_LIST");
  auto& json_array = m_json[array_name];
  if(json_array.Empty()){
    FormatPrint(std::cerr, "ERROR<%s>:   unable to find array<%s>\n", __func__, array_name.c_str());
    throw;
  }
  bool flag_found_cmd = false;
  for(auto& json_cmd: json_array.GetArray()){
    if( json_cmd["name"] != name )
      continue;
    auto& json_value = json_cmd["value"];
    if(!json_value.IsString()){
      FormatPrint(std::cerr, "ERROR<%s>:   command value<%s> is not a string\n", __func__, Stringify(json_value).c_str());
      throw;
    }
    uint64_t cmd_value = std::stoull(json_value.GetString(),0,16);
    SetAlpideRegister("CHIP_CMD", cmd_value);
    flag_found_cmd = true;
  }
  if(!flag_found_cmd){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find command<%s> in array<%s>\n", __func__, name.c_str(), array_name.c_str());
    throw;
  } 
}

void FirmwarePortal::SendAlpideBroadcast(const std::string& name){
  DebugFormatPrint(std::cout, "INFO<%s>:  %s( name=%s )\n", __func__, __func__, name.c_str());
  static const std::string array_name("CHIP_CMD_LIST");
  auto& json_array = m_json[array_name];
  if(json_array.Empty()){
    FormatPrint(std::cerr, "ERROR<%s>:   unable to find array<%s>\n", __func__, array_name.c_str());
    throw;
  }
  bool flag_found_cmd = false;
  for(auto& json_cmd: json_array.GetArray()){
    if( json_cmd["name"] != name )
      continue;
    auto& json_value = json_cmd["value"];
    if(!json_value.IsString()){
      FormatPrint(std::cerr, "ERROR<%s>:   command value<%s> is not a string\n", __func__, Stringify(json_value).c_str());
      throw;
    }
    uint64_t cmd_value = std::stoull(json_value.GetString(),0,16);
    SetFirmwareRegister("DET_CMD_OPCODE", cmd_value);
    flag_found_cmd = true;
  }
  if(!flag_found_cmd){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find command<%s> in array<%s>\n", __func__, name.c_str(), array_name.c_str());
    throw;
  }
}


uint64_t FirmwarePortal::GetFirmwareRegister(const std::string& name){
  DebugFormatPrint(std::cout, "INFO<%s>:  %s( name=%s )\n", __func__, __func__, name.c_str());
  static const std::string array_name("FIRMWARE_REG_LIST_V4");
  auto& json_array = m_json[array_name];
  if(json_array.Empty()){
    FormatPrint(std::cerr, "ERROR<%s>:   unable to find array<%s>\n", __func__, array_name.c_str());
    throw;
  }
  uint64_t value;
  bool flag_found_reg = false;
  for(auto& json_reg: json_array.GetArray()){
    if( json_reg["name"] != name )
      continue;
    auto& json_addr = json_reg["address"];
    if(json_addr.IsString()){
      uint64_t address = std::stoull(json_reg["address"].GetString(), 0, 16);
      value = ReadWord(address);
    }
    else if(json_addr.IsArray()){
      auto& json_bytes = json_reg["bytes"];
      auto& json_words = json_reg["words"];
      if(!json_bytes.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   bytes<%s> is not an int\n", __func__, Stringify(json_bytes).c_str());
	throw;
      }
      if(!json_words.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   words<%s> is not an int\n", __func__, Stringify(json_words).c_str());
	throw;
      }
      uint64_t n_bytes = json_bytes.GetUint64();
      uint64_t n_words = json_words.GetUint64();
      if((!n_bytes)||(!n_words)||(n_bytes%n_words)){
	FormatPrint(std::cerr, "ERROR<%s>: incorrect bytes<%u> or words<%u>\n" , __func__, json_bytes.GetUint64(), json_words.GetUint64());
	throw;
      }
      uint64_t n_bits_per_word = 8*n_bytes/n_words;

      auto& json_endian = json_reg["endian"];
      bool flag_is_little_endian;
      if(json_endian=="LE"){
	flag_is_little_endian = true;
      }
      else if(json_endian=="BE"){
	flag_is_little_endian = false;
      }
      else{
	FormatPrint(std::cerr, "ERROR<%s>: unknown endian<%s>\n", __func__, Stringify(json_endian).c_str());
	throw;
      }

      if(n_words != json_addr.Size()){
	FormatPrint(std::cerr, "ERROR<%s>: address<%s> array's size does not match the word number which is %u\n", __func__ , Stringify(json_addr).c_str(), n_words);
	throw;
      }
      uint64_t i=0;
      value = 0;
      for(auto& name_in_array: json_addr.GetArray() ){
	if(!name_in_array.IsString()){
	  FormatPrint(std::cerr, "ERROR<%s>: name<%s> is not a string value\n", __func__, Stringify(name_in_array).c_str());
	  throw;
	}
	std::string name_in_array_str = name_in_array.GetString();
	uint64_t sub_value = GetFirmwareRegister(name_in_array_str);
	uint64_t add_value;
	if(flag_is_little_endian){
	  uint64_t f = n_bits_per_word*i;
	  uint64_t b = 0;
	  add_value = (sub_value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s sub_value=%#016x << %u  >>%u add_value=%#016x \n", __func__, "LE", sub_value, f, b, add_value);
	}
	else{
	  uint64_t f = n_bits_per_word*(n_words-1-i);
	  uint64_t b = 0;
	  add_value = (sub_value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s sub_value=%#016x << %u  >>%u add_value=%#016x \n", __func__, "BE", sub_value, f, b, add_value);
	}
	value += add_value;
	i++;
      }
    }
    else{
      FormatPrint(std::cerr, "ERROR<%s>: unknown address format<%s>\n", __func__, Stringify(json_addr).c_str());
      throw;
    }
    flag_found_reg = true;
    break;
  }
  if(!flag_found_reg){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find register<%s> in array<%s>\n", __func__, name.c_str(), array_name.c_str());
    throw;
  }
  DebugFormatPrint(std::cout, "INFO<%s>: %s( name=%s ) return value=%#016x \n", __func__, __func__, name.c_str(), value);
  return value;
}


uint64_t FirmwarePortal::GetAlpideRegister(const std::string& name){  
  DebugFormatPrint(std::cout, "INFO<%s>:  %s( name=%s )\n",__func__, __func__, name.c_str());
  static const std::string array_name("CHIP_REG_LIST");
  auto& json_array = m_json[array_name];
  if(json_array.Empty()){
    FormatPrint(std::cerr, "ERROR<%s>:   unable to find array<%s>\n", __func__, array_name.c_str());
    throw;
  }
  uint64_t value;
  bool flag_found_reg = false;
  for(auto& json_reg: json_array.GetArray()){
    if( json_reg["name"] != name )
      continue;
    auto& json_addr = json_reg["address"];
    if(json_addr.IsString()){
      uint64_t address = std::stoull(json_reg["address"].GetString(), 0, 16);
      uint64_t nr_old = GetFirmwareRegister("DET_READ_CNT");
      SetFirmwareRegister("DET_READ_ADDR", address);
      std::chrono::system_clock::time_point  tp_timeout = std::chrono::system_clock::now() +  std::chrono::milliseconds(1000);
      bool flag_enable_counter_check = true; //TODO: enable it for a real hardware;
      if(!flag_enable_counter_check){
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	FormatPrint(std::cout, "WARN<%s>: checking of the read count is disabled\n", __func__);
      }
      while(flag_enable_counter_check){
	uint64_t nr_new = GetFirmwareRegister("DET_READ_CNT");
	if(nr_new != nr_old){
	  break;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	if(std::chrono::system_clock::now() > tp_timeout){
	  FormatPrint(std::cerr, "ERROR<%s>:  timeout to read back Alpide register<%s>\n", __func__, name.c_str());
	  throw;
	}
      }
      value = GetFirmwareRegister("DET_READ_DATA");
    }
    else if(json_addr.IsArray()){
      auto& json_bytes = json_reg["bytes"];
      auto& json_words = json_reg["words"];
      if(!json_bytes.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   bytes<%s> is not an int\n", __func__, Stringify(json_bytes).c_str());
	throw;
      }
      if(!json_words.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   words<%s> is not an int\n", __func__, Stringify(json_words).c_str());
	throw;
      }
      uint64_t n_bytes = json_bytes.GetUint64();
      uint64_t n_words = json_words.GetUint64();
      if((!n_bytes)||(!n_words)||(n_bytes%n_words)){
	FormatPrint(std::cerr, "ERROR<%s>: incorrect bytes<%u> or words<%u>\n" , __func__, json_bytes.GetUint64(), json_words.GetUint64());
	throw;
      }
      uint64_t n_bits_per_word = 8*n_bytes/n_words;

      auto& json_endian = json_reg["endian"];
      bool flag_is_little_endian;
      if(json_endian=="LE"){
	flag_is_little_endian = true;
      }
      else if(json_endian=="BE"){
	flag_is_little_endian = false;
      }
      else{
	FormatPrint(std::cerr, "ERROR<%s>: unknown endian<%s>\n", __func__, Stringify(json_endian).c_str());
	throw;
      }

      if(n_words != json_addr.Size()){
	FormatPrint(std::cerr, "ERROR<%s>: address<%s> array's size does not match the word number which is %u\n", __func__ , Stringify(json_addr).c_str(), n_words);
	throw;
      }
      uint64_t i=0;
      value = 0;
      for(auto& name_in_array: json_addr.GetArray() ){
	if(!name_in_array.IsString()){
	  FormatPrint(std::cerr, "ERROR<%s>: name<%s> is not a string value\n", __func__, Stringify(name_in_array).c_str());
	  throw;
	}
	std::string name_in_array_str = name_in_array.GetString();
	uint64_t sub_value = GetAlpideRegister(name_in_array_str);
	uint64_t add_value;
	if(flag_is_little_endian){
	  uint64_t f = n_bits_per_word*i;
	  uint64_t b = 0;
	  add_value = (sub_value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s sub_value=%#016x << %u  >>%u add_value=%#016x \n", __func__, "LE", sub_value, f, b, add_value);
	}
	else{
	  uint64_t f = n_bits_per_word*(n_words-1-i);
	  uint64_t b = 0;
	  add_value = (sub_value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s sub_value=%#016x << %u  >>%u add_value=%#016x \n", __func__, "BE", sub_value, f, b, add_value);
	}
	value += add_value;
	i++;
      }
    }
    else{
      FormatPrint(std::cerr, "ERROR<%s>: unknown address format<%s>\n", __func__, Stringify(json_addr).c_str());
      throw;
    }
    flag_found_reg = true;
    break;
  }
  if(!flag_found_reg){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find register<%s> in array<%s>\n", __func__, name.c_str(), array_name.c_str());
    throw;
  }  
  DebugFormatPrint(std::cout, "INFO<%s>: %s( name=%s ) return value=%#016x \n", __func__, __func__, name.c_str(), value);
  return value;  
}


std::string FirmwarePortal::LoadFileToString(const std::string& path){
  std::ifstream ifs(path);
  if(!ifs.good()){
      std::cerr<<"LoadFileToString:: ERROR, unable to load file<"<<path<<">\n";
      throw;
  }

  std::string str;
  str.assign((std::istreambuf_iterator<char>(ifs) ),
             (std::istreambuf_iterator<char>()));
  return str;
}


void FirmwarePortal::SetRegionRegister(uint64_t region, const std::string& name, uint64_t value){
  DebugFormatPrint(std::cout, "INFO<%s>: %s( name=%s ,  value=%#016x )\n", __func__, __func__, name.c_str(), value);
  static const std::string array_name("CHIP_REG_LIST");
  auto& json_array = m_json[array_name];
  if(json_array.Empty()){
    FormatPrint(std::cerr, "ERROR<%s>:   unable to find array<%s>\n", __func__, array_name.c_str());
    throw;
  }   
  
  bool flag_found_reg = false;
  for(auto& json_reg: json_array.GetArray()){
    if( json_reg["name"] != name )
      continue;
    auto& json_addr = json_reg["address"];
    if(json_addr.IsString()){
      uint64_t address_region_local = (std::stoull(json_reg["address"].GetString(), 0, 16)) & 0x07ff;
      uint64_t address = (region << 11) + address_region_local;
      SetFirmwareRegister("DET_WRITE_DATA", value);
      SetFirmwareRegister("DET_WRITE_ADDR", address);
    }
    else if(json_addr.IsArray()){
      auto& json_bytes = json_reg["bytes"];
      auto& json_words = json_reg["words"];
      if(!json_bytes.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   bytes<%s> is not an int\n", __func__, Stringify(json_bytes).c_str());
	throw;
      }
      if(!json_words.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   words<%s> is not an int\n", __func__, Stringify(json_words).c_str());
	throw;
      }
      uint64_t n_bytes = json_bytes.GetUint64();
      uint64_t n_words = json_words.GetUint64();
      if((!n_bytes)||(!n_words)||(n_bytes%n_words)){
	FormatPrint(std::cerr, "ERROR<%s>: incorrect bytes<%u> or words<%u>\n" , __func__, json_bytes.GetUint64(), json_words.GetUint64());
	throw;
      }
      uint64_t n_bits_per_word = 8*n_bytes/n_words;

      auto& json_endian = json_reg["endian"];
      bool flag_is_little_endian;
      if(json_endian=="LE"){
	flag_is_little_endian = true;
      }
      else if(json_endian=="BE"){
	flag_is_little_endian = false;
      }
      else{
	FormatPrint(std::cerr, "ERROR<%s>: unknown endian<%s>\n", __func__, Stringify(json_endian).c_str());
	throw;
      }
      
      if(n_words != json_addr.Size()){
	FormatPrint(std::cerr, "ERROR<%s>: address<%s> array's size does not match the word number which is %u\n", __func__ ,Stringify(json_addr).c_str(), n_words);
	throw;
      }
      uint64_t i=0;
      for(auto& name_in_array: json_addr.GetArray() ){
	if(!name_in_array.IsString()){
	  FormatPrint(std::cerr, "ERROR<%s>: name<%s> is not a string value\n", __func__, Stringify(name_in_array).c_str());
	  throw;
	}
	std::string name_in_array_str = name_in_array.GetString();
	uint64_t sub_value;
	if(flag_is_little_endian){
	  uint64_t f = (8*sizeof(value)-n_bits_per_word*(i+1));
	  uint64_t b = (8*sizeof(value)-n_bits_per_word);
	  sub_value = (value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s value=%#016x << %u  >>%u sub_value=%#016x \n", __func__, "LE", value, f, b, sub_value);
	}
	else{
	  uint64_t f = (8*sizeof(value)-n_bits_per_word*(n_words-i));
	  uint64_t b = (8*sizeof(value)-n_bits_per_word);
	  sub_value = (value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s value=%#016x << %u  >>%u sub_value=%#016x \n", __func__, "BE", value, f, b, sub_value);
	}
	SetRegionRegister(region, name_in_array_str, sub_value);
	i++;  
      }
    }
    else{
      FormatPrint(std::cerr, "ERROR<%s>: unknown address format<%s>\n", __func__, Stringify(json_addr).c_str());
      throw;
    }
    flag_found_reg = true;
    break;
  }
  if(!flag_found_reg){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find register<%s> in array<%s>\n", __func__, name.c_str(), array_name.c_str());
    throw;
  }  
}


void FirmwarePortal::BroadcastRegionRegister(const std::string& name, uint64_t value){
  DebugFormatPrint(std::cout, "INFO<%s>: %s( name=%s ,  value=%#016x )\n", __func__, __func__, name.c_str(), value);
  static const std::string array_name("CHIP_REG_LIST");
  auto& json_array = m_json[array_name];
  if(json_array.Empty()){
    FormatPrint(std::cerr, "ERROR<%s>:   unable to find array<%s>\n", __func__, array_name.c_str());
    throw;
  }   
  
  bool flag_found_reg = false;
  for(auto& json_reg: json_array.GetArray()){
    if( json_reg["name"] != name )
      continue;
    auto& json_addr = json_reg["address"];
    if(json_addr.IsString()){
      uint64_t address_region_local = (std::stoull(json_reg["address"].GetString(), 0, 16)) & 0x07ff;
      uint64_t address = address_region_local | 0x80; // 0b1000 0000 broadcast_bit
      SetFirmwareRegister("DET_WRITE_DATA", value);
      SetFirmwareRegister("DET_WRITE_ADDR", address);
    }
    else if(json_addr.IsArray()){
      auto& json_bytes = json_reg["bytes"];
      auto& json_words = json_reg["words"];
      if(!json_bytes.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   bytes<%s> is not an int\n", __func__, Stringify(json_bytes).c_str());
	throw;
      }
      if(!json_words.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   words<%s> is not an int\n", __func__, Stringify(json_words).c_str());
	throw;
      }
      uint64_t n_bytes = json_bytes.GetUint64();
      uint64_t n_words = json_words.GetUint64();
      if((!n_bytes)||(!n_words)||(n_bytes%n_words)){
	FormatPrint(std::cerr, "ERROR<%s>: incorrect bytes<%u> or words<%u>\n" , __func__, json_bytes.GetUint64(), json_words.GetUint64());
	throw;
      }
      uint64_t n_bits_per_word = 8*n_bytes/n_words;

      auto& json_endian = json_reg["endian"];
      bool flag_is_little_endian;
      if(json_endian=="LE"){
	flag_is_little_endian = true;
      }
      else if(json_endian=="BE"){
	flag_is_little_endian = false;
      }
      else{
	FormatPrint(std::cerr, "ERROR<%s>: unknown endian<%s>\n", __func__, Stringify(json_endian).c_str());
	throw;
      }
      
      if(n_words != json_addr.Size()){
	FormatPrint(std::cerr, "ERROR<%s>: address<%s> array's size does not match the word number which is %u\n", __func__ ,Stringify(json_addr).c_str(), n_words);
	throw;
      }
      uint64_t i=0;
      for(auto& name_in_array: json_addr.GetArray() ){
	if(!name_in_array.IsString()){
	  FormatPrint(std::cerr, "ERROR<%s>: name<%s> is not a string value\n", __func__, Stringify(name_in_array).c_str());
	  throw;
	}
	std::string name_in_array_str = name_in_array.GetString();
	uint64_t sub_value;
	if(flag_is_little_endian){
	  uint64_t f = (8*sizeof(value)-n_bits_per_word*(i+1));
	  uint64_t b = (8*sizeof(value)-n_bits_per_word);
	  sub_value = (value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s value=%#016x << %u  >>%u sub_value=%#016x \n", __func__, "LE", value, f, b, sub_value);
	}
	else{
	  uint64_t f = (8*sizeof(value)-n_bits_per_word*(n_words-i));
	  uint64_t b = (8*sizeof(value)-n_bits_per_word);
	  sub_value = (value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s value=%#016x << %u  >>%u sub_value=%#016x \n", __func__, "BE", value, f, b, sub_value);
	}
	BroadcastRegionRegister(name_in_array_str, sub_value);
	i++;  
      }
    }
    else{
      FormatPrint(std::cerr, "ERROR<%s>: unknown address format<%s>\n", __func__, Stringify(json_addr).c_str());
      throw;
    }
    flag_found_reg = true;
    break;
  }
  if(!flag_found_reg){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find register<%s> in array<%s>\n", __func__, name.c_str(), array_name.c_str());
    throw;
  }
}


void FirmwarePortal::SetPixelRegister(uint64_t x, uint64_t y, const std::string& name, uint64_t value){
  if(name!="MASK_EN" && name!="PULSE_EN"){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find register<%s>. There are MASK_EN and PULSE_EN in pixel.\n", __func__, name.c_str());
    throw;
  }
  
  uint64_t bit_ROWREGM_DATA = value? 0x0002:0x0000;
  uint64_t bit_ROWREGM_SEL = (name=="MASK_EN")?0x0000:0x0001; // mismatch between pALPIDE-3 and ALPIDE manual, using later
  uint64_t conf_value = bit_ROWREGM_DATA | bit_ROWREGM_SEL;

  uint64_t column_line_region_number = x/32;
  uint64_t column_line_number_local = x%32;
  uint64_t column_line_number_local_pattern = 1ULL << column_line_number_local;

  uint64_t row_line_region_number = y/16;
  uint64_t row_line_number_local = y%16;
  uint64_t row_line_number_local_pattern = 1ULL << row_line_number_local;
  
  BroadcastRegionRegister("REGION_COLUMN_SELECT", 0);
  BroadcastRegionRegister("REGION_ROW_SELECT", 0); //disable all selection lines
  
  SetAlpideRegister("PIX_CONF_GLOBAL", conf_value);
  SetRegionRegister(column_line_region_number, "REGION_COLUMN_SELECT", column_line_number_local_pattern);
  SetRegionRegister(row_line_region_number, "REGION_ROW_SELECT", row_line_number_local_pattern);

  BroadcastRegionRegister("REGION_COLUMN_SELECT", 0);
  BroadcastRegionRegister("REGION_ROW_SELECT", 0); //disable all selection lines
}

void FirmwarePortal::SetPixelRegisterFullChip(const std::string& name, uint64_t value){
  if(name!="MASK_EN" && name!="PULSE_EN"){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find register<%s>. There are MASK_EN and PULSE_EN in pixel.\n", __func__, name.c_str());
    throw;
  }
  
  uint64_t bit_ROWREGM_DATA = value? 0x0002:0x0000;
  uint64_t bit_ROWREGM_SEL = (name=="MASK_EN")?0x0000:0x0001;
  uint64_t conf_value = bit_ROWREGM_DATA | bit_ROWREGM_SEL;

  //not sure what is toggle bit, ignored?

  BroadcastRegionRegister("REGION_COLUMN_SELECT", 0);
  BroadcastRegionRegister("REGION_ROW_SELECT", 0); //disable all selection lines
  
  SetAlpideRegister("PIX_CONF_GLOBAL", conf_value);  
  BroadcastRegionRegister("REGION_COLUMN_SELECT", 0xffffffff);
  BroadcastRegionRegister("REGION_ROW_SELECT", 0xffff); //enable all selection lines
  
  BroadcastRegionRegister("REGION_COLUMN_SELECT", 0);
  BroadcastRegionRegister("REGION_ROW_SELECT", 0); //disable all selection lines
}


void FirmwarePortal::SetPixelRegisterFullColumn(uint64_t x, const std::string& name, uint64_t value){
  if(name!="MASK_EN" && name!="PULSE_EN"){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find register<%s>. There are MASK_EN and PULSE_EN in pixel.\n", __func__, name.c_str());
    throw;
  }
  
  uint64_t bit_ROWREGM_DATA = value? 0x0002:0x0000;
  uint64_t bit_ROWREGM_SEL = (name=="MASK_EN")?0x0000:0x0001; // mismatch between pALPIDE-3 and ALPIDE manual, using later
  uint64_t conf_value = bit_ROWREGM_DATA | bit_ROWREGM_SEL;

  uint64_t column_line_region_number = x/32;
  uint64_t column_line_number_local = x%32;
  uint64_t column_line_number_local_pattern = 1ULL << column_line_number_local;
  
  BroadcastRegionRegister("REGION_COLUMN_SELECT", 0);
  BroadcastRegionRegister("REGION_ROW_SELECT", 0); //disable all selection lines
  
  SetAlpideRegister("PIX_CONF_GLOBAL", conf_value);
  SetRegionRegister(column_line_region_number, "REGION_COLUMN_SELECT", column_line_number_local_pattern);
  BroadcastRegionRegister("REGION_ROW_SELECT", 0xffff); //enable all row selection lines

  BroadcastRegionRegister("REGION_COLUMN_SELECT", 0);
  BroadcastRegionRegister("REGION_ROW_SELECT", 0); //disable all selection lines
}

void FirmwarePortal::SetPixelRegisterFullRow(uint64_t y, const std::string& name, uint64_t value){
  if(name!="MASK_EN" && name!="PULSE_EN"){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find register<%s>. There are MASK_EN and PULSE_EN in pixel.\n", __func__, name.c_str());
    throw;
  }
  
  uint64_t bit_ROWREGM_DATA = value? 0x0002:0x0000;
  uint64_t bit_ROWREGM_SEL = (name=="MASK_EN")?0x0000:0x0001; // mismatch between pALPIDE-3 and ALPIDE manual, using later
  uint64_t conf_value = bit_ROWREGM_DATA | bit_ROWREGM_SEL;

  uint64_t row_line_region_number = y/16;
  uint64_t row_line_number_local = y%16;
  uint64_t row_line_number_local_pattern = 1ULL << row_line_number_local;
  
  BroadcastRegionRegister("REGION_COLUMN_SELECT", 0);
  BroadcastRegionRegister("REGION_ROW_SELECT", 0); //disable all selection lines
  
  SetAlpideRegister("PIX_CONF_GLOBAL", conf_value);
  BroadcastRegionRegister("REGION_COLUMN_SELECT", 0xffffffff); //enable all column selection lines
  SetRegionRegister(row_line_region_number, "REGION_ROW_SELECT", row_line_number_local_pattern);

  BroadcastRegionRegister("REGION_COLUMN_SELECT", 0);
  BroadcastRegionRegister("REGION_ROW_SELECT", 0); //disable all selection lines
}


uint64_t FirmwarePortal::GetRegionRegister(uint64_t region, const std::string& name){  
  DebugFormatPrint(std::cout, "INFO<%s>:  %s( name=%s )\n",__func__, __func__, name.c_str());
  static const std::string array_name("CHIP_REG_LIST");
  auto& json_array = m_json[array_name];
  if(json_array.Empty()){
    FormatPrint(std::cerr, "ERROR<%s>:   unable to find array<%s>\n", __func__, array_name.c_str());
    throw;
  }
  uint64_t value;
  bool flag_found_reg = false;
  for(auto& json_reg: json_array.GetArray()){
    if( json_reg["name"] != name )
      continue;
    auto& json_addr = json_reg["address"];
    if(json_addr.IsString()){
      uint64_t address_region_local = (std::stoull(json_reg["address"].GetString(), 0, 16)) & 0x07ff;
      uint64_t address = (region << 11) + address_region_local;
      FormatPrint(std::cout, "INFO<%s>: %u(%#x)    %u(%#x) )\n", __func__, address, address, address_region_local, address_region_local);

      uint64_t nr_old = GetFirmwareRegister("DET_READ_CNT");
      SetFirmwareRegister("DET_READ_ADDR", address);
      std::chrono::system_clock::time_point  tp_timeout = std::chrono::system_clock::now() +  std::chrono::milliseconds(1000);
      bool flag_enable_counter_check = true; //TODO: enable it for a real hardware;
      if(!flag_enable_counter_check){
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	FormatPrint(std::cout, "WARN<%s>: checking of the read count is disabled\n", __func__);
      }
      while(flag_enable_counter_check){
	uint64_t nr_new = GetFirmwareRegister("DET_READ_CNT");
	if(nr_new != nr_old){
	  break;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	if(std::chrono::system_clock::now() > tp_timeout){
	  FormatPrint(std::cerr, "ERROR<%s>:  timeout to read back Alpide register<%s>\n", __func__, name.c_str());
	  throw;
	}
      }
      value = GetFirmwareRegister("DET_READ_DATA");
    }
    else if(json_addr.IsArray()){
      auto& json_bytes = json_reg["bytes"];
      auto& json_words = json_reg["words"];
      if(!json_bytes.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   bytes<%s> is not an int\n", __func__, Stringify(json_bytes).c_str());
	throw;
      }
      if(!json_words.IsUint64()){
	FormatPrint(std::cerr, "ERROR<%s>:   words<%s> is not an int\n", __func__, Stringify(json_words).c_str());
	throw;
      }
      uint64_t n_bytes = json_bytes.GetUint64();
      uint64_t n_words = json_words.GetUint64();
      if((!n_bytes)||(!n_words)||(n_bytes%n_words)){
	FormatPrint(std::cerr, "ERROR<%s>: incorrect bytes<%u> or words<%u>\n" , __func__, json_bytes.GetUint64(), json_words.GetUint64());
	throw;
      }
      uint64_t n_bits_per_word = 8*n_bytes/n_words;

      auto& json_endian = json_reg["endian"];
      bool flag_is_little_endian;
      if(json_endian=="LE"){
	flag_is_little_endian = true;
      }
      else if(json_endian=="BE"){
	flag_is_little_endian = false;
      }
      else{
	FormatPrint(std::cerr, "ERROR<%s>: unknown endian<%s>\n", __func__, Stringify(json_endian).c_str());
	throw;
      }

      if(n_words != json_addr.Size()){
	FormatPrint(std::cerr, "ERROR<%s>: address<%s> array's size does not match the word number which is %u\n", __func__ , Stringify(json_addr).c_str(), n_words);
	throw;
      }
      uint64_t i=0;
      value = 0;
      for(auto& name_in_array: json_addr.GetArray() ){
	if(!name_in_array.IsString()){
	  FormatPrint(std::cerr, "ERROR<%s>: name<%s> is not a string value\n", __func__, Stringify(name_in_array).c_str());
	  throw;
	}
	std::string name_in_array_str = name_in_array.GetString();
	uint64_t sub_value = GetRegionRegister(region, name_in_array_str);
	uint64_t add_value;
	if(flag_is_little_endian){
	  uint64_t f = n_bits_per_word*i;
	  uint64_t b = 0;
	  add_value = (sub_value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s sub_value=%#016x << %u  >>%u add_value=%#016x \n", __func__, "LE", sub_value, f, b, add_value);
	}
	else{
	  uint64_t f = n_bits_per_word*(n_words-1-i);
	  uint64_t b = 0;
	  add_value = (sub_value<<f)>>b;
	  DebugFormatPrint(std::cout, "INFO<%s>:  %s sub_value=%#016x << %u  >>%u add_value=%#016x \n", __func__, "BE", sub_value, f, b, add_value);
	}
	value += add_value;
	i++;
      }
    }
    else{
      FormatPrint(std::cerr, "ERROR<%s>: unknown address format<%s>\n", __func__, Stringify(json_addr).c_str());
      throw;
    }
    flag_found_reg = true;
    break;
  }
  if(!flag_found_reg){
    FormatPrint(std::cerr, "ERROR<%s>: unable to find register<%s> in array<%s>\n", __func__, name.c_str(), array_name.c_str());
    throw;
  }  
  DebugFormatPrint(std::cout, "INFO<%s>: %s( name=%s ) return value=%#016x \n", __func__, __func__, name.c_str(), value);
  return value;  
}



void FirmwarePortal::InjectPulse(){
  //SetAlpideRegister("FROMU_CONF_1", ); // APULSE/DPULSE
  SetAlpideRegister("FROMU_PULSING_2", 0xff); // duration
  SendAlpideBroadcast("PULSE");
}


void FirmwarePortal::fw_setid(uint32_t id){
  std::string cmd("DEVICE");
  char ch_id = id%6 + '0';
  cmd = cmd+ch_id;
  SendFirmwareCommand(cmd);
}

void FirmwarePortal::fw_init(){
  SendFirmwareCommand("RESET");
  std::this_thread::sleep_for(std::chrono::seconds(2));
  
  SendFirmwareCommand("TRIGGER_VETO");
  // stop trigger, go into configure mode

  // "sensor":{"FROMU_CONF_1":1792, "FROMU_CONF_2":13, "VCASN":61, "VCASN2":73, "ITHR":56 }

  //=========== init part ========================
  // 3.8 Chip initialization
  // GRST
  SendAlpideBroadcast("GRST"); // chip global reset
  SetAlpideRegister("CHIP_MODE", 0x3c); // configure mode
  // DAC setup
  SetAlpideRegister("VRESETP", 0x75); //117
  SetAlpideRegister("VRESETD", 0x93); //147
  SetAlpideRegister("VCASP", 0x56);   //86
  // uint32_t vcasn = 57;
  // uint32_t ithr  = 51;
  uint32_t vcasn = 61;
  uint32_t ithr  = 56;

  SetAlpideRegister("VCASN", vcasn);   //57 Y50
  SetAlpideRegister("VPULSEH", 0xff); //255
  SetAlpideRegister("VPULSEL", 0x0);  //0
  SetAlpideRegister("VCASN2",vcasn+12);  //62 Y63  VCASN+12
  SetAlpideRegister("VCLIP", 0x0);    //0
  SetAlpideRegister("VTEMP", 0x0);
  SetAlpideRegister("IAUX2", 0x0);
  SetAlpideRegister("IRESET", 0x32);  //50
  SetAlpideRegister("IDB", 0x40);     //64
  SetAlpideRegister("IBIAS", 0x40);   //64
  SetAlpideRegister("ITHR", ithr);   //51  empty 0x32; 0x12 data, not full.  0x33 default, threshold
  // 3.8.1 Configuration of in-pixel logic
  SendAlpideBroadcast("PRST");  //pixel matrix reset
  SetPixelRegisterFullChip("MASK_EN", 0);
  SetPixelRegisterFullChip("PULSE_EN", 0);
  SendAlpideBroadcast("PRST");  //pixel matrix reset
  // 3.8.2 Configuration and start-up of the Data Transmission Unit, PLL
  SetAlpideRegister("DTU_CONF", 0x008d); // default
  SetAlpideRegister("DTU_DAC",  0x0088); // default
  SetAlpideRegister("DTU_CONF", 0x0085); // clear pll disable bit
  SetAlpideRegister("DTU_CONF", 0x0185); // set pll reset bit
  SetAlpideRegister("DTU_CONF", 0x0085); // clear reset bit
  // 3.8.3 Setting up of readout
  // 3.8.3.1a (OB) Setting CMU and DMU Configuration Register
  SetAlpideRegister("CMU_DMU_CONF", 0x70); //Token, disable MCH, enable DDR, no previous OB
  SetAlpideRegister("TEST_CTRL", 0x400); //Disable Busy Line
  // 3.8.3.2 Setting FROMU Configuration Registers and enabling readout mode
  // FROMU Configuration Register 1,2
  SetAlpideRegister("FROMU_CONF_1", 0x00); //Disable external busy, no triger delay
  SetAlpideRegister("FROMU_CONF_2", 13); //STROBE duration, alice testbeam 100
  // FROMU Pulsing Register 1,2
  // m_fw->SetAlpideRegister("FROMU_PULSING_2", 0xffff); //yiliu: test pulse duration, max
  // Periphery Control Register (CHIP MODE)
  // m_fw->SetAlpideRegister("CHIP_MODE", 0x3d); //trigger MODE
  // RORST
  // m_fw->SendAlpideBroadcast("RORST"); //Readout (RRU/TRU/DMU) reset, commit token
  //===========end of init part =====================

  //user init
  //
  //
  // m_fw->SetFirmwareRegister("DEVICE_ID", 0xff);
  //
  //end of user init
}

void FirmwarePortal::fw_start(){
  SetAlpideRegister("CMU_DMU_CONF", 0x70);// token
  SetAlpideRegister("CHIP_MODE", 0x3d); //trigger MODE
  SendAlpideBroadcast("RORST"); //Readout (RRU/TRU/DMU) reset, commit token
  SendFirmwareCommand("TRIGGER_ALLOW"); //run, fw forward trigger
}

void FirmwarePortal::fw_stop(){
  SendFirmwareCommand("TRIGGER_VETO"); // stop trigger, fw goes into configure mode 
  SetAlpideRegister("CHIP_MODE", 0x3c); // sensor goes to configure mode
}
