# -*- coding: utf-8 -*-
"""Dragon Radar 掌机端一键部署 (SSH/SFTP)

用法:
    python tools/deploy.py <掌机IP> [用户名]
密码从环境变量 RADAR_PASS 读取, 未设置则交互输入(不会写进代码/历史)。

流程: 杀掉运行中的进程 -> SFTP 上传 -> chmod -> 后台试运行验证
注意: 覆盖正在执行的二进制会失败(掌机 SD 卡为 FAT), 所以必须先 kill -9。
"""
import getpass
import os
import sys

import paramiko

REMOTE_DIR = "/mnt/SDCARD/Apps/DragonRadar"

# (本地路径, 远端路径) —— 构建产物 + 启动脚本 + 配置模板
FILES = [
    ("build/dragonradar", REMOTE_DIR + "/dragonradar"),
    ("tools/launch.sh", REMOTE_DIR + "/launch.sh"),
    ("tools/config.json", REMOTE_DIR + "/config.json"),
    ("tools/phone_ip.txt", REMOTE_DIR + "/phone_ip.txt"),
]


def run(ssh, cmd, timeout=15):
    _, out, err = ssh.exec_command(cmd, timeout=timeout)
    return (
        out.read().decode("utf-8", "replace").strip(),
        err.read().decode("utf-8", "replace").strip(),
    )


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    host = sys.argv[1]
    user = sys.argv[2] if len(sys.argv) > 2 else "root"
    pwd = os.environ.get("RADAR_PASS") or getpass.getpass("password for %s@%s: " % (user, host))

    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    print("[1/4] 连接 %s ..." % host)
    ssh.connect(host, username=user, password=pwd, timeout=10)
    print("  arch:", run(ssh, "uname -m")[0])

    print("[2/4] 结束运行中的程序 ...")
    # 必须 kill -9: SIGTERM 打不断 fb 直写主循环, 残留进程会和新进程抢 framebuffer
    print("  ", run(ssh, "kill -9 $(pidof dragonradar) 2>/dev/null; sleep 2; echo done")[0])

    print("[3/4] SFTP 上传 ...")
    sftp = ssh.open_sftp()
    for d in (REMOTE_DIR, REMOTE_DIR + "/lib"):
        try:
            sftp.mkdir(d)
        except IOError:
            pass
    for local, remote in FILES:
        if not os.path.exists(local):
            print("   skip (missing):", local)
            continue
        sftp.put(local, remote)
        print("   ->", remote)
    sftp.close()

    run(ssh, "chmod +x %s/dragonradar %s/launch.sh" % (REMOTE_DIR, REMOTE_DIR))

    print("[4/4] 后台启动 ...")
    run(
        ssh,
        'sh -c "cd %s; LD_LIBRARY_PATH=%s/lib SDL_AUDIODRIVER=alsa '
        './dragonradar >/tmp/radar.log 2>&1 &"' % (REMOTE_DIR, REMOTE_DIR),
    )
    import time

    time.sleep(4)
    print("  pid:", run(ssh, "pidof dragonradar")[0])
    print("  log:", run(ssh, "tail -5 /tmp/radar.log")[0])
    ssh.close()
    print("完成。")


if __name__ == "__main__":
    main()
