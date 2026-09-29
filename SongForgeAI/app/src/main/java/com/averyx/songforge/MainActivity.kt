package com.averyx.songforge

import android.app.Activity
import android.content.ContentValues
import android.content.Context
import android.graphics.Color
import android.media.MediaMetadataRetriever
import android.media.MediaPlayer
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.MediaStore
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.text.InputType
import android.util.Base64
import android.view.Gravity
import android.view.View
import android.widget.*
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.URL
import java.security.KeyStore
import java.util.concurrent.Executors
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import javax.net.ssl.HttpsURLConnection

class MainActivity : Activity() {
    private val worker = Executors.newSingleThreadExecutor()
    private lateinit var apiKeyInput: EditText
    private lateinit var promptInput: EditText
    private lateinit var lyricsInput: EditText
    private lateinit var status: TextView
    private lateinit var lengthLabel: TextView
    private lateinit var instrumental: CheckBox
    private lateinit var generateLyrics: Button
    private lateinit var generateSong: Button
    private lateinit var playButton: Button
    private lateinit var downloadButton: Button
    private var activePlan: JSONObject? = null
    private var latestFile: File? = null
    private var player: MediaPlayer? = null
    private var durationSec = 90

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Color.rgb(9, 11, 16)
        window.navigationBarColor = Color.rgb(9, 11, 16)
        setContentView(buildUi())
        apiKeyInput.setText(loadApiKey())
        promptInput.setText("Emotional energetic progressive house with intimate male vocals, nostalgic verse, huge memorable chorus and a powerful melodic drop. 126 BPM, modern polished production.")
    }

    private fun buildUi(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.rgb(9, 11, 16))
        }
        val scroll = ScrollView(this)
        val body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(18), dp(20), dp(18), dp(28))
        }
        scroll.addView(body)
        root.addView(scroll, LinearLayout.LayoutParams(-1, -1))
        body.addView(text("SongForge AI", 28f, true))
        body.addView(text("Real prompt → lyrics → Music v2.5 audio", 14f, false).apply { setTextColor(Color.rgb(170, 176, 190)) })
        spacer(body, 16)
        body.addView(text("ElevenLabs API key", 14f, true))
        apiKeyInput = edit("xi-api-key", 1).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        body.addView(apiKeyInput)
        body.addView(Button(this).apply {
            text = "Save key securely"
            setOnClickListener {
                saveApiKey(apiKeyInput.text.toString().trim())
                toast("API key saved in Android Keystore")
            }
        })
        spacer(body, 16)
        body.addView(text("Describe the song", 14f, true))
        promptInput = edit("Describe genre, mood, vocals, structure and production...", 5)
        body.addView(promptInput)
        spacer(body, 12)
        lengthLabel = text("Length: 90 seconds", 14f, true)
        body.addView(lengthLabel)
        body.addView(SeekBar(this).apply {
            max = 270
            progress = 60
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar?, p: Int, fromUser: Boolean) {
                    durationSec = p + 30
                    lengthLabel.text = "Length: $durationSec seconds"
                }
                override fun onStartTrackingTouch(s: SeekBar?) {}
                override fun onStopTrackingTouch(s: SeekBar?) {}
            })
        })
        instrumental = CheckBox(this).apply {
            text = "Instrumental only"
            setTextColor(Color.WHITE)
        }
        body.addView(instrumental)
        spacer(body, 12)
        val actions = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER }
        generateLyrics = Button(this).apply { text = "Generate lyrics"; setOnClickListener { requestPlan() } }
        generateSong = Button(this).apply { text = "Generate song"; setOnClickListener { requestSong() } }
        actions.addView(generateLyrics, LinearLayout.LayoutParams(0, dp(54), 1f))
        actions.addView(generateSong, LinearLayout.LayoutParams(0, dp(54), 1f).apply { marginStart = dp(8) })
        body.addView(actions)
        spacer(body, 12)
        status = text("Ready", 14f, true)
        body.addView(status)
        spacer(body, 12)
        body.addView(text("Lyrics / sections", 14f, true))
        lyricsInput = edit("Generate lyrics first, then edit them here before creating the song.", 12)
        body.addView(lyricsInput)
        spacer(body, 12)
        val audioActions = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        playButton = Button(this).apply { text = "Play"; isEnabled = false; setOnClickListener { togglePlay() } }
        downloadButton = Button(this).apply { text = "Download"; isEnabled = false; setOnClickListener { latestFile?.let { exportSong(it) } } }
        audioActions.addView(playButton, LinearLayout.LayoutParams(0, dp(54), 1f))
        audioActions.addView(downloadButton, LinearLayout.LayoutParams(0, dp(54), 1f).apply { marginStart = dp(8) })
        body.addView(audioActions)
        spacer(body, 12)
        body.addView(text("Uses the official ElevenLabs Music v2.5 API. Music API access requires an eligible paid plan and uses your account credits.", 12f, false).apply {
            setTextColor(Color.rgb(160, 165, 180))
        })
        return root
    }

    private fun requestPlan() {
        val key = apiKeyInput.text.toString().trim()
        val prompt = promptInput.text.toString().trim()
        if (key.isBlank()) return toast("Add your ElevenLabs API key first")
        if (prompt.isBlank()) return toast("Describe the song first")
        saveApiKey(key)
        setBusy(true, "Writing lyrics and structure…")
        worker.execute {
            try {
                val plan = createPlan(key, prompt, durationSec * 1000, instrumental.isChecked)
                activePlan = plan
                val lyrics = extractLyrics(plan)
                runOnUiThread {
                    lyricsInput.setText(lyrics)
                    setBusy(false, "Plan ready — edit the lyrics or generate the song")
                }
            } catch (t: Throwable) {
                runOnUiThread { setBusy(false, "Error: " + safeMessage(t)) }
            }
        }
    }

    private fun requestSong() {
        val key = apiKeyInput.text.toString().trim()
        val prompt = promptInput.text.toString().trim()
        if (key.isBlank()) return toast("Add your ElevenLabs API key first")
        if (prompt.isBlank()) return toast("Describe the song first")
        saveApiKey(key)
        setBusy(true, "Preparing song…")
        worker.execute {
            try {
                var plan = activePlan ?: createPlan(key, prompt, durationSec * 1000, instrumental.isChecked)
                val edited = lyricsInput.text.toString().trim()
                if (edited.isNotBlank() && !instrumental.isChecked) plan = applyEditedLyrics(plan, edited)
                activePlan = plan
                runOnUiThread { status.text = "Generating real audio with Music v2.5…" }
                val file = compose(key, plan)
                latestFile = file
                rememberSong(file)
                val duration = readDuration(file)
                runOnUiThread {
                    setBusy(false, "Ready — generated " + (duration / 1000) + "s • " + file.name)
                    playButton.isEnabled = true
                    downloadButton.isEnabled = true
                    startPlayback(file)
                }
            } catch (t: Throwable) {
                runOnUiThread { setBusy(false, "Error: " + safeMessage(t)) }
            }
        }
    }

    private fun createPlan(key: String, prompt: String, lengthMs: Int, noVocals: Boolean): JSONObject {
        val enhanced = buildString {
            append(prompt)
            append("\n\nCreate a polished, commercially structured complete song with obvious section changes, evolving arrangement, memorable hooks and professional dynamics. ")
            if (noVocals) append("Instrumental only. No lyrics and no sung vocals. ")
            else append("Write natural, emotional, memorable, easy-to-sing lyrics with a strong repeated chorus hook. Avoid generic AI cliches, meaningless filler and forced rhymes. Use intelligible original vocals. ")
            append("Do not repeat one loop for the whole track. Make verse, build, chorus/drop and breakdown clearly different.")
        }
        val body = JSONObject().put("prompt", enhanced).put("music_length_ms", lengthMs).put("model_id", "music_v2_5")
        val bytes = postJson("https://api.elevenlabs.io/v1/music/plan", key, body.toString(), 120_000)
        return JSONObject(bytes.toString(Charsets.UTF_8))
    }

    private fun compose(key: String, plan: JSONObject): File {
        val body = JSONObject().put("composition_plan", plan).put("model_id", "music_v2_5").put("store_for_inpainting", true)
        val bytes = postJson("https://api.elevenlabs.io/v1/music?output_format=mp3_48000_192", key, body.toString(), 720_000)
        if (bytes.size < 16_000) throw IllegalStateException("The music service returned an unexpectedly small audio file.")
        val dir = File(filesDir, "songs").apply { mkdirs() }
        return File(dir, "song_" + System.currentTimeMillis() + ".mp3").apply { writeBytes(bytes) }
    }

    private fun postJson(url: String, key: String, json: String, readTimeout: Int): ByteArray {
        val c = URL(url).openConnection() as HttpsURLConnection
        try {
            c.requestMethod = "POST"
            c.setRequestProperty("xi-api-key", key)
            c.setRequestProperty("Content-Type", "application/json")
            c.setRequestProperty("Accept", "*/*")
            c.connectTimeout = 30_000
            c.readTimeout = readTimeout
            c.doOutput = true
            c.outputStream.use { it.write(json.toByteArray(Charsets.UTF_8)) }
            val code = c.responseCode
            val bytes = (if (code in 200..299) c.inputStream else c.errorStream)?.use { it.readBytes() } ?: ByteArray(0)
            if (code !in 200..299) {
                val msg = bytes.toString(Charsets.UTF_8).take(700)
                throw IllegalStateException("ElevenLabs request failed (" + code + "): " + msg)
            }
            return bytes
        } finally { c.disconnect() }
    }

    private fun extractLyrics(plan: JSONObject): String {
        if (plan.has("chunks")) {
            val chunks = plan.optJSONArray("chunks") ?: JSONArray()
            val out = ArrayList<String>()
            for (i in 0 until chunks.length()) {
                val value = chunks.optJSONObject(i)?.optString("text").orEmpty().trim()
                if (value.isNotBlank()) out.add(value)
            }
            return out.joinToString("\n\n")
        }
        if (plan.has("sections")) {
            val sections = plan.optJSONArray("sections") ?: JSONArray()
            val out = ArrayList<String>()
            for (i in 0 until sections.length()) {
                val s = sections.optJSONObject(i) ?: continue
                val name = s.optString("section_name", "Section")
                val lines = s.optJSONArray("lines") ?: JSONArray()
                val txt = StringBuilder("[" + name + "]")
                for (j in 0 until lines.length()) txt.append("\n").append(lines.optString(j))
                out.add(txt.toString())
            }
            return out.joinToString("\n\n")
        }
        return ""
    }

    private fun applyEditedLyrics(plan: JSONObject, edited: String): JSONObject {
        val chunks = plan.optJSONArray("chunks") ?: return plan
        val sections = splitSections(edited)
        if (sections.isEmpty()) return plan
        var cursor = 0
        for (i in 0 until chunks.length()) {
            val chunk = chunks.optJSONObject(i) ?: continue
            if (cursor >= sections.size) break
            val old = chunk.optString("text")
            if (old.contains("[") || old.lines().size > 1) chunk.put("text", sections[cursor++])
        }
        return plan
    }

    private fun splitSections(text: String): List<String> {
        val result = mutableListOf<StringBuilder>()
        for (line in text.lines()) {
            val t = line.trim()
            if (t.startsWith("[") && t.contains("]")) result.add(StringBuilder(t))
            else if (result.isNotEmpty()) result.last().append("\n").append(line)
        }
        return result.map { it.toString().trim() }.filter { it.isNotBlank() }
    }

    private fun togglePlay() {
        val f = latestFile ?: return
        val p = player
        if (p == null) startPlayback(f)
        else if (p.isPlaying) { p.pause(); playButton.text = "Play" }
        else { p.start(); playButton.text = "Pause" }
    }

    private fun startPlayback(file: File) {
        player?.release()
        player = MediaPlayer().apply {
            setDataSource(file.absolutePath)
            setOnPreparedListener { it.start(); playButton.text = "Pause" }
            setOnCompletionListener { playButton.text = "Play" }
            prepareAsync()
        }
    }

    private fun exportSong(file: File) {
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                val values = ContentValues().apply {
                    put(MediaStore.Audio.Media.DISPLAY_NAME, file.name)
                    put(MediaStore.Audio.Media.MIME_TYPE, "audio/mpeg")
                    put(MediaStore.Audio.Media.RELATIVE_PATH, "Music/SongForgeAI")
                }
                val uri = contentResolver.insert(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, values) ?: throw IllegalStateException("Could not create download")
                contentResolver.openOutputStream(uri)?.use { out -> file.inputStream().use { input -> input.copyTo(out) } }
                toast("Saved to Music/SongForgeAI")
            } else {
                val dir = getExternalFilesDir(Environment.DIRECTORY_MUSIC) ?: filesDir
                val out = File(dir, file.name)
                file.copyTo(out, overwrite = true)
                toast("Saved to " + out.absolutePath)
            }
        } catch (t: Throwable) { toast("Download failed: " + safeMessage(t)) }
    }

    private fun readDuration(file: File): Long {
        val r = MediaMetadataRetriever()
        return try {
            r.setDataSource(file.absolutePath)
            r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
        } finally { r.release() }
    }

    private fun rememberSong(file: File) {
        val p = getSharedPreferences("library", Context.MODE_PRIVATE)
        val paths = p.getStringSet("songs", emptySet())?.toMutableSet() ?: mutableSetOf()
        paths.add(file.absolutePath)
        p.edit().putStringSet("songs", paths).apply()
    }

    private fun setBusy(busy: Boolean, message: String) {
        status.text = message
        generateLyrics.isEnabled = !busy
        generateSong.isEnabled = !busy
    }

    private fun saveApiKey(value: String) {
        if (value.isBlank()) return
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, secretKey())
        val encrypted = cipher.doFinal(value.toByteArray(Charsets.UTF_8))
        getSharedPreferences("secure", Context.MODE_PRIVATE).edit()
            .putString("ct", Base64.encodeToString(encrypted, Base64.NO_WRAP))
            .putString("iv", Base64.encodeToString(cipher.iv, Base64.NO_WRAP)).apply()
    }

    private fun loadApiKey(): String {
        val p = getSharedPreferences("secure", Context.MODE_PRIVATE)
        val ct = p.getString("ct", null) ?: return ""
        val iv = p.getString("iv", null) ?: return ""
        return try {
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE, secretKey(), GCMParameterSpec(128, Base64.decode(iv, Base64.NO_WRAP)))
            String(cipher.doFinal(Base64.decode(ct, Base64.NO_WRAP)), Charsets.UTF_8)
        } catch (_: Throwable) { "" }
    }

    private fun secretKey(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getKey("songforge-key", null) as? SecretKey)?.let { return it }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        gen.init(KeyGenParameterSpec.Builder("songforge-key", KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
        return gen.generateKey()
    }

    private fun edit(hintText: String, minLines: Int): EditText = EditText(this).apply {
        hint = hintText
        setHintTextColor(Color.rgb(120, 126, 140))
        setTextColor(Color.WHITE)
        setBackgroundColor(Color.rgb(24, 28, 38))
        setPadding(dp(12), dp(10), dp(12), dp(10))
        setMinLines(minLines)
        gravity = Gravity.TOP
        inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
    }

    private fun text(value: String, size: Float, bold: Boolean): TextView = TextView(this).apply {
        text = value
        textSize = size
        setTextColor(Color.WHITE)
        if (bold) setTypeface(typeface, android.graphics.Typeface.BOLD)
    }

    private fun spacer(parent: LinearLayout, h: Int) { parent.addView(Space(this), LinearLayout.LayoutParams(1, dp(h))) }
    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()
    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_LONG).show()
    private fun safeMessage(t: Throwable): String = (t.message ?: t.javaClass.simpleName).take(900)

    override fun onDestroy() {
        super.onDestroy()
        player?.release()
        worker.shutdownNow()
    }
}
