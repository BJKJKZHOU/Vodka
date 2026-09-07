#!/usr/bin/env python3
"""Generate JustCanFd FAST plot metadata from AxDr_L parameter.yaml."""

from __future__ import annotations

import argparse
import os
from pathlib import Path

try:
    import yaml
except ImportError as exc:
    raise SystemExit("PyYAML is required: python -m pip install PyYAML") from exc

HERE = Path(__file__).resolve().parent
VODKA_ROOT = HERE.parents[1]
DEFAULT_SOURCE = VODKA_ROOT.parent / "AxDr_L_Motor" / "Parameter" / "parameter.yaml"
OUTPUT = HERE / "axdr_plot_meta.generated.h"


def source_path(cli_source: str | None) -> Path:
    if cli_source:
        return Path(cli_source).expanduser().resolve()

    motor_root = os.environ.get("AXDR_L_MOTOR_ROOT")
    if motor_root:
        return Path(motor_root).expanduser().resolve() / "Parameter" / "parameter.yaml"

    return DEFAULT_SOURCE


def load_plot_scales(path: Path) -> list[tuple[int, float, str]]:
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schema") != 1:
        raise ValueError("unsupported AxDr parameter schema")

    objects = data.get("objects")
    if not isinstance(objects, dict):
        raise ValueError("parameter objects must be a mapping")

    result: list[tuple[int, float, str]] = []
    for name, obj in objects.items():
        if not isinstance(obj, dict) or "plot_scale" not in obj:
            continue

        scale = float(obj["plot_scale"])
        if scale <= 0.0:
            continue

        object_id = obj.get("id")
        if not isinstance(object_id, int) or not 0 <= object_id <= 0xFFFF:
            raise ValueError(f"{name}: invalid id")
        if obj.get("type") != "f32":
            raise ValueError(f"{name}: FAST plot metadata requires f32")

        result.append((object_id, scale, name))

    result.sort(key=lambda item: item[0])
    return result


def c_float(value: float) -> str:
    text = f"{value:.9g}"
    if "e" not in text.lower() and "." not in text:
        text += ".0"
    return text + "f"


def render(entries: list[tuple[int, float, str]], source: Path) -> str:
    lines = [
        "/* Generated from AxDr_L_Motor/Parameter/parameter.yaml. DO NOT EDIT. */",
        "#ifndef AXDR_PLOT_META_GENERATED_H",
        "#define AXDR_PLOT_META_GENERATED_H",
        "",
        "#include <cstdint>",
        "",
        "struct AxDrPlotScaleEntry",
        "{",
        "    uint16_t id;",
        "    float scale;",
        "};",
        "",
        "static const AxDrPlotScaleEntry kAxDrPlotScales[] =",
        "{",
    ]

    for object_id, scale, name in entries:
        lines.append(f"    {{ 0x{object_id:04X}U, {c_float(scale)} }}, // {name}")

    lines += [
        "};",
        "",
        "static const int kAxDrPlotScaleCount =",
        "    static_cast<int>(sizeof(kAxDrPlotScales) / sizeof(kAxDrPlotScales[0]));",
        "",
        "#endif // AXDR_PLOT_META_GENERATED_H",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", help="Path to AxDr_L_Motor/Parameter/parameter.yaml")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    source = source_path(args.source)
    try:
        content = render(load_plot_scales(source), source)
    except (OSError, ValueError, yaml.YAMLError) as exc:
        print(f"AxDr parameter sync failed: {exc}")
        return 2

    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != content:
            print(f"stale: {OUTPUT.relative_to(VODKA_ROOT)}")
            return 1
        print("JustCanFd AxDr plot metadata is up to date")
        return 0

    OUTPUT.write_text(content, encoding="utf-8")
    print(OUTPUT.relative_to(VODKA_ROOT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
