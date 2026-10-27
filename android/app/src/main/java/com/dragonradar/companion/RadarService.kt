package com.dragonradar.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.IBinder
import android.os.Looper
import android.util.Log
import androidx.core.app.NotificationCompat
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedReader
import java.io.BufferedOutputStream
import java.io.InputStreamReader
import java.io.PrintWriter
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import kotlin.concurrent.thread

/**
 * 前台服务: GPS 采集 + TCP 9000 服务端
 * 每 300ms 把本人坐标与龙珠列表下发给掌机; 接收掌机回传的已找到状态。
 */
class RadarService : Service() {

    private var serverThread: Thread? = null
    @Volatile private var running = false
    private var locCb: LocationListener? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val nm = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
        /* MIUI 会把 LOW 重要级的常驻通知折叠进"静默通知", 用户看不到;
         * 用 DEFAULT 级别保证在通知列表直接可见, 但显式静音免打扰 */
        nm.deleteNotificationChannel("radar")
        val ch = NotificationChannel(CHANNEL, "龙珠雷达", NotificationManager.IMPORTANCE_DEFAULT)
        ch.setSound(null, null)
        ch.setShowBadge(false)
        nm.createNotificationChannel(ch)
        running = true
        Hub.selfIp = localIp() ?: ""

        /* 通知栏带"关闭"按钮: addAction + 自定义布局双保险(部分 ROM 不渲染 addAction) */
        val stopPi = android.app.PendingIntent.getService(
            this, 2,
            Intent(this, RadarService::class.java).setAction(ACTION_STOP),
            android.app.PendingIntent.FLAG_IMMUTABLE
        )
        val rv = android.widget.RemoteViews(packageName, R.layout.notification_radar)
        rv.setOnClickPendingIntent(R.id.btn_stop, stopPi)
        val n: Notification = NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.ic_menu_mylocation)
            .setOngoing(true)
            .setContentTitle("龙珠雷达正在运行")
            .setContentText("定位与龙珠数据下发中")
            .setCustomContentView(rv)
            .setStyle(NotificationCompat.DecoratedCustomViewStyle())
            .addAction(0, "关闭", stopPi)
            .build()
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        } else {
            startForeground(1, n)
        }

        if (intent?.action == ACTION_STOP) {   /* 来自通知"关闭"按钮 */
            stopRadar()
            return START_NOT_STICKY
        }

        /* 自愈式启动: 不依赖 started 标志, 每次都确保三个组件活着。
         * 旧线程通过 generation 号感知换代后自动退出, 先同步关掉旧套接字再起新线程,
         * 避免"服务在但端口没监听/UDP 不应答"的僵尸状态(掌机会永远发现不了手机)。 */
        locCb?.let {
            try { (getSystemService(LOCATION_SERVICE) as LocationManager).removeUpdates(it) } catch (_: Exception) {}
            locCb = null
        }
        startGps()
        startUdp()
        startServer()
        return START_STICKY
    }

    /* 用户主动停止: 掐断 accept 循环、断开掌机、移除通知 */
    private fun stopRadar() {
        running = false
        try { serverSocket?.close() } catch (_: Exception) {}
        try { udpSock?.close() } catch (_: Exception) {}
        try { currentClient?.close() } catch (_: Exception) {}
        currentClient = null
        Hub.linkOk = false
        if (Build.VERSION.SDK_INT >= 33) stopForeground(STOP_FOREGROUND_REMOVE)
        else @Suppress("DEPRECATION") stopForeground(true)
        stopSelf()
    }

    override fun onDestroy() {
        running = false
        locCb?.let {
            try { (getSystemService(LOCATION_SERVICE) as LocationManager).removeUpdates(it) } catch (_: Exception) {}
        }
        try { serverSocket?.close() } catch (_: Exception) {}
        try { udpSock?.close() } catch (_: Exception) {}
        try { currentClient?.close() } catch (_: Exception) {}
        Hub.linkOk = false
        super.onDestroy()
    }

    /* ---------------- GPS ---------------- */
    private fun startGps() {
        val lm = getSystemService(LOCATION_SERVICE) as LocationManager
        val cb = object : LocationListener {
            override fun onLocationChanged(l: Location) {
                val (gla, glo) = Geo.wgs2gcj(l.latitude, l.longitude)
                Hub.meLat = gla
                Hub.meLng = glo
                Hub.meAcc = l.accuracy.toDouble()
                if (l.hasBearing()) Hub.meHdg = l.bearing.toDouble()
                Hub.haveMe = true
                Hub.gpsOn = true
            }
            override fun onProviderDisabled(provider: String) { Hub.gpsOn = false }
            override fun onProviderEnabled(provider: String) { Hub.gpsOn = true }
        }
        locCb = cb
        try {
            lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 0f, cb, Looper.getMainLooper())
        } catch (e: Exception) { Log.w(TAG, "gps provider: ${e.message}") }
        try {
            lm.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 2000L, 0f, cb, Looper.getMainLooper())
        } catch (e: Exception) { Log.w(TAG, "network provider: ${e.message}") }
        // 初始位置用 lastKnown 兜底, 避免首屏空白
        for (p in listOf(LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER)) {
            try {
                val l = lm.getLastKnownLocation(p) ?: continue
                val (gla, glo) = Geo.wgs2gcj(l.latitude, l.longitude)
                Hub.meLat = gla; Hub.meLng = glo
                Hub.meAcc = l.accuracy.toDouble()
                Hub.haveMe = true
                break
            } catch (_: Exception) {}
        }
    }

    /* ---------------- UDP 发现(掌机广播 -> 回本机 IP) ---------------- */
    private fun localIp(): String? {
        try {
            val nis = java.net.NetworkInterface.getNetworkInterfaces() ?: return null
            var fallback: String? = null
            for (ni in nis) {
                if (!ni.isUp) continue
                val name = ni.name ?: ""
                /* 热点与 Station 接口名含 wlan 或 ap; 移动数据接口(rmnet/ccmni 等)
                 * 的 IP 对掌机不可达, 开热点+流量同开时接口遍历顺序不保证, 必须优先选 wlan */
                val isWifi = name.contains("wlan") || name.contains("ap") || name.contains("softap")
                for (ia in ni.inetAddresses) {
                    if (!ia.isLoopbackAddress && ia is java.net.Inet4Address) {
                        if (isWifi) return ia.hostAddress
                        if (fallback == null) fallback = ia.hostAddress
                    }
                }
            }
            return fallback
        } catch (_: Exception) {}
        return null
    }

    private var udpThread: Thread? = null
    @Volatile private var udpSock: java.net.DatagramSocket? = null
    @Volatile private var udpGen = 0
    private fun startUdp() {
        udpGen++
        val myGen = udpGen
        try { udpSock?.close() } catch (_: Exception) {}   /* 旧线程的 receive 立刻抛异常退出 */
        val ds = java.net.DatagramSocket(9999)
        ds.broadcast = true
        ds.soTimeout = 500
        udpSock = ds
        udpThread = thread(name = "udp-disc-$myGen") {
            val buf = ByteArray(64)
            while (running && udpGen == myGen) {
                try {
                    val p = java.net.DatagramPacket(buf, buf.size)
                    ds.receive(p)
                    val s = String(p.data, 0, p.length)
                    if (s.startsWith("DRAGONRADAR")) {
                        /* 每次应答都重新取 IP: Wi-Fi/热点切换时接口会变 */
                        val ip = localIp() ?: Hub.selfIp
                        if (ip.isNotEmpty()) {
                            Hub.selfIp = ip
                            val reply = "DRAGONRADAR:$ip".toByteArray()
                            ds.send(java.net.DatagramPacket(reply, reply.size, p.address, p.port))
                        }
                    }
                } catch (_: java.net.SocketTimeoutException) {
                } catch (_: Exception) {
                    break   /* 套接字被换代关闭 */
                }
            }
            try { ds.close() } catch (_: Exception) {}
        }
    }

    /* ---------------- TCP 服务 ---------------- */
    /* 当前活跃的掌机连接; 新连接会顶掉旧连接(解决死连接卡住写线程) */
    @Volatile private var currentClient: Socket? = null
    @Volatile private var serverSocket: ServerSocket? = null
    @Volatile private var serverGen = 0

    private fun startServer() {
        serverGen++
        val myGen = serverGen
        try { serverSocket?.close() } catch (_: Exception) {}   /* 旧 accept 立刻抛异常退出 */
        val ss = ServerSocket()
        serverSocket = ss
        try {
            ss.reuseAddress = true
            ss.bind(InetSocketAddress(9000))
        } catch (e: Exception) {
            Log.e(TAG, "bind 9000 failed: ${e.message}")
            return
        }
        serverThread = thread(start = true, name = "tcp-accept-$myGen") {
            while (running && serverGen == myGen) {
                val s = try { ss.accept() } catch (e: Exception) { break }
                s.tcpNoDelay = true
                s.soTimeout = 6000   /* 读超时: 用于快速感知死连接 */
                Log.i(TAG, "radar connected: ${s.inetAddress?.hostAddress}")
                /* 踢掉旧客户端: close() 会立刻解除旧写线程的阻塞 */
                currentClient?.let { old -> try { old.close() } catch (_: Exception) {} }
                currentClient = s
                Hub.linkOk = true
                Hub.foundIds = emptySet()
                thread(start = true, name = "tcp-client") { handleClient(s) }
            }
            try { ss.close() } catch (_: Exception) {}
        }
    }

    private fun dropClient(s: Socket) {
        if (currentClient === s) {
            currentClient = null
            Hub.linkOk = false
        }
        try { s.close() } catch (_: Exception) {}
    }

    private fun handleClient(s: Socket) {
        val out = PrintWriter(BufferedOutputStream(s.getOutputStream()), false)
        // 读线程: 接收掌机状态(已找到清单); 读超时只是心跳空转, 不代表断开
        thread(name = "tcp-read") {
            try {
                val br = BufferedReader(InputStreamReader(s.getInputStream()))
                while (currentClient === s) {
                    val line = try { br.readLine() } catch (_: java.net.SocketTimeoutException) { continue } ?: break
                    if (!line.contains("\"st\"")) continue
                    try {
                        val o = JSONObject(line)
                        val arr = o.optJSONArray("found") ?: continue
                        val set = HashSet<Int>()
                        for (i in 0 until arr.length()) set.add(arr.getInt(i))
                        Hub.foundIds = set
                    } catch (_: Exception) {}
                }
            } catch (_: Exception) {}
            dropClient(s)
        }
        try {
            while (currentClient === s) {
                if (Hub.clearFlag) {
                    Hub.clearFlag = false
                    out.print("{\"cmd\":\"clear\"}\n")
                    out.flush()
                }
                out.print(buildStateLine())
                out.flush()
                Thread.sleep(300)
            }
        } catch (_: Exception) {}
        dropClient(s)
    }

    private fun buildStateLine(): String {
        val o = JSONObject()
        o.put("la", Hub.meLat)
        o.put("lo", Hub.meLng)
        o.put("acc", Hub.meAcc)
        o.put("hdg", Hub.meHdg)
        val arr = JSONArray()
        for (b in Hub.balls) {
            val item = JSONArray()
            item.put(b.lat); item.put(b.lng); item.put(b.id)
            arr.put(item)
        }
        o.put("balls", arr)
        return o.toString() + "\n"
    }

    companion object {
        private const val TAG = "RadarService"
        const val ACTION_STOP = "com.dragonradar.companion.STOP"
        private const val CHANNEL = "radar_v2"
    }
}
