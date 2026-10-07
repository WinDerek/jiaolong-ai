#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace jiaolong {
namespace server {

// Represents the OAuth 2.0 token pair issued to a client after a successful
// username/password login. The access token is short-lived and is sent on
// every API call as `Authorization: Bearer <accessToken>`; the refresh token
// is long-lived (one week) and is exchanged for a fresh pair when the access
// token expires.
struct ClientTokenPair {
  std::string access_token;
  std::string refresh_token;
  // Seconds from issuance until each token expires.
  std::chrono::seconds access_token_lifetime;
  std::chrono::seconds refresh_token_lifetime;
};

// Manages client credentials for the Jiaolong Server.
//
// The username and password are configured by the operator in the settings
// file (`~/.jiaolong/settings.json`, keys `username` and `password`) and are
// passed in at construction time; clients can never read or modify them.
//
// The manager implements the OAuth 2.0 resource-owner-password flow: a
// successful login issues an access token and a refresh token. All tokens are
// kept only in memory (never persisted), so a server restart invalidates every
// issued token and every client must log in again. Access tokens expire after
// one hour; refresh tokens expire after one week, after which the user must
// log in again. Refresh tokens are rotated on every refresh so a used refresh
// token cannot be replayed.
class ClientCredentialManager {
 public:
  // Clock used to evaluate token expiration; injectable for tests. Defaults to
  // the system wall clock.
  using Clock = std::function<std::chrono::system_clock::time_point()>;

  // Access-token lifetime: 1 day.
  static constexpr std::chrono::seconds kAccessTokenLifetime{24 * 60 * 60};
  // Refresh-token lifetime: 7 days (one week). After this the user must log in
  // again.
  static constexpr std::chrono::seconds kRefreshTokenLifetime{7 * 24 * 60 * 60};

  explicit ClientCredentialManager(std::string username, std::string password,
      Clock clock = std::chrono::system_clock::now);

  // Validates the given username/password against the configured credentials.
  // On success issues and stores a new token pair in memory and returns it;
  // returns std::nullopt when the credentials are invalid.
  std::optional<ClientTokenPair> Login(const std::string& username,
                                       const std::string& password);

  // Returns true when `access_token` is currently issued (in memory) and has
  // not expired.
  bool ValidateAccessToken(const std::string& access_token) const;

  // Exchanges a valid refresh token for a fresh token pair. The old pair is
  // revoked (refresh-token rotation). Returns std::nullopt when the refresh
  // token is unknown or already expired.
  std::optional<ClientTokenPair> Refresh(const std::string& refresh_token);

  // Revokes the token pair associated with the given access token. No-op when
  // the access token is unknown.
  void Logout(const std::string& access_token);

 private:
  struct TokenRecord {
    std::string access_token;
    std::string refresh_token;
    std::chrono::system_clock::time_point access_expires_at;
    std::chrono::system_clock::time_point refresh_expires_at;
  };

  // Issues and stores a new token pair. Must be called with mutex_ held.
  ClientTokenPair IssueTokenPair();

  // Removes a token record from all maps. Must be called with mutex_ held.
  void Revoke(const TokenRecord& record);

  std::chrono::system_clock::time_point Now() const;

  const std::string username_;
  const std::string password_;
  const Clock clock_;
  mutable std::mutex mutex_;
  // access token -> token record.
  std::map<std::string, TokenRecord> access_tokens_;
  // refresh token -> access token (for O(1) refresh lookups).
  std::map<std::string, std::string> refresh_tokens_;
};

}  // namespace server
}  // namespace jiaolong