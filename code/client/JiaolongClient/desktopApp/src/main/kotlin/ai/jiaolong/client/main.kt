package ai.jiaolong.client

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEvent
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.isCtrlPressed
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.type
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application

fun main() = application {
    // The most recent Ctrl+R key event captured by the window. It is forwarded
    // down to the dashboard page, which is the only screen that reacts to it
    // (by refreshing its data on desktop). Once the dashboard page has handled
    // the event it clears it again so the event is not replayed on a later
    // recomposition or navigation.
    var refreshShortcutEvent by remember { mutableStateOf<KeyEvent?>(null) }

    Window(
        onCloseRequest = ::exitApplication,
        title = "Jiaolong AI",
        onPreviewKeyEvent = { event ->
            if (
                event.type == KeyEventType.KeyDown &&
                event.key == Key.R &&
                event.isCtrlPressed
            ) {
                refreshShortcutEvent = event
                true
            } else {
                false
            }
        },
    ) {
        App(
            refreshShortcutEvent = refreshShortcutEvent,
            onRefreshShortcutHandled = { refreshShortcutEvent = null },
        )
    }
}