#pragma once

#include <string>
#include <vector>
#include <chrono>

#include "mysystem.hh"
#include "TelEvent.hpp"

class AltelReader{
public:
  ~AltelReader();
  AltelReader();
  bool Open();
  void Close();

  std::shared_ptr<altel::TelEvent> Read(const std::chrono::milliseconds &timeout);

  std::vector<std::shared_ptr<altel::TelEvent>> Read(size_t size_max_pkg,
                                                     const std::chrono::milliseconds &timeout_idle,
                                                     const std::chrono::milliseconds &timeout_total);
  std::string readRawPack(const std::chrono::milliseconds &timeout_idel);

  static std::string binToHexString(const char *bin, int len);
  static std::string hexToBinString(const char *hex, int len);
  static std::string binToHexString(const std::string& bin);
  static std::string hexToBinString(const std::string& hex);
  static std::shared_ptr<altel::TelEvent> createTelEvent(const std::string& raw);
  static std::string LoadFileToString(const std::string& path);
  static size_t dumpBrokenData(int fd_rx);
  static std::string readPack(int fd_rx, const std::chrono::milliseconds &timeout_idel);

private:
  int m_fd{0};
  std::string m_file_path;
};
