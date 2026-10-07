package ai.jiaolong.client.ui

import androidx.compose.foundation.ScrollState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier

/**
 * Android implementation of [SessionHistoryScrollbar]: a no-op, because Android
 * does not show persistent scrollbars and the Compose foundation scrollbar APIs
 * used on desktop are not available on this target.
 */
@Composable
actual fun SessionHistoryScrollbar(
    scrollState: ScrollState,
    modifier: Modifier,
) {
    // No scrollbar is rendered on Android.
}