#include <gtest/gtest.h>

#include <chrono>
#include <functional>

#include "server/client_credential_manager.h"

namespace jiaolong {
namespace server {
namespace {

class ClientCredentialManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    now_ = std::chrono::system_clock::now();
  }

  // Advances the test clock by the given duration.
  void Advance(std::chrono::seconds duration) {
    now_ += duration;
  }

  std::chrono::system_clock::time_point now_;
  ClientCredentialManager::Clock clock() {
    return [this]() { return now_; };
  }
};

TEST_F(ClientCredentialManagerTest, RejectsInvalidCredentials) {
  ClientCredentialManager manager("alice", "secret", clock());
  EXPECT_FALSE(manager.Login("alice", "wrong").has_value());
  EXPECT_FALSE(manager.Login("bob", "secret").has_value());
  EXPECT_FALSE(manager.Login("", "secret").has_value());
  EXPECT_FALSE(manager.Login("alice", "").has_value());
}

TEST_F(ClientCredentialManagerTest, LoginIssuesValidTokenPair) {
  ClientCredentialManager manager("alice", "secret", clock());
  const auto tokens = manager.Login("alice", "secret");
  ASSERT_TRUE(tokens.has_value());
  EXPECT_FALSE(tokens->access_token.empty());
  EXPECT_FALSE(tokens->refresh_token.empty());
  EXPECT_NE(tokens->access_token, tokens->refresh_token);
  EXPECT_EQ(tokens->access_token_lifetime.count(),
            ClientCredentialManager::kAccessTokenLifetime.count());
  EXPECT_EQ(tokens->refresh_token_lifetime.count(),
            ClientCredentialManager::kRefreshTokenLifetime.count());
  EXPECT_TRUE(manager.ValidateAccessToken(tokens->access_token));
  EXPECT_FALSE(manager.ValidateAccessToken("jl_access_bogus"));
}

TEST_F(ClientCredentialManagerTest, UnknownTokensAreRejected) {
  ClientCredentialManager manager("alice", "secret", clock());
  EXPECT_FALSE(manager.ValidateAccessToken("jl_access_unknown"));
  EXPECT_FALSE(manager.Refresh("jl_refresh_unknown").has_value());
}

TEST_F(ClientCredentialManagerTest, EachLoginIssuesDistinctTokens) {
  ClientCredentialManager manager("alice", "secret", clock());
  const auto first = manager.Login("alice", "secret");
  const auto second = manager.Login("alice", "secret");
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(first->access_token, second->access_token);
  EXPECT_NE(first->refresh_token, second->refresh_token);
}

TEST_F(ClientCredentialManagerTest, AccessTokenExpiresBeforeRefreshToken) {
  ClientCredentialManager manager("alice", "secret", clock());
  const auto tokens = manager.Login("alice", "secret");
  ASSERT_TRUE(tokens.has_value());

  // After the access-token lifetime the access token is invalid but the
  // refresh token still works (the credentials stay valid for one week).
  Advance(ClientCredentialManager::kAccessTokenLifetime + std::chrono::seconds(1));
  EXPECT_FALSE(manager.ValidateAccessToken(tokens->access_token));

  const auto refreshed = manager.Refresh(tokens->refresh_token);
  ASSERT_TRUE(refreshed.has_value());
  EXPECT_TRUE(manager.ValidateAccessToken(refreshed->access_token));
}

TEST_F(ClientCredentialManagerTest, RefreshRotatesTokensAndInvalidatesOldPair) {
  ClientCredentialManager manager("alice", "secret", clock());
  const auto tokens = manager.Login("alice", "secret");
  ASSERT_TRUE(tokens.has_value());

  const auto refreshed = manager.Refresh(tokens->refresh_token);
  ASSERT_TRUE(refreshed.has_value());
  EXPECT_NE(refreshed->access_token, tokens->access_token);
  EXPECT_NE(refreshed->refresh_token, tokens->refresh_token);
  // The old access token and refresh token are revoked.
  EXPECT_FALSE(manager.ValidateAccessToken(tokens->access_token));
  EXPECT_FALSE(manager.Refresh(tokens->refresh_token).has_value());
  EXPECT_TRUE(manager.ValidateAccessToken(refreshed->access_token));
}

TEST_F(ClientCredentialManagerTest, RefreshTokenExpiresAfterOneWeek) {
  ClientCredentialManager manager("alice", "secret", clock());
  const auto tokens = manager.Login("alice", "secret");
  ASSERT_TRUE(tokens.has_value());

  // The client credentials are valid for one week. After that the user must
  // log in again: neither token can be used.
  Advance(ClientCredentialManager::kRefreshTokenLifetime + std::chrono::seconds(1));
  EXPECT_FALSE(manager.ValidateAccessToken(tokens->access_token));
  EXPECT_FALSE(manager.Refresh(tokens->refresh_token).has_value());

  // A fresh login works again.
  const auto relogin = manager.Login("alice", "secret");
  ASSERT_TRUE(relogin.has_value());
  EXPECT_TRUE(manager.ValidateAccessToken(relogin->access_token));
}

TEST_F(ClientCredentialManagerTest, LogoutInvalidatesTokenPair) {
  ClientCredentialManager manager("alice", "secret", clock());
  const auto tokens = manager.Login("alice", "secret");
  ASSERT_TRUE(tokens.has_value());

  manager.Logout(tokens->access_token);
  EXPECT_FALSE(manager.ValidateAccessToken(tokens->access_token));
  EXPECT_FALSE(manager.Refresh(tokens->refresh_token).has_value());
}

}  // namespace
}  // namespace server
}  // namespace jiaolong