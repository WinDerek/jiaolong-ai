package ai.jiaolong.client.settings

import ai.jiaolong.client.storage.JiaolongClientStorage

/**
 * Client-side server settings persisted in [JiaolongClientStorage].
 *
 * The server base URL is always read from local storage so the running client
 * and the stored setting never diverge. On first boot (when local storage does
 * not contain a value yet), the default server base URL is written into local
 * storage so that all subsequent reads are consistent.
 */
class ServerSettings(
    private val storage: JiaolongClientStorage,
) {
    companion object {
        /** Local storage key under which the server base URL is stored. */
        const val KEY_SERVER_BASE_URL = "server_base_url"

        /**
         * Default server base URL used to seed local storage on first boot.
         * Matches the server's default port (8989) and the `/api` REST prefix.
         */
        const val DEFAULT_SERVER_BASE_URL = "http://127.0.0.1:8989/api/"
    }

    /**
     * Returns the configured server base URL, seeding local storage with
     * [DEFAULT_SERVER_BASE_URL] if no value is stored yet.
     */
    fun serverBaseUrl(): String {
        val stored = storage.get(KEY_SERVER_BASE_URL)
        if (stored != null) {
            return stored
        }
        storage.set(KEY_SERVER_BASE_URL, DEFAULT_SERVER_BASE_URL)
        return DEFAULT_SERVER_BASE_URL
    }

    /** Persists [value] as the new server base URL. */
    fun setServerBaseUrl(value: String) {
        storage.set(KEY_SERVER_BASE_URL, value)
    }
}