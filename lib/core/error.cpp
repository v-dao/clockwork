#include "cw/error.hpp"

namespace cw {

const char* error_code_str(Error e) noexcept {
  switch (e) {
    case Error::Ok:
      return "Ok";
    case Error::InvalidArgument:
      return "InvalidArgument";
    case Error::IOError:
      return "IOError";
    case Error::Internal:
      return "Internal";
    case Error::NoSnapshot:
      return "NoSnapshot";
    case Error::ParseError:
      return "ParseError";
    case Error::WrongState:
      return "WrongState";
    case Error::NotAllowedWhenFederated:
      return "NotAllowedWhenFederated";
    case Error::UnsupportedScenarioVersion:
      return "UnsupportedScenarioVersion";
  }
  return "Unknown";
}

const char* error_message(Error e) noexcept {
  switch (e) {
    case Error::Ok:
      return "Success";
    case Error::InvalidArgument:
      return "Invalid argument or value out of range";
    case Error::IOError:
      return "File or I/O operation failed";
    case Error::Internal:
      return "Internal error";
    case Error::NoSnapshot:
      return "No snapshot to restore (save a snapshot first)";
    case Error::ParseError:
      return "Scenario text or structure parse/validation failed";
    case Error::WrongState:
      return "Operation not allowed in current engine state";
    case Error::NotAllowedWhenFederated:
      return "Snapshot save/restore not allowed in federated simulation mode";
    case Error::UnsupportedScenarioVersion:
      return "Scenario version not supported by this engine";
  }
  return "Unknown error code";
}

}  // namespace cw
