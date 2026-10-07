package ai.jiaolong.client.ui

import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.VerticalScrollbar
import androidx.compose.foundation.rememberScrollbarAdapter
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier

/**
 * Desktop (JVM) implementation of [SessionHistoryScrollbar]: a real draggable
 * vertical scrollbar backed by the Compose foundation scrollbar APIs.
 */
@Composable
actual fun SessionHistoryScrollbar(
    scrollState: ScrollState,
    modifier: Modifier,
) {
    VerticalScrollbar(
        adapter = rememberScrollbarAdapter(scrollState),
        modifier = modifier,
    )
}