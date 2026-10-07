#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace jiaolong {

enum AgentTurnType {
  kMessage,
  kSlashCommand
};

class AgentTurn {

 public:

  AgentTurn(const nlohmann::json& message) :
      type(kMessage), message_(message) {}

  AgentTurn(const std::string& slash_command) :
      type(kSlashCommand), command_(slash_command) {}

  AgentTurnType type;

  nlohmann::json ToJson() const {
    nlohmann::json json;
    switch (type) {
      case AgentTurnType::kMessage:
        json["type"] = "message";
        json["message"] = message_;
        break;
      case AgentTurnType::kSlashCommand:
        json["type"] = "slash_command";
        json["command"] = command_;
        break;
    }
    return json;
  }

 private:

  nlohmann::json message_;
  std::string command_;

};

}  // namespace jiaolong
