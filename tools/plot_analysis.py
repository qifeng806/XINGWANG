# -*- coding: utf-8 -*-
"""生成数据分析图（docs/数据分析图.png）。"""

import sys
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reference_parse as rp  # noqa: E402

C = {n: i for i, (n, _, _, _) in enumerate(rp.FIELDS)}

for f in ("Microsoft YaHei", "SimHei", "DengXian"):
    try:
        font_manager.findfont(f, fallback_to_default=False)
        plt.rcParams["font.sans-serif"] = [f]
        break
    except Exception:
        continue
plt.rcParams["axes.unicode_minus"] = False

FPS = 20.8  # 由 0.10Hz 激励周期 ≈208 帧反推


def load(path):
    rc, _ = rp.parse(path, correct=True)
    rr, _ = rp.parse(path, correct=False)
    return rc, rr


def unwrap(v):
    return np.degrees(np.unwrap(np.radians(np.asarray(v, dtype=float))))


def col(rows, idx):
    return np.array([r[2][idx] for r in rows], dtype=float)


def main():
    out, f05, f06, f07 = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    c05, r05 = load(f05)
    c06, r06 = load(f06)
    c07, r07 = load(f07)

    fig = plt.figure(figsize=(16.5, 9.5))
    fig.suptitle("星网转台 p20r20y10 0.10Hz 实测数据分析（帧率≈20.8Hz）", fontsize=14)

    # ① 电机使能：方位跟踪
    ax = fig.add_subplot(2, 3, 1)
    t = np.arange(len(c05)) / FPS
    ax.plot(t, unwrap(col(r05, C["AZ_TARGET"])), label="方位目标(原始)", lw=1.4)
    ax.plot(t, unwrap(col(c05, C["AZ_ENCODER"])), label="方位编码器", lw=1.1, ls="--")
    ax.set_title("① 05 电机使能：编码器跟随原始目标\n误差RMS=0.81°，滞后2帧(≈96ms)")
    ax.set_xlabel("时间 (s)")
    ax.set_ylabel("角度 (°)")
    ax.legend(fontsize=8)
    ax.grid(alpha=.3)

    # ② 方位前馈 vs 方位目标
    ax = fig.add_subplot(2, 3, 2)
    for rows_r, name, color, mk in ((r05, "05 减前馈(电机使能)", "#1E88E5", "o"),
                                    (r06, "06 减前馈(电机失能)", "#26A69A", "s"),
                                    (r07, "07 加前馈(电机失能)", "#E53935", "^")):
        tg = unwrap(col(rows_r, C["AZ_TARGET"]))
        ff = col(rows_r, C["AZ_FF"])
        k = np.polyfit(tg, ff, 1)[0]
        ax.scatter(tg, ff, s=6, alpha=.35, color=color, marker=mk,
                   label="%s  斜率=%+.2f" % (name, k))
    ax.set_title("② 加/减“方位前馈速度”：曲线整体镜像\n（符号相反、幅值不变）")
    ax.set_xlabel("方位目标角 (°)")
    ax.set_ylabel("方位前馈速度 (°/s)")
    ax.legend(fontsize=8)
    ax.grid(alpha=.3)

    # ③ 俯仰：前馈与目标角速度同相
    ax = fig.add_subplot(2, 3, 4)
    d_el = np.gradient(unwrap(col(r06, C["EL_TARGET"]))) * FPS
    ff_el = col(r06, C["EL_FF"])
    ax.plot(d_el[1:-1], label="俯仰目标角速度 (°/s)", lw=1.2)
    ax.plot(ff_el[1:-1], label="俯仰前馈速度 (°/s)", lw=1.2)
    ax.set_title("③ 俯仰(EL)：前馈与目标角速度同相 r=+0.98")
    ax.set_xlabel("帧序号")
    ax.set_ylabel("°/s")
    ax.legend(fontsize=8)
    ax.grid(alpha=.3)

    # ④ 方位：前馈与目标角速度无关
    ax = fig.add_subplot(2, 3, 5)
    d_az = np.gradient(unwrap(col(r06, C["AZ_TARGET"]))) * FPS
    ax.plot(d_az[1:-1], label="方位目标角速度 (°/s)", lw=1.2)
    ax.plot(col(r06, C["AZ_FF"]), label="方位前馈速度 (°/s)", lw=1.2)
    ax.set_title("④ 方位(AZ)：与目标角速度无关 r=−0.26\n却与目标角位置同相 r=−0.91")
    ax.set_xlabel("帧序号")
    ax.set_ylabel("°/s")
    ax.legend(fontsize=8)
    ax.grid(alpha=.3)

    # ⑤ 电机失能：编码器冻结
    ax = fig.add_subplot(2, 3, 6)
    ax.plot(col(c06, C["AZ_ENCODER"]), label="方位编码器(恒定)", lw=1.6)
    ax.plot(col(c06, C["EL_ENCODER"]), label="俯仰编码器(恒定)", lw=1.6)
    ax.plot(col(c06, C["RL_ENCODER"]), label="横滚编码器(恒定)", lw=1.6)
    ax.plot(col(c06, C["AZ_SPEED"]), label="方位运行速度(上报)", lw=1.0, alpha=.8)
    ax.set_title("⑤ 06 电机失能：编码器冻结不动\n“运行速度”仍报 ±20~40°/s")
    ax.set_xlabel("帧序号")
    ax.set_ylabel("°  /  °/s")
    ax.legend(fontsize=8)
    ax.grid(alpha=.3)

    fig.tight_layout(rect=(0, 0, 1, 0.96))
    fig.savefig(out, dpi=110)
    print("ok ->", out)


if __name__ == "__main__":
    main()
