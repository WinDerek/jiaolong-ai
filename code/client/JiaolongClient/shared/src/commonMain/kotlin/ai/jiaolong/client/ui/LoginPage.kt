package ai.jiaolong.client.ui

import ai.jiaolong.client.resources.Res
import ai.jiaolong.client.resources.logo_jiaolong_512x512
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import org.jetbrains.compose.resources.painterResource
import androidx.compose.foundation.Image

/**
 * Login page shown when the client has no valid credentials (first run, the
 * stored refresh token expired after one week, or the server restarted and
 * invalidated its in-memory tokens).
 *
 * The user enters the username and password configured in the server's
 * settings file. The values are sent only to the server's OAuth 2.0 login
 * endpoint; the client never stores or modifies the server-side credentials.
 */
@Composable
fun LoginPage(
    isLoggingIn: Boolean,
    errorMessage: String?,
    onLogin: (username: String, password: String) -> Unit,
) {
    var username by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }

    Scaffold { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            Image(
                painter = painterResource(Res.drawable.logo_jiaolong_512x512),
                contentDescription = "Jiaolong logo",
                modifier = Modifier.size(96.dp),
            )
            Spacer(modifier = Modifier.size(16.dp))
            Text(
                text = "Jiaolong AI",
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold,
            )
            Spacer(modifier = Modifier.size(8.dp))
            Text(
                text = "Sign in to continue",
                style = MaterialTheme.typography.bodyMedium,
            )
            Spacer(modifier = Modifier.size(24.dp))
            OutlinedTextField(
                value = username,
                onValueChange = { username = it },
                label = { Text("Username") },
                modifier = Modifier.fillMaxWidth(),
                enabled = !isLoggingIn,
                singleLine = true,
            )
            Spacer(modifier = Modifier.size(12.dp))
            OutlinedTextField(
                value = password,
                onValueChange = { password = it },
                label = { Text("Password") },
                modifier = Modifier.fillMaxWidth(),
                enabled = !isLoggingIn,
                singleLine = true,
                visualTransformation = PasswordVisualTransformation(),
            )
            if (errorMessage != null) {
                Spacer(modifier = Modifier.size(12.dp))
                Text(
                    text = errorMessage,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.error,
                )
            }
            Spacer(modifier = Modifier.size(24.dp))
            Button(
                enabled = !isLoggingIn && username.isNotBlank() && password.isNotBlank(),
                onClick = { onLogin(username.trim(), password) },
                modifier = Modifier.fillMaxWidth(),
            ) {
                if (isLoggingIn) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(16.dp),
                        strokeWidth = 2.dp,
                        color = LocalContentColor.current,
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Signing in...")
                } else {
                    Text("Sign in")
                }
            }
        }
    }
}