package ai.jiaolong.client.auth

import ai.jiaolong.client.platform.currentTimeMillis
import ai.jiaolong.client.storage.JiaolongClientStorage
import io.ktor.client.HttpClient
import io.ktor.client.call.body
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/**
 * Manages the client's OAuth 2.0 credentials for the Jiaolong Server REST API.
 *
 * The username and password configured on the server are only ever sent to the
 * server's login endpoint; the client never stores or modifies them. After a
 * successful login the access token and refresh token (together with their
 * expiry timestamps) are persisted in [JiaolongClientStorage] so the user
 * stays logged in across app restarts.
 *
 * Credentials are valid for one week (the server's refresh-token lifetime).
 * When the refresh token is missing or expired the user must log in again and
 * the app shows the login page. Access tokens are short-lived and are
 * refreshed automatically using the refresh token.
 */
class AuthController(
    private val httpClient: HttpClient,
    private val storage: JiaolongClientStorage,
) {
    companion object {
        const val KEY_ACCESS_TOKEN = "auth_access_token"
        const val KEY_REFRESH_TOKEN = "auth_refresh_token"
        const val KEY_ACCESS_TOKEN_EXPIRES_AT = "auth_access_token_expires_at"
        const val KEY_REFRESH_TOKEN_EXPIRES_AT = "auth_refresh_token_expires_at"

        /** Client credentials are valid for one week, matching the server's
         *  refresh-token lifetime. After that the user must log in again. */
        const val REFRESH_TOKEN_LIFETIME_MILLIS = 7L * 24L * 60L * 60L * 1000L
    }

    /**
     * True when a refresh token is stored and has not expired yet. The user is
     * considered logged in and can use or refresh the access token.
     */
    fun hasValidCredentials(): Boolean {
        val refreshToken = storage.get(KEY_REFRESH_TOKEN) ?: return false
        if (refreshToken.isBlank()) return false
        val expiresAt = storage.get(KEY_REFRESH_TOKEN_EXPIRES_AT)?.toLongOrNull()
            ?: return false
        return expiresAt > currentTimeMillis()
    }

    /** Returns the stored access token if it has not expired, or null. */
    fun accessTokenOrNull(): String? {
        val token = storage.get(KEY_ACCESS_TOKEN) ?: return null
        if (token.isBlank()) return null
        val expiresAt = storage.get(KEY_ACCESS_TOKEN_EXPIRES_AT)?.toLongOrNull()
            ?: return null
        return if (expiresAt > currentTimeMillis()) token else null
    }

    /**
     * Returns a valid access token, refreshing it with the refresh token when
     * the stored access token is missing or expired. Returns null when the
     * credentials are not valid (the user must log in again).
     */
    suspend fun ensureAccessToken(): String? {
        accessTokenOrNull()?.let { return it }
        if (!hasValidCredentials()) return null
        val refreshToken = storage.get(KEY_REFRESH_TOKEN) ?: return null
        return try {
            val response = httpClient.post("auth/refresh") {
                setBody(RefreshTokenRequest(refreshToken = refreshToken))
            }.body<AuthTokenResponse>()
            saveTokens(response)
            response.accessToken
        } catch (e: Exception) {
            null
        }
    }

    /**
     * Logs in with the given username/password against POST /api/auth/login.
     * On success stores the returned token pair and returns true; returns
     * false on invalid credentials or network failure.
     */
    suspend fun login(username: String, password: String): Boolean {
        return try {
            val response = httpClient.post("auth/login") {
                setBody(LoginRequest(username = username, password = password))
            }.body<AuthTokenResponse>()
            saveTokens(response)
            true
        } catch (e: Exception) {
            false
        }
    }

    /** Clears all stored credentials so the user must log in again. */
    fun logout() {
        storage.set(KEY_ACCESS_TOKEN, "")
        storage.set(KEY_REFRESH_TOKEN, "")
        storage.set(KEY_ACCESS_TOKEN_EXPIRES_AT, "")
        storage.set(KEY_REFRESH_TOKEN_EXPIRES_AT, "")
    }

    private fun saveTokens(response: AuthTokenResponse) {
        val now = currentTimeMillis()
        val accessLifetimeMillis =
            if (response.expiresIn > 0) response.expiresIn * 1000L else 0L
        // Credentials are valid for one week. When the server does not report a
        // refresh-token lifetime, fall back to the one-week constant.
        val refreshLifetimeMillis = if (response.refreshExpiresIn > 0) {
            response.refreshExpiresIn * 1000L
        } else {
            REFRESH_TOKEN_LIFETIME_MILLIS
        }
        storage.set(KEY_ACCESS_TOKEN, response.accessToken)
        storage.set(KEY_REFRESH_TOKEN, response.refreshToken)
        storage.set(
            KEY_ACCESS_TOKEN_EXPIRES_AT,
            (now + accessLifetimeMillis).toString(),
        )
        storage.set(
            KEY_REFRESH_TOKEN_EXPIRES_AT,
            (now + refreshLifetimeMillis).toString(),
        )
    }

    @Serializable
    private data class LoginRequest(
        val username: String,
        val password: String,
    )

    @Serializable
    private data class RefreshTokenRequest(
        @SerialName("refreshToken") val refreshToken: String,
    )

    @Serializable
    private data class AuthTokenResponse(
        @SerialName("accessToken") val accessToken: String,
        @SerialName("refreshToken") val refreshToken: String,
        @SerialName("tokenType") val tokenType: String = "Bearer",
        @SerialName("expiresIn") val expiresIn: Long = 0,
        @SerialName("refreshExpiresIn") val refreshExpiresIn: Long = 0,
    )
}

/**
 * Returns the currently stored, non-expired access token from local storage.
 *
 * Used as the token provider when building the shared HTTP client (the client
 * is created before the [AuthController], so it reads the token directly from
 * storage instead of depending on the controller instance).
 */
internal fun storedAccessToken(storage: JiaolongClientStorage): String? {
    val token = storage.get(AuthController.KEY_ACCESS_TOKEN) ?: return null
    if (token.isBlank()) return null
    val expiresAt = storage.get(AuthController.KEY_ACCESS_TOKEN_EXPIRES_AT)
        ?.toLongOrNull() ?: return null
    return if (expiresAt > currentTimeMillis()) token else null
}