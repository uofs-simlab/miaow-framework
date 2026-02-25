#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path


def escape(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def svg_box(x: int, y: int, width: int, height: int, label: str) -> str:
    text_x = x + width // 2
    text_y = y + height // 2 + 6
    return f"""
  <rect x="{x}" y="{y}" width="{width}" height="{height}" rx="12" ry="12"
        fill="#F8FAFC" stroke="#334155" stroke-width="2"/>
  <text x="{text_x}" y="{text_y}" text-anchor="middle"
        font-family="Helvetica, Arial, sans-serif" font-size="20" fill="#0F172A">{escape(label)}</text>
""".rstrip()


def svg_execute_box(x: int, y: int, label: str) -> str:
    return svg_box(x, y, 250, 54, label)


def svg_cat_node(center_x: int, center_y: int, label: str) -> str:
    left_ear = f"{center_x - 40},{center_y - 52} {center_x - 14},{center_y - 90} {center_x - 2},{center_y - 48}"
    right_ear = f"{center_x + 2},{center_y - 48} {center_x + 14},{center_y - 90} {center_x + 40},{center_y - 52}"
    return f"""
  <g>
    <polygon points="{left_ear}" fill="#F5D0A9" stroke="#7C2D12" stroke-width="2"/>
    <polygon points="{right_ear}" fill="#F5D0A9" stroke="#7C2D12" stroke-width="2"/>
    <circle cx="{center_x}" cy="{center_y - 20}" r="42" fill="#FDBA74" stroke="#7C2D12" stroke-width="2"/>
    <circle cx="{center_x - 14}" cy="{center_y - 28}" r="4" fill="#0F172A"/>
    <circle cx="{center_x + 14}" cy="{center_y - 28}" r="4" fill="#0F172A"/>
    <polygon points="{center_x},{center_y - 16} {center_x - 5},{center_y - 8} {center_x + 5},{center_y - 8}"
             fill="#9F1239"/>
    <line x1="{center_x - 32}" y1="{center_y - 16}" x2="{center_x - 56}" y2="{center_y - 20}"
          stroke="#7C2D12" stroke-width="2"/>
    <line x1="{center_x - 32}" y1="{center_y - 10}" x2="{center_x - 56}" y2="{center_y - 6}"
          stroke="#7C2D12" stroke-width="2"/>
    <line x1="{center_x + 32}" y1="{center_y - 16}" x2="{center_x + 56}" y2="{center_y - 20}"
          stroke="#7C2D12" stroke-width="2"/>
    <line x1="{center_x + 32}" y1="{center_y - 10}" x2="{center_x + 56}" y2="{center_y - 6}"
          stroke="#7C2D12" stroke-width="2"/>
    <text x="{center_x}" y="{center_y + 45}" text-anchor="middle"
          font-family="Helvetica, Arial, sans-serif" font-size="18" fill="#0F172A">{escape(label)}</text>
  </g>
""".rstrip()


def svg_arrow(x1: int, y1: int, x2: int, y2: int) -> str:
    return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#334155" stroke-width="2.5" marker-end="url(#arrow)"/>'


def build_svg() -> str:
    width = 1280
    height = 840

    runtime_x, runtime_y, runtime_w, runtime_h = 420, 40, 440, 64
    orch_x, orch_y, orch_w, orch_h = 470, 160, 340, 64
    job_x, job_y, job_w, job_h = 510, 280, 260, 64

    cat_centers = [(260, 500), (640, 500), (1020, 500)]
    exec_boxes = [(135, 690), (515, 690), (895, 690)]

    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        "  <defs>",
        '    <marker id="arrow" markerWidth="10" markerHeight="8" refX="10" refY="4" orient="auto">',
        '      <path d="M0,0 L10,4 L0,8 Z" fill="#334155"/>',
        "    </marker>",
        "  </defs>",
        f'  <rect x="0" y="0" width="{width}" height="{height}" fill="#FFFBEB"/>',
        svg_box(runtime_x, runtime_y, runtime_w, runtime_h, "Runtime::run_workflow(config_files)"),
        svg_box(orch_x, orch_y, orch_w, orch_h, "orchestrator_actor"),
        svg_box(job_x, job_y, job_w, job_h, "job_actor"),
    ]

    parts.append(svg_arrow(runtime_x + runtime_w // 2, runtime_y + runtime_h, orch_x + orch_w // 2, orch_y))
    parts.append(svg_arrow(orch_x + orch_w // 2, orch_y + orch_h, job_x + job_w // 2, job_y))

    for index, (cat_x, cat_y) in enumerate(cat_centers):
        parts.append(svg_cat_node(cat_x, cat_y, f"simulation_unit_actor [{index}]"))
        parts.append(svg_arrow(job_x + job_w // 2, job_y + job_h, cat_x, cat_y - 85))

    for index, (box_x, box_y) in enumerate(exec_boxes):
        parts.append(svg_execute_box(box_x, box_y, f"SimulationUnit::execute(config[{index}])"))
        cat_x, _ = cat_centers[index]
        parts.append(svg_arrow(cat_x, 555, box_x + 125, box_y))

    parts.append(
        '  <text x="640" y="805" text-anchor="middle" font-family="Helvetica, Arial, sans-serif" '
        'font-size="16" fill="#334155">Cat nodes represent simulation_unit_actor workers</text>'
    )
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate an SVG actor workflow diagram with cat worker nodes")
    parser.add_argument(
        "--output",
        default="docs/figures/actors_cat.svg",
        help="Output SVG path relative to repo root",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    requested_output = Path(args.output)
    if requested_output.is_absolute():
        output_path = requested_output
    else:
        output_path = repo_root / requested_output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(build_svg(), encoding="utf-8")
    print(f"Wrote diagram to {output_path}")


if __name__ == "__main__":
    main()
