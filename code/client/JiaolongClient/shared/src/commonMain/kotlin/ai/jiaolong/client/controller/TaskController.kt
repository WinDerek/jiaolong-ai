package ai.jiaolong.client.controller

import ai.jiaolong.client.model.SessionHistory
import ai.jiaolong.client.model.Task
import io.ktor.client.HttpClient
import io.ktor.client.call.body
import io.ktor.client.request.delete
import io.ktor.client.request.get
import io.ktor.client.request.parameter
import io.ktor.client.request.post
import io.ktor.client.request.put
import io.ktor.client.request.setBody
import io.ktor.client.statement.HttpResponse
import io.ktor.client.statement.bodyAsText
import io.ktor.http.isSuccess
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/**
 * Task-related business logic for the Jiaolong client. Replaces the previous
 * in-memory sample data by talking to the Jiaolong Server REST API through a
 * Ktor [HttpClient].
 *
 * @param httpClient Ktor HTTP client used to reach the server.
 */
class TaskController(
    private val httpClient: HttpClient,
) {
    companion object {
        /** Default token limit used when creating a task, applied client-side. */
        const val DEFAULT_TOKEN_LIMIT = 10_000_000
    }

    /** GET /api/tasks?projectId={projectId} - loads the tasks that belong to
     *  the given project from the server. The project id is mandatory: the
     *  server only returns the tasks of that project. */
    suspend fun listTasks(projectId: String): List<Task> {
        return httpClient.get("tasks") {
            parameter("projectId", projectId)
        }.body<List<TaskResponse>>().map { it.toTask() }
    }

    /** GET /api/tasks/search?projectId={projectId}&keywords={keywords} -
     *  searches the tasks of the given project by keywords. The server splits
     *  the keywords on whitespace and returns only the tasks whose title or
     *  description contains every keyword (case-insensitive). An empty keyword
     *  string returns every task of the project. */
    suspend fun searchTasks(projectId: String, keywords: String): List<Task> {
        return httpClient.get("tasks/search") {
            parameter("projectId", projectId)
            parameter("keywords", keywords)
        }.body<List<TaskResponse>>().map { it.toTask() }
    }

    /** GET /api/tasks/{id} - loads the latest detail of a single task from the
     *  server. */
    suspend fun getTask(id: String): Task {
        return httpClient.get("tasks/$id").body<TaskResponse>().toTask()
    }

    /** GET /api/tasks/total-token-usage - loads the total token usage of all
     *  tasks from the server. */
    suspend fun getTotalTokenUsage(): Int {
        return httpClient.get("tasks/total-token-usage")
            .body<TotalTokenUsageResponse>()
            .totalTokenUsage
    }

    /** POST /api/tasks - creates a new task under the given project and
     *  returns it. The project id is mandatory. [readonlyDirectoryIds] selects
     *  the subset of the project's readonly-directory catalog that the task
     *  should mount; it defaults to empty (no readonly directory). */
    suspend fun createTask(
        projectId: String,
        title: String,
        description: String,
        workingDirectory: String,
        tokenLimit: Int = DEFAULT_TOKEN_LIMIT,
        readonlyDirectoryIds: List<String> = emptyList(),
    ): Task {
        return httpClient.post("tasks") {
            setBody(
                CreateTaskRequest(
                    projectId = projectId,
                    title = title,
                    description = description,
                    workingDirectory = workingDirectory,
                    tokenLimit = tokenLimit,
                    readonlyDirectoryIds = readonlyDirectoryIds,
                ),
            )
        }.body<TaskResponse>().toTask()
    }

    /** PUT /api/tasks/{id} - updates a task's editable fields and returns it. */
    suspend fun updateTask(
        id: String,
        title: String,
        description: String,
        workingDirectory: String,
        tokenLimit: Int,
        state: String,
    ): Task {
        return httpClient.put("tasks/$id") {
            setBody(
                UpdateTaskRequest(
                    title = title,
                    description = description,
                    workingDirectory = workingDirectory,
                    tokenLimit = tokenLimit,
                    state = state,
                ),
            )
        }.body<TaskResponse>().toTask()
    }

    /** POST /api/tasks/{id}/work - lets the Jiaolong agent work on the task.
     *  When [resume] is true the agent resumes previous work on the task from
     *  the persisted session history instead of starting a fresh round. */
    suspend fun startWork(id: String, resume: Boolean = false): Task {
        val workUrl = if (resume) "tasks/$id/work?resume=true" else "tasks/$id/work"
        return httpClient.post(workUrl).body<TaskResponse>().toTask()
    }

    /** DELETE /api/tasks/{id} - deletes a task from the server. */
    suspend fun deleteTask(id: String) {
        httpClient.delete("tasks/$id")
    }

    /** GET /api/tasks/{id}/session-history - loads the session history JSON
     *  data of a task from the server. */
    suspend fun getSessionHistory(id: String): SessionHistory {
        return httpClient.get("tasks/$id/session-history").body<SessionHistory>()
    }

    /** POST /api/tasks/{id}/confirm-completion - confirms completion of a task
     *  that is in the needs_review state (needs_review -> completed). */
    suspend fun confirmCompletion(id: String): Task {
        return httpClient.post("tasks/$id/confirm-completion").body<TaskResponse>().toTask()
    }

    /** POST /api/tasks/{id}/init-branch - initializes a new branch for the
     *  task (runs jj git fetch, jj new main@origin and jj bookmark create
     *  task_<task_id> in the task working directory). Returns the captured
     *  command output. */
    suspend fun initBranch(id: String): String {
        return httpClient.post("tasks/$id/init-branch").vcsResponseBody()
    }

    /**
     * Parses the JSON body of an init-branch response. Success responses carry
     * a 2xx status and a {"stdout": "..."} body, while error
     * responses carry a non-2xx status and an {"error": "..."} body. The Ktor
     * client does not throw on non-2xx statuses by default, and because
     * [VcsActionResponse] only declares the optional [VcsActionResponse.stdout]
     * field, an error body would otherwise be silently deserialized as a
     * successful response and the client would report success on failure.
     */
    private suspend fun HttpResponse.vcsResponseBody(): String {
        if (!status.isSuccess()) {
            throw IllegalStateException(
                "Server returned HTTP ${status.value}: ${bodyAsText()}",
            )
        }
        return body<VcsActionResponse>().stdout.orEmpty()
    }

    @Serializable
    private data class VcsActionResponse(
        val stdout: String? = null,
    )

    @Serializable
    private data class TotalTokenUsageResponse(
        @SerialName("totalTokenUsage") val totalTokenUsage: Int,
    )

    @Serializable
    private data class CreateTaskRequest(
        @SerialName("projectId") val projectId: String,
        val title: String,
        val description: String,
        @SerialName("workingDirectory") val workingDirectory: String,
        @SerialName("tokenLimit") val tokenLimit: Int,
        @SerialName("readonlyDirectoryIds") val readonlyDirectoryIds: List<String> = emptyList(),
    )

    @Serializable
    private data class UpdateTaskRequest(
        val title: String,
        val description: String,
        @SerialName("workingDirectory") val workingDirectory: String,
        @SerialName("tokenLimit") val tokenLimit: Int,
        val state: String,
    )

    @Serializable
    private data class TaskResponse(
        val id: String,
        val title: String,
        val description: String,
        @SerialName("workingDirectory") val workingDirectory: String,
        @SerialName("tokenLimit") val tokenLimit: Int,
        @SerialName("updatedAt") val updatedAt: String,
        @SerialName("projectId") val projectId: String = "",
        val state: String? = null,
        @SerialName("agentWorking") val agentWorking: Boolean = false,
        @SerialName("totalTokenUsage") val totalTokenUsage: Int = 0,
        @SerialName("failureReason") val failureReason: String = "",
    )

    private fun TaskResponse.toTask(): Task = Task(
        id = id,
        title = title,
        description = description,
        workingDirectory = workingDirectory,
        updatedAt = updatedAt,
        projectId = projectId,
        assigneeAgent = if (agentWorking) "jiaolong-agent" else null,
        agentWorking = agentWorking,
        state = state,
        tokenLimit = tokenLimit,
        totalTokenUsage = totalTokenUsage,
        failureReason = failureReason,
    )
}
