package ai.jiaolong.client.platform

/**
 * Returns true when running on a desktop (JVM) target.
 *
 * Used to enable desktop-specific interactions such as the refresh button
 * and Ctrl+R keyboard shortcut on the task list page.
 */
expect fun isDesktop(): Boolean

/**
 * Returns the current wall-clock time in milliseconds since the Unix epoch
 * (1970-01-01T00:00:00Z). Used to validate the client's stored OAuth 2.0
 * credentials (access/refresh token expiry).
 */
expect fun currentTimeMillis(): Long
