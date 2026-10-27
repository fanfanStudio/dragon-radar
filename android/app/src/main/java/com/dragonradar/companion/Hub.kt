package com.dragonradar.companion

import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.CopyOnWriteArrayList

data class Ball(val id: Int, val lat: Double, val lng: Double)

/** 掌机服务与界面共享的实时状态 */
object Hub {
    @Volatile var meLat: Double = 0.0
    @Volatile var meLng: Double = 0.0
    @Volatile var meAcc: Double = 0.0
    @Volatile var meHdg: Double = 0.0
    @Volatile var haveMe: Boolean = false
    @Volatile var gpsOn: Boolean = false
    @Volatile var linkOk: Boolean = false
    @Volatile var foundIds: Set<Int> = emptySet()
    @Volatile var clearFlag: Boolean = false   // UI -> 服务: 发送清空命令
    @Volatile var selfIp: String = ""          // 本机局域网 IP, 给掌机发现/用户查看

    val balls = CopyOnWriteArrayList<Ball>()

    fun snapshot(): String {
        val o = JSONObject()
        o.put("la", meLat)
        o.put("lo", meLng)
        o.put("acc", meAcc)
        o.put("hdg", meHdg)
        o.put("have", haveMe)
        o.put("gpsOn", gpsOn)
        o.put("link", linkOk)
        o.put("ip", selfIp)
        o.put("found", JSONArray(foundIds.sorted()))
        val arr = JSONArray()
        for (b in balls) {
            val item = JSONArray()
            item.put(b.lat); item.put(b.lng); item.put(b.id)
            arr.put(item)
        }
        o.put("balls", arr)
        return o.toString()
    }
}
