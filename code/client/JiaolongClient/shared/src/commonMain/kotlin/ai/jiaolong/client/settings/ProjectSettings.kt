package ai.jiaolong.client.settings

import ai.jiaolong.client.storage.JiaolongClientStorage

/**
 * Client-side project settings persisted in [JiaolongClientStorage].
 *
 * Remembers the id of the project the user last selected on the dashboard so
 * that the next time the client launches it shows the same project instead of
 * falling back to the first ordered project returned by the server.
 */
class ProjectSettings(
    private val storage: JiaolongClientStorage,
) {
    companion object {
        /** Local storage key under which the selected project id is stored. */
        const val KEY_SELECTED_PROJECT_ID = "selected_project_id"
    }

    /**
     * Returns the id of the project the user last selected, or `null` if no
     * project has been selected yet.
     */
    fun selectedProjectId(): String? = storage.get(KEY_SELECTED_PROJECT_ID)

    /** Persists [value] as the id of the project the user selected. */
    fun setSelectedProjectId(value: String) {
        storage.set(KEY_SELECTED_PROJECT_ID, value)
    }
}