package ai.jiaolong.client.ui

import ai.jiaolong.client.controller.TaskController
import ai.jiaolong.client.model.Project
import ai.jiaolong.client.model.ReadonlyDirectory
import ai.jiaolong.client.model.Task
import ai.jiaolong.client.platform.isDesktop
import ai.jiaolong.client.resources.Res
import ai.jiaolong.client.resources.logo_jiaolong_512x512
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.ShowChart
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.BasicAlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.KeyEvent
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import org.jetbrains.compose.resources.painterResource
import androidx.compose.foundation.Image

/**
 * Fill color of the selected project item in the project switcher dialog. It
 * matches the fill color of the selected LLM provider card on the settings
 * page.
 */
private val SelectedProjectFillColor = Color(0xFF76C0EC)

/**
 * Dashboard page: total token usage shown on its own line under a "Token
 * Usage" title, a project switcher (with the current project title and a
 * dropdown icon) above the "Tasks" title, summary cards for active, completed
 * and todo task counts under the "Tasks"
 * title, task list sorted by status (needs_review, in_progress, todo, failed,
 * completed last) as returned by the server, a button to create a new task,
 * and pull-down-to-refresh.
 *
 * Refresh is triggered by pulling down on Android, or by pressing Ctrl+R /
 * clicking the refresh button in the top app bar on desktop. After a refresh
 * attempt, an auto-dismiss snackbar shows whether it succeeded or failed.
 *
 * While a request is in flight, a loading animation is shown at the top of the
 * page so the user always knows the data may be outdated or still loading.
 *
 * If the latest data load failed, a persistent (non-dismissable) error message
 * is shown at the top so the user always knows the displayed data may be
 * stale. This is separate from the dismissable [errorMessage] banner used for
 * other operation failures.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DashboardPage(
    tasks: List<Task>,
    totalTokenUsage: Int,
    searchKeywords: String,
    onSearch: (String) -> Unit,
    isRefreshing: Boolean,
    errorMessage: String?,
    onDismissError: () -> Unit,
    loadErrorMessage: String?,
    refreshMessage: String?,
    onDismissRefreshMessage: () -> Unit,
    onRefresh: () -> Unit,
    refreshShortcutEvent: KeyEvent?,
    onRefreshShortcutHandled: () -> Unit,
    currentProject: Project?,
    projects: List<Project>,
    isProjectsLoading: Boolean,
    projectsErrorMessage: String?,
    onRefreshProjects: () -> Unit,
    onSelectProject: (Project) -> Unit,
    onCreateProject: (String, String, String, Int) -> Unit,
    defaultTaskWorkingDirectory: String?,
    onCreateTask: (String, String, String, Int, List<String>) -> Unit,
    onOpenTask: (String) -> Unit,
    onOpenSettings: () -> Unit,
    onOpenServerMonitor: () -> Unit,
    onOpenProjectDetail: () -> Unit,
) {
    var showCreateDialog by remember { mutableStateOf(false) }
    // Whether the project switcher dialog (opened by tapping the project
    // button) is shown.
    var showProjectSwitcher by remember { mutableStateOf(false) }
    // Whether the create project dialog (opened by tapping the "+" button next
    // to the project switcher) is shown.
    var showCreateProjectDialog by remember { mutableStateOf(false) }
    val snackbarHostState = remember { SnackbarHostState() }
    val currentRefreshMessage by rememberUpdatedState(refreshMessage)

    LaunchedEffect(refreshMessage) {
        val message = refreshMessage ?: return@LaunchedEffect
        snackbarHostState.showSnackbar(message)
        if (currentRefreshMessage == message) {
            onDismissRefreshMessage()
        }
    }

    // The Ctrl+R shortcut is handled on the dashboard page itself: main.kt
    // forwards the captured key event down through the app, and this effect
    // turns it into a refresh when running on desktop. The event is marked as
    // handled afterwards so it is not replayed on a later recomposition.
    LaunchedEffect(refreshShortcutEvent) {
        if (refreshShortcutEvent == null) return@LaunchedEffect
        onRefreshShortcutHandled()
        if (isDesktop()) {
            onRefresh()
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Image(
                            painter = painterResource(Res.drawable.logo_jiaolong_512x512),
                            contentDescription = "Jiaolong logo",
                            modifier = Modifier.size(32.dp),
                        )
                        Text("Jiaolong AI")
                    }
                },
                actions = {
                    if (isDesktop()) {
                        IconButton(onClick = onRefresh) {
                            Icon(
                                imageVector = Icons.Default.Refresh,
                                contentDescription = "Refresh",
                            )
                        }
                    }
                    IconButton(onClick = onOpenServerMonitor) {
                        Icon(
                            imageVector = Icons.Default.ShowChart,
                            contentDescription = "Server monitor",
                        )
                    }
                    IconButton(onClick = onOpenSettings) {
                        Icon(
                            imageVector = Icons.Default.Settings,
                            contentDescription = "Settings",
                        )
                    }
                    IconButton(onClick = { showCreateDialog = true }) {
                        Icon(
                            imageVector = Icons.Default.Add,
                            contentDescription = "Add task",
                        )
                    }
                },
            )
        },
        snackbarHost = { SnackbarHost(snackbarHostState) },
    ) { innerPadding ->
        Column(
            modifier = Modifier.fillMaxSize().padding(innerPadding),
        ) {
            if (isRefreshing) {
                // Show a loading animation at the top of the dashboard while a
                // request is in flight so the user can tell the data is being
                // (re)loaded from the server.
                LinearProgressIndicator(
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            if (loadErrorMessage != null) {
                LoadErrorBanner(
                    message = loadErrorMessage,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                )
            }
            if (errorMessage != null) {
                ErrorBanner(
                    message = errorMessage,
                    onDismiss = onDismissError,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                )
            }
            if (isDesktop()) {
                TaskList(
                    tasks = tasks,
                    totalTokenUsage = totalTokenUsage,
                    searchKeywords = searchKeywords,
                    onSearch = onSearch,
                    currentProject = currentProject,
                    isProjectsLoading = isProjectsLoading,
                    onOpenProjectSwitcher = {
                        // Pull the latest project list from the server every
                        // time the user opens the project switcher.
                        onRefreshProjects()
                        showProjectSwitcher = true
                    },
                    onOpenProjectDetail = onOpenProjectDetail,
                    onOpenTask = onOpenTask,
                    modifier = Modifier.fillMaxSize(),
                )
            } else {
                PullToRefreshBox(
                    isRefreshing = isRefreshing,
                    onRefresh = onRefresh,
                    modifier = Modifier.fillMaxSize(),
                ) {
                    TaskList(
                        tasks = tasks,
                        totalTokenUsage = totalTokenUsage,
                        searchKeywords = searchKeywords,
                        onSearch = onSearch,
                        currentProject = currentProject,
                        isProjectsLoading = isProjectsLoading,
                        onOpenProjectSwitcher = {
                            // Pull the latest project list from the server
                            // every time the user opens the project switcher.
                            onRefreshProjects()
                            showProjectSwitcher = true
                        },
                        onOpenProjectDetail = onOpenProjectDetail,
                        onOpenTask = onOpenTask,
                        modifier = Modifier.fillMaxSize(),
                    )
                }
            }
        }
    }

    if (showCreateDialog) {
        CreateTaskDialog(
            initialWorkingDirectory = defaultTaskWorkingDirectory,
            readonlyDirectories = currentProject?.readonlyDirectories.orEmpty(),
            onDismiss = { showCreateDialog = false },
            onCreate = { title, description, workingDirectory, tokenLimit, readonlyDirectoryIds ->
                onCreateTask(title, description, workingDirectory, tokenLimit, readonlyDirectoryIds)
                showCreateDialog = false
            },
        )
    }

    // The project switcher dialog is fixed to 60% of the dashboard page
    // height, so measure the page height here and pass it down to the dialog.
    // The box only hosts the dialog window, so it does not affect the page
    // layout.
    BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
        val projectSwitcherDialogHeight = maxHeight * 0.6f
        if (showProjectSwitcher) {
            ProjectSwitcherDialog(
                projects = projects,
                currentProject = currentProject,
                isProjectsLoading = isProjectsLoading,
                errorMessage = projectsErrorMessage,
                onDismiss = { showProjectSwitcher = false },
                onSelectProject = { project ->
                    onSelectProject(project)
                    showProjectSwitcher = false
                },
                onCreateProject = {
                    showProjectSwitcher = false
                    showCreateProjectDialog = true
                },
                height = projectSwitcherDialogHeight,
            )
        }
    }

    if (showCreateProjectDialog) {
        CreateProjectDialog(
            onDismiss = { showCreateProjectDialog = false },
            onCreateProject = { name, description, defaultTaskWorkingDirectory, ordering ->
                onCreateProject(name, description, defaultTaskWorkingDirectory, ordering)
                showCreateProjectDialog = false
            },
        )
    }
}

/**
 * Scrollable task list shown on the dashboard. On desktop this is rendered
 * directly (pull-to-refresh is an Android-only interaction), while on Android
 * it is wrapped in a [PullToRefreshBox].
 */
@Composable
private fun TaskList(
    tasks: List<Task>,
    totalTokenUsage: Int,
    searchKeywords: String,
    onSearch: (String) -> Unit,
    currentProject: Project?,
    isProjectsLoading: Boolean,
    onOpenProjectSwitcher: () -> Unit,
    onOpenProjectDetail: () -> Unit,
    onOpenTask: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    LazyColumn(
        modifier = modifier,
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item {
            Text(
                text = "Token Usage",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
            )
        }
        item {
            Text(
                text = "Total token: $totalTokenUsage",
                style = MaterialTheme.typography.bodyLarge,
            )
        }
        item {
            ProjectBar(
                currentProject = currentProject,
                isProjectsLoading = isProjectsLoading,
                onOpenProjectSwitcher = onOpenProjectSwitcher,
            )
        }
        item {
            // Button just below the project switcher that opens the detail page
            // of the current project. It is disabled while no project has been
            // selected or loaded yet.
            GreyButton(
                onClick = onOpenProjectDetail,
                enabled = currentProject != null,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text(
                    text = "Project details",
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.weight(1f),
                )
                Icon(
                    imageVector = Icons.AutoMirrored.Filled.ArrowForward,
                    contentDescription = "Open project details",
                )
            }
        }
        item {
            // Search box: type keywords and submit them to have the server
            // return only the tasks of the current project that match.
            TaskSearchBar(
                initialKeywords = searchKeywords,
                onSearch = onSearch,
                modifier = Modifier.fillMaxWidth(),
            )
        }
        item {
            Text(
                text = "Tasks",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
            )
        }
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                TaskStatCard(
                    label = "Active",
                    count = tasks.count { it.effectiveState() == "in_progress" },
                    modifier = Modifier.weight(1f),
                )
                TaskStatCard(
                    label = "Completed",
                    count = tasks.count { it.effectiveState() == "completed" },
                    modifier = Modifier.weight(1f),
                )
                TaskStatCard(
                    label = "Todo",
                    count = tasks.count { it.effectiveState() == "todo" },
                    modifier = Modifier.weight(1f),
                )
            }
        }
        if (tasks.isEmpty()) {
            item {
                Text(
                    text = "No tasks yet. Tap + to create one.",
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
        items(tasks, key = { it.id }) { task ->
            TaskCard(task = task, onClick = { onOpenTask(task.id) })
        }
    }
}

/**
 * Search box shown on the dashboard. The user types keywords and submits them
 * (tapping the search icon) to have the server return only the tasks whose
 * title or description match. Tapping the clear icon resets the search and
 * reloads the full task list. [initialKeywords] seeds the box when the page is
 * (re)entered so an active search survives navigation.
 */
@Composable
private fun TaskSearchBar(
    initialKeywords: String,
    onSearch: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    var keywords by remember { mutableStateOf(initialKeywords) }
    OutlinedTextField(
        value = keywords,
        onValueChange = { keywords = it },
        modifier = modifier,
        singleLine = true,
        label = { Text("Search tasks") },
        placeholder = { Text("Enter keywords") },
        trailingIcon = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (keywords.isNotEmpty()) {
                    IconButton(onClick = {
                        keywords = ""
                        onSearch("")
                    }) {
                        Icon(
                            imageVector = Icons.Default.Clear,
                            contentDescription = "Clear search",
                        )
                    }
                }
                IconButton(onClick = { onSearch(keywords) }) {
                    Icon(
                        imageVector = Icons.Default.Search,
                        contentDescription = "Search tasks",
                    )
                }
            }
        },
    )
}

/**
 * Button shown above the "Tasks" title on the dashboard: it shows the current
 * project title with a dropdown icon and opens the project switcher dialog.
 * It uses the same grey, rounded, borderless style as the create project
 * button shown inside [ProjectSwitcherDialog].
 */
@Composable
private fun ProjectBar(
    currentProject: Project?,
    isProjectsLoading: Boolean,
    onOpenProjectSwitcher: () -> Unit,
) {
    GreyButton(
        onClick = onOpenProjectSwitcher,
        modifier = Modifier.fillMaxWidth(),
    ) {
        Text(
            text = if (currentProject != null) "Project: ${currentProject.name}" else (if (isProjectsLoading) "Loading projects…" else "Select project"),
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            maxLines = 1,
            modifier = Modifier.weight(1f),
        )
        Icon(
            imageVector = Icons.Default.ArrowDropDown,
            contentDescription = "Switch project",
        )
    }
}

/**
 * Button with a grey background and rounded corners, without a border. Used
 * for the project switcher and the create project "+" button so they share the
 * same style.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun GreyButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    content: @Composable RowScope.() -> Unit,
) {
    Surface(
        onClick = onClick,
        modifier = modifier,
        enabled = enabled,
        shape = RoundedCornerShape(8.dp),
        color = MaterialTheme.colorScheme.surfaceVariant,
        contentColor = MaterialTheme.colorScheme.onSurfaceVariant,
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            content = content,
        )
    }
}

/**
 * Dialog that lets the user switch the current project. It lists the projects
 * loaded from the server and marks the current one, with a create project
 * button of the same width below the list. The dialog is fixed to [height]
 * (80% of the dashboard page height) and its body scrolls vertically, so a
 * long project list stays reachable without growing the dialog. While the
 * project list is still loading, the list and the create project button are
 * hidden and the title shows the loading status instead. The caller pulls the
 * latest project list from the server before showing the dialog.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ProjectSwitcherDialog(
    projects: List<Project>,
    currentProject: Project?,
    isProjectsLoading: Boolean,
    errorMessage: String?,
    onDismiss: () -> Unit,
    onSelectProject: (Project) -> Unit,
    onCreateProject: () -> Unit,
    height: Dp,
) {
    BasicAlertDialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier.fillMaxWidth().height(height),
            shape = MaterialTheme.shapes.extraLarge,
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 6.dp,
        ) {
            Column(
                modifier = Modifier.fillMaxSize().padding(24.dp),
            ) {
                Text(
                    text = if (isProjectsLoading) "Loading projects…" else "Switch project",
                    style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.padding(bottom = 16.dp),
                )

                // The scrollable content (the project list and the button to
                // create a new project) fills the remaining dialog height and
                // scrolls vertically, so a long list stays reachable without
                // growing the dialog.
                Column(
                    modifier = Modifier
                        .weight(1f)
                        .verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    if (isProjectsLoading) {
                        // While loading, hide the project list and the create
                        // project button so the user cannot act on stale or
                        // empty data.
                        LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                    } else {
                        if (errorMessage != null) {
                            Text(
                                text = errorMessage,
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.error,
                            )
                        }
                        if (projects.isEmpty()) {
                            Text(
                                text = "No projects yet. Tap + to create one.",
                                style = MaterialTheme.typography.bodyMedium,
                            )
                        }

                        // The project list
                        projects.forEach { project ->
                            val selected = project.id == currentProject?.id
                            Surface(
                                onClick = { onSelectProject(project) },
                                modifier = Modifier.fillMaxWidth(),
                                shape = RoundedCornerShape(0.dp),
                                color = if (selected) {
                                    SelectedProjectFillColor
                                } else {
                                    MaterialTheme.colorScheme.surface
                                },
                                border = BorderStroke(
                                    width = 1.dp,
                                    color = if (selected) Color(0xFF425B9A) else Color.Gray,
                                ),
                            ) {
                                Column(modifier = Modifier.padding(12.dp)) {
                                    Text(
                                        text = project.name,
                                        style = MaterialTheme.typography.titleSmall,
                                        fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal,
                                        color = if (selected) Color(0xFF425B9A) else Color.Black
                                    )
                                    if (project.description.isNotBlank()) {
                                        Text(
                                            text = project.description,
                                            style = MaterialTheme.typography.bodySmall,
                                            maxLines = 2,
                                            color = if (selected) Color(0xFF425B9A) else Color(0xFF333333)
                                        )
                                    }
                                }
                            }
                        }

                        // The button to create a new project
                        GreyButton(
                            onClick = onCreateProject,
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Icon(
                                imageVector = Icons.Default.Add,
                                contentDescription = "Create project",
                            )
                            Text("New project")
                        }
                    }
                }

                Row(
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                    horizontalArrangement = Arrangement.End,
                ) {
                    TextButton(onClick = onDismiss) { Text("Cancel") }
                }
            }
        }
    }
}

/**
 * Dialog that lets the user fill the fields of a new project and submit it to
 * the server. Name and default task working directory are required;
 * description and ordering are optional.
 */
@Composable
private fun CreateProjectDialog(
    onDismiss: () -> Unit,
    onCreateProject: (String, String, String, Int) -> Unit,
) {
    var name by remember { mutableStateOf("") }
    var description by remember { mutableStateOf("") }
    var defaultTaskWorkingDirectory by remember { mutableStateOf("") }
    var orderingText by remember { mutableStateOf("0") }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Create new project") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    label = { Text("Name") },
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = description,
                    onValueChange = { description = it },
                    label = { Text("Description") },
                    modifier = Modifier.fillMaxWidth(),
                    minLines = 2,
                )
                OutlinedTextField(
                    value = defaultTaskWorkingDirectory,
                    onValueChange = { defaultTaskWorkingDirectory = it },
                    label = { Text("Default task working directory") },
                    placeholder = { Text("/path/to/workspace") },
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = orderingText,
                    onValueChange = { newValue -> orderingText = newValue.filter { it.isDigit() } },
                    label = { Text("Ordering") },
                    placeholder = { Text("0") },
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = {
            TextButton(
                enabled = name.isNotBlank() &&
                    defaultTaskWorkingDirectory.isNotBlank(),
                onClick = {
                    onCreateProject(
                        name.trim(),
                        description.trim(),
                        defaultTaskWorkingDirectory.trim(),
                        orderingText.toIntOrNull() ?: 0,
                    )
                },
            ) {
                Text("Create")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

/**
 * Non-dismissable banner shown on a page when the latest data load from the
 * server failed. Unlike [ErrorBanner], it has no dismiss action and stays
 * visible until a subsequent load succeeds, so the user always knows the
 * displayed data may be stale.
 */
@Composable
fun LoadErrorBanner(
    message: String,
    modifier: Modifier = Modifier,
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.errorContainer),
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                text = message,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onErrorContainer,
                modifier = Modifier.weight(1f),
            )
        }
    }
}

/**
 * Effective status of a task. Falls back to "in_progress" for tasks that have
 * an assigned agent but no reported state, matching the badge in
 * [TaskStatusBadge].
 */
private fun Task.effectiveState(): String? =
    state ?: if (assigneeAgent != null) "in_progress" else null

/**
 * Summary card on the dashboard showing a single task count statistic (such
 * as active, completed or todo tasks).
 */
@Composable
private fun TaskStatCard(
    label: String,
    count: Int,
    modifier: Modifier = Modifier,
) {
    Card(modifier = modifier) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(
                text = count.toString(),
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,
            )
            Text(
                text = label,
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }
}

@Composable
private fun TaskCard(task: Task, onClick: () -> Unit) {
    Card(onClick = onClick, modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(
                    text = task.title,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.weight(1f),
                )
                TaskStatusBadge(task = task)
            }
            Text(
                text = task.description,
                style = MaterialTheme.typography.bodyMedium,
                maxLines = 2,
            )
            Text(
                text = "modified ${task.updatedAt}",
                style = MaterialTheme.typography.labelSmall,
            )
        }
    }
}

/**
 * Status badge for a task in the task list. Shows the task's current state as
 * reported by the server. For servers that do not report a state, it falls
 * back to "In progress" for tasks with an active agent assigned. Unknown
 * states are displayed with a human-readable label derived from the raw state
 * value, so newly introduced states still show up.
 */
@Composable
private fun TaskStatusBadge(task: Task) {
    val state = task.state ?: if (task.assigneeAgent != null) "in_progress" else null
    if (state == null) return

    val label = when (state) {
        "pending" -> "Pending"
        "in_progress" -> "In progress"
        "completed" -> "Completed"
        "failed" -> "Failed"
        else -> state.replace('_', ' ').replaceFirstChar { it.uppercase() }
    }
    val containerColor = when (state) {
        "in_progress" -> MaterialTheme.colorScheme.tertiaryContainer
        "completed" -> MaterialTheme.colorScheme.secondaryContainer
        "failed" -> MaterialTheme.colorScheme.errorContainer
        else -> MaterialTheme.colorScheme.surfaceVariant
    }

    Surface(
        shape = MaterialTheme.shapes.small,
        color = containerColor,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
        )
    }
}

@Composable
private fun CreateTaskDialog(
    initialWorkingDirectory: String?,
    readonlyDirectories: List<ReadonlyDirectory>,
    onDismiss: () -> Unit,
    onCreate: (String, String, String, Int, List<String>) -> Unit,
) {
    var title by remember { mutableStateOf("") }
    var description by remember { mutableStateOf("") }
    var workingDirectory by remember { mutableStateOf(initialWorkingDirectory.orEmpty()) }
    var tokenLimitText by remember { mutableStateOf(TaskController.DEFAULT_TOKEN_LIMIT.toString()) }
    // Ids of the readonly directories selected for the task being created.
    // Empty by default: a new task mounts no readonly directory.
    var selectedReadonlyDirectoryIds by remember { mutableStateOf(emptySet<String>()) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Create new task") },
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
                OutlinedTextField(
                    value = workingDirectory,
                    onValueChange = { workingDirectory = it },
                    label = { Text("Working directory") },
                    placeholder = { Text("/path/to/workspace") },
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = tokenLimitText,
                    onValueChange = { newValue -> tokenLimitText = newValue.filter { it.isDigit() } },
                    label = { Text("Token limit") },
                    placeholder = { Text(TaskController.DEFAULT_TOKEN_LIMIT.toString()) },
                    modifier = Modifier.fillMaxWidth(),
                )

                // Readonly-directory multi-select: one toggle row per entry of
                // the current project's catalog. Nothing is selected by
                // default, so a new task mounts no readonly directory unless
                // the user explicitly picks one or more.
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(
                        text = "Read-only directories",
                        style = MaterialTheme.typography.labelLarge,
                    )
                    if (readonlyDirectories.isEmpty()) {
                        Text(
                            text = "This project has no read-only directories.",
                            style = MaterialTheme.typography.bodySmall,
                        )
                    } else {
                        readonlyDirectories.forEach { directory ->
                            val selected = directory.id in selectedReadonlyDirectoryIds
                            ReadonlyDirectoryToggleRow(
                                directory = directory,
                                selected = selected,
                                onToggle = {
                                    selectedReadonlyDirectoryIds = if (selected) {
                                        selectedReadonlyDirectoryIds - directory.id
                                    } else {
                                        selectedReadonlyDirectoryIds + directory.id
                                    }
                                },
                            )
                        }
                    }
                }
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
                        onCreate(
                            title.trim(),
                            description.trim(),
                            workingDirectory.trim(),
                            limit,
                            // Only ids that are still part of the catalog are
                            // handed over, in catalog order.
                            readonlyDirectories
                                .map { it.id }
                                .filter { it in selectedReadonlyDirectoryIds },
                        )
                    }
                },
            ) {
                Text("Create")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

/**
 * One toggle row in the create task dialog's readonly-directory multi-select.
 * The whole row is clickable, so tapping anywhere on it toggles whether the
 * directory is associated with the task being created. The row shows the
 * catalog entry's alias and, when present, its description.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ReadonlyDirectoryToggleRow(
    directory: ReadonlyDirectory,
    selected: Boolean,
    onToggle: () -> Unit,
) {
    Surface(
        onClick = onToggle,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(8.dp),
        color = if (selected) {
            MaterialTheme.colorScheme.secondaryContainer
        } else {
            MaterialTheme.colorScheme.surfaceVariant
        },
        border = BorderStroke(
            width = 1.dp,
            color = if (selected) Color(0xFF425B9A) else Color.Gray,
        ),
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            // The row itself handles the click, so the checkbox is display
            // only (onCheckedChange = null) to avoid a double toggle.
            Checkbox(
                checked = selected,
                onCheckedChange = null,
            )
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = directory.alias,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
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
}
