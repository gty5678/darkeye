"""Compare the old DVD recorder and independent QRhi demo JSON measurements."""
import argparse
import json
from pathlib import Path


def number(value):
    return "—" if value is None else f"{value:.2f}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old", type=Path, required=True)
    parser.add_argument("--new", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    old = json.loads(args.old.read_text(encoding="utf-8-sig"))
    new = json.loads(args.new.read_text(encoding="utf-8-sig"))
    old_rows = {row["totalDvdCount"]: row for row in old["measurements"]}
    lines = [
        "# QRhi DVD 独立 Demo 性能记录", "",
        f"Qt：旧版 {old['qtVersion']}，新版 {new['qtVersion']}；后端：{new['backend']}。",
        "使用相同的源封面与作品列表，最大驻留窗口 60 件，1280×720 逻辑像素。",
        "旧版采用 HDR/IBL/AO，新版采用基础 Metallic-Roughness、环境光及方向光。",
        "两者均由 16ms PreciseTimer 推进相机；每组预热后采样 30 次，静态场景按需绘制。",
        "场景渲染/帧提交频率及 CPU 时间用于短程原型回归，未测量屏幕实际呈现 FPS。",
        "GPU 执行时间和驱动显存尚未采集，完整视觉与性能验收留待业务接入阶段。", "",
        "| DVD 数量 | 旧场景渲染 Hz | 新帧提交 Hz | 旧 P95 交互步 ms | 新 P95 交互步 ms | 旧/新进程 CPU s | 新 P95 CPU 提交 ms | 新 Draw Calls | 新封面纹理 MiB |",
        "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in new["measurements"]:
        count = row["totalDvdCount"]
        previous = old_rows[count]
        dimensions = ("pixelWidth", "pixelHeight", "dpr")
        if any(previous[key] != row[key] for key in dimensions):
            raise RuntimeError(f"Cannot compare different pixel sizes/DPR at {count} DVDs")
        lines.append(
            f"| {count} | {number(previous.get('sceneRenderHz'))} | "
            f"{number(row['frameSubmissionHz'])} | {number(previous['p95InteractionStepMs'])} | "
            f"{number(row['p95InteractionStepMs'])} | "
            f"{number(previous['processCpuSeconds'])}/{number(row['processCpuSeconds'])} | "
            f"{number(row['p95CpuSubmissionMs'])} | "
            f"{row['drawCalls']} | {number(row['sourceTextureBytes'] / 1048576)} |"
        )
    lines += ["", "计数说明：新 Draw Calls 包含一个显示颜色编码 pass；旧 Qt Quick 3D 的内部 Draw Calls 未采集。",
              "新封面纹理数据为 GPU 源纹理的 texel/mipmap 估算，未包含 RenderTarget、驱动对齐和隐式缓存。",
              "进程 CPU 时间包括进程内所有线程，短采样受系统计时粒度影响，0.00 不表示没有 CPU 工作。",
              "源封面仅 8 张循环复用；本记录验证不同总量的虚拟化行为，不能推断 1000 张独立封面的冷加载成本。",
              "输入采样文件由 --old 与 --new 指定；[画面对照说明](qrhi3d-visual-comparison.md)。", ""]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
