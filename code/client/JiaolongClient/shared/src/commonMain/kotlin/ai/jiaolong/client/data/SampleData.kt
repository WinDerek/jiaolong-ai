package ai.jiaolong.client.data

import ai.jiaolong.client.model.Commit
import ai.jiaolong.client.model.CommitComment
import ai.jiaolong.client.model.Comment
import ai.jiaolong.client.model.DiffLine
import ai.jiaolong.client.model.DiffLineType
import ai.jiaolong.client.model.FileDiff
import ai.jiaolong.client.model.Role
import ai.jiaolong.client.model.Task

/**
 * In-memory sample data used to render the initial remote working UI.
 * A real backend integration will replace this later.
 */
object SampleData {
    const val TASK_1 = "task-1"
    const val TASK_2 = "task-2"
    const val COMMIT_1 = "commit-1"

    val tasks = listOf(
        Task(
            id = TASK_1,
            title = "Implement remote working dashboard",
            description = "Build the initial dashboard page showing the system status and the task list for the jiaolong remote working feature.",
            workingDirectory = "/workspace/code/client/JiaolongClient",
            updatedAt = "2023-11-14 22:14:00",
            assigneeAgent = "jiaolong-agent",
            agentWorking = true,
            comments = listOf(
                Comment(
                    id = "comment-1",
                    author = "jiaolong-agent",
                    role = Role.AGENT,
                    content = "I've generated a commit based on latest task information.",
                    createdAt = 1_700_000_100_000,
                    linkedCommitId = COMMIT_1,
                ),
                Comment(
                    id = "comment-2",
                    author = "alice",
                    role = Role.USER,
                    content = "Thanks! I'll review the generated commit.",
                    createdAt = 1_700_000_200_000,
                ),
            ),
        ),
        Task(
            id = TASK_2,
            title = "Design task detail page",
            description = "Add the task detail page with comments, editing, and agent commit links.",
            workingDirectory = "/workspace/code/agent_and_server",
            updatedAt = "2023-11-14 22:14:10",
            assigneeAgent = null,
            comments = emptyList(),
        ),
    )

    val commits = listOf(
        Commit(
            id = COMMIT_1,
            message = "feat: add initial remote working dashboard",
            files = listOf(
                FileDiff(
                    filePath = "shared/src/commonMain/kotlin/ai/jiaolong/client/ui/DashboardPage.kt",
                    lines = listOf(
                        DiffLine(null, 1, "@@ -0,0 +1,12 @@", DiffLineType.CONTEXT),
                        DiffLine(null, 2, "+package ai.jiaolong.client.ui", DiffLineType.ADDED),
                        DiffLine(null, 3, "+", DiffLineType.ADDED),
                        DiffLine(null, 4, "+import androidx.compose.material3.Scaffold", DiffLineType.ADDED),
                        DiffLine(null, 5, "+import androidx.compose.material3.Text", DiffLineType.ADDED),
                        DiffLine(null, 6, "+", DiffLineType.ADDED),
                        DiffLine(null, 7, "+@Composable", DiffLineType.ADDED),
                        DiffLine(null, 8, "+fun DashboardPage() {", DiffLineType.ADDED),
                        DiffLine(null, 9, "+    // Dashboard UI", DiffLineType.ADDED),
                        DiffLine(null, 10, "+}", DiffLineType.ADDED),
                    ),
                ),
                FileDiff(
                    filePath = "shared/src/commonMain/kotlin/ai/jiaolong/client/ui/TaskDetailPage.kt",
                    lines = listOf(
                        DiffLine(1, 1, "@@ -1,4 +1,6 @@", DiffLineType.CONTEXT),
                        DiffLine(2, 2, " package ai.jiaolong.client.ui", DiffLineType.CONTEXT),
                        DiffLine(3, 3, " ", DiffLineType.CONTEXT),
                        DiffLine(null, 4, "+import androidx.compose.material3.OutlinedTextField", DiffLineType.ADDED),
                        DiffLine(null, 5, "+import androidx.compose.material3.Button", DiffLineType.ADDED),
                        DiffLine(4, 6, " fun TaskDetailPage() {", DiffLineType.CONTEXT),
                    ),
                ),
            ),
            comments = listOf(
                CommitComment(
                    id = "commit-comment-1",
                    author = "alice",
                    content = "Could we also show the number of active agents here?",
                    createdAt = 1_700_000_300_000,
                    lineNumber = 5,
                ),
            ),
            approved = false,
        ),
    )
}
