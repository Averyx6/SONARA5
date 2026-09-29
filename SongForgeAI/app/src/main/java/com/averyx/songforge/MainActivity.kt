package com.averyx.songforge

import android.app.Activity
import android.content.ContentValues
import android.graphics.Color
import android.media.MediaPlayer
import android.os.Build
import android.os.Bundle
import android.provider.MediaStore
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.widget.*
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

class MainActivity : Activity() {
    private val worker = Executors.newSingleThreadExecutor()
    private lateinit var engineInput: EditText
    private lateinit var promptInput: EditText
    private lateinit var lyricsInput: EditText
    private lateinit var status: TextView
    private lateinit var lengthLabel: TextView
    private lateinit var autoLyrics: CheckBox
    private lateinit var instrumental: CheckBox
    private lateinit var studioMode: CheckBox
    private lateinit var generateButton: Button
    private lateinit var playButton: Button
    private lateinit var nextButton: Button
    private lateinit var downloadButton: Button
    private var durationSec = 150
    private var versions = mutableListOf<File>()
    private var versionIndex = 0
    private var player: MediaPlayer? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Color.rgb(8,10,15)
        window.navigationBarColor = Color.rgb(8,10,15)
        setContentView(buildUi())
        val prefs=getSharedPreferences("settings",MODE_PRIVATE)
        engineInput.setText(prefs.getString("engine","http://127.0.0.1:8001"))
        promptInput.setText("Emotional energetic progressive house with intimate male vocals, nostalgic verse, huge memorable chorus, powerful melodic drop, 126 BPM, polished modern mix.")
    }

    private fun buildUi(): View {
        val root=LinearLayout(this).apply{orientation=LinearLayout.VERTICAL;setBackgroundColor(Color.rgb(8,10,15))}
        val scroll=ScrollView(this)
        val body=LinearLayout(this).apply{orientation=LinearLayout.VERTICAL;setPadding(dp(18),dp(18),dp(18),dp(28))}
        scroll.addView(body); root.addView(scroll,LinearLayout.LayoutParams(-1,-1))
        body.addView(label("SongForge Local",28f,true))
        body.addView(label("No paid music API • ACE-Step 1.5 on your own hardware",13f,false).apply{setTextColor(Color.rgb(164,170,186))})
        spacer(body,14)

        body.addView(label("Local engine address",14f,true))
        engineInput=edit("http://192.168.x.x:8001",1)
        body.addView(engineInput)
        body.addView(Button(this).apply{text="Test local engine";setOnClickListener{testEngine()}})
        spacer(body,14)

        body.addView(label("Describe the song",14f,true))
        promptInput=edit("Genre, emotion, vocalist, arrangement, drop, instruments...",5)
        body.addView(promptInput)

        spacer(body,10)
        lengthLabel=label("Length: 150 seconds",14f,true); body.addView(lengthLabel)
        body.addView(SeekBar(this).apply{
            max=270; progress=120
            setOnSeekBarChangeListener(object:SeekBar.OnSeekBarChangeListener{
                override fun onProgressChanged(s:SeekBar?,p:Int,fromUser:Boolean){durationSec=p+30;lengthLabel.text="Length: "+durationSec+" seconds"}
                override fun onStartTrackingTouch(s:SeekBar?){}
                override fun onStopTrackingTouch(s:SeekBar?){}
            })
        })

        autoLyrics=CheckBox(this).apply{text="Auto-write lyrics locally (needs ACE-Step LM, usually >6 GB VRAM)";setTextColor(Color.WHITE);isChecked=true}
        instrumental=CheckBox(this).apply{text="Instrumental only";setTextColor(Color.WHITE)}
        studioMode=CheckBox(this).apply{text="Studio mode — generate 2 different versions";setTextColor(Color.WHITE);isChecked=true}
        body.addView(autoLyrics);body.addView(instrumental);body.addView(studioMode)

        spacer(body,10)
        body.addView(label("Lyrics (optional)",14f,true))
        lyricsInput=edit("[Verse 1]\n...\n\n[Pre-Chorus]\n...\n\n[Chorus]\n...\n\n[Drop]\n",11)
        body.addView(lyricsInput)

        spacer(body,10)
        generateButton=Button(this).apply{text="GENERATE LOCAL SONG";setOnClickListener{generateSong()}}
        body.addView(generateButton,LinearLayout.LayoutParams(-1,dp(58)))

        spacer(body,10)
        status=label("Ready — start the local engine first.",13f,true);body.addView(status)
        spacer(body,10)

        val row=LinearLayout(this).apply{orientation=LinearLayout.HORIZONTAL;gravity=Gravity.CENTER}
        playButton=Button(this).apply{text="Play";isEnabled=false;setOnClickListener{togglePlay()}}
        nextButton=Button(this).apply{text="Version B";isEnabled=false;setOnClickListener{nextVersion()}}
        downloadButton=Button(this).apply{text="Download";isEnabled=false;setOnClickListener{currentFile()?.let{exportSong(it)}}}
        row.addView(playButton,LinearLayout.LayoutParams(0,dp(54),1f))
        row.addView(nextButton,LinearLayout.LayoutParams(0,dp(54),1f).apply{marginStart=dp(6)})
        row.addView(downloadButton,LinearLayout.LayoutParams(0,dp(54),1f).apply{marginStart=dp(6)})
        body.addView(row)

        spacer(body,12)
        body.addView(label("Studio mode creates two different random-seed versions. Section tags such as [Verse], [Pre-Chorus], [Chorus], [Build], [Drop] and [Breakdown] help ACE-Step produce a real arrangement instead of one repeated loop.",12f,false).apply{setTextColor(Color.rgb(150,156,174))})
        return root
    }

    private fun testEngine(){
        saveEngine()
        setBusy(true,"Checking local engine…")
        worker.execute{
            try{
                val json=getJson(base()+"/health",20_000)
                val ok=json.optJSONObject("data")?.optString("status")=="ok" || json.optString("status")=="ok"
                runOnUiThread{setBusy(false,if(ok)"Local ACE-Step engine connected ✓" else "Engine replied, but health response was unexpected")}
            }catch(t:Throwable){runOnUiThread{setBusy(false,"Cannot reach engine: "+safe(t))}}
        }
    }

    private fun generateSong(){
        saveEngine()
        val p=promptInput.text.toString().trim()
        if(p.isBlank()) return toast("Describe the song first")
        setBusy(true,"Submitting local song…")
        versions.clear();versionIndex=0
        worker.execute{
            try{
                val submit=postJson(base()+"/release_task",buildRequest(p),60_000)
                val taskId=submit.optJSONObject("data")?.optString("task_id").orEmpty()
                if(taskId.isBlank()) throw IllegalStateException(submit.optString("error","No task id returned"))
                runOnUiThread{status.text="Generating locally… lower-VRAM hardware can take longer."}
                val results=poll(taskId)
                val downloaded=mutableListOf<File>()
                var generatedLyrics=""
                for(i in 0 until results.length()){
                    val item=results.optJSONObject(i)?:continue
                    if(generatedLyrics.isBlank()) generatedLyrics=item.optString("lyrics","")
                    val rel=item.optString("file","")
                    if(rel.isBlank()) continue
                    downloaded.add(downloadAudio(rel,i))
                }
                if(downloaded.isEmpty()) throw IllegalStateException("Generation finished but no audio file was returned.")
                versions=downloaded
                runOnUiThread{
                    if(generatedLyrics.isNotBlank() && (autoLyrics.isChecked || lyricsInput.text.toString().isBlank())) lyricsInput.setText(generatedLyrics)
                    playButton.isEnabled=true;downloadButton.isEnabled=true;nextButton.isEnabled=versions.size>1
                    val suffix=if(versions.size==1)"" else "s"
                    setBusy(false,"Ready — "+versions.size+" local version"+suffix+" generated")
                    startPlayback(versions[0])
                }
            }catch(t:Throwable){runOnUiThread{setBusy(false,"Generation failed: "+safe(t))}}
        }
    }

    private fun buildRequest(userPrompt:String):JSONObject{
        val lyrics=lyricsInput.text.toString().trim()
        val auto=autoLyrics.isChecked && lyrics.isBlank() && !instrumental.isChecked
        val humanDirection="""
Produce a complete convincing song, not an AI demo and not a repeated loop.
Use a clear musical identity, a strong motif, natural phrase lengths, restrained repetition, evolving instrumentation,
real transitions, clear section contrast, tension before payoff, and a memorable final chorus or drop.
Vocals should sound emotionally intentional and rhythmically singable, not over-worded.
Lyrics should use concrete natural language, consistent perspective, concise singable lines, a short memorable hook,
and avoid generic AI cliches, random poetic filler, forced rhymes, neon/city-lights/shadows/echoes language unless specifically requested.
The arrangement must evolve like a human-produced song and the mix should be balanced, punchy and dynamic.
""".trimIndent()
        val enhanced=(userPrompt+"\n"+humanDirection).take(3800)
        val o=JSONObject()
        if(auto){
            o.put("sample_mode",true)
            o.put("sample_query",enhanced)
        }else{
            o.put("prompt",enhanced)
            o.put("lyrics",if(instrumental.isChecked)"[Instrumental]" else lyrics)
            o.put("use_format",lyrics.isNotBlank() && !instrumental.isChecked)
        }
        o.put("thinking",!instrumental.isChecked)
        o.put("vocal_language","en")
        o.put("audio_duration",durationSec)
        o.put("batch_size",if(studioMode.isChecked)2 else 1)
        o.put("audio_format","mp3")
        o.put("mp3_bitrate","192k")
        o.put("mp3_sample_rate",48000)
        o.put("model","acestep-v15-turbo")
        o.put("inference_steps",8)
        o.put("use_random_seed",true)
        o.put("lm_temperature",0.82)
        o.put("lm_cfg_scale",2.5)
        o.put("lm_top_p",0.9)
        o.put("lm_repetition_penalty",1.08)
        return o
    }

    private fun poll(taskId:String):JSONArray{
        val deadline=System.currentTimeMillis()+45*60*1000L
        while(System.currentTimeMillis()<deadline){
            val q=JSONObject().put("task_id_list",JSONArray().put(taskId))
            val r=postJson(base()+"/query_result",q,30_000)
            val item=r.optJSONArray("data")?.optJSONObject(0)
            when(item?.optInt("status",0)){
                1 -> return JSONArray(item.optString("result","[]"))
                2 -> throw IllegalStateException("Local model reported a failed generation.")
            }
            Thread.sleep(2500)
        }
        throw IllegalStateException("Local generation timed out.")
    }

    private fun downloadAudio(relative:String,index:Int):File{
        val full=if(relative.startsWith("http://")||relative.startsWith("https://")) relative else base()+if(relative.startsWith("/"))relative else "/"+relative
        val c=URL(full).openConnection() as HttpURLConnection
        c.connectTimeout=30_000;c.readTimeout=180_000
        val bytes=try{
            val code=c.responseCode
            if(code !in 200..299) throw IllegalStateException("Audio download failed ("+code+")")
            c.inputStream.use{it.readBytes()}
        }finally{c.disconnect()}
        if(bytes.size<8000) throw IllegalStateException("Generated audio file was unexpectedly small.")
        val dir=File(filesDir,"songs").apply{mkdirs()}
        return File(dir,"local_"+System.currentTimeMillis()+"_"+(index+1)+".mp3").apply{writeBytes(bytes)}
    }

    private fun getJson(url:String,timeout:Int):JSONObject{
        val c=URL(url).openConnection() as HttpURLConnection
        c.connectTimeout=timeout;c.readTimeout=timeout
        val txt=try{
            val code=c.responseCode
            val s=(if(code in 200..299)c.inputStream else c.errorStream)?.bufferedReader()?.use{it.readText()}?:""
            if(code !in 200..299) throw IllegalStateException("HTTP "+code+": "+s.take(500))
            s
        }finally{c.disconnect()}
        return JSONObject(txt)
    }

    private fun postJson(url:String,body:JSONObject,timeout:Int):JSONObject{
        val c=URL(url).openConnection() as HttpURLConnection
        try{
            c.requestMethod="POST";c.doOutput=true;c.connectTimeout=30_000;c.readTimeout=timeout
            c.setRequestProperty("Content-Type","application/json")
            c.outputStream.use{it.write(body.toString().toByteArray())}
            val code=c.responseCode
            val txt=(if(code in 200..299)c.inputStream else c.errorStream)?.bufferedReader()?.use{it.readText()}?:""
            if(code !in 200..299) throw IllegalStateException("Local engine HTTP "+code+": "+txt.take(700))
            val obj=JSONObject(txt)
            if(obj.optInt("code",200)!=200) throw IllegalStateException(obj.optString("error","Local engine error"))
            return obj
        }finally{c.disconnect()}
    }

    private fun nextVersion(){
        if(versions.size<2)return
        versionIndex=(versionIndex+1)%versions.size
        val next=(('A'.code+((versionIndex+1)%versions.size)).toChar()).toString()
        nextButton.text="Version "+next
        startPlayback(versions[versionIndex])
        status.text="Playing Version "+(('A'.code+versionIndex).toChar())
    }

    private fun currentFile():File?=versions.getOrNull(versionIndex)
    private fun togglePlay(){
        val f=currentFile()?:return
        val p=player
        if(p==null)startPlayback(f)
        else if(p.isPlaying){p.pause();playButton.text="Play"}
        else{p.start();playButton.text="Pause"}
    }
    private fun startPlayback(file:File){
        player?.release()
        player=MediaPlayer().apply{
            setDataSource(file.absolutePath)
            setOnPreparedListener{it.start();playButton.text="Pause"}
            setOnCompletionListener{playButton.text="Play"}
            prepareAsync()
        }
    }
    private fun exportSong(file:File){
        try{
            val values=ContentValues().apply{
                put(MediaStore.Audio.Media.DISPLAY_NAME,file.name)
                put(MediaStore.Audio.Media.MIME_TYPE,"audio/mpeg")
                if(Build.VERSION.SDK_INT>=29)put(MediaStore.Audio.Media.RELATIVE_PATH,"Music/SongForgeLocal")
            }
            val uri=contentResolver.insert(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI,values)?:throw IllegalStateException("Could not create file")
            contentResolver.openOutputStream(uri)?.use{out->file.inputStream().use{it.copyTo(out)}}
            toast("Saved to Music/SongForgeLocal")
        }catch(t:Throwable){toast("Download failed: "+safe(t))}
    }

    private fun saveEngine(){getSharedPreferences("settings",MODE_PRIVATE).edit().putString("engine",engineInput.text.toString().trim()).apply()}
    private fun base():String=engineInput.text.toString().trim().trimEnd('/')
    private fun setBusy(b:Boolean,msg:String){status.text=msg;generateButton.isEnabled=!b}
    private fun safe(t:Throwable)=((t.message?:t.javaClass.simpleName)).take(800)
    private fun toast(s:String)=Toast.makeText(this,s,Toast.LENGTH_LONG).show()
    private fun dp(v:Int)=(v*resources.displayMetrics.density).toInt()
    private fun spacer(p:LinearLayout,h:Int){p.addView(Space(this),LinearLayout.LayoutParams(1,dp(h)))}
    private fun label(v:String,s:Float,b:Boolean)=TextView(this).apply{text=v;textSize=s;setTextColor(Color.WHITE);if(b)setTypeface(typeface,android.graphics.Typeface.BOLD)}
    private fun edit(h:String,min:Int)=EditText(this).apply{
        hint=h;setHintTextColor(Color.rgb(110,116,132));setTextColor(Color.WHITE);setBackgroundColor(Color.rgb(23,27,36))
        setPadding(dp(12),dp(10),dp(12),dp(10));setMinLines(min);gravity=Gravity.TOP
        inputType=InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
    }
    override fun onDestroy(){super.onDestroy();player?.release();worker.shutdownNow()}
}
