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
      return "成功";
    case Error::InvalidArgument:
      return "参数无效或超出允许范围";
    case Error::IOError:
      return "文件或 I/O 操作失败";
    case Error::Internal:
      return "内部错误";
    case Error::NoSnapshot:
      return "没有可恢复的快照（需先保存快照）";
    case Error::ParseError:
      return "想定文本或结构解析/校验失败";
    case Error::WrongState:
      return "当前引擎状态不允许该操作";
    case Error::NotAllowedWhenFederated:
      return "联邦仿真模式下不允许保存或恢复快照";
    case Error::UnsupportedScenarioVersion:
      return "想定版本号不受当前引擎支持";
  }
  return "未知错误码";
}

}  // namespace cw
