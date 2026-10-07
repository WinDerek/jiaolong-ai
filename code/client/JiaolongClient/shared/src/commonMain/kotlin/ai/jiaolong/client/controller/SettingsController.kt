package ai.jiaolong.client.controller

import ai.jiaolong.client.model.LlmProvider
import io.ktor.client.HttpClient
import io.ktor.client.call.body
import io.ktor.client.request.get
import io.ktor.client.request.post
import io.ktor.client.request.put
import io.ktor.client.request.setBody
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/**
 * Settings-related business logic for the Jiaolong client. Talks to the
 * Jiaolong Server REST API through a Ktor [HttpClient] to read and update the
 * server-side settings (currently the enabled LLM provider).
 *
 * @param httpClient Ktor HTTP client used to reach the server.
 */
class SettingsController(
    private val httpClient: HttpClient,
) {
    /**
     * GET /api/settings/llm-providers - loads the list of LLM providers and
     * the index of the currently enabled one.
     */
    suspend fun listLlmProviders(): LlmProvidersData {
        val response = httpClient.get("settings/llm-providers")
            .body<LlmProvidersResponse>()
        return response.toData()
    }

    /**
     * PUT /api/settings/llm-providers - sets the enabled LLM provider by its
     * index in the `llmProviders` array and returns the updated list.
     */
    suspend fun setEnabledLlmProvider(index: Int): LlmProvidersData {
        val response = httpClient.put("settings/llm-providers") {
            setBody(SetEnabledLlmProviderRequest(enabledLlmProviderIndex = index))
        }.body<LlmProvidersResponse>()
        return response.toData()
    }

    /**
     * POST /api/server/stop - gracefully stops the Jiaolong Server. The server
     * acknowledges the request before shutting down, so a successful response
     * means the server is stopping.
     */
    suspend fun stopServer() {
        httpClient.post("server/stop")
    }

    /** The LLM provider list together with the index of the enabled provider. */
    data class LlmProvidersData(
        val enabledLlmProviderIndex: Int?,
        val llmProviders: List<LlmProvider>,
    )

    @Serializable
    private data class SetEnabledLlmProviderRequest(
        @SerialName("enabledLlmProviderIndex") val enabledLlmProviderIndex: Int,
    )

    @Serializable
    private data class LlmProvidersResponse(
        @SerialName("enabledLlmProviderIndex") val enabledLlmProviderIndex: Int? = null,
        @SerialName("llmProviders") val llmProviders: List<LlmProviderResponse> = emptyList(),
    )

    @Serializable
    private data class LlmProviderResponse(
        val enabled: Boolean = false,
        @SerialName("baseUrl") val baseUrl: String = "",
        val model: String = "",
        @SerialName("llmCooldownDuration") val llmCooldownDuration: Int = 0,
        @SerialName("retryTimes") val retryTimes: Int = 0,
    )

    private fun LlmProvidersResponse.toData(): LlmProvidersData = LlmProvidersData(
        enabledLlmProviderIndex = enabledLlmProviderIndex?.takeIf { it >= 0 },
        llmProviders = llmProviders.mapIndexed { index, provider ->
            LlmProvider(
                index = index,
                enabled = provider.enabled,
                baseUrl = provider.baseUrl,
                model = provider.model,
                llmCooldownDuration = provider.llmCooldownDuration,
                retryTimes = provider.retryTimes,
            )
        },
    )
}