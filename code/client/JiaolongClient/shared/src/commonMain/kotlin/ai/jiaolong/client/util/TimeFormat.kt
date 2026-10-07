package ai.jiaolong.client.util

/**
 * Formats an epoch millis timestamp as a simple UTC date-time string.
 * Used only for display in the initial UI; a real backend integration
 * should provide properly formatted timestamps.
 */
fun formatTimestamp(epochMillis: Long): String {
    val totalSeconds = epochMillis / 1000
    val days = totalSeconds / 86_400
    val secondsOfDay = totalSeconds % 86_400
    val hours = secondsOfDay / 3_600
    val minutes = (secondsOfDay % 3_600) / 60
    val seconds = secondsOfDay % 60

    val (year, month, day) = civilFromDays(days)
    return "${pad(year)}-${pad(month)}-${pad(day)} ${pad(hours)}:${pad(minutes)}:${pad(seconds)}"
}

private fun pad(value: Long): String {
    val s = value.toString()
    return if (s.length < 2) "0$s" else s
}

/** Convert days since 1970-01-01 to a (year, month, day) triple (UTC). */
private fun civilFromDays(z: Long): Triple<Long, Long, Long> {
    val z2 = z + 719_468
    val era = if (z2 >= 0) z2 else z2 - 146_096
    val doe = z2 - era * 146_097
    val yoe = (doe - doe / 1_460 + doe / 36_524 - doe / 146_096) / 365
    val y = yoe + era * 400
    val doy = doe - (365 * yoe + yoe / 4 - yoe / 100)
    val mp = (5 * doy + 2) / 153
    val d = doy - (153 * mp + 2) / 5 + 1
    val m = if (mp < 10) mp + 3 else mp - 9
    val year = if (m <= 2) y + 1 else y
    return Triple(year, m, d)
}

/**
 * Formats a duration given in milliseconds as a short human-readable string,
 * e.g. "820 ms", "3.4 s" or "1 m 05 s". Used to report how long an operation
 * (such as initializing a branch and starting the agent) took.
 */
fun formatDuration(durationMillis: Long): String {
    if (durationMillis < 1_000) return "$durationMillis ms"
    val totalSeconds = durationMillis / 1_000
    if (totalSeconds < 60) {
        val tenths = (durationMillis % 1_000) / 100
        return "$totalSeconds.${tenths} s"
    }
    val minutes = totalSeconds / 60
    val seconds = totalSeconds % 60
    val paddedSeconds = if (seconds < 10) "0$seconds" else seconds.toString()
    return "$minutes m $paddedSeconds s"
}
