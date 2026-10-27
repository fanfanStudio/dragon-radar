package com.dragonradar.companion

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.webkit.JavascriptInterface
import android.webkit.WebView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import org.json.JSONArray

class MainActivity : AppCompatActivity() {

    private lateinit var web: WebView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        web = WebView(this)
        setContentView(web)
        web.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            allowFileAccess = true
        }
        web.addJavascriptInterface(Bridge(), "DR")
        web.loadUrl("file:///android_asset/index.html")

        requestPerms()
        requestBatteryIgnore()
        ContextCompat.startForegroundService(this, Intent(this, RadarService::class.java))
    }

    /* 请求忽略电池优化: MIUI/HyperOS 后台杀服务的主要来源, 同意后划掉界面也能保活 */
    private fun requestBatteryIgnore() {
        try {
            val pm = getSystemService(POWER_SERVICE) as android.os.PowerManager
            if (!pm.isIgnoringBatteryOptimizations(packageName)) {
                val i = Intent(android.provider.Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS)
                    .setData(android.net.Uri.parse("package:$packageName"))
                startActivity(i)
            }
        } catch (_: Exception) {}
    }

    override fun onDestroy() {
        web.destroy()
        super.onDestroy()
    }

    private fun requestPerms() {
        val need = ArrayList<String>()
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION)
            != PackageManager.PERMISSION_GRANTED) {
            need.add(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        if (Build.VERSION.SDK_INT >= 33 &&
            ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
            != PackageManager.PERMISSION_GRANTED) {
            need.add(Manifest.permission.POST_NOTIFICATIONS)
        }
        if (need.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, need.toTypedArray(), 100)
        }
    }

    inner class Bridge {
        /** JS -> 本地: 龙珠列表变化, [[lat,lng,id],...] */
        @JavascriptInterface
        fun setBalls(json: String) {
            try {
                val arr = JSONArray(json)
                val list = ArrayList<Ball>(arr.length())
                for (i in 0 until arr.length()) {
                    val b = arr.getJSONArray(i)
                    list.add(Ball(b.getInt(2), b.getDouble(0), b.getDouble(1)))
                }
                Hub.balls.clear()
                Hub.balls.addAll(list)
            } catch (_: Exception) {}
        }

        /** JS -> 本地: 清空龙珠(两端联动) */
        @JavascriptInterface
        fun clearBalls() {
            /* 关键: 必须同时清空 Hub.balls, 否则下一帧 300ms 的状态推送
             * 会把旧龙珠原样再发一遍, 掌机刚清完又长回来 */
            Hub.balls.clear()
            Hub.foundIds = emptySet()
            Hub.clearFlag = true
        }

        /** JS 轮询: 读取实时状态 */
        @JavascriptInterface
        fun snapshot(): String = Hub.snapshot()

        @JavascriptInterface
        fun toast(msg: String) {
            runOnUiThread { Toast.makeText(this@MainActivity, msg, Toast.LENGTH_SHORT).show() }
        }
    }
}
