package ai.jiaolong.client.ui

import ai.jiaolong.client.model.SessionHistory
import ai.jiaolong.client.model.SessionMessage
import ai.jiaolong.client.model.SessionTurn
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ExpandLess
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.FilterList
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive

/**
 * Task session history page: shows the session history JSON data returned by
 * the Jiaolong Server `GET /api/tasks/{id}/session-history` endpoint. The
 * content can be long, so it is rendered inside a vertically scrollable
 * column: a session information card and a session statistics card at the top,
 * followed by one card per agent turn (messages with their role, content,
 * reasoning and tool calls, and slash commands). The statistics card shows the
 * number of messages and the number of LLM calls (assistant messages). When the
 * content is taller than the available screen height, a vertical scrollbar is
 * shown on the right edge so that the user can drag it to scroll.
 *
 * A toggle button lets the user show only tool calls: when enabled, only
 * assistant and tool turns are listed, the assistant's content and reasoning
 * are hidden, and only the tool calls and the tool results remain.
 *
 * While the request is in flight a loading indicator is shown; when the
 * request fails and no data is available yet, an error banner and a retry
 * button are shown.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SessionHistoryPage(
    taskId: String,
    sessionHistory: SessionHistory?,
    isLoading: Boolean,
    errorMessage: String?,
    onBack: () -> Unit,
    onRetry: () -> Unit,
) {
    val scrollState = rememberScrollState()
    val coroutineScope = rememberCoroutineScope()
    // When enabled, only assistant and tool turns are shown, with the
    // assistant's content and reasoning hidden so that only tool calls and
    // tool results remain.
    var showOnlyToolCalls by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Session History") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = "Go back",
                        )
                    }
                },
            )
        },
    ) { innerPadding ->
        when {
            sessionHistory != null -> {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(innerPadding),
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .verticalScroll(scrollState)
                            .padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(16.dp),
                    ) {
                        if (errorMessage != null) {
                            ErrorBanner(message = errorMessage, onDismiss = {})
                        }

                        // Placed at the top of the history so the newest entries
                        // can be reached with a single tap.
                        ScrollToButton(
                            label = "Scroll to bottom",
                            icon = Icons.Filled.KeyboardArrowDown,
                            onClick = {
                                coroutineScope.launch {
                                    scrollState.animateScrollTo(scrollState.maxValue)
                                }
                            },
                        )

                        SessionInfoSection(taskId = taskId, sessionHistory = sessionHistory)

                        SessionStatisticsSection(sessionHistory = sessionHistory)

                        HorizontalDivider()

                        // Toggle button that switches between showing all agent
                        // turns and showing only tool calls (assistant tool calls
                        // and tool results).
                        FilterChip(
                            selected = showOnlyToolCalls,
                            onClick = { showOnlyToolCalls = !showOnlyToolCalls },
                            label = { Text("Only tool calls") },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Filled.FilterList,
                                    contentDescription = null,
                                    modifier = Modifier.size(18.dp),
                                )
                            },
                        )

                        // In the "only tool calls" mode only assistant and tool
                        // turns are kept; all other turns are hidden.
                        val visibleTurns = if (showOnlyToolCalls) {
                            sessionHistory.agentTurns.filter { turn ->
                                val role = turn.message?.role
                                role == "assistant" || role == "tool"
                            }
                        } else {
                            sessionHistory.agentTurns
                        }

                        Text(
                            text = if (showOnlyToolCalls) {
                                "Tool calls (${visibleTurns.size})"
                            } else {
                                "Agent turns (${visibleTurns.size})"
                            },
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold,
                        )
                        if (visibleTurns.isEmpty()) {
                            Text(
                                text = if (showOnlyToolCalls) {
                                    "No tool calls in this session."
                                } else {
                                    "No session history data yet."
                                },
                                style = MaterialTheme.typography.bodyMedium,
                            )
                        }
                        visibleTurns.forEach { turn ->
                            SessionTurnCard(
                                turn = turn,
                                showOnlyToolCalls = showOnlyToolCalls,
                            )
                        }

                        // Placed at the bottom of the history so the beginning can
                        // be reached with a single tap.
                        ScrollToButton(
                            label = "Scroll to top",
                            icon = Icons.Filled.KeyboardArrowUp,
                            onClick = {
                                coroutineScope.launch {
                                    scrollState.animateScrollTo(0)
                                }
                            },
                        )
                    }

                    // Only show the scrollbar when the content is taller than
                    // the available height, so the user can drag it to scroll
                    // through the session history.
                    if (scrollState.maxValue > 0) {
                        SessionHistoryScrollbar(
                            scrollState = scrollState,
                            modifier = Modifier
                                .align(Alignment.CenterEnd)
                                .fillMaxHeight(),
                        )
                    }
                }
            }

            errorMessage != null -> {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(innerPadding)
                        .padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    ErrorBanner(message = errorMessage, onDismiss = {})
                    Button(onClick = onRetry) {
                        Text("Retry")
                    }
                }
            }

            else -> {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(innerPadding),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center,
                ) {
                    CircularProgressIndicator()
                    Text(
                        text = "Loading session history...",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 12.dp),
                    )
                }
            }
        }
    }
}

/**
 * A full width button which animates the session history scroll position to a
 * target. The "Scroll to bottom" button is placed at the top of the history and
 * the "Scroll to top" button is placed at the bottom.
 */
@Composable
private fun ScrollToButton(
    label: String,
    icon: ImageVector,
    onClick: () -> Unit,
) {
    OutlinedButton(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            modifier = Modifier.size(18.dp),
        )
        Spacer(modifier = Modifier.width(8.dp))
        Text(label)
    }
}

/** Session information card: task id, session id, file location and creation
 *  timestamp, each on its own row. */
@Composable
private fun SessionInfoSection(
    taskId: String,
    sessionHistory: SessionHistory,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                text = "Session information",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
            )
            SessionInfoRow(
                label = "Task ID",
                value = sessionHistory.taskId ?: taskId,
            )
            HorizontalDivider()
            SessionInfoRow(
                label = "Session ID",
                value = sessionHistory.sessionId ?: "Unknown",
            )
            HorizontalDivider()
            SessionInfoRow(
                label = "Creation timestamp",
                value = sessionHistory.creationTimestamp ?: "Unknown",
            )
            HorizontalDivider()
            SessionInfoRow(
                label = "File location",
                value = sessionHistory.fileLocation ?: "Unknown",
            )
        }
    }
}

/** Session statistics card: number of messages and number of LLM calls. */
@Composable
private fun SessionStatisticsSection(sessionHistory: SessionHistory) {
    val statistics = computeSessionHistoryStatistics(sessionHistory)
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                text = "Statistics",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
            )
            SessionInfoRow(
                label = "Messages",
                value = statistics.messageCount.toString(),
            )
            HorizontalDivider()
            SessionInfoRow(
                label = "LLM calls (assistant messages)",
                value = statistics.llmCallCount.toString(),
            )
        }
    }
}

/** Aggregated statistics of a session history. */
internal data class SessionHistoryStatistics(
    /** Total number of messages in the session history. */
    val messageCount: Int,
    /** Number of LLM calls, i.e. assistant messages. */
    val llmCallCount: Int,
)

/**
 * Computes the session history statistics shown on the session history page:
 * the total number of messages and the number of LLM calls (assistant
 * messages). Slash command turns are not messages and are therefore excluded
 * from [SessionHistoryStatistics.messageCount].
 */
internal fun computeSessionHistoryStatistics(
    sessionHistory: SessionHistory,
): SessionHistoryStatistics {
    val messages = sessionHistory.agentTurns.mapNotNull { turn -> turn.message }
    return SessionHistoryStatistics(
        messageCount = messages.size,
        llmCallCount = messages.count { message -> message.role == "assistant" },
    )
}

/** A single labeled row in the session information section. */
@Composable
private fun SessionInfoRow(
    label: String,
    value: String,
) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelLarge,
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
        )
    }
}

/** Renders a single agent turn as a message card or a slash command card. */
@Composable
private fun SessionTurnCard(turn: SessionTurn, showOnlyToolCalls: Boolean) {
    val message = turn.message
    when {
        message != null -> SessionMessageCard(
            message = message,
            showOnlyToolCalls = showOnlyToolCalls,
        )
        turn.type == "slash_command" || turn.command != null ->
            SlashCommandCard(command = turn.command.orEmpty())
        else -> Card(modifier = Modifier.fillMaxWidth()) {
            Text(
                text = "Unknown turn",
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(12.dp),
            )
        }
    }
}

/**
 * A message card showing the role, content, reasoning and tool calls. When
 * [showOnlyToolCalls] is enabled the assistant's content and reasoning are
 * hidden so that a tool call message only shows its tool calls, while a tool
 * message shows its result.
 */
@Composable
private fun SessionMessageCard(message: SessionMessage, showOnlyToolCalls: Boolean) {
    val role = message.role ?: "unknown"
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Surface(
                shape = MaterialTheme.shapes.small,
                color = roleColor(role),
            ) {
                Text(
                    text = role,
                    style = MaterialTheme.typography.labelMedium,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                )
            }
            // In the "only tool calls" mode the assistant content is hidden;
            // tool results (role "tool") are still shown.
            if (!showOnlyToolCalls || role != "assistant") {
                renderJsonAsText(message.content)?.let { content ->
                    if (role == "tool") {
                        CollapsibleToolOutput(
                            text = content,
                            textStyle = MaterialTheme.typography.bodyMedium,
                        )
                    } else {
                        Text(
                            text = content,
                            style = MaterialTheme.typography.bodyMedium,
                        )
                    }
                }
            }
            // Reasoning is hidden in the "only tool calls" mode.
            if (!showOnlyToolCalls) {
                renderJsonAsText(message.reasoningContent)?.let { reasoning ->
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Text(
                            text = "reasoning",
                            style = MaterialTheme.typography.labelSmall,
                            fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.tertiary,
                        )
                        Text(
                            text = reasoning,
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }
            }
            message.toolCallId?.let { toolCallId ->
                Text(
                    text = "tool_call_id: $toolCallId",
                    style = MaterialTheme.typography.labelSmall,
                    fontFamily = FontFamily.Monospace,
                )
            }
            if (!message.toolCalls.isNullOrEmpty()) {
                message.toolCalls.forEachIndexed { index, toolCall ->
                    ToolCallCard(index = index, element = toolCall)
                }
            }
        }
    }
}

/** A slash command card showing the command text. */
@Composable
private fun SlashCommandCard(command: String) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(
                text = "slash command",
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.tertiary,
            )
            Text(
                text = command.ifBlank { "/" },
                style = MaterialTheme.typography.bodyMedium,
                fontFamily = FontFamily.Monospace,
            )
        }
    }
}

/** A single tool call inside a message: function name and its arguments. */
@Composable
private fun ToolCallCard(index: Int, element: JsonElement) {
    val obj = element as? JsonObject
    val function = obj?.get("function") as? JsonObject
    val name = (function?.get("name") as? JsonPrimitive)?.content ?: "tool call"
    val arguments = function?.get("arguments")
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(
            modifier = Modifier.padding(10.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(
                text = "tool call ${index + 1}: $name",
                style = MaterialTheme.typography.labelMedium,
                fontWeight = FontWeight.SemiBold,
            )
            arguments?.let { args ->
                val argsText = if (args is JsonPrimitive) args.content else args.toString()
                if (argsText.isNotBlank()) {
                    CollapsibleToolOutput(
                        text = argsText,
                        textStyle = MaterialTheme.typography.bodySmall,
                        fontFamily = FontFamily.Monospace,
                    )
                }
            }
        }
    }
}

/** Maximum number of content lines shown while a tool output is collapsed. */
private const val COLLAPSED_TOOL_OUTPUT_LINES = 3

/**
 * Collapsible text used for potentially very long tool output (a tool message
 * content or a tool call's arguments). At most [COLLAPSED_TOOL_OUTPUT_LINES]
 * lines are shown while collapsed, with a trailing ellipsis. When the text does
 * not fit within those lines, clicking on it toggles between the collapsed and
 * the expanded state, and an expand/collapse icon reflects the current state.
 */
@Composable
private fun CollapsibleToolOutput(
    text: String,
    textStyle: TextStyle,
    modifier: Modifier = Modifier,
    fontFamily: FontFamily? = null,
) {
    var expanded by remember(text) { mutableStateOf(false) }
    var hasOverflow by remember(text) { mutableStateOf(false) }

    Column(
        modifier = modifier
            .fillMaxWidth()
            .clickable(enabled = hasOverflow) { expanded = !expanded },
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            text = text,
            style = textStyle,
            fontFamily = fontFamily,
            maxLines = if (expanded) Int.MAX_VALUE else COLLAPSED_TOOL_OUTPUT_LINES,
            overflow = TextOverflow.Ellipsis,
            onTextLayout = { result ->
                // While expanded the text is never truncated, so only the
                // collapsed state decides whether the expand/collapse affordance
                // is needed.
                if (!expanded) {
                    hasOverflow = result.hasVisualOverflow
                }
            },
        )
        if (hasOverflow) {
            Icon(
                imageVector = if (expanded) {
                    Icons.Filled.ExpandLess
                } else {
                    Icons.Filled.ExpandMore
                },
                contentDescription = if (expanded) {
                    "Collapse tool output"
                } else {
                    "Expand tool output"
                },
                modifier = Modifier
                    .align(Alignment.End)
                    .size(20.dp),
            )
        }
    }
}

/** Role badge container color used in message cards. */
@Composable
private fun roleColor(role: String): androidx.compose.ui.graphics.Color = when (role) {
    "user" -> MaterialTheme.colorScheme.primaryContainer
    "assistant" -> MaterialTheme.colorScheme.tertiaryContainer
    "tool" -> MaterialTheme.colorScheme.secondaryContainer
    "system" -> MaterialTheme.colorScheme.surfaceVariant
    else -> MaterialTheme.colorScheme.surfaceVariant
}

/**
 * Converts a JSON message field (content / reasoning content) into a plain
 * string for display. Plain strings are returned as-is; content part arrays
 * (OpenAI style) are joined line by line using their `text` parts; any other
 * JSON value is rendered as its compact JSON representation. Returns null for
 * missing or null values.
 */
private fun renderJsonAsText(element: JsonElement?): String? {
    if (element == null || element is JsonNull) return null
    if (element is JsonPrimitive) return element.content
    if (element is JsonArray) {
        val rendered = element.mapNotNull { part ->
            val partObject = part as? JsonObject
            val type = (partObject?.get("type") as? JsonPrimitive)?.content
            val text = (partObject?.get("text") as? JsonPrimitive)?.content
            if (partObject != null && type == "text" && text != null) {
                text
            } else {
                part.toString()
            }
        }
        return rendered.joinToString("\n").ifEmpty { element.toString() }
    }
    return element.toString()
}