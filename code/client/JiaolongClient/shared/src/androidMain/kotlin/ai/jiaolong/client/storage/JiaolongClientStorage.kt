package ai.jiaolong.client.storage

import android.content.Context
import android.content.SharedPreferences

/**
 * Android implementation of [JiaolongClientStorage] backed by SharedPreferences.
 */
actual fun createJiaolongClientStorage(): JiaolongClientStorage =
    AndroidJiaolongClientStorage(
        checkNotNull(AndroidJiaolongClientStorageContext.context) {
            "JiaolongClientStorage is not initialized. Call " +
                "AndroidJiaolongClientStorageContext.init(context) before using it."
        }
    )

/**
 * Holds the application [Context] used by [createJiaolongClientStorage].
 *
 * The Android app must call [init] (e.g., from `Application.onCreate` or
 * `MainActivity.onCreate`) before the first use of the storage.
 */
object AndroidJiaolongClientStorageContext {
    @Volatile
    var context: Context? = null
        internal set

    fun init(context: Context) {
        this.context = context.applicationContext
    }
}

class AndroidJiaolongClientStorage(context: Context) : JiaolongClientStorage {

    private val preferences: SharedPreferences =
        context.getSharedPreferences("jiaolong_client_storage", Context.MODE_PRIVATE)

    override fun get(key: String): String? = preferences.getString(key, null)

    override fun set(key: String, value: String) {
        preferences.edit().putString(key, value).apply()
    }
}