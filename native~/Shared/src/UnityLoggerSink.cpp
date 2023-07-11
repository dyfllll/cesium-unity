#include "UnityLoggerSink.h"

#include <DotNet/System/Object.h>
#include <DotNet/System/String.h>
#include <DotNet/UnityEngine/Debug.h>

using namespace CesiumForUnity;
using namespace DotNet;

void UnityLoggerSink::sink_it_(const spdlog::details::log_msg& msg) {
  switch (msg.level) {
  case spdlog::level::warn:
    break;
  default:
    spdlog::memory_buf_t formatted;
    this->formatter_->format(msg, formatted);
    UnityEngine::Debug::Log(System::String(fmt::to_string(formatted)));
    break;
  }

}

void UnityLoggerSink::flush_() {}
