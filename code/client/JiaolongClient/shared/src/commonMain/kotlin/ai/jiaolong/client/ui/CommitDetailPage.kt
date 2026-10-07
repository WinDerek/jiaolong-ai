package ai.jiaolong.client.ui

import ai.jiaolong.client.model.Commit
import ai.jiaolong.client.model.CommitComment
import ai.jiaolong.client.model.DiffLine
import ai.jiaolong.client.model.DiffLineType
import ai.jiaolong.client.model.FileDiff
import ai.jiaolong.client.util.formatTimestamp
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

/**
 * Commit detail page: commit message, file diffs, line comments, commit
 * comments, and an approve action.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CommitDetailPage(
    commit: Commit,
    onBack: () -> Unit,
    onPostCommitComment: (String, Int?) -> Unit,
    onApprove: () -> Unit,
) {
    var commitCommentText by remember { mutableStateOf("") }
    var showCommitCommentDialog by remember { mutableStateOf(false) }
    var selectedLine by remember { mutableStateOf<Int?>(null) }
    var lineCommentText by remember { mutableStateOf("") }
    var showLineCommentDialog by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Commit detail") },
                navigationIcon = {
                    TextButton(onClick = onBack) { Text("Back") }
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
            // Commit message and actions.
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = "Commit message",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                )
                Card(modifier = Modifier.fillMaxWidth()) {
                    Text(
                        text = commit.message,
                        style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.padding(12.dp),
                    )
                }
                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Button(onClick = onApprove, enabled = !commit.approved) {
                        Text(if (commit.approved) "Approved" else "Approve commit")
                    }
                    if (commit.approved) {
                        Text(
                            text = "This commit has been approved.",
                            style = MaterialTheme.typography.labelMedium,
                        )
                    }
                }
                Button(onClick = { showCommitCommentDialog = true }) {
                    Text("Post commit comment")
                }
            }

            HorizontalDivider()

            // File diffs.
            commit.files.forEach { file ->
                FileDiffSection(
                    file = file,
                    onLineClick = { line ->
                        selectedLine = line
                        lineCommentText = ""
                        showLineCommentDialog = true
                    },
                )
            }

            // Comments.
            if (commit.comments.isNotEmpty()) {
                HorizontalDivider()
                Text(
                    text = "Comments",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                )
                commit.comments.forEach { comment ->
                    CommitCommentCard(comment = comment)
                }
            }
        }
    }

    if (showCommitCommentDialog) {
        AlertDialog(
            onDismissRequest = { showCommitCommentDialog = false },
            title = { Text("Post commit comment") },
            text = {
                OutlinedTextField(
                    value = commitCommentText,
                    onValueChange = { commitCommentText = it },
                    label = { Text("Comment") },
                    modifier = Modifier.fillMaxWidth(),
                    minLines = 3,
                )
            },
            confirmButton = {
                TextButton(
                    enabled = commitCommentText.isNotBlank(),
                    onClick = {
                        onPostCommitComment(commitCommentText.trim(), null)
                        commitCommentText = ""
                        showCommitCommentDialog = false
                    },
                ) { Text("Post") }
            },
            dismissButton = {
                TextButton(onClick = { showCommitCommentDialog = false }) { Text("Cancel") }
            },
        )
    }

    if (showLineCommentDialog && selectedLine != null) {
        AlertDialog(
            onDismissRequest = { showLineCommentDialog = false },
            title = { Text("Line comment on line $selectedLine") },
            text = {
                OutlinedTextField(
                    value = lineCommentText,
                    onValueChange = { lineCommentText = it },
                    label = { Text("Comment") },
                    modifier = Modifier.fillMaxWidth(),
                    minLines = 3,
                )
            },
            confirmButton = {
                TextButton(
                    enabled = lineCommentText.isNotBlank(),
                    onClick = {
                        onPostCommitComment(lineCommentText.trim(), selectedLine)
                        lineCommentText = ""
                        showLineCommentDialog = false
                        selectedLine = null
                    },
                ) { Text("Post") }
            },
            dismissButton = {
                TextButton(onClick = {
                    showLineCommentDialog = false
                    selectedLine = null
                }) { Text("Cancel") }
            },
        )
    }
}

@Composable
private fun FileDiffSection(file: FileDiff, onLineClick: (Int) -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column {
            Surface(color = MaterialTheme.colorScheme.surfaceVariant) {
                Text(
                    text = file.filePath,
                    style = MaterialTheme.typography.labelLarge,
                    modifier = Modifier.fillMaxWidth().padding(10.dp),
                )
            }
            Column {
                file.lines.forEach { line ->
                    DiffLineRow(
                        line = line,
                        onClick = {
                            val lineNumber = line.newLineNumber ?: line.oldLineNumber ?: 0
                            onLineClick(lineNumber)
                        },
                    )
                }
            }
        }
    }
}

@Composable
private fun DiffLineRow(line: DiffLine, onClick: () -> Unit) {
    val background = when (line.type) {
        DiffLineType.ADDED -> Color(0xFFE6FFEC)
        DiffLineType.REMOVED -> Color(0xFFFFEBEE)
        DiffLineType.CONTEXT -> Color.Transparent
    }
    val prefix = when (line.type) {
        DiffLineType.ADDED -> "+"
        DiffLineType.REMOVED -> "-"
        DiffLineType.CONTEXT -> " "
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(background)
            .clickable(onClick = onClick)
            .padding(horizontal = 10.dp, vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = "${line.oldLineNumber?.toString()?.padStart(4) ?: "    "} " +
                "${line.newLineNumber?.toString()?.padStart(4) ?: "    "} " +
                "$prefix${line.content}",
            style = MaterialTheme.typography.bodySmall,
            fontFamily = FontFamily.Monospace,
        )
    }
}

@Composable
private fun CommitCommentCard(comment: CommitComment) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = comment.author,
                    style = MaterialTheme.typography.labelLarge,
                    fontWeight = FontWeight.SemiBold,
                )
                if (comment.lineNumber != null) {
                    Surface(
                        shape = MaterialTheme.shapes.small,
                        color = MaterialTheme.colorScheme.secondaryContainer,
                    ) {
                        Text(
                            text = "line ${comment.lineNumber}",
                            style = MaterialTheme.typography.labelSmall,
                            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                        )
                    }
                }
            }
            Text(
                text = comment.content,
                style = MaterialTheme.typography.bodyMedium,
            )
            Text(
                text = formatTimestamp(comment.createdAt),
                style = MaterialTheme.typography.labelSmall,
            )
        }
    }
}
