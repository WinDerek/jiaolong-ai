package ai.jiaolong.client

import ai.jiaolong.client.auth.AuthController
import ai.jiaolong.client.auth.storedAccessToken
import ai.jiaolong.client.controller.ProjectController
import ai.jiaolong.client.controller.SettingsController
import ai.jiaolong.client.controller.SystemHealthController
import ai.jiaolong.client.controller.TaskController
import ai.jiaolong.client.data.SampleData
import ai.jiaolong.client.model.CommitComment
import ai.jiaolong.client.model.LlmProvider
import ai.jiaolong.client.model.Project
import ai.jiaolong.client.model.SessionHistory
import ai.jiaolong.client.model.SystemHealthFrame
import ai.jiaolong.client.model.Task
import ai.jiaolong.client.platform.currentTimeMillis
import ai.jiaolong.client.settings.ProjectSettings
import ai.jiaolong.client.settings.ServerSettings
import ai.jiaolong.client.storage.createJiaolongClientStorage
import ai.jiaolong.client.ui.CommitDetailPage
import ai.jiaolong.client.ui.DashboardPage
import ai.jiaolong.client.ui.LoginPage
import ai.jiaolong.client.ui.OperationResult
import ai.jiaolong.client.ui.ProjectDetailPage
import ai.jiaolong.client.ui.ServerMonitorPage
import ai.jiaolong.client.ui.SessionHistoryPage
import ai.jiaolong.client.ui.SettingsPage
import ai.jiaolong.client.ui.TaskDetailPage
import ai.jiaolong.client.util.createHttpClient
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.toMutableStateList
import androidx.compose.ui.input.key.KeyEvent
import androidx.compose.ui.tooling.preview.Preview
import coil3.ImageLoader
import coil3.compose.setSingletonImageLoaderFactory
import kotlinx.coroutines.launch

/** Screens of the Jiaolong Client UI, navigated with a simple back stack. */
private sealed interface Screen {
    data object Dashboard : Screen
    data object Settings : Screen
    data object ServerMonitor : Screen
    data class ProjectDetail(val projectId: String) : Screen
    data class TaskDetail(val taskId: String) : Screen
    data class SessionHistory(val taskId: String) : Screen
    data class CommitDetail(val commitId: String) : Screen
}

/**
 * Entry point of the Jiaolong Client UI. Task data is loaded from and written
 * back to the Jiaolong Server through [TaskController]; commit data is still
 * provided by sample data until the corresponding server endpoints are ready.
 *
 * The server base URL is read from local storage on boot (seeding local storage
 * with the default value on first run) and is used to create the HTTP client.
 * When the user changes it on the settings page, the HTTP client is re-created
 * with the new base URL.
 */
@Composable
@Preview
fun App(
    refreshShortcutEvent: KeyEvent? = null,
    onRefreshShortcutHandled: () -> Unit = {},
    onRegisterBackHandler: ((State<Boolean>, () -> Unit) -> Unit)? = null,
) {
    // Set up Coil's singleton ImageLoader once so AsyncImage can load the
    // Jiaolong logo on every platform (Android configures a default
    // automatically; other targets require an explicit factory).
    setSingletonImageLoaderFactory { context ->
        ImageLoader.Builder(context).build()
    }

    val scope = rememberCoroutineScope()
    val storage = remember { createJiaolongClientStorage() }
    val serverSettings = remember { ServerSettings(storage) }
    val projectSettings = remember { ProjectSettings(storage) }
    var serverBaseUrl by remember { mutableStateOf(serverSettings.serverBaseUrl()) }
    // The HTTP client reads the current OAuth 2.0 access token from local
    // storage for every request, so every API call automatically carries the
    // `Authorization: Bearer <token>` header once the user has logged in.
    val httpClient = remember(serverBaseUrl) {
        createHttpClient(serverBaseUrl) { storedAccessToken(storage) }
    }
    val authController = remember(serverBaseUrl) { AuthController(httpClient, storage) }
    val taskController = remember(serverBaseUrl) { TaskController(httpClient) }
    val projectController = remember(serverBaseUrl) { ProjectController(httpClient) }
    val settingsController = remember(serverBaseUrl) { SettingsController(httpClient) }
    val systemHealthController = remember(serverBaseUrl) { SystemHealthController(httpClient) }
    val tasks = remember { mutableStateListOf<Task>() }
    // Total token usage of all tasks, loaded together with the task list from
    // the server and shown in the dashboard statistics row.
    var totalTokenUsage by remember { mutableStateOf(0) }
    // Keywords currently applied to the dashboard task list. Empty means no
    // search filter (the full project task list is shown).
    var searchKeywords by remember { mutableStateOf("") }
    // Projects loaded from the server. The project switcher on the dashboard
    // lists them and lets the user pick the current project. By default the
    // current project is the first ordered project (least `ordering` value).
    var projects by remember { mutableStateOf<List<Project>>(emptyList()) }
    var currentProject by remember { mutableStateOf<Project?>(null) }
    var isProjectsLoading by remember { mutableStateOf(false) }
    var projectsErrorMessage by remember { mutableStateOf<String?>(null) }
    // Project detail state: the project detail page reloads the newest project
    // data (including its readonly-directory catalog) from the server every
    // time it is entered.
    var projectDetail by remember { mutableStateOf<Project?>(null) }
    var isProjectDetailLoading by remember { mutableStateOf(false) }
    var projectDetailLoadErrorMessage by remember { mutableStateOf<String?>(null) }
    var isAddReadonlyDirectoryPending by remember { mutableStateOf(false) }
    var addReadonlyDirectoryResult by remember { mutableStateOf<OperationResult?>(null) }
    val commits = remember { SampleData.commits.toMutableStateList() }
    val backStack = remember { mutableStateListOf<Screen>(Screen.Dashboard) }
    // Whether there is a screen below the current one that the app back stack
    // can pop. Exposed to platform hosts (e.g. Android) so they can connect the
    // system back button to the in-app navigation.
    val canGoBack = remember { derivedStateOf { backStack.size > 1 } }
    var isRefreshing by remember { mutableStateOf(false) }
    var errorMessage by remember { mutableStateOf<String?>(null) }
    var loadErrorMessage by remember { mutableStateOf<String?>(null) }
    var refreshMessage by remember { mutableStateOf<String?>(null) }
    var isInitBranchAndStartWorkPending by remember { mutableStateOf(false) }
    var initBranchAndStartWorkResult by remember { mutableStateOf<OperationResult?>(null) }
    // Task detail load state: the task detail page reloads the newest task
    // data from the server every time it is entered.
    var isTaskDetailLoading by remember { mutableStateOf(false) }
    var taskDetailLoadErrorMessage by remember { mutableStateOf<String?>(null) }
    var sessionHistory by remember { mutableStateOf<SessionHistory?>(null) }
    var isSessionHistoryLoading by remember { mutableStateOf(false) }
    var sessionHistoryErrorMessage by remember { mutableStateOf<String?>(null) }
    var isStopServerPending by remember { mutableStateOf(false) }
    var stopServerResult by remember { mutableStateOf<OperationResult?>(null) }
    // Server monitor state: the latest system health frames returned by the
    // `GET /api/server/system-health` endpoint, loaded when the server monitor
    // page is opened and refreshed periodically while it is visible.
    var systemHealthFrames by remember { mutableStateOf<List<SystemHealthFrame>>(emptyList()) }
    var isSystemHealthLoading by remember { mutableStateOf(false) }
    var systemHealthErrorMessage by remember { mutableStateOf<String?>(null) }
    // Authentication state: the client shows the login page until it has valid
    // credentials (a refresh token that is not expired). Credentials are valid
    // for one week; after that the user must log in again.
    var isLoggedIn by remember { mutableStateOf(authController.hasValidCredentials()) }
    var isLoggingIn by remember { mutableStateOf(false) }
    var loginErrorMessage by remember { mutableStateOf<String?>(null) }
    var nextCommitCommentId by remember { mutableStateOf(SampleData.commits.sumOf { it.comments.size } + 1) }
    var timeCounter by remember { mutableStateOf(1_700_000_000_000L) }
    var llmProviders by remember { mutableStateOf<List<LlmProvider>>(emptyList()) }
    var enabledLlmProviderIndex by remember { mutableStateOf<Int?>(null) }
    var isLlmProvidersLoading by remember { mutableStateOf(false) }
    var llmProvidersErrorMessage by remember { mutableStateOf<String?>(null) }

    /**
     * Ensures the client has valid credentials and a fresh access token before
     * talking to the server. Returns false (and switches to the login page)
     * when the credentials are missing or expired, or the refresh failed.
     */
    suspend fun ensureAuthenticated(): Boolean {
        if (!authController.hasValidCredentials()) {
            isLoggedIn = false
            return false
        }
        if (authController.ensureAccessToken() == null) {
            isLoggedIn = false
            return false
        }
        return true
    }

    fun handleLogout() {
        authController.logout()
        isLoggedIn = false
        backStack.clear()
        backStack.add(Screen.Dashboard)
        // Drop the previously loaded project state so a fresh login reloads it
        // from the (possibly different) server.
        projects = emptyList()
        currentProject = null
        projectsErrorMessage = null
        // Drop any active task search so the next login starts from the full
        // task list.
        searchKeywords = ""
    }

    fun refresh() {
        isRefreshing = true
        scope.launch {
            if (!ensureAuthenticated()) {
                isRefreshing = false
                return@launch
            }
            try {
                // The task list API is scoped to a project, so make sure the
                // project list has been loaded and a current project is known
                // before requesting the tasks.
                if (currentProject == null) {
                    val loadedProjects = projectController.listProjects()
                    projects = loadedProjects
                    // Restore the project the user last selected (persisted in
                    // local storage), falling back to the first ordered
                    // project when it no longer exists.
                    currentProject = loadedProjects
                        .firstOrNull { it.id == projectSettings.selectedProjectId() }
                        ?: loadedProjects.firstOrNull()
                }
                val projectId = currentProject?.id
                if (projectId == null) {
                    tasks.clear()
                    totalTokenUsage = 0
                    loadErrorMessage = null
                    refreshMessage = null
                    errorMessage = "Create a project before listing its tasks."
                    return@launch
                }
                // Apply the active keyword search (if any) so a refresh keeps the
                // same filter the user submitted.
                val loadedTasks = if (searchKeywords.isBlank()) {
                    taskController.listTasks(projectId)
                } else {
                    taskController.searchTasks(projectId, searchKeywords)
                }
                val loadedTotalTokenUsage = taskController.getTotalTokenUsage()
                tasks.clear()
                tasks.addAll(loadedTasks)
                totalTokenUsage = loadedTotalTokenUsage
                errorMessage = null
                loadErrorMessage = null
                refreshMessage = "Tasks refreshed successfully"
            } catch (e: Exception) {
                // Keep the current list and tell the user that loading failed.
                // This is a persistent, non-dismissable indicator on the dashboard,
                // separate from the dismissable operation error messages.
                loadErrorMessage = "Failed to load the latest data. Showing previously loaded data."
                refreshMessage = "Failed to refresh tasks: ${e.message ?: "unknown error"}"
            } finally {
                isRefreshing = false
            }
        }
    }

    /**
     * Applies a keyword search to the dashboard task list. An empty keyword
     * string clears the search and reloads the full task list. The active
     * keywords are remembered so subsequent refreshes keep the same filter.
     */
    fun searchTasks(keywords: String) {
        searchKeywords = keywords.trim()
        refresh()
    }

    // Applies a freshly loaded project list and keeps the current project in
    // sync: it stays selected while it still exists, otherwise the first
    // ordered project (the one with the least `ordering` value) becomes the
    // current project.
    fun applyProjects(loaded: List<Project>) {
        projects = loaded
        val currentId = currentProject?.id
        // Keep the current project while it still exists, otherwise restore
        // the project the user last selected (persisted in local storage),
        // falling back to the first ordered project.
        currentProject = loaded.firstOrNull { it.id == currentId }
            ?: loaded.firstOrNull { it.id == projectSettings.selectedProjectId() }
            ?: loaded.firstOrNull()
    }

    /**
     * Loads the list of projects from the server. Called when the dashboard
     * page is entered and every time the user taps the project switcher, so
     * the client always shows the latest server-side project list.
     */
    fun loadProjects() {
        if (isProjectsLoading) return
        isProjectsLoading = true
        scope.launch {
            if (!ensureAuthenticated()) {
                isProjectsLoading = false
                return@launch
            }
            try {
                applyProjects(projectController.listProjects())
                projectsErrorMessage = null
            } catch (e: Exception) {
                // Keep the previously loaded projects (if any) and tell the
                // user that the latest refresh failed.
                projectsErrorMessage =
                    "Failed to load projects: ${e.message ?: "unknown error"}"
            } finally {
                isProjectsLoading = false
            }
        }
    }

    /**
     * Creates a project from the dashboard and reloads the project list from
     * the server so the dashboard reflects the canonical server-side state.
     */
    fun createProject(
        name: String,
        description: String,
        defaultTaskWorkingDirectory: String,
        ordering: Int,
    ) {
        if (isProjectsLoading) return
        isProjectsLoading = true
        scope.launch {
            if (!ensureAuthenticated()) {
                isProjectsLoading = false
                return@launch
            }
            try {
                projectController.createProject(
                    name,
                    description,
                    defaultTaskWorkingDirectory,
                    ordering,
                )
                applyProjects(projectController.listProjects())
                errorMessage = null
            } catch (e: Exception) {
                // Tell the user that the project could not be created. The
                // create dialog is already closed, so surface the failure in
                // the dashboard's dismissable error banner.
                errorMessage =
                    "Failed to create project: ${e.message ?: "unknown error"}"
            } finally {
                isProjectsLoading = false
            }
        }
    }

    // Replaces the given project inside the loaded project list (and the
    // current project when it is the same) so screens that read from the list
    // stay in sync with the freshest server data.
    fun applyProjectUpdate(updated: Project) {
        val index = projects.indexOfFirst { it.id == updated.id }
        if (index >= 0) {
            projects = projects.toMutableList().also { it[index] = updated }
        }
        if (currentProject?.id == updated.id) {
            currentProject = updated
        }
    }

    /**
     * Loads the latest detail of a single project (including its
     * readonly-directory catalog) from the server. Called every time the
     * project detail page is entered so the page never renders stale dashboard
     * data.
     */
    fun loadProjectDetail(projectId: String) {
        if (isProjectDetailLoading) return
        isProjectDetailLoading = true
        scope.launch {
            if (!ensureAuthenticated()) {
                isProjectDetailLoading = false
                return@launch
            }
            try {
                val loaded = projectController.getProject(projectId)
                projectDetail = loaded
                applyProjectUpdate(loaded)
                projectDetailLoadErrorMessage = null
            } catch (e: Exception) {
                // Keep showing the previously loaded project (if any) and tell
                // the user that the latest detail could not be loaded.
                projectDetailLoadErrorMessage =
                    "Failed to load the latest project detail: ${e.message ?: "unknown error"}"
            } finally {
                isProjectDetailLoading = false
            }
        }
    }

    /**
     * Adds a readonly directory to the current project, then refreshes the
     * project detail from the server so the page reflects the canonical
     * server-side catalog.
     */
    fun addReadonlyDirectory(
        projectId: String,
        alias: String,
        realPath: String,
        description: String,
    ) {
        if (isAddReadonlyDirectoryPending) return
        isAddReadonlyDirectoryPending = true
        addReadonlyDirectoryResult = null
        scope.launch {
            if (!ensureAuthenticated()) {
                isAddReadonlyDirectoryPending = false
                return@launch
            }
            try {
                val updated = projectController.addReadonlyDirectory(
                    projectId,
                    alias,
                    realPath,
                    description,
                )
                projectDetail = updated
                applyProjectUpdate(updated)
                addReadonlyDirectoryResult = OperationResult(
                    message = "Read-only directory added successfully.",
                    isError = false,
                )
            } catch (e: Exception) {
                // Show the failure result on the project detail page.
                addReadonlyDirectoryResult = OperationResult(
                    message = "Failed to add read-only directory: ${e.message ?: "unknown error"}",
                    isError = true,
                )
            } finally {
                isAddReadonlyDirectoryPending = false
            }
        }
    }

    fun openProjectDetail(projectId: String) {
        // Clear previous state so the page always opens fresh and shows the
        // loading indicator for the initial request.
        projectDetail = null
        projectDetailLoadErrorMessage = null
        addReadonlyDirectoryResult = null
        backStack.add(Screen.ProjectDetail(projectId))
    }

    /**
     * Loads the latest detail of a single task from the server. Called every
     * time the task detail page is entered so the page always shows the newest
     * server-side task data instead of the copy collected on the dashboard.
     */
    fun loadTaskDetail(taskId: String) {
        if (isTaskDetailLoading) return
        isTaskDetailLoading = true
        taskDetailLoadErrorMessage = null
        scope.launch {
            if (!ensureAuthenticated()) {
                isTaskDetailLoading = false
                return@launch
            }
            try {
                val loaded = taskController.getTask(taskId)
                val index = tasks.indexOfFirst { it.id == taskId }
                if (index >= 0) {
                    // Keep the comments posted locally: the server does not
                    // return comments as part of the task detail yet.
                    tasks[index] = loaded.copy(comments = tasks[index].comments)
                } else {
                    tasks.add(loaded)
                }
                taskDetailLoadErrorMessage = null
            } catch (e: Exception) {
                // Keep showing the previously loaded task and tell the user
                // that the latest detail could not be loaded.
                taskDetailLoadErrorMessage =
                    "Failed to load the latest task detail: ${e.message ?: "unknown error"}"
            } finally {
                isTaskDetailLoading = false
            }
        }
    }

    fun handleLogin(username: String, password: String) {
        if (isLoggingIn) return
        isLoggingIn = true
        loginErrorMessage = null
        scope.launch {
            val success = authController.login(username, password)
            isLoggingIn = false
            if (success) {
                isLoggedIn = true
                refresh()
            } else {
                loginErrorMessage = "Sign in failed. Check your username and password."
            }
        }
    }

    fun goBack() {
        if (backStack.size > 1) {
            backStack.removeAt(backStack.lastIndex)
            // Reload the task list and the project list from the server
            // whenever the user returns to the dashboard page, so the
            // dashboard always reflects the canonical server-side state
            // instead of locally cached data.
            if (backStack.lastOrNull() is Screen.Dashboard) {
                refresh()
                loadProjects()
            }
        }
    }

    fun saveServerBaseUrl(newBaseUrl: String) {
        val normalized = newBaseUrl.trim().let {
            if (it.isNotEmpty() && !it.endsWith("/")) "$it/" else it
        }
        serverSettings.setServerBaseUrl(normalized)
        serverBaseUrl = normalized
        goBack()
    }

    fun loadLlmProviders() {
        isLlmProvidersLoading = true
        scope.launch {
            if (!ensureAuthenticated()) {
                isLlmProvidersLoading = false
                return@launch
            }
            try {
                val data = settingsController.listLlmProviders()
                llmProviders = data.llmProviders
                enabledLlmProviderIndex = data.enabledLlmProviderIndex
                llmProvidersErrorMessage = null
            } catch (e: Exception) {
                // Keep the previously loaded providers (if any) and tell the
                // user that loading failed.
                llmProvidersErrorMessage =
                    "Failed to load LLM providers: ${e.message ?: "unknown error"}"
            } finally {
                isLlmProvidersLoading = false
            }
        }
    }

    fun selectLlmProvider(index: Int) {
        scope.launch {
            if (!ensureAuthenticated()) return@launch
            try {
                val data = settingsController.setEnabledLlmProvider(index)
                llmProviders = data.llmProviders
                enabledLlmProviderIndex = data.enabledLlmProviderIndex
                llmProvidersErrorMessage = null
            } catch (e: Exception) {
                llmProvidersErrorMessage =
                    "Failed to set LLM provider: ${e.message ?: "unknown error"}"
            }
        }
    }

    fun stopServer() {
        if (isStopServerPending) return
        isStopServerPending = true
        stopServerResult = null
        scope.launch {
            if (!ensureAuthenticated()) return@launch
            try {
                settingsController.stopServer()
                errorMessage = null
                stopServerResult = OperationResult(
                    message = "Jiaolong Server is stopping.",
                    isError = false,
                )
            } catch (e: Exception) {
                // Show the failure result on the settings page.
                stopServerResult = OperationResult(
                    message = "Failed to stop server: ${e.message ?: "unknown error"}",
                    isError = true,
                )
            } finally {
                isStopServerPending = false
            }
        }
    }

    fun loadSystemHealthData() {
        if (isSystemHealthLoading) return
        isSystemHealthLoading = true
        scope.launch {
            if (!ensureAuthenticated()) {
                isSystemHealthLoading = false
                return@launch
            }
            try {
                systemHealthFrames = systemHealthController.getSystemHealthData()
                systemHealthErrorMessage = null
            } catch (e: Exception) {
                // Keep the previously loaded frames (if any) and tell the user
                // that the latest refresh failed.
                systemHealthErrorMessage =
                    "Failed to load server system health data: ${e.message ?: "unknown error"}"
            } finally {
                isSystemHealthLoading = false
            }
        }
    }

    fun openServerMonitor() {
        // Clear previous state so the page always opens fresh and shows the
        // loading indicator for the initial request.
        systemHealthFrames = emptyList()
        systemHealthErrorMessage = null
        backStack.add(Screen.ServerMonitor)
        loadSystemHealthData()
    }

    fun createTask(
        title: String,
        description: String,
        workingDirectory: String,
        tokenLimit: Int,
        readonlyDirectoryIds: List<String>,
    ) {
        // Every task is created under the current project, so a project must
        // be selected before the create request can be sent.
        val projectId = currentProject?.id
        if (projectId == null) {
            errorMessage = "Select a project before creating a task."
            return
        }
        // Show the dashboard loading animation while the create request and the
        // follow-up task list reload are in flight.
        isRefreshing = true
        scope.launch {
            try {
                if (!ensureAuthenticated()) {
                    isRefreshing = false
                    return@launch
                }
                taskController.createTask(
                    projectId,
                    title,
                    description,
                    workingDirectory,
                    tokenLimit,
                    readonlyDirectoryIds,
                )

                // The server updates the associated project's default task
                // working directory to the one used for this task, so reload
                // the project list to keep the task creation form's default
                // value in sync with the server.
                loadProjects()

                // Reload the task list from the server after creation so the
                // dashboard always reflects the canonical, server-side state
                // instead of just appending the newly created task locally.
                // refresh() manages the isRefreshing flag from here on; the
                // flag stays up until the reload has finished.
                refresh()
            } catch (e: Exception) {
                // Tell the user that the task could not be created.
                isRefreshing = false
                errorMessage = "Failed to create task: ${e.message ?: "unknown error"}"
            }
        }
    }

    fun editTask(
        taskId: String,
        title: String,
        description: String,
        workingDirectory: String,
        tokenLimit: Int,
        state: String,
    ) {
        scope.launch {
            if (!ensureAuthenticated()) return@launch
            try {
                val updated = taskController.updateTask(
                    taskId,
                    title,
                    description,
                    workingDirectory,
                    tokenLimit,
                    state,
                )
                val index = tasks.indexOfFirst { it.id == taskId }
                if (index >= 0) {
                    tasks[index] = updated
                }
                errorMessage = null
            } catch (e: Exception) {
                // Tell the user that the task could not be updated.
                errorMessage = "Failed to update task: ${e.message ?: "unknown error"}"
            }
        }
    }

    /**
     * Initializes the task branch first and, only when that succeeded, lets
     * the agent start working on the task. The two requests are chained and
     * fail fast: when init branch fails the agent is not started. The
     * wall-clock duration from the click to the moment the agent started is
     * measured and reported together with the outcome.
     */
    fun initBranchAndStartWork(taskId: String) {
        if (isInitBranchAndStartWorkPending) return
        isInitBranchAndStartWorkPending = true
        initBranchAndStartWorkResult = null
        // Captured synchronously on click so the reported duration covers the
        // whole chained operation.
        val startedAt = currentTimeMillis()
        scope.launch {
            if (!ensureAuthenticated()) {
                isInitBranchAndStartWorkPending = false
                return@launch
            }
            try {
                // First request: initialize the branch. When it fails, stop
                // here (fail fast) so the agent is never started.
                try {
                    taskController.initBranch(taskId)
                } catch (e: Exception) {
                    initBranchAndStartWorkResult = OperationResult(
                        message = "Failed to init branch: ${e.message ?: "unknown error"}",
                        isError = true,
                    )
                    return@launch
                }
                // Second request: let the agent start working. Only reached
                // when the branch was initialized successfully.
                val updated = taskController.startWork(taskId)
                val index = tasks.indexOfFirst { it.id == taskId }
                if (index >= 0) {
                    tasks[index] = updated
                }
                errorMessage = null
                initBranchAndStartWorkResult = OperationResult(
                    message = "Branch initialized and agent started successfully",
                    isError = false,
                    durationMillis = currentTimeMillis() - startedAt,
                )
            } catch (e: Exception) {
                // The branch was initialized but the agent could not be
                // started.
                initBranchAndStartWorkResult = OperationResult(
                    message = "Failed to start working: ${e.message ?: "unknown error"}",
                    isError = true,
                )
            } finally {
                isInitBranchAndStartWorkPending = false
            }
        }
    }

    // Resumes a failed task: asks the server to let the agent work on the task
    // again, this time resuming from the persisted session history instead of
    // starting a fresh round.
    fun resumeTask(taskId: String) {
        scope.launch {
            if (!ensureAuthenticated()) return@launch
            try {
                val updated = taskController.startWork(taskId, resume = true)
                val index = tasks.indexOfFirst { it.id == taskId }
                if (index >= 0) {
                    tasks[index] = updated
                }
                errorMessage = null
            } catch (e: Exception) {
                // Tell the user that the agent could not be resumed.
                errorMessage = "Failed to resume task: ${e.message ?: "unknown error"}"
            }
        }
    }

    fun confirmCompletion(taskId: String) {
        scope.launch {
            if (!ensureAuthenticated()) return@launch
            try {
                val updated = taskController.confirmCompletion(taskId)
                val index = tasks.indexOfFirst { it.id == taskId }
                if (index >= 0) {
                    tasks[index] = updated
                }
                errorMessage = null
            } catch (e: Exception) {
                // Tell the user that the task could not be confirmed as completed.
                errorMessage = "Failed to confirm completion: ${e.message ?: "unknown error"}"
            }
        }
    }

    fun loadSessionHistory(taskId: String) {
        if (isSessionHistoryLoading) return
        isSessionHistoryLoading = true
        scope.launch {
            if (!ensureAuthenticated()) {
                isSessionHistoryLoading = false
                return@launch
            }
            try {
                sessionHistory = taskController.getSessionHistory(taskId)
                sessionHistoryErrorMessage = null
            } catch (e: Exception) {
                // Tell the user that the session history could not be loaded.
                sessionHistoryErrorMessage =
                    "Failed to load session history: ${e.message ?: "unknown error"}"
            } finally {
                isSessionHistoryLoading = false
            }
        }
    }

    fun openSessionHistory(taskId: String) {
        // Clear previous session history state so the page always opens fresh
        // and shows the loading indicator for the new task.
        sessionHistory = null
        sessionHistoryErrorMessage = null
        backStack.add(Screen.SessionHistory(taskId))
        loadSessionHistory(taskId)
    }

    fun deleteTask(taskId: String) {
        scope.launch {
            if (!ensureAuthenticated()) return@launch
            try {
                taskController.deleteTask(taskId)
                tasks.removeAll { it.id == taskId }
                // Leave the task detail page when the open task has been deleted.
                val currentScreen = backStack.lastOrNull()
                if (currentScreen is Screen.TaskDetail && currentScreen.taskId == taskId) {
                    goBack()
                }
                errorMessage = null
            } catch (e: Exception) {
                // Tell the user that the task could not be deleted.
                errorMessage = "Failed to delete task: ${e.message ?: "unknown error"}"
            }
        }
    }

    fun postCommitComment(commitId: String, content: String, lineNumber: Int?) {
        val index = commits.indexOfFirst { it.id == commitId }
        if (index >= 0) {
            val commit = commits[index]
            commits[index] = commit.copy(
                comments = commit.comments + CommitComment(
                    id = "commit-comment-${nextCommitCommentId++}",
                    author = "alice",
                    content = content,
                    createdAt = ++timeCounter,
                    lineNumber = lineNumber,
                ),
            )
        }
    }

    fun approveCommit(commitId: String) {
        val index = commits.indexOfFirst { it.id == commitId }
        if (index >= 0) {
            commits[index] = commits[index].copy(approved = true)
        }
    }

    // Load tasks on boot (only when the user is logged in) and reload them
    // whenever the server base URL or the login state changes so the dashboard
    // always targets the currently configured server with valid credentials.
    LaunchedEffect(serverBaseUrl, isLoggedIn) {
        if (isLoggedIn) {
            refresh()
            loadProjects()
        }
    }

    LaunchedEffect(Unit) {
        onRegisterBackHandler?.invoke(canGoBack, ::goBack)
    }

    // The Ctrl+R key event captured in main.kt is forwarded to the dashboard
    // page, which is the only screen that handles it. When the event arrives
    // while a different screen is being shown, consume it here so it is not
    // replayed (as an unexpected refresh) when the user later navigates back to
    // the dashboard.
    LaunchedEffect(refreshShortcutEvent, backStack.last()) {
        if (refreshShortcutEvent != null && backStack.last() !is Screen.Dashboard) {
            onRefreshShortcutHandled()
        }
    }

    // Without valid credentials the user is shown the login page; all other
    // screens are only reachable after a successful login.
    if (!isLoggedIn) {
        MaterialTheme {
            LoginPage(
                isLoggingIn = isLoggingIn,
                errorMessage = loginErrorMessage,
                onLogin = { username, password -> handleLogin(username, password) },
            )
        }
        return
    }

    MaterialTheme {
        when (val screen = backStack.last()) {
        is Screen.Dashboard -> DashboardPage(
            tasks = tasks,
            totalTokenUsage = totalTokenUsage,
            searchKeywords = searchKeywords,
            onSearch = { keywords -> searchTasks(keywords) },
            isRefreshing = isRefreshing,
            errorMessage = errorMessage,
            onDismissError = { errorMessage = null },
            loadErrorMessage = loadErrorMessage,
            refreshMessage = refreshMessage,
            onDismissRefreshMessage = { refreshMessage = null },
            onRefresh = { refresh() },
            refreshShortcutEvent = refreshShortcutEvent,
            onRefreshShortcutHandled = onRefreshShortcutHandled,
            currentProject = currentProject,
            projects = projects,
            isProjectsLoading = isProjectsLoading,
            projectsErrorMessage = projectsErrorMessage,
            onRefreshProjects = { loadProjects() },
            onSelectProject = { project ->
                currentProject = project
                // Remember the selected project id in local storage so the
                // next launch restores the same project.
                projectSettings.setSelectedProjectId(project.id)
                // The task list is scoped to the current project, so reload
                // the tasks of the newly selected project.
                refresh()
            },
            onCreateProject = { name, description, defaultTaskWorkingDirectory, ordering ->
                createProject(name, description, defaultTaskWorkingDirectory, ordering)
            },
            defaultTaskWorkingDirectory = currentProject?.defaultTaskWorkingDirectory,
            onCreateTask = { title, description, workingDirectory, tokenLimit, readonlyDirectoryIds ->
                createTask(title, description, workingDirectory, tokenLimit, readonlyDirectoryIds)
            },
            onOpenTask = { taskId ->
                // Clear the previous init branch & start working result so a
                // task detail page always opens without a stale operation
                // result.
                initBranchAndStartWorkResult = null
                taskDetailLoadErrorMessage = null
                backStack.add(Screen.TaskDetail(taskId))
            },
            onOpenSettings = {
                // Clear previous stop server result so the settings page
                // always opens without a stale operation result.
                stopServerResult = null
                backStack.add(Screen.Settings)
                loadLlmProviders()
            },
            onOpenServerMonitor = { openServerMonitor() },
            onOpenProjectDetail = {
                currentProject?.id?.let { openProjectDetail(it) }
            },
        )

        is Screen.ProjectDetail -> {
            // Reload the newest project detail from the server every time the
            // page is entered so it never renders stale dashboard data.
            LaunchedEffect(screen.projectId) {
                loadProjectDetail(screen.projectId)
            }
            ProjectDetailPage(
                project = projectDetail?.takeIf { it.id == screen.projectId },
                isLoading = isProjectDetailLoading,
                loadErrorMessage = projectDetailLoadErrorMessage,
                addReadonlyDirectoryResult = addReadonlyDirectoryResult,
                isAddReadonlyDirectoryPending = isAddReadonlyDirectoryPending,
                onBack = { goBack() },
                onRefresh = { loadProjectDetail(screen.projectId) },
                onAddReadonlyDirectory = { alias, realPath, description ->
                    addReadonlyDirectory(screen.projectId, alias, realPath, description)
                },
            )
        }

        is Screen.ServerMonitor -> ServerMonitorPage(
            frames = systemHealthFrames,
            isLoading = isSystemHealthLoading,
            errorMessage = systemHealthErrorMessage,
            onBack = { goBack() },
            onRefresh = { loadSystemHealthData() },
        )

        is Screen.Settings -> SettingsPage(
            serverBaseUrl = serverBaseUrl,
            llmProviders = llmProviders,
            enabledLlmProviderIndex = enabledLlmProviderIndex,
            isLlmProvidersLoading = isLlmProvidersLoading,
            llmProvidersErrorMessage = llmProvidersErrorMessage,
            onBack = { goBack() },
            onSave = { saveServerBaseUrl(it) },
            onSelectLlmProvider = { selectLlmProvider(it) },
            onRetryLoadLlmProviders = { loadLlmProviders() },
            isStopServerPending = isStopServerPending,
            stopServerResult = stopServerResult,
            onStopServer = { stopServer() },
            onLogout = { handleLogout() },
        )

        is Screen.TaskDetail -> {
            // Reload the newest task detail from the server every time the
            // page is entered (including when returning from a sub-page), so
            // the page never renders stale dashboard data.
            LaunchedEffect(screen.taskId) {
                loadTaskDetail(screen.taskId)
            }
            val task = tasks.find { it.id == screen.taskId }
            if (task != null) {
                TaskDetailPage(
                    task = task,
                    errorMessage = errorMessage,
                    onDismissError = { errorMessage = null },
                    onBack = { goBack() },
                    onRefresh = { loadTaskDetail(task.id) },
                    onEditTask = { title, description, workingDirectory, tokenLimit, state ->
                        editTask(task.id, title, description, workingDirectory, tokenLimit, state)
                    },
                    onInitBranchAndStartWork = { initBranchAndStartWork(task.id) },
                    onResumeTask = { resumeTask(task.id) },
                    onViewSessionHistory = { openSessionHistory(task.id) },
                    isInitBranchAndStartWorkPending = isInitBranchAndStartWorkPending,
                    initBranchAndStartWorkResult = initBranchAndStartWorkResult,
                    onConfirmCompletion = { confirmCompletion(task.id) },
                    onDeleteTask = { deleteTask(task.id) },
                    isLoading = isTaskDetailLoading,
                    loadErrorMessage = taskDetailLoadErrorMessage,
                )
            }
        }

        is Screen.SessionHistory -> SessionHistoryPage(
            taskId = screen.taskId,
            sessionHistory = sessionHistory,
            isLoading = isSessionHistoryLoading,
            errorMessage = sessionHistoryErrorMessage,
            onBack = { goBack() },
            onRetry = { loadSessionHistory(screen.taskId) },
        )

        is Screen.CommitDetail -> {
            val commit = commits.find { it.id == screen.commitId }
            if (commit != null) {
                CommitDetailPage(
                    commit = commit,
                    onBack = { goBack() },
                    onPostCommitComment = { content, line -> postCommitComment(commit.id, content, line) },
                    onApprove = { approveCommit(commit.id) },
                )
            }
        }
        }
    }
}