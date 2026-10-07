package ai.jiaolong.client

import ai.jiaolong.client.storage.AndroidJiaolongClientStorageContext
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.tooling.preview.Preview

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)

        AndroidJiaolongClientStorageContext.init(applicationContext)

        setContent {
            // Connect the system back button to the in-app back stack (registered
            // by App) so pressing back pops the current screen
            // instead of finishing the activity. The handler is only enabled when
            // there is a screen below the current one; on the root screen the
            // default system behavior (exiting the app) is preserved.
            val backEnabledState = remember { mutableStateOf<State<Boolean>?>(null) }
            val backActionState = remember { mutableStateOf<(() -> Unit)?>(null) }

            BackHandler(enabled = backEnabledState.value?.value == true) {
                backActionState.value?.invoke()
            }

            App(
                onRegisterBackHandler = { enabled, action ->
                    backEnabledState.value = enabled
                    backActionState.value = action
                },
            )
        }
    }
}

@Preview
@Composable
fun AppAndroidPreview() {
    App()
}