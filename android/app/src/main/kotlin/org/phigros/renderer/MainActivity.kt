package org.phigros.renderer

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { PhigrosTheme { MainScreen() } }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen() {
    val context  = LocalContext.current
    val library  = remember { ChartLibrary(context) }
    var charts   by remember { mutableStateOf(library.scan()) }
    var selected by remember { mutableStateOf<ChartEntry?>(null) }
    var showSettings by remember { mutableStateOf(false) }
    var error    by remember { mutableStateOf("") }

    val importLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenMultipleDocuments()
    ) { uris ->
        uris.forEach { uri ->
            library.importFrom(uri) ?: run { error = "Failed to import $uri" }
        }
        charts = library.scan()
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Chart Library") },
                actions = {
                    IconButton(onClick = { importLauncher.launch(arrayOf("*/*")) }) {
                        Icon(Icons.Default.Add, "Import")
                    }
                    IconButton(onClick = { charts = library.scan() }) {
                        Icon(Icons.Default.Refresh, "Refresh")
                    }
                    IconButton(onClick = { showSettings = true }) {
                        Icon(Icons.Default.Settings, "Settings")
                    }
                }
            )
        }
    ) { padding ->
        Box(Modifier.fillMaxSize().padding(padding)) {
            if (charts.isEmpty()) {
                Text(
                    "No charts found.\nCopy .json or .phbc files to app storage.",
                    Modifier.align(Alignment.Center).padding(32.dp),
                    textAlign = TextAlign.Center,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else {
                LazyColumn {
                    items(charts, key = { it.path }) { chart ->
                        ChartListItem(
                            chart    = chart,
                            selected = chart == selected,
                            onClick  = { selected = chart }
                        )
                        HorizontalDivider()
                    }
                }
            }

            // Play-mode panel slides up when a chart is selected
            selected?.let { chart ->
                PlayModePanelSheet(
                    chart    = chart,
                    onDismiss = { selected = null },
                    onStart  = { mode, scriptPath ->
                        val i = Intent(context, GameActivity::class.java).apply {
                            putExtra(GameActivity.EXTRA_CHART,       chart)
                            putExtra(GameActivity.EXTRA_PLAY_MODE,   mode.name)
                            putExtra(GameActivity.EXTRA_SCRIPT_PATH, scriptPath)
                        }
                        context.startActivity(i)
                        selected = null
                    }
                )
            }

            if (error.isNotEmpty()) {
                AlertDialog(
                    onDismissRequest = { error = "" },
                    title = { Text("Error") },
                    text  = { Text(error) },
                    confirmButton = { TextButton(onClick = { error = "" }) { Text("OK") } }
                )
            }
        }
    }

    if (showSettings) {
        SettingsDialog(onDismiss = { showSettings = false })
    }
}

@Composable
private fun ChartListItem(chart: ChartEntry, selected: Boolean, onClick: () -> Unit) {
    ListItem(
        headlineContent = {
            Text(chart.displayName,
                fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal)
        },
        supportingContent = {
            Text(chart.path.substringAfterLast('/'), fontSize = 12.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        },
        modifier = Modifier.clickable(onClick = onClick),
        colors = if (selected)
            ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.primaryContainer)
        else ListItemDefaults.colors()
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PlayModePanelSheet(
    chart: ChartEntry,
    onDismiss: () -> Unit,
    onStart: (PlayMode, String?) -> Unit
) {
    val context = LocalContext.current
    var mode by remember { mutableStateOf(PlayMode.AUTOPLAY) }
    var scriptPath by remember { mutableStateOf<String?>(null) }

    val scriptLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        uri?.let { scriptPath = it.path }
    }

    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(horizontal = 24.dp, vertical = 8.dp).navigationBarsPadding()) {
            Text(chart.displayName, fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(16.dp))

            Text("Play Mode", style = MaterialTheme.typography.labelMedium)
            Spacer(Modifier.height(8.dp))

            // Segmented button row
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                PlayMode.entries.forEachIndexed { idx, m ->
                    SegmentedButton(
                        shape = SegmentedButtonDefaults.itemShape(idx, PlayMode.entries.size),
                        selected = mode == m,
                        onClick  = { mode = m }
                    ) { Text(m.label, fontSize = 13.sp) }
                }
            }

            Spacer(Modifier.height(8.dp))
            Text(mode.description,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant)

            if (mode == PlayMode.SCRIPTPLAY) {
                Spacer(Modifier.height(12.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        scriptPath?.substringAfterLast('/') ?: "No script selected",
                        Modifier.weight(1f),
                        style = MaterialTheme.typography.bodySmall,
                        color = if (scriptPath == null)
                            MaterialTheme.colorScheme.error
                        else
                            MaterialTheme.colorScheme.onSurface
                    )
                    OutlinedButton(onClick = { scriptLauncher.launch(arrayOf("*/*")) }) {
                        Text("Choose…")
                    }
                }
            }

            Spacer(Modifier.height(20.dp))
            Button(
                onClick  = { onStart(mode, scriptPath) },
                modifier = Modifier.fillMaxWidth(),
                enabled  = mode != PlayMode.SCRIPTPLAY || scriptPath != null
            ) { Text("Start Game", fontSize = 16.sp) }
            Spacer(Modifier.height(16.dp))
        }
    }
}
