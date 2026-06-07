package org.phigros.renderer

import android.content.Context
import android.net.Uri
import java.io.File

/**
 * Scans app-specific storage for playable chart files (.json, .phbc).
 * Charts are stored in [chartsDir] (external/charts/).
 * Zip archives are extracted via the C++ bridge.
 */
class ChartLibrary(private val context: Context) {

    private val chartsDir: File get() =
        File(context.getExternalFilesDir(null), "charts").also { it.mkdirs() }

    // ── Public API ──────────────────────────────────────────────────────────

    /** Drain Inbox-like incoming files, then return all chart entries. */
    fun scan(): List<ChartEntry> {
        drainIncoming()
        return chartsDir.listFiles { f ->
            f.isFile && (f.extension.lowercase() == "json" ||
                         f.extension.lowercase() == "phbc")
        }
            ?.map { ChartEntry(id = it.name, displayName = it.nameWithoutExtension, path = it.absolutePath) }
            ?.sortedBy { it.displayName }
            ?: emptyList()
    }

    /** Import a content URI (from file picker or intent). Returns new entry or null on error. */
    fun importFrom(uri: Uri): ChartEntry? = runCatching {
        val name = resolveFileName(uri)
        val ext = name.substringAfterLast('.', "").lowercase()

        if (ext == "zip") {
            val tmp = File(context.cacheDir, name)
            copyUriToFile(uri, tmp)
            extractZip(tmp)
            tmp.delete()
            null // caller should rescan
        } else if (ext == "json" || ext == "phbc") {
            val dest = File(chartsDir, name)
            copyUriToFile(uri, dest)
            ChartEntry(id = dest.name, displayName = dest.nameWithoutExtension, path = dest.absolutePath)
        } else null
    }.getOrNull()

    fun chartsDir(): File = chartsDir

    // ── Private helpers ─────────────────────────────────────────────────────

    /** Move any .json/.phbc/.zip files from the app's incoming cache into chartsDir. */
    private fun drainIncoming() {
        val incoming = context.cacheDir
        incoming.listFiles()?.forEach { f ->
            val ext = f.extension.lowercase()
            if (ext == "json" || ext == "phbc" || ext == "zip") {
                processFile(f)
            }
        }
    }

    private fun processFile(file: File) {
        val ext = file.extension.lowercase()
        when (ext) {
            "zip" -> { extractZip(file); file.delete() }
            "json", "phbc" -> {
                val dest = File(chartsDir, file.name)
                if (dest.exists()) dest.delete()
                file.copyTo(dest, overwrite = true)
                file.delete()
            }
        }
    }

    private fun extractZip(zip: File) {
        NativeBridge.extractChartZip(zip.absolutePath, chartsDir.absolutePath)
    }

    private fun copyUriToFile(uri: Uri, dest: File) {
        context.contentResolver.openInputStream(uri)?.use { input ->
            dest.outputStream().use { input.copyTo(it) }
        }
    }

    private fun resolveFileName(uri: Uri): String {
        // Try to get the display name from content resolver
        context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val idx = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (idx >= 0 && cursor.moveToFirst()) {
                val name = cursor.getString(idx)
                if (!name.isNullOrBlank()) return name
            }
        }
        return uri.lastPathSegment?.substringAfterLast('/')
            ?: "chart_${System.currentTimeMillis()}.json"
    }
}
