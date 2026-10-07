package ai.jiaolong.client.ui

import ai.jiaolong.client.model.Task
import ai.jiaolong.client.util.formatDuration
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.History
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.RestartAlt

/**
 * Result of an operation triggered from the task detail page (e.g. init branch
 * & start working). [message] is the text shown to the user
 * and [isError] marks whether the operation failed, which controls how the
 * result is displayed. [durationMillis], when set, is the wall-clock time the
 * operation took and is shown below the message.
 */
data class OperationResult(
    val message: String,
    val isError: Boolean,
    val durationMillis: Long? = null,
)

/**
 * Task detail page: title, description, task information (task id, status,
 * working directory, token limit, token usage, each on its own row; status,
 * working directory and token limit are editable through the edit dialog),
 * action buttons (edit, init branch & start working, resume the task when the
 * task is failed, view session history, confirm completion when the task is in
 * needs_review, delete).
 *
 * The init branch & start working button chains the original init branch and
 * start working requests and fails fast: the agent is not started when init
 * branch fails. It shows a pending state while the requests are in flight. The
 * init branch & start working button displays the server result (success or
 * failure) below the button once the response has been received; the result
 * also reports the duration measured from the click to the moment the agent
 * started.
 *
 * The page renders the latest task detail loaded from the server. While that
 * reload is in flight a loading animation is shown at the top of the page, and
 * when it fails a persistent (non-dismissable) [LoadErrorBanner] is shown above
 * the possibly stale task data.
 *
 * Besides the automatic reload when the page is entered, the refresh button in
 * the top app bar asks the server for the latest task detail again on demand.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TaskDetailPage(
    task: Task,
    errorMessage: String?,
    onDismissError: () -> Unit,
    onBack: () -> Unit,
    onRefresh: () -> Unit,
    onEditTask: (String, String, String, Int, String) -> Unit,
    onInitBranchAndStartWork: () -> Unit,
    onResumeTask: () -> Unit,
    onViewSessionHistory: () -> Unit,
    onConfirmCompletion: () -> Unit,
    onDeleteTask: () -> Unit,
    isInitBranchAndStartWorkPending: Boolean = false,
    initBranchAndStartWorkResult: OperationResult? = null,
    isLoading: Boolean = false,
    loadErrorMessage: String? = null,
) {
    var showEditDialog by remember { mutableStateOf(false) }
    var showDeleteDialog by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(task.title) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = "Go back",
                        )
                    }
                },
                actions = {
                    // Requests the latest task detail from the server. Disabled
                    // while a reload is already in flight.
                    IconButton(onClick = onRefresh, enabled = !isLoading) {
                        Icon(
                            imageVector = Icons.Default.Refresh,
                            contentDescription = "Refresh task detail",
                        )
                    }
                },
            )
        },
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            if (isLoading) {
                // The latest task detail is being reloaded from the server, so
                // the data currently shown may be outdated. A loading animation
                // at the top of the page makes that visible to the user.
                LinearProgressIndicator(
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            if (loadErrorMessage != null) {
                // Persistent (non-dismissable) banner shown when the latest
                // task detail load failed, so the user knows the data may be
                // stale. This is separate from the dismissable [errorMessage]
                // banner used for operation failures.
                LoadErrorBanner(message = loadErrorMessage)
            }
            if (errorMessage != null) {
                ErrorBanner(
                    message = errorMessage,
                    onDismiss = onDismissError,
                )
            }

            // Title and description.
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = task.description,
                    style = MaterialTheme.typography.bodyLarge,
                )
            }

            HorizontalDivider()

            // Task information: task id, task status, working directory,
            // token limit and token usage, each displayed on its own row.
            // Status, working directory and token limit can be edited by the
            // client through the edit dialog.
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = "Task information",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                )
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(
                        modifier = Modifier.padding(12.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        TaskInfoRow(
                            label = "Task ID",
                            value = task.id,
                        )
                        HorizontalDivider()
                        TaskInfoRow(
                            label = "Status",
                            value = formatTaskState(task.state),
                        )
                        HorizontalDivider()
                        TaskInfoRow(
                            label = "Working directory",
                            value = task.workingDirectory,
                        )
                        HorizontalDivider()
                        TaskInfoRow(
                            label = "Token limit",
                            value = task.tokenLimit.toString(),
                        )
                        HorizontalDivider()
                        TaskInfoRow(
                            label = "Token usage",
                            value = task.totalTokenUsage.toString(),
                        )
                        // The failure reason is only meaningful (and only
                        // shown) while the task is in the failed state.
                        if (task.state == "failed" && task.failureReason.isNotBlank()) {
                            HorizontalDivider()
                            TaskInfoRow(
                                label = "Failure reason",
                                value = formatFailureReason(task.failureReason),
                            )
                        }
                    }
                }
            }

            HorizontalDivider()

            // Task actions.
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { showEditDialog = true }) {
                    Icon(
                        imageVector = Icons.Default.Edit,
                        contentDescription = "Edit task",
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Edit task")
                }
                // Initializes the task branch and, only when that succeeded,
                // lets the agent start working on the task. The caller chains
                // the two requests and fails fast, and the button shows the
                // combined outcome (including the duration until the agent
                // started) below it.
                Button(
                    enabled = !task.agentWorking && !isInitBranchAndStartWorkPending,
                    onClick = onInitBranchAndStartWork,
                ) {
                    if (isInitBranchAndStartWorkPending) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(16.dp),
                            strokeWidth = 2.dp,
                            color = LocalContentColor.current,
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Init branch & start working...")
                    } else if (task.agentWorking) {
                        Text("Agent is working...")
                    } else {
                        Icon(
                            imageVector = Icons.Default.PlayArrow,
                            contentDescription = "Init branch & start working",
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Init branch & start working")
                    }
                }
                initBranchAndStartWorkResult?.let { result ->
                    OperationResultText(result = result)
                }
                // A failed task can be resumed: the agent picks up its previous
                // work from the persisted session history instead of starting
                // a fresh round.
                if (task.state == "failed") {
                    Button(
                        enabled = !task.agentWorking,
                        onClick = onResumeTask,
                    ) {
                        Icon(
                            imageVector = Icons.Default.RestartAlt,
                            contentDescription = "Resume task",
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Resume task")
                    }
                }
                Button(onClick = onViewSessionHistory) {
                    Icon(
                        imageVector = Icons.Default.History,
                        contentDescription = "View session history",
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("View Session History")
                }
                if (task.state == "needs_review") {
                    Button(onClick = onConfirmCompletion) {
                        Icon(
                            imageVector = Icons.Default.CheckCircle,
                            contentDescription = "Confirm completion",
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Confirm Completion")
                    }
                }
                Button(
                    onClick = { showDeleteDialog = true },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error,
                        contentColor = MaterialTheme.colorScheme.onError,
                    ),
                ) {
                    Icon(
                        imageVector = Icons.Default.Delete,
                        contentDescription = "Delete task",
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Delete task")
                }
            }
        }
    }

    if (showEditDialog) {
        EditTaskDialog(
            initialTitle = task.title,
            initialDescription = task.description,
            initialState = task.state ?: "todo",
            initialWorkingDirectory = task.workingDirectory,
            initialTokenLimit = task.tokenLimit,
            onDismiss = { showEditDialog = false },
            onSave = { title, description, workingDirectory, tokenLimit, state ->
                onEditTask(title, description, workingDirectory, tokenLimit, state)
                showEditDialog = false
            },
        )
    }

    if (showDeleteDialog) {
        DeleteTaskDialog(
            onDismiss = { showDeleteDialog = false },
            onConfirm = {
                showDeleteDialog = false
                onDeleteTask()
            },
        )
    }
}

/**
 * Text shown below the init branch & start working button with the outcome of
 * the operation. Success is rendered with the theme's primary color and failure
 * with the error color. When the operation reported a duration it is shown
 * below the message as well.
 */
@Composable
private fun OperationResultText(result: OperationResult) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(
            text = result.message,
            style = MaterialTheme.typography.bodyMedium,
            color = if (result.isError) {
                MaterialTheme.colorScheme.error
            } else {
                MaterialTheme.colorScheme.primary
            },
        )
        result.durationMillis?.let { durationMillis ->
            Text(
                text = "Duration: ${formatDuration(durationMillis)}",
                style = MaterialTheme.typography.labelSmall,
            )
        }
    }
}

/** A single labeled row in the task information section. */
@Composable
private fun TaskInfoRow(
    label: String,
    value: String,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
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

/** Human-readable label for a task state reported by the server. */
private fun formatTaskState(state: String?): String = when (state) {
    null -> "Unknown"
    "todo" -> "Todo"
    "in_progress" -> "In progress"
    "needs_review" -> "Needs review"
    "completed" -> "Completed"
    "failed" -> "Failed"
    "done" -> "Done"
    "approved" -> "Approved"
    else -> state.replace('_', ' ').replaceFirstChar { it.uppercase() }
}

/**
 * Human-readable description of a task failure reason reported by the server.
 * Unknown values fall back to the raw string so a newer server value never
 * renders as an empty row.
 */
private fun formatFailureReason(reason: String): String = when (reason) {
    "token_not_enough" ->
        "Ran out of tokens: the task reached its token limit before the agent finished."
    "execution_failure" ->
        "Execution failure: the agent hit an error while running the task."
    else -> reason
}

@Composable
private fun DeleteTaskDialog(
    onDismiss: () -> Unit,
    onConfirm: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Delete task") },
        text = {
            Text("Are you sure you want to delete this task? This action cannot be undone.")
        },
        confirmButton = {
            TextButton(
                onClick = onConfirm,
            ) {
                Text(
                    text = "Delete",
                    color = MaterialTheme.colorScheme.error,
                )
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

/**
 * Edit dialog for a task. Besides title and description it lets the client
 * edit the task status, working directory and token limit, each of which is
 * also displayed on its own row in the task information section.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun EditTaskDialog(
    initialTitle: String,
    initialDescription: String,
    initialState: String,
    initialWorkingDirectory: String,
    initialTokenLimit: Int,
    onDismiss: () -> Unit,
    onSave: (String, String, String, Int, String) -> Unit,
) {
    var title by remember { mutableStateOf(initialTitle) }
    var description by remember { mutableStateOf(initialDescription) }
    var state by remember { mutableStateOf(initialState) }
    var workingDirectory by remember { mutableStateOf(initialWorkingDirectory) }
    var tokenLimitText by remember { mutableStateOf(initialTokenLimit.toString()) }

    val validStates = listOf("todo", "in_progress", "needs_review", "failed", "completed")

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Edit task") },
        text = {
            Column(
                modifier = Modifier.verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                OutlinedTextField(
                    value = title,
                    onValueChange = { title = it },
                    label = { Text("Title") },
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = description,
                    onValueChange = { description = it },
                    label = { Text("Description") },
                    modifier = Modifier.fillMaxWidth(),
                    minLines = 3,
                )
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(
                        text = "Status",
                        style = MaterialTheme.typography.labelLarge,
                    )
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        validStates.forEach { candidate ->
                            FilterChip(
                                selected = state == candidate,
                                onClick = { state = candidate },
                                label = { Text(formatTaskState(candidate)) },
                            )
                        }
                    }
                }
                OutlinedTextField(
                    value = workingDirectory,
                    onValueChange = { workingDirectory = it },
                    label = { Text("Working directory") },
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = tokenLimitText,
                    onValueChange = { newValue -> tokenLimitText = newValue.filter { it.isDigit() } },
                    label = { Text("Token limit") },
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = {
            val tokenLimit = tokenLimitText.toIntOrNull()
            TextButton(
                enabled = title.isNotBlank() &&
                    workingDirectory.isNotBlank() &&
                    tokenLimit != null &&
                    tokenLimit > 0,
                onClick = {
                    val limit = tokenLimit
                    if (limit != null) {
                        onSave(
                            title.trim(),
                            description.trim(),
                            workingDirectory.trim(),
                            limit,
                            state,
                        )
                    }
                },
            ) {
                Text("Save")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}