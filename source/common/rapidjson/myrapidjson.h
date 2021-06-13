#ifndef __MYSYSTEM_INCLUDE_RAPIDJSON
#define __MYSYSTEM_INCLUDE_RAPIDJSON

#include <cstddef>
#include <cstdint>
#include <string>

// SIMD optimization
// #define RAPIDJSON_SSE2
// or
// #define RAPIDJSON_SSE42
// or
// #define RAPIDJSON_NEON
// or
// not enable

#  define RAPIDJSON_HAS_CXX11_TYPETRAITS 1
#  define RAPIDJSON_HAS_CXX11_NOEXCEPT 1
#  define RAPIDJSON_HAS_CXX11_RVALUE_REFS 1
#  define RAPIDJSON_HAS_CXX11_RANGE_FOR 1
#  define RAPIDJSON_HAS_STDSTRING 1
#  define RAPIDJSON_NO_INT64DEFINE 1
   namespace rapidjson { typedef ::std::uint64_t uint64_t; typedef ::std::int64_t int64_t;}
#  define RAPIDJSON_NO_SIZETYPEDEFINE 1
   namespace rapidjson { typedef ::std::size_t SizeType;}
#  include "rapidjson.h"
#  include "allocators.h"
#  include "encodedstream.h"
#  include "filereadstream.h"
#  include "memorystream.h"
#  include "pointer.h"
#  include "reader.h"
#  include "stringbuffer.h"
#  include "cursorstreamwrapper.h"
#  include "encodings.h"
#  include "filewritestream.h"
#  include "istreamwrapper.h"
#  include "prettywriter.h"
#  include "schema.h"
#  include "writer.h"
#  include "document.h"
#  include "fwd.h"
#  include "memorybuffer.h"
#  include "ostreamwrapper.h"
#  include "stream.h"
#  include "error/en.h"


using JsonAllocator = rapidjson::CrtAllocator;
using JsonStackAllocator = rapidjson::CrtAllocator;
using JsonValue = rapidjson::GenericValue<rapidjson::UTF8<char>, rapidjson::CrtAllocator>;
using JsonDocument = rapidjson::GenericDocument<rapidjson::UTF8<char>,
                                                rapidjson::CrtAllocator, rapidjson::CrtAllocator>;
using JsonReader = rapidjson::GenericReader<rapidjson::UTF8<char>, rapidjson::UTF8<char>,
                                            rapidjson::CrtAllocator>;
using JsonWriter = rapidjson::Writer<rapidjson::FileWriteStream,
                                     rapidjson::UTF8<char>, rapidjson::UTF8<char>,
                                     rapidjson::CrtAllocator, rapidjson::kWriteDefaultFlags>;

#include <filesystem>
#include <fstream>

struct JsonFileSerializer{
  JsonFileSerializer(const std::filesystem::path &filepath){
    std::fprintf(stdout, "output file  %s\n", filepath.c_str());

    

    std::filesystem::path path_dir_output = std::filesystem::absolute(filepath).parent_path();
    std::filesystem::file_status st_dir_output =
      std::filesystem::status(path_dir_output);
    if (!std::filesystem::exists(st_dir_output)) {
      std::fprintf(stdout, "Output folder does not exist: %s\n",
                   path_dir_output.c_str());
      std::filesystem::file_status st_parent =
        std::filesystem::status(path_dir_output.parent_path());
      if (std::filesystem::exists(st_parent) &&
          std::filesystem::is_directory(st_parent)) {
        if (std::filesystem::create_directory(path_dir_output)) {
          std::fprintf(stdout, "Create output folder: %s\n", path_dir_output.c_str());
        } else {
          std::fprintf(stderr, "Unable to create folder: %s\n", path_dir_output.c_str());
          throw;
        }
      } else {
        std::fprintf(stderr, "Unable to create folder: %s\n", path_dir_output.c_str());
        throw;
      }
    }

    std::filesystem::file_status st_file = std::filesystem::status(filepath);
    if (std::filesystem::exists(st_file)) {
      std::fprintf(stderr, "File < %s > exists.\n", filepath.c_str());
      throw;
    }

    fp = std::fopen(filepath.c_str(), "w");
    if (!fp) {
      std::fprintf(stderr, "File opening failed: %s \n", filepath.c_str());
      throw;
    }
    os.reset(new rapidjson::FileWriteStream(fp, writeBuffer, sizeof(writeBuffer)));
    writer.reset(new JsonWriter(*os));
    isvalid = true;
  }

  ~JsonFileSerializer(){
    if(os){
      os->Flush();
    }
    if (fp){
      std::fclose(fp);
    }
  }

  operator bool() const {
    return isvalid;
  }

  void putNextJsonValue(const JsonValue& js){
    js.Accept(*writer);
    rapidjson::PutN(*os, '\n', 2);
  }

  void close(){
    if(os){
      os->Flush();
    }
    if (fp){
      std::fclose(fp);
    }
    isvalid=false;
  }

  bool isvalid{0};
  char writeBuffer[UINT16_MAX+1];
  std::FILE *fp{0};
  std::unique_ptr<rapidjson::FileWriteStream> os;
  std::unique_ptr<JsonWriter> writer;
};

struct JsonFileDeserializer {
  JsonFileDeserializer(const std::filesystem::path &filepath) {
    std::fprintf(stdout, "input file  %s\n", filepath.c_str());
    std::filesystem::file_status st_file = std::filesystem::status(filepath);
    if (!std::filesystem::exists(st_file)) {
      std::fprintf(stderr, "File < %s > does not exist.\n", filepath.c_str());
      throw;
    }
    if (!std::filesystem::is_regular_file(st_file)) {
      std::fprintf(stderr, "File < %s > is not regular file.\n",
                   filepath.c_str());
      throw;
    }
    filesize = std::filesystem::file_size(filepath);

    fp = std::fopen(filepath.c_str(), "r");
    if (!fp) {
      std::fprintf(stderr, "File opening failed: %s \n", filepath.c_str());
      throw;
    }
    is.reset(new rapidjson::FileReadStream(fp, readBuffer, sizeof(readBuffer)));
    rapidjson::SkipWhitespace(*is);
    if (is->Peek() == '[') {
      is->Take();
      isarray = true;
    } else {
      isarray = false;
    }
    isvalid = true;
  }

  ~JsonFileDeserializer() {
    if (fp) {
      std::fclose(fp);
    }
  }

  bool operator()(JsonDocument &doc) {
    reader.Parse<rapidjson::kParseStopWhenDoneFlag>(*is, doc);
    if (reader.HasParseError()) {
      if (is->Tell() + 10 < filesize) {
        std::fprintf(
                     stderr,
                     "rapidjson error<%s> when parsing input data at offset %llu\n",
                     rapidjson::GetParseError_En(reader.GetParseErrorCode()),
                     reader.GetErrorOffset());
        throw;
      } // otherwise, it reaches almost end of file. mute the errer message
      isvalid = false;
      return false;
    }
    if (isarray) {
      if (is->Peek() == ',') {
        is->Take();
      } else {
        rapidjson::SkipWhitespace(*is);
        if (is->Peek() == ',') {
          is->Take();
        }
      }
    }
    isvalid = true;
    return true;
  }


  operator bool() const {
    return isvalid;
  }

  JsonDocument getNextJsonDocument(){
    JsonDocument jsd;
    if(isvalid){
      jsd.Populate(*this);
    }
    return jsd;
  }

  JsonValue getNextJsonValue(JsonAllocator& jsa){
    JsonValue jsv;
    if(isvalid){
      JsonDocument jsd(&jsa);
      jsd.Populate(*this);
      if(isvalid){
        jsd.Swap(jsv);
      }
    }
    return jsv;
  }

  size_t filesize{0};
  std::FILE *fp{0};
  char readBuffer[UINT16_MAX + 1];
  std::unique_ptr<rapidjson::FileReadStream> is;
  JsonReader reader;
  bool isvalid{false};
  bool isarray{false};

  /*
  static void example(const std::string &datafile_name) {
    JsonAllocator s_jsa;
    JsonDocument doc(&s_jsa);
    JsonGeneratorArrayUnwrap gen(datafile_name);
    int n = 0;
    std::chrono::high_resolution_clock::time_point t1 =
      std::chrono::high_resolution_clock::now();
    while (1) { // doc is cleared at beginning of each loop
      doc.Populate(gen);
      if (!gen) {
        break;
      }
      n++;
    }
    std::chrono::high_resolution_clock::time_point t2 =
      std::chrono::high_resolution_clock::now();
    double time_sec_total =
      std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1)
      .count();
    double time_sec_per = time_sec_total / n;
    std::printf(
                "datapack %llu, time_sec_total %f, time_sec_per %f,  data_req %f \n", n,
                time_sec_total, time_sec_per, 1. / time_sec_per);
  }
  */
};

namespace JsonUtils{
  inline std::string readFile(const std::string& path){
    std::ifstream ifs(path);
    if(!ifs.good()){
      std::fprintf(stderr, "unable to read file path<%s>\n", path);
      throw std::runtime_error("File open error\n");
    }
    std::string str;
    str.assign((std::istreambuf_iterator<char>(ifs) ),
               (std::istreambuf_iterator<char>()));
    return str;
  }

  inline JsonDocument createJsonDocument(const std::string& str){
    std::istringstream iss(str);
    rapidjson::IStreamWrapper isw(iss);
    JsonDocument jsdoc;
    jsdoc.ParseStream(isw);
    if(jsdoc.HasParseError()){
      std::fprintf(stderr, "rapidjson error<%s> when parsing input data at offset %llu\n",
                   rapidjson::GetParseError_En(jsdoc.GetParseError()), jsdoc.GetErrorOffset());
      throw std::runtime_error("TelGL::createJsonDocument has ParseError\n");
    }
    return jsdoc;
  }

  inline JsonValue createJsonValue(JsonAllocator& jsa, const std::string& str){
    JsonDocument jsdoc = createJsonDocument(str);
    JsonValue js;
    js.CopyFrom(jsdoc, jsa);
    return js;
  }

  inline std::string stringJsonValue(const JsonValue& o, bool pretty = true){
    rapidjson::StringBuffer sb;
    if(pretty){
      rapidjson::PrettyWriter<rapidjson::StringBuffer> w(sb);
      o.Accept(w);
    }
    else{
      rapidjson::Writer<rapidjson::StringBuffer> w(sb);
      o.Accept(w);
    }
    return std::string(sb.GetString(), sb.GetSize());
  }

  inline void printJsonValue(const JsonValue& o, bool pretty = true){
    rapidjson::StringBuffer sb;
    if(pretty){
      rapidjson::PrettyWriter<rapidjson::StringBuffer> w(sb);
      o.Accept(w);
    }
    else{
      rapidjson::Writer<rapidjson::StringBuffer> w(sb);
      o.Accept(w);
    }
    rapidjson::PutN(sb, '\n', 1);
    std::fwrite(sb.GetString(), 1, sb.GetSize(), stdout);
  }
}
#endif
