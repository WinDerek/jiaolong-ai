package ai.jiaolong.client.controller

import ai.jiaolong.client.model.Project
import ai.jiaolong.client.model.ReadonlyDirectory
import io.ktor.client.HttpClient
import io.ktor.client.call.body
import io.ktor.client.request.get
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/**
 * Project-related business logic for the Jiaolong client. Talks to the
 * Jiaolong Server REST API through a Ktor [HttpClient] to list the available
 * projects and to create new ones.
 *
 * @param httpClient Ktor HTTP client used to reach the server.
 */
class ProjectController(
    private val httpClient: HttpClient,
) {
    /**
     * GET /api/projects - loads all projects from the server. The server
     * returns them ordered by `ordering` ascending, so the first entry is the
     * project with the least ordering value (the default/current project).
     */
    suspend fun listProjects(): List<Project> {
        return httpClient.get("projects").body<List<ProjectResponse>>().map { it.toProject() }
    }

    /** POST /api/projects - creates a new project and returns it. */
    suspend fun createProject(
        name: String,
        description: String,
        defaultTaskWorkingDirectory: String,
        ordering: Int = 0,
    ): Project {
        return httpClient.post("projects") {
            setBody(
                CreateProjectRequest(
                    name = name,
                    description = description,
                    defaultTaskWorkingDirectory = defaultTaskWorkingDirectory,
                    ordering = ordering,
                ),
            )
        }.body<ProjectResponse>().toProject()
    }

    /**
     * GET /api/projects/{id} - loads a single project's detail, including its
     * readonly-directory catalog. Backs the project detail page, which reloads
     * the project from the server every time it is entered.
     */
    suspend fun getProject(id: String): Project {
        return httpClient.get("projects/$id").body<ProjectResponse>().toProject()
    }

    /**
     * POST /api/projects/{id}/readonly-directories - appends a new readonly
     * directory to the project's catalog and returns the updated project.
     */
    suspend fun addReadonlyDirectory(
        projectId: String,
        alias: String,
        realPath: String,
        description: String = "",
    ): Project {
        return httpClient.post("projects/$projectId/readonly-directories") {
            setBody(
                AddReadonlyDirectoryRequest(
                    alias = alias,
                    realPath = realPath,
                    description = description,
                ),
            )
        }.body<ProjectResponse>().toProject()
    }

    @Serializable
    private data class CreateProjectRequest(
        val name: String,
        val description: String,
        @SerialName("defaultTaskWorkingDirectory") val defaultTaskWorkingDirectory: String,
        val ordering: Int,
    )

    @Serializable
    private data class AddReadonlyDirectoryRequest(
        val alias: String,
        val realPath: String,
        val description: String,
    )

    @Serializable
    private data class ReadonlyDirectoryResponse(
        val id: String = "",
        val alias: String,
        @SerialName("realPath") val realPath: String,
        val description: String = "",
    )

    @Serializable
    private data class ProjectResponse(
        val id: String,
        val name: String,
        val description: String = "",
        @SerialName("defaultTaskWorkingDirectory") val defaultTaskWorkingDirectory: String = "",
        val ordering: Int = 0,
        @SerialName("readonlyDirectories")
        val readonlyDirectories: List<ReadonlyDirectoryResponse> = emptyList(),
    )

    private fun ProjectResponse.toProject(): Project = Project(
        id = id,
        name = name,
        description = description,
        defaultTaskWorkingDirectory = defaultTaskWorkingDirectory,
        ordering = ordering,
        readonlyDirectories = readonlyDirectories.map { it.toReadonlyDirectory() },
    )

    private fun ReadonlyDirectoryResponse.toReadonlyDirectory(): ReadonlyDirectory =
        ReadonlyDirectory(
            id = id,
            alias = alias,
            realPath = realPath,
            description = description,
        )
}
