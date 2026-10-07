package ai.jiaolong.client.util

import io.ktor.client.HttpClient
import io.ktor.client.HttpClientConfig
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.HttpTimeout
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.client.plugins.defaultRequest
import io.ktor.client.request.header
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.contentType
import io.ktor.serialization.kotlinx.json.json
import kotlinx.serialization.json.Json

/** Shared JSON serializer used by Ktor and the remote controllers. */
internal val defaultJson = Json {
    ignoreUnknownKeys = true
    prettyPrint = false
}

/**
 * Creates the Ktor [HttpClient] used by the Jiaolong client.
 *
 * @param baseUrl Base URL of the Jiaolong Server REST API. The caller always
 *   provides the value read from local storage; there is no hardcoded fallback
 *   in the request path.
 * @param accessTokenProvider Optional provider of the current OAuth 2.0 access
 *   token. When it returns a non-empty token every request automatically
 *   carries the `Authorization: Bearer <token>` header. The provider is
 *   evaluated for each request, so tokens refreshed while the app is running
 *   are picked up automatically.
 */
fun createHttpClient(
    baseUrl: String,
    accessTokenProvider: () -> String? = { null },
): HttpClient = HttpClient(CIO) {
    install(ContentNegotiation) {
        json(defaultJson)
    }
    // VCS operations (e.g. initializing a task branch) can take a while
    // because the server runs git/jj commands (fetch, rebase, push) before
    // returning. Use a longer timeout than Ktor's default (15 seconds) so the
    // client does not report "Request timeout has expired" while the server is
    // still working. The socket timeout is raised as well so the request is
    // not aborted when the server does not send any data during the operation.
    install(HttpTimeout) {
        requestTimeoutMillis = 300_000
        socketTimeoutMillis = 300_000
    }
    defaultRequest {
        url(baseUrl)
        contentType(ContentType.Application.Json)
        accessTokenProvider()?.takeIf { it.isNotBlank() }?.let { token ->
            header(HttpHeaders.Authorization, "Bearer $token")
        }
    }
}