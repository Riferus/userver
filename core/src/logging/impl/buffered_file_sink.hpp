#pragma once

#include <mutex>
#include <string_view>

#include <spdlog/sinks/sink.h>

#include <logging/impl/base_sink.hpp>
#include <userver/fs/blocking/c_file.hpp>

#include "open_file_helper.hpp"

USERVER_NAMESPACE_BEGIN

namespace logging::impl {

class BufferedFileSink : public BaseSink {
 public:
  BufferedFileSink() = delete;
  explicit BufferedFileSink(const std::string& filename);

  void Reopen(ReopenMode mode) override;

  ~BufferedFileSink() override;

  void Flush() override;

 protected:
  void Write(std::string_view log) final;

  explicit BufferedFileSink(fs::blocking::CFile&& file);
  fs::blocking::CFile& GetFile();

 private:
  std::string filename_;
  fs::blocking::CFile file_;
};

class BufferedStdoutFileSink final : public BufferedFileSink {
 public:
  BufferedStdoutFileSink();
  ~BufferedStdoutFileSink() final;
  void Flush() final;
  void Reopen(ReopenMode) final;
};

class BufferedStderrFileSink final : public BufferedFileSink {
 public:
  BufferedStderrFileSink();
  ~BufferedStderrFileSink() final;
  void Flush() final;
  void Reopen(ReopenMode) final;
};

}  // namespace logging::impl

USERVER_NAMESPACE_END
