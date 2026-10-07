package ai.jiaolong.client.ui

import ai.jiaolong.client.model.Project
import ai.jiaolong.client.model.ReadonlyDirectory
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

/**
 * Project detail page: a back button in the top app bar, the project title and
 * description, and the list of the project's readonly directories with an add
 * button (a "+" icon) to append a new one.
 *
 * The page renders the latest project detail loaded from the server. While that
 * load is in flight a loading animation is shown at the top of the page, and
 * when it fails a persistent (non-dismissable) [LoadErrorBanner] is shown above
 * the possibly stale project data. Besides the automatic reload when the page
 * is entered, the refresh button in the top app bar asks the server for the
 * latest project detail again on demand.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProjectDetailPage(
    project: Project?,
    isLoading: Boolean,
    loadErrorMessage: String?,
    addReadonlyDirectoryResult: OperationResult?,
    isAddReadonlyDirectoryPending: Boolean,
    onBack: () -> Unit,
    onRefresh: () -> Unit,
    onAddReadonlyDirectory: (String, String, String) -> Unit,
) {
    var showAddDialog by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(project?.name ?: "Project details") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = "Go back",
                        )
                    }
                },
                actions = {
                    IconButton(onClick = onRefresh, enabled = !isLoading) {
                        Icon(
                            imageVector = Icons.Default.Refresh,
                            contentDescription = "Refresh project detail",
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
                // The latest project detail is being reloaded from the server,
                // so the data currently shown may be outdated.
                LinearProgressIndicator(
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            if (loadErrorMessage != null) {
                // Persistent (non-dismissable) banner shown when the latest
                // project detail load failed, so the user knows the data may be
                // stale.
                LoadErrorBanner(message = loadErrorMessage)
            }

            if (project != null) {
                // Title and description of the project.
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        text = project.name,
                        style = MaterialTheme.typography.headlineSmall,
                        fontWeight = FontWeight.Bold,
                    )
                    Text(
                        text = project.description,
                        style = MaterialTheme.typography.bodyLarge,
                    )
                }

                HorizontalDivider()

                // The readonly-directory catalog followed by the button to add
                // a new entry.
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        text = "Read-only directories",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                    )
                    if (project.readonlyDirectories.isEmpty()) {
                        Text(
                            text = "No read-only directories yet.",
                            style = MaterialTheme.typography.bodyMedium,
                        )
                    }
                    project.readonlyDirectories.forEach { directory ->
                        ReadonlyDirectoryCard(directory = directory)
                    }

                    Button(
                        onClick = { showAddDialog = true },
                        enabled = !isAddReadonlyDirectoryPending,
                    ) {
                        if (isAddReadonlyDirectoryPending) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(16.dp),
                                strokeWidth = 2.dp,
                                color = LocalContentColor.current,
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text("Adding...")
                        } else {
                            Icon(
                                imageVector = Icons.Default.Add,
                                contentDescription = "Add read-only directory",
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text("Add read-only directory")
                        }
                    }
                    addReadonlyDirectoryResult?.let { result ->
                        Text(
                            text = result.message,
                            style = MaterialTheme.typography.bodyMedium,
                            color = if (result.isError) {
                                MaterialTheme.colorScheme.error
                            } else {
                                MaterialTheme.colorScheme.primary
                            },
                        )
                    }
                }
            } else if (!isLoading && loadErrorMessage == null) {
                Text(
                    text = "Project not found.",
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
    }

    if (showAddDialog) {
        AddReadonlyDirectoryDialog(
            onDismiss = { showAddDialog = false },
            onAdd = { alias, realPath, description ->
                onAddReadonlyDirectory(alias, realPath, description)
                showAddDialog = false
            },
        )
    }
}

/** Card showing a single readonly directory of the project's catalog. */
@Composable
private fun ReadonlyDirectoryCard(directory: ReadonlyDirectory) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(
                text = directory.alias,
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.SemiBold,
            )
            Text(
                text = directory.realPath,
                style = MaterialTheme.typography.bodyMedium,
            )
            if (directory.description.isNotBlank()) {
                Text(
                    text = directory.description,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }
    }
}

/**
 * Dialog that lets the user fill the fields of a new readonly directory and
 * submit it to the server. Alias and real path are required and validated
 * client-side (the server validates them again); description is optional.
 */
@Composable
private fun AddReadonlyDirectoryDialog(
    onDismiss: () -> Unit,
    onAdd: (String, String, String) -> Unit,
) {
    var alias by remember { mutableStateOf("") }
    var realPath by remember { mutableStateOf("") }
    var description by remember { mutableStateOf("") }

    val trimmedAlias = alias.trim()
    val trimmedRealPath = realPath.trim()
    val aliasValid = trimmedAlias.isNotEmpty() &&
        trimmedAlias != "workspace" &&
        trimmedAlias.matches(Regex("^[A-Za-z0-9_-]+$"))
    val realPathValid = trimmedRealPath.startsWith("/")

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Add read-only directory") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedTextField(
                    value = alias,
                    onValueChange = { alias = it },
                    label = { Text("Alias") },
                    placeholder = { Text("docs") },
                    isError = alias.isNotEmpty() && !aliasValid,
                    modifier = Modifier.fillMaxWidth(),
                )
                if (alias.isNotEmpty() && !aliasValid) {
                    Text(
                        text = "Use letters, digits, '_' or '-', and avoid \"workspace\".",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.error,
                    )
                }
                OutlinedTextField(
                    value = realPath,
                    onValueChange = { realPath = it },
                    label = { Text("Real path") },
                    placeholder = { Text("/path/to/directory") },
                    isError = realPath.isNotEmpty() && !realPathValid,
                    modifier = Modifier.fillMaxWidth(),
                )
                if (realPath.isNotEmpty() && !realPathValid) {
                    Text(
                        text = "Enter an absolute path.",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.error,
                    )
                }
                OutlinedTextField(
                    value = description,
                    onValueChange = { description = it },
                    label = { Text("Description") },
                    modifier = Modifier.fillMaxWidth(),
                    minLines = 2,
                )
            }
        },
        confirmButton = {
            TextButton(
                enabled = aliasValid && realPathValid,
                onClick = {
                    onAdd(trimmedAlias, trimmedRealPath, description.trim())
                },
            ) {
                Text("Add")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}