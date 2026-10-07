package ai.jiaolong.client.storage

/**
 * Unified interface for managing key-value data stored on the client side.
 *
 * Both keys and values are strings. Platform-specific implementations:
 * - Desktop (JVM): JSON file at `~/.jiaolong/client/storage.json`.
 * - Android: SharedPreferences.
 */
interface JiaolongClientStorage {
    /**
     * Returns the value stored under [key], or `null` if the key is not present.
     */
    fun get(key: String): String?

    /**
     * Stores [value] under [key], replacing any existing value.
     */
    fun set(key: String, value: String)
}

/**
 * Returns the platform-specific [JiaolongClientStorage] implementation.
 */
expect fun createJiaolongClientStorage(): JiaolongClientStorage