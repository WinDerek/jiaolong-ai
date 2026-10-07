#include "server/client_credential_manager.h"

#include <utility>

#include "util/uuid_generator.h"

namespace jiaolong {
namespace server {

namespace {

constexpr char kAccessTokenPrefix[] = "jl_access_";
constexpr char kRefreshTokenPrefix[] = "jl_refresh_";

std::string NewAccessToken() {
  return kAccessTokenPrefix + GenerateUuid();
}

std::string NewRefreshToken() {
  return kRefreshTokenPrefix + GenerateUuid();
}

}  // namespace

ClientCredentialManager::ClientCredentialManager(std::string username,
    std::string password, Clock clock)
    : username_(std::move(username)),
      password_(std::move(password)),
      clock_(std::move(clock)) {}

std::optional<ClientTokenPair> ClientCredentialManager::Login(
    const std::string& username, const std::string& password) {
  if (username != username_ || password != password_) {
    return std::nullopt;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return IssueTokenPair();
}

bool ClientCredentialManager::ValidateAccessToken(
    const std::string& access_token) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = access_tokens_.find(access_token);
  if (it == access_tokens_.end()) {
    return false;
  }
  return it->second.access_expires_at > Now();
}

std::optional<ClientTokenPair> ClientCredentialManager::Refresh(
    const std::string& refresh_token) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto refresh_token_it = refresh_tokens_.find(refresh_token);
  if (refresh_token_it == refresh_tokens_.end()) {
    return std::nullopt;
  }
  const std::string access_token = refresh_token_it->second;
  const auto access_token_it = access_tokens_.find(access_token);
  if (access_token_it == access_tokens_.end()) {
    return std::nullopt;
  }
  const TokenRecord old_record = access_token_it->second;
  if (old_record.refresh_expires_at <= Now()) {
    // The refresh token has expired: revoke the pair and require a new login.
    Revoke(old_record);
    return std::nullopt;
  }
  // Rotate: the old pair is revoked and a fresh pair is issued.
  Revoke(old_record);
  return IssueTokenPair();
}

void ClientCredentialManager::Logout(const std::string& access_token) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = access_tokens_.find(access_token);
  if (it == access_tokens_.end()) {
    return;
  }
  Revoke(it->second);
}

ClientTokenPair ClientCredentialManager::IssueTokenPair() {
  const std::chrono::system_clock::time_point now = Now();
  TokenRecord record;
  record.access_token = NewAccessToken();
  record.refresh_token = NewRefreshToken();
  record.access_expires_at = now + kAccessTokenLifetime;
  record.refresh_expires_at = now + kRefreshTokenLifetime;

  ClientTokenPair pair;
  pair.access_token = record.access_token;
  pair.refresh_token = record.refresh_token;
  pair.access_token_lifetime = kAccessTokenLifetime;
  pair.refresh_token_lifetime = kRefreshTokenLifetime;

  access_tokens_[record.access_token] = record;
  refresh_tokens_[record.refresh_token] = record.access_token;
  return pair;
}

void ClientCredentialManager::Revoke(const TokenRecord& record) {
  access_tokens_.erase(record.access_token);
  refresh_tokens_.erase(record.refresh_token);
}

std::chrono::system_clock::time_point ClientCredentialManager::Now() const {
  if (clock_) {
    return clock_();
  }
  return std::chrono::system_clock::now();
}

}  // namespace server
}  // namespace jiaolong