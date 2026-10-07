package ai.jiaolong.client.ui

import androidx.compose.foundation.ScrollState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier

/**
 * Vertical scrollbar for the session history page.
 *
 * Desktop (JVM) renders a real draggable scrollbar on the right edge using the
 * Compose foundation scrollbar APIs, which are only available on that target.
 * Android does not show a scrollbar, matching the platform convention, so its
 * implementation is a no-op.
 *
 * This `expect`/`actual` indirection keeps [SessionHistoryPage] in `commonMain`
 * while still allowing the desktop-only scrollbar APIs to be used on JVM.
 */
@Composable
expect fun SessionHistoryScrollbar(
    scrollState: ScrollState,
    modifier: Modifier,
)