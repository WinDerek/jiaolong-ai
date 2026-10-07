package ai.jiaolong.client.storage

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/**
 * Desktop (JVM) implementation of [JiaolongClientStorage].
 *
 * The data is persisted as a JSON object in the local file
 * `~/.jiaolong/client/storage.json`.
 */
actual fun createJiaolongClientStorage(): JiaolongClientStorage =
    DesktopJiaolongClientStorage()

class DesktopJiaolongClientStorage(
    private val storageFile: File = defaultStorageFile(),
) : JiaolongClientStorage {

    private val json = Json {
        prettyPrint = true
        ignoreUnknownKeys = true
    }

    override fun get(key: String): String? = synchronized(this) {
        readAll()[key]
    }

    override fun set(key: String, value: String) {
        synchronized(this) {
            val updated = readAll().toMutableMap()
            updated[key] = value
            writeAll(updated)
        }
    }

    private fun readAll(): Map<String, String> {
        if (!storageFile.exists()) return emptyMap()
        return runCatching {
            json.decodeFromString<StorageData>(storageFile.readText()).values
        }.getOrDefault(emptyMap())
    }

    private fun writeAll(values: Map<String, String>) {
        storageFile.parentFile?.mkdirs()
        val tempFile = File(storageFile.parentFile, "${storageFile.name}.tmp")
        tempFile.writeText(json.encodeToString(StorageData(values)))
        Files.move(
            tempFile.toPath(),
            storageFile.toPath(),
            StandardCopyOption.REPLACE_EXISTING,
        )
    }

    private companion object {
        fun defaultStorageFile(): File {
            val home = System.getProperty("user.home")
                ?: System.getenv("HOME")
                ?: "."
            return File(home, ".jiaolong/client/storage.json")
        }
    }
}

@Serializable
internal data class StorageData(
    val values: Map<String, String> = emptyMap(),
)