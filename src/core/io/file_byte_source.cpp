#include "core/io/file_byte_source.h"

namespace diarium {

bool read_file(const std::string& path, std::string* out) {
  std::FILE* fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) return false;
  out->clear();
  char buf[4096];
  size_t got;
  while ((got = std::fread(buf, 1, sizeof(buf), fp)) > 0) out->append(buf, got);
  const bool ok = std::ferror(fp) == 0;
  std::fclose(fp);
  return ok;
}

bool write_file(const std::string& path, const std::string& data) {
  std::FILE* fp = std::fopen(path.c_str(), "wb");
  if (fp == nullptr) return false;
  const size_t wrote = std::fwrite(data.data(), 1, data.size(), fp);
  const bool ok = wrote == data.size() && std::ferror(fp) == 0;
  std::fclose(fp);
  return ok;
}

}  // namespace diarium
