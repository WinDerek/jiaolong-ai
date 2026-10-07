package ai.jiaolong.client.ui

import ai.jiaolong.client.model.SystemHealthFrame
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Minimum temperature range (in degrees Celsius) shown on the chart's y axis.
 * The axis always covers at least [MIN_TEMPERATURE_CELSIUS] ~
 * [MAX_TEMPERATURE_CELSIUS] and is extended outwards when the data goes beyond
 * that range, so every data point stays visible.
 */
private const val MIN_TEMPERATURE_CELSIUS = 20.0
private const val MAX_TEMPERATURE_CELSIUS = 100.0

/**
 * Server monitor page: visualizes the CPU temperature of the Jiaolong Server
 * as a line chart drawn on a [Canvas] (no chart library is used).
 *
 * The data comes from the Jiaolong Server `GET /api/server/system-health`
 * endpoint, which returns the last 600 frames gathered by the server's system
 * health perception loop. The page refreshes automatically every
 * [autoRefreshIntervalMillis] so the chart keeps showing the latest readings
 * while it is open; the refresh button in the top app bar triggers a manual
 * refresh.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ServerMonitorPage(
    frames: List<SystemHealthFrame>,
    isLoading: Boolean,
    errorMessage: String?,
    onBack: () -> Unit,
    onRefresh: () -> Unit,
    autoRefreshIntervalMillis: Long = 5_000,
) {
    val currentOnRefresh by rememberUpdatedState(onRefresh)

    // Keep polling the server while the monitor page is open so the chart
    // stays fresh. The effect is cancelled automatically when the page leaves
    // composition (e.g. the user navigates back).
    LaunchedEffect(Unit) {
        while (true) {
            delay(autoRefreshIntervalMillis)
            currentOnRefresh()
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Server Monitor") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = "Go back",
                        )
                    }
                },
                actions = {
                    IconButton(onClick = onRefresh, enabled = !isLoading) {
                        Icon(
                            imageVector = Icons.Default.Refresh,
                            contentDescription = "Refresh",
                        )
                    }
                },
            )
        },
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            when {
                frames.isEmpty() && isLoading -> {
                    Column(
                        modifier = Modifier.fillMaxSize(),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center,
                    ) {
                        CircularProgressIndicator()
                        Text(
                            text = "Loading server system health data...",
                            style = MaterialTheme.typography.bodyMedium,
                            modifier = Modifier.padding(top = 12.dp),
                        )
                    }
                }

                frames.isEmpty() && errorMessage != null -> {
                    Column(
                        modifier = Modifier.fillMaxSize(),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center,
                    ) {
                        Text(
                            text = errorMessage,
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.error,
                        )
                        Button(
                            onClick = onRefresh,
                            modifier = Modifier.padding(top = 12.dp),
                        ) {
                            Text("Retry")
                        }
                    }
                }

                else -> {
                    if (errorMessage != null) {
                        // Data is still shown from the previous load; tell the
                        // user the latest refresh failed.
                        ErrorBanner(message = errorMessage, onDismiss = {})
                    }
                    CpuTemperatureSummaryCard(frames = frames)
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Column(modifier = Modifier.padding(16.dp)) {
                            Text(
                                text = "CPU temperature",
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.SemiBold,
                            )
                            Text(
                                text = "Last ${frames.size} data frame(s) from the server perception loop.",
                                style = MaterialTheme.typography.bodySmall,
                            )
                            Spacer(modifier = Modifier.height(12.dp))
                            CpuTemperatureLineChart(
                                frames = frames,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .height(240.dp),
                            )
                            Spacer(modifier = Modifier.height(4.dp))
                            Text(
                                text = "Auto-refreshes every ${autoRefreshIntervalMillis / 1000} seconds",
                                style = MaterialTheme.typography.labelSmall,
                            )
                        }
                    }
                }
            }
        }
    }
}

/** Summary card showing the latest CPU temperature and min/max in the window. */
@Composable
private fun CpuTemperatureSummaryCard(frames: List<SystemHealthFrame>) {
    val latest = frames.lastOrNull()
    val minTemp = frames.minOfOrNull { it.cpuTemperatureCelsius }
    val maxTemp = frames.maxOfOrNull { it.cpuTemperatureCelsius }

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                text = "Current CPU temperature",
                style = MaterialTheme.typography.titleSmall,
            )
            Text(
                text = latest?.let { "${it.cpuTemperatureCelsius.roundToInt()}°C" } ?: "--",
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.primary,
            )
            if (minTemp != null && maxTemp != null) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text(
                        text = "Min ${minTemp.roundToInt()}°C",
                        style = MaterialTheme.typography.bodySmall,
                    )
                    Text(
                        text = "Max ${maxTemp.roundToInt()}°C",
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
        }
    }
}

/**
 * Line chart of the CPU temperature drawn on a [Canvas]. The chart shows a
 * horizontal grid with temperature labels on the left, time labels at the
 * bottom, a filled area under the line and a dot for each data frame.
 */
@Composable
private fun CpuTemperatureLineChart(
    frames: List<SystemHealthFrame>,
    modifier: Modifier = Modifier,
) {
    val textMeasurer = rememberTextMeasurer()
    val labelStyle = TextStyle(fontSize = 10.sp, color = Color.Gray)
    val lineColor = MaterialTheme.colorScheme.primary
    val fillColor = lineColor.copy(alpha = 0.15f)
    val gridColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f)

    Canvas(modifier = modifier) {
        if (frames.isEmpty()) return@Canvas

        val leftPadding = 44.dp.toPx()
        val rightPadding = 8.dp.toPx()
        val topPadding = 8.dp.toPx()
        val bottomPadding = 20.dp.toPx()
        val chartWidth = size.width - leftPadding - rightPadding
        val chartHeight = size.height - topPadding - bottomPadding
        if (chartWidth <= 0f || chartHeight <= 0f) return@Canvas

        // Temperature range shown on the y axis. The axis always covers at
        // least MIN_TEMPERATURE_CELSIUS ~ MAX_TEMPERATURE_CELSIUS; when the data
        // goes below/above that range the corresponding border is extended to
        // cover all the data points.
        val rawMin = frames.minOf { it.cpuTemperatureCelsius }
        val rawMax = frames.maxOf { it.cpuTemperatureCelsius }
        val axisMin = min(MIN_TEMPERATURE_CELSIUS, rawMin)
        val axisMax = max(MAX_TEMPERATURE_CELSIUS, rawMax)
        val tempRange = max(axisMax - axisMin, 1.0)

        fun xFor(index: Int): Float {
            if (frames.size <= 1) return leftPadding + chartWidth / 2f
            return leftPadding + chartWidth * index / (frames.size - 1)
        }

        fun yFor(temperature: Double): Float {
            val ratio = ((temperature - axisMin) / tempRange).toFloat()
            return topPadding + chartHeight * (1f - ratio)
        }

        // Horizontal grid lines with temperature labels.
        val gridLineCount = 4
        for (i in 0..gridLineCount) {
            val temperature = axisMin + tempRange * i / gridLineCount
            val y = yFor(temperature)
            drawLine(
                color = gridColor,
                start = Offset(leftPadding, y),
                end = Offset(leftPadding + chartWidth, y),
                strokeWidth = 1.dp.toPx(),
            )
            val label = "${temperature.roundToInt()}°"
            val layout = textMeasurer.measure(label, labelStyle)
            drawText(
                textLayoutResult = layout,
                topLeft = Offset(
                    x = leftPadding - layout.size.width - 6.dp.toPx(),
                    y = y - layout.size.height / 2f,
                ),
            )
        }

        // Time labels at the bottom (first, middle and last frame).
        val labelIndices = when {
            frames.size <= 2 -> frames.indices.toList()
            else -> listOf(0, frames.size / 2, frames.size - 1)
        }
        for (index in labelIndices) {
            val layout = textMeasurer.measure(
                formatTimeOfDay(frames[index].timestamp),
                labelStyle,
            )
            drawText(
                textLayoutResult = layout,
                topLeft = Offset(
                    x = xFor(index) - layout.size.width / 2f,
                    y = topPadding + chartHeight + 4.dp.toPx(),
                ),
            )
        }

        // Vertical axis lines.
        drawLine(
            color = gridColor,
            start = Offset(leftPadding, topPadding),
            end = Offset(leftPadding, topPadding + chartHeight),
            strokeWidth = 1.dp.toPx(),
        )
        drawLine(
            color = gridColor,
            start = Offset(leftPadding, topPadding + chartHeight),
            end = Offset(leftPadding + chartWidth, topPadding + chartHeight),
            strokeWidth = 1.dp.toPx(),
        )

        // Filled area under the line.
        val fillPath = Path().apply {
            moveTo(xFor(0), topPadding + chartHeight)
            frames.forEachIndexed { index, frame ->
                lineTo(xFor(index), yFor(frame.cpuTemperatureCelsius))
            }
            lineTo(xFor(frames.size - 1), topPadding + chartHeight)
            close()
        }
        drawPath(path = fillPath, color = fillColor)

        // The line itself.
        val linePath = Path().apply {
            frames.forEachIndexed { index, frame ->
                val x = xFor(index)
                val y = yFor(frame.cpuTemperatureCelsius)
                if (index == 0) moveTo(x, y) else lineTo(x, y)
            }
        }
        drawPath(
            path = linePath,
            color = lineColor,
            style = Stroke(width = 2.dp.toPx(), cap = StrokeCap.Round),
        )

        // A dot for every data frame, with the latest one highlighted.
        frames.forEachIndexed { index, frame ->
            val center = Offset(xFor(index), yFor(frame.cpuTemperatureCelsius))
            val isLatest = index == frames.size - 1
            drawCircle(
                color = if (isLatest) lineColor else lineColor.copy(alpha = 0.7f),
                radius = if (isLatest) 4.dp.toPx() else 2.5.dp.toPx(),
                center = center,
            )
        }
    }
}

/** Formats an epoch millis timestamp as "HH:mm:ss" (UTC) for chart labels. */
private fun formatTimeOfDay(epochMillis: Long): String {
    val totalSeconds = epochMillis / 1000
    val secondsOfDay = totalSeconds % 86_400
    val hours = secondsOfDay / 3_600
    val minutes = (secondsOfDay % 3_600) / 60
    val seconds = secondsOfDay % 60
    return "${pad2(hours)}:${pad2(minutes)}:${pad2(seconds)}"
}

private fun pad2(value: Long): String = value.toString().padStart(2, '0')