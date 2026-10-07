#include "server/settings_service.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace jiaolong {
namespace server {

namespace {

constexpr char kLlmProviders[] = "llmProviders";
constexpr char kEnabledLlmProviderIndex[] = "enabledLlmProviderIndex";
constexpr char kEnabled[] = "enabled";
constexpr char kBaseUrl[] = "baseUrl";
constexpr char kModel[] = "model";
constexpr char kLlmCooldownDuration[] = "llmCooldownDuration";
constexpr char kRetryTimes[] = "retryTimes";

SettingsResult Failure(const std::string& message, bool invalid_request) {
  SettingsResult result;
  result.success = false;
  result.error_message = message;
  result.invalid_request = invalid_request;
  return result;
}

// Returns a single LLM provider JSON object for API responses. The security
// key is deliberately omitted so credentials are never exposed to clients.
nlohmann::json LlmProviderToJson(const nlohmann::json& provider) {
  return nlohmann::json{
      {kEnabled, provider.value(kEnabled, false)},
      {kBaseUrl, provider.value(kBaseUrl, "")},
      {kModel, provider.value(kModel, "")},
      {kLlmCooldownDuration, provider.value(kLlmCooldownDuration, 0)},
      {kRetryTimes, provider.value(kRetryTimes, 0)},
  };
}

// Returns the enabled index stored in the settings file, or -1 when the field
// is absent, null, or not an integer (treated as absent).
int StoredEnabledIndex(const nlohmann::json& settings) {
  const auto it = settings.find(kEnabledLlmProviderIndex);
  if (it == settings.end() || it->is_null() || !it->is_number_integer()) {
    return -1;
  }
  return it->get<int>();
}

}  // namespace

SettingsService::SettingsService() = default;

std::string SettingsService::SettingsFilePath() const {
  if (!settings_file_path_.empty()) {
    return settings_file_path_;
  }
  const char* home_directory = std::getenv("HOME");
  if (home_directory == nullptr) {
    return "";
  }
  return (std::filesystem::path(home_directory) /
      ".jiaolong" / "settings.json").string();
}

SettingsResult SettingsService::LoadSettings() const {
  const std::string settings_file_path = SettingsFilePath();
  if (settings_file_path.empty()) {
    return Failure("Failed to locate settings file: HOME is not set.", false);
  }
  std::ifstream settings_file(settings_file_path);
  if (!settings_file.is_open()) {
    return Failure("Failed to open settings file: " + settings_file_path,
                   false);
  }
  std::stringstream buffer;
  buffer << settings_file.rdbuf();
  nlohmann::json settings = nlohmann::json::parse(buffer.str(), nullptr, false);
  if (settings.is_discarded()) {
    return Failure("Failed to parse settings file: " + settings_file_path,
                   false);
  }
  SettingsResult result;
  result.success = true;
  result.value = std::move(settings);
  return result;
}

bool SettingsService::SaveSettings(const nlohmann::json& settings,
                                   std::string& error_message) const {
  const std::string settings_file_path = SettingsFilePath();
  if (settings_file_path.empty()) {
    error_message = "Failed to locate settings file: HOME is not set.";
    return false;
  }
  std::ofstream settings_file(settings_file_path);
  if (!settings_file.is_open()) {
    error_message =
        "Failed to open settings file for writing: " + settings_file_path;
    return false;
  }
  settings_file << settings.dump(2) << std::endl;
  return true;
}

SettingsResult SettingsService::ListLlmProviders() const {
  SettingsResult loaded = LoadSettings();
  if (!loaded.success) {
    return loaded;
  }
  const nlohmann::json& settings = loaded.value;
  const auto providers_it = settings.find(kLlmProviders);
  if (providers_it == settings.end() || !providers_it->is_array()) {
    return Failure("llmProviders must be an array.", false);
  }

  nlohmann::json providers = nlohmann::json::array();
  for (const auto& provider : *providers_it) {
    providers.push_back(LlmProviderToJson(provider));
  }

  SettingsResult result;
  result.success = true;
  result.value = nlohmann::json{
      {kEnabledLlmProviderIndex, StoredEnabledIndex(settings)},
      {kLlmProviders, providers},
  };
  return result;
}

SettingsResult SettingsService::SetEnabledLlmProvider(
    const nlohmann::json& request) const {
  if (!request.is_object()) {
    return Failure("request body must be a JSON object", true);
  }
  const auto index_it = request.find(kEnabledLlmProviderIndex);
  if (index_it == request.end() || !index_it->is_number_integer()) {
    return Failure("missing or invalid field: enabledLlmProviderIndex", true);
  }
  const int new_index = index_it->get<int>();

  SettingsResult loaded = LoadSettings();
  if (!loaded.success) {
    return loaded;
  }
  nlohmann::json settings = loaded.value;
  const auto providers_it = settings.find(kLlmProviders);
  if (providers_it == settings.end() || !providers_it->is_array()) {
    return Failure("llmProviders must be an array.", false);
  }
  if (new_index < 0 ||
      new_index >= static_cast<int>(providers_it->size())) {
    return Failure("enabledLlmProviderIndex out of range.", true);
  }

  // Persist the new index and keep each provider's `enabled` flag consistent
  // with the selected index (the selected provider is the only enabled one).
  settings[kEnabledLlmProviderIndex] = new_index;
  for (int i = 0; i < static_cast<int>(providers_it->size()); ++i) {
    (*providers_it)[i][kEnabled] = (i == new_index);
  }
  std::string error_message;
  if (!SaveSettings(settings, error_message)) {
    return Failure(error_message, false);
  }

  // Rebuild and return the same payload as ListLlmProviders().
  nlohmann::json providers = nlohmann::json::array();
  for (const auto& provider : *providers_it) {
    providers.push_back(LlmProviderToJson(provider));
  }
  SettingsResult result;
  result.success = true;
  result.value = nlohmann::json{
      {kEnabledLlmProviderIndex, new_index},
      {kLlmProviders, providers},
  };
  return result;
}

}  // namespace server
}  // namespace jiaolong