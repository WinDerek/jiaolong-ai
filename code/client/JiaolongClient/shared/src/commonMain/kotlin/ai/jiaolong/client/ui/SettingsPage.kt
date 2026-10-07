package ai.jiaolong.client.ui

import ai.jiaolong.client.model.LlmProvider
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
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

/**
 * Settings page: lets the user view and edit the server base URL used by the
 * client to talk to the Jiaolong Server REST API, and choose the enabled LLM
 * provider that Jiaolong CLI should use.
 *
 * Each LLM provider is shown as a card. The card of the currently enabled
 * provider is highlighted with navy blue; the other cards are white. All cards
 * have a drop shadow.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsPage(
    serverBaseUrl: String,
    llmProviders: List<LlmProvider>,
    enabledLlmProviderIndex: Int?,
    isLlmProvidersLoading: Boolean,
    llmProvidersErrorMessage: String?,
    onBack: () -> Unit,
    onSave: (String) -> Unit,
    onSelectLlmProvider: (Int) -> Unit,
    onRetryLoadLlmProviders: () -> Unit,
    isStopServerPending: Boolean = false,
    stopServerResult: OperationResult? = null,
    onStopServer: () -> Unit,
    onLogout: () -> Unit,
) {
    var baseUrl by remember { mutableStateOf(serverBaseUrl) }
    var showStopServerDialog by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
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
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            // Base URL
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = "Jiaolong Client Settings",
                    style = MaterialTheme.typography.titleMedium,
                )
                Text(
                    text = "Server Base URL",
                    style = MaterialTheme.typography.titleSmall,
                )
                Text(
                    text = "Base URL of Jiaolong Server REST API, " +
                        "e.g., http://127.0.0.1:8989/api/",
                    style = MaterialTheme.typography.bodySmall,
                )
                OutlinedTextField(
                    value = baseUrl,
                    onValueChange = { baseUrl = it },
                    label = { Text("Server Base URL") },
                    placeholder = { Text("http://127.0.0.1:8989/api/") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                )
                Button(
                    enabled = baseUrl.isNotBlank(),
                    onClick = { onSave(baseUrl) },
                ) {
                    Text("Save")
                }
            }

            // LLM provider
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = "Remote Jiaolong Server Settings",
                    style = MaterialTheme.typography.titleMedium,
                )
                Text(
                    text = "LLM Provider",
                    style = MaterialTheme.typography.titleSmall,
                )
                Text(
                    text = "The enabled LLM provider that Jiaolong CLI will use.",
                    style = MaterialTheme.typography.bodySmall,
                )
                LlmProviderSection(
                    llmProviders = llmProviders,
                    enabledLlmProviderIndex = enabledLlmProviderIndex,
                    isLoading = isLlmProvidersLoading,
                    errorMessage = llmProvidersErrorMessage,
                    onSelectLlmProvider = onSelectLlmProvider,
                    onRetry = onRetryLoadLlmProviders,
                )
            }

            // Server lifecycle
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = "Server",
                    style = MaterialTheme.typography.titleMedium,
                )
                Text(
                    text = "Stop the Jiaolong Server that this client is connected to.",
                    style = MaterialTheme.typography.bodySmall,
                )
                Button(
                    enabled = !isStopServerPending,
                    onClick = { showStopServerDialog = true },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error,
                        contentColor = MaterialTheme.colorScheme.onError,
                    ),
                ) {
                    if (isStopServerPending) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(16.dp),
                            strokeWidth = 2.dp,
                            color = LocalContentColor.current,
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Stopping server...")
                    } else {
                        Text("Stop Server")
                    }
                }
                stopServerResult?.let { result ->
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

            // Account / authentication
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = "Account",
                    style = MaterialTheme.typography.titleMedium,
                )
                Text(
                    text = "Sign out of the Jiaolong Server. You will need to " +
                        "enter your username and password again.",
                    style = MaterialTheme.typography.bodySmall,
                )
                Button(onClick = onLogout) {
                    Text("Sign out")
                }
            }
        }
    }

    if (showStopServerDialog) {
        AlertDialog(
            onDismissRequest = { showStopServerDialog = false },
            title = { Text("Stop server") },
            text = {
                Text("Are you sure you want to stop the Jiaolong Server? It will no longer be reachable from this client.")
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        showStopServerDialog = false
                        onStopServer()
                    },
                ) {
                    Text(
                        text = "Stop",
                        color = MaterialTheme.colorScheme.error,
                    )
                }
            },
            dismissButton = {
                TextButton(onClick = { showStopServerDialog = false }) {
                    Text("Cancel")
                }
            },
        )
    }
}

@Composable
private fun LlmProviderSection(
    llmProviders: List<LlmProvider>,
    enabledLlmProviderIndex: Int?,
    isLoading: Boolean,
    errorMessage: String?,
    onSelectLlmProvider: (Int) -> Unit,
    onRetry: () -> Unit,
) {
    when {
        isLoading -> {
            CircularProgressIndicator(modifier = Modifier.padding(16.dp))
        }
        errorMessage != null -> {
            Text(
                text = errorMessage,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.error,
            )
            TextButton(onClick = onRetry) { Text("Retry") }
        }
        llmProviders.isEmpty() -> {
            Text(
                text = "No LLM providers configured.",
                style = MaterialTheme.typography.bodyMedium,
            )
        }
        else -> {
            llmProviders.forEach { provider ->
                LlmProviderCard(
                    provider = provider,
                    selected = provider.index == enabledLlmProviderIndex,
                    onClick = { onSelectLlmProvider(provider.index) },
                )
            }
        }
    }
}

/**
 * A single LLM provider card. The enabled provider is highlighted with navy
 * blue while the other providers use a white container. Cards have a drop
 * shadow and are clickable so the user can switch the enabled provider.
 */
@Composable
private fun LlmProviderCard(
    provider: LlmProvider,
    selected: Boolean,
    onClick: () -> Unit,
) {
    val containerColor = if (selected) NavyBlue else Color.White
    val contentColor = if (selected) Color.White else Color.Black
    Card(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = containerColor,
            contentColor = contentColor,
        ),
        elevation = CardDefaults.cardElevation(defaultElevation = 4.dp),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(
                text = provider.model.ifBlank { "LLM Provider" },
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.SemiBold,
            )
            Text(
                text = provider.baseUrl,
                style = MaterialTheme.typography.bodyMedium,
            )
            if (selected) {
                Text(
                    text = "Enabled",
                    style = MaterialTheme.typography.labelSmall,
                )
            }
        }
    }
}

/** Navy blue color used to highlight the enabled LLM provider card. */
private val NavyBlue = Color(0xFF2E6FA0)
