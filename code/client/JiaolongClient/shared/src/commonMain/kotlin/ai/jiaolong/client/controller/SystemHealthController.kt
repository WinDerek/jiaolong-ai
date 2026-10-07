package ai.jiaolong.client.controller

import ai.jiaolong.client.model.SystemHealthFrame
import io.ktor.client.HttpClient
import io.ktor.client.call.body
import io.ktor.client.request.get
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/**
 * Server-monitor related business logic for the Jiaolong client. Talks to the
 * Jiaolong Server REST API through a Ktor [HttpClient] to read the server's
 * system health data (currently the CPU temperature).
 *
 * @param httpClient Ktor HTTP client used to reach the server.
 */
class SystemHealthController(
    private val httpClient: HttpClient,
) {
    /**
     * GET /api/server/system-health - loads the latest server system health
     * data. Returns up to the last [MAX_DATA_FRAMES] data frames (oldest first)
     * gathered by the server's system health perception loop.
     */
    suspend fun getSystemHealthData(): List<SystemHealthFrame> {
        return httpClient.get("server/system-health")
            .body<SystemHealthDataResponse>()
            .frames
            .map { it.toFrame() }
    }

    @Serializable
    private data class SystemHealthDataResponse(
        val frames: List<SystemHealthFrameResponse> = emptyList(),
    )

    @Serializable
    private data class SystemHealthFrameResponse(
        val timestamp: Long = 0,
        @SerialName("cpuTemperatureCelsius") val cpuTemperatureCelsius: Double = 0.0,
    )

    private fun SystemHealthFrameResponse.toFrame(): SystemHealthFrame =
        SystemHealthFrame(
            timestamp = timestamp,
            cpuTemperatureCelsius = cpuTemperatureCelsius,
        )
}