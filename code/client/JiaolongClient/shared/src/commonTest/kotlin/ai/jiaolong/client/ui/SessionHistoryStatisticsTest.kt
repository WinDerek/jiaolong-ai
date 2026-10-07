package ai.jiaolong.client.ui

import ai.jiaolong.client.model.SessionHistory
import ai.jiaolong.client.model.SessionMessage
import ai.jiaolong.client.model.SessionTurn
import kotlin.test.Test
import kotlin.test.assertEquals

class SessionHistoryStatisticsTest {
    @Test
    fun countsMessagesAndLlmCalls() {
        val history = SessionHistory(
            agentTurns = listOf(
                SessionTurn(type = "message", message = SessionMessage(role = "system")),
                SessionTurn(type = "message", message = SessionMessage(role = "user")),
                SessionTurn(type = "message", message = SessionMessage(role = "assistant")),
                SessionTurn(type = "message", message = SessionMessage(role = "tool")),
                SessionTurn(type = "message", message = SessionMessage(role = "assistant")),
                SessionTurn(type = "slash_command", command = "/help"),
            ),
        )

        val statistics = computeSessionHistoryStatistics(history)

        assertEquals(5, statistics.messageCount)
        assertEquals(2, statistics.llmCallCount)
    }

    @Test
    fun emptyHistoryHasZeroStatistics() {
        val statistics = computeSessionHistoryStatistics(SessionHistory())

        assertEquals(0, statistics.messageCount)
        assertEquals(0, statistics.llmCallCount)
    }
}