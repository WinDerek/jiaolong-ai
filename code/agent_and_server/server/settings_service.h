#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace jiaolong {
namespace server {

// Result of a SettingsService operation. When `success` is false,
// `error_message` describes the problem and `invalid_request` indicates
// whether the failure was caused by an invalid client request (HTTP 400) or
// by an internal/settings-file problem (HTTP 500).
struct SettingsResult {
  bool success = false;
  nlohmann::json value;
  std::string error_message;
  bool invalid_request = false;
};

// Business logic for the settings REST APIs. SettingsService reads from and
// writes to the Jiaolong settings file (`~/.jiaolong/settings.json`) directly;
// it does not own any storage.
class SettingsService {
 public:
  SettingsService();

  // GET /api/settings/llm-providers.
  // Returns the enabled LLM provider index together with the list of LLM
  // providers (security keys are intentionally not exposed to clients):
  //   {
  //     "enabledLlmProviderIndex": <int>,
  //     "llmProviders": [
  //       {
  //         "enabled": <bool>,
  //         "baseUrl": <string>,
  //         "model": <string>,
  //         "llmCooldownDuration": <int>,
  //         "retryTimes": <int>
  //       }, ...
  //     ]
  //   }
  // When the settings file does not store `enabledLlmProviderIndex` yet, -1 is
  // returned.
  SettingsResult ListLlmProviders() const;

  // PUT /api/settings/llm-providers.
  // Sets the enabled LLM provider. The request body must be a JSON object
  // {"enabledLlmProviderIndex": <int>} whose value is a valid index into the
  // `llmProviders` array. Persists the new index (and updates each provider's
  // `enabled` flag to stay consistent) in the settings file, then returns the
  // same payload as ListLlmProviders().
  SettingsResult SetEnabledLlmProvider(const nlohmann::json& request) const;

 private:
  // Resolves the settings file path, either the explicitly configured one or
  // the default `~/.jiaolong/settings.json` (empty when HOME is not set).
  std::string SettingsFilePath() const;

  SettingsResult LoadSettings() const;
  bool SaveSettings(const nlohmann::json& settings,
                    std::string& error_message) const;

  std::string settings_file_path_;
};

}  // namespace server
}  // namespace jiaolong