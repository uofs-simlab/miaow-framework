#!/usr/bin/env python3
"""Stage SYMFLUENCE-generated Noah-OWP inputs for a MIAOW runtime tree.

The script runs the supported SYMFLUENCE NGEN preprocessing steps, selects a
small deterministic catchment subset, and copies only the Noah runtime inputs
needed by MIAOW into the requested target directory.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import re
import shutil
import sys
from collections.abc import Callable
from pathlib import Path
from typing import Any


LOGGER = logging.getLogger("stage_symfluence_noah_owp_inputs")

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TARGET_DIR = PROJECT_ROOT / "examples" / "04_noah_owp"
CAT_INPUT_PATTERN = re.compile(r"^cat-(\d+)\.input$")
CatchmentFiles = tuple[str, Path, Path]

# `create_pour_point` is inserted at runtime for non-point domains
SYMFLUENCE_STEPS = [
    "setup_project",
    "acquire_attributes",
    "define_domain",
    "discretize_domain",
    "acquire_forcings",
    "model_agnostic_preprocessing",
    "build_model_ready_store",
    "model_specific_preprocessing",
]

# Noah BMI input names mapped to the CSV headers written by SYMFLUENCE
NOAH_VARIABLES_NAMES_MAP = {
    "SFCPRS": "land_surface_air__pressure",
    "SFCTMP": "land_surface_air__temperature",
    "SOLDN": "land_surface_radiation~incoming~shortwave__energy_flux",
    "LWDN": "land_surface_radiation~incoming~longwave__energy_flux",
    "UU": "land_surface_wind__x_component_of_velocity",
    "VV": "land_surface_wind__y_component_of_velocity",
    "Q2": "atmosphere_air_water~vapor__specific_humidity",
    "PRCPNONC": "atmosphere_water__liquid_equivalent_precipitation_rate",
}


def positive_int(raw_value: str) -> int:
    """Parse a positive integer CLI argument."""
    value = int(raw_value)
    if value <= 0:
        raise argparse.ArgumentTypeError("value must be greater than zero")
    return value


def build_parser() -> argparse.ArgumentParser:
    """Build the command-line interface for the staging workflow."""
    parser = argparse.ArgumentParser(
        description=(
            "Run SYMFLUENCE's Bow NGEN preprocessing workflow and stage a "
            "small Noah-OWP external-forcing subset for MIAOW."
        )
    )
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--keep-first", type=positive_int, default=3)
    parser.add_argument(
        "--target-dir",
        type=Path,
        default=DEFAULT_TARGET_DIR,
        help="MIAOW Noah-OWP runtime directory to populate",
    )
    parser.add_argument(
        "--realization-path",
        type=Path,
        default=None,
        help="Realization JSON path; defaults to <target-dir>/realization.json",
    )
    return parser


def configure_logging() -> None:
    """Configure concise timestamped logging for SYMFLUENCE and staging output."""
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )


def load_symfluence() -> tuple[type[Any], Callable[[Path, str], Path]]:
    """Import SYMFLUENCE from the active preprocessing virtual environment."""
    if sys.prefix == sys.base_prefix:
        raise RuntimeError(
            "Run this script inside scripts/preprocessing/.venv."
        )

    try:
        import symfluence
        from symfluence import SYMFLUENCE
        from symfluence.core.mixins.project import resolve_data_subdir
    except ImportError as exception:
        raise RuntimeError(
            "Unable to import symfluence from the preprocessing venv."
        ) from exception

    module_path = Path(symfluence.__file__).resolve()
    prefix_path = Path(sys.prefix).resolve()
    if prefix_path not in module_path.parents:
        raise RuntimeError(
            f"symfluence resolved outside the active venv: {module_path}"
        )

    version = getattr(symfluence, "__version__", "unknown")
    LOGGER.info("Using symfluence %s from %s", version, module_path)
    return SYMFLUENCE, resolve_data_subdir


def validate_ngen_config(workflow: Any) -> None:
    """Require the SYMFLUENCE NGEN path that generates Noah-ready artifacts."""
    hydrological_model = str(
        workflow.typed_config.model.hydrological_model
    ).strip().upper()
    if hydrological_model != "NGEN":
        raise RuntimeError("Set HYDROLOGICAL_MODEL: NGEN in the SYMFLUENCE config.")

    modules_selected = ",".join(
        module.strip().upper()
        for module in str(workflow.typed_config.model.ngen.modules_selected).split(",")
        if module.strip()
    )
    if modules_selected != "NOAH,PET,CFE":
        raise RuntimeError(
            "Set NGEN_MODULES_SELECTED: NOAH,PET,CFE in the SYMFLUENCE config."
        )


def workflow_steps(workflow: Any) -> list[str]:
    """Return the exact SYMFLUENCE preprocessing steps used by this workflow."""
    steps = list(SYMFLUENCE_STEPS)
    if workflow.typed_config.domain.definition_method != "point":
        steps.insert(1, "create_pour_point")
    return steps


def run_required_steps(workflow: Any, force: bool) -> None:
    """Run pending SYMFLUENCE steps, or all selected steps when forced."""
    steps = workflow_steps(workflow)
    if force:
        LOGGER.info("Force rerun requested for steps: %s", ", ".join(steps))
        workflow.run_individual_steps(steps)
        return

    status = workflow.get_workflow_status()
    status_by_name = {item["cli_name"]: item for item in status["step_details"]}
    pending_steps = [
        step
        for step in steps
        if not status_by_name.get(step, {}).get("complete", False)
    ]

    if pending_steps:
        LOGGER.info("Running pending SYMFLUENCE steps: %s", ", ".join(pending_steps))
        workflow.run_individual_steps(pending_steps)
    else:
        LOGGER.info("Selected SYMFLUENCE workflow steps are already current")


def catchment_number(path: Path) -> int:
    """Extract the numeric catchment ID from a `cat-<id>.input` path."""
    match = CAT_INPUT_PATTERN.match(path.name)
    if match is None:
        raise RuntimeError(f"Unexpected Noah input name: {path.name}")
    return int(match.group(1))


def require_existing_directory(path: Path, description: str) -> None:
    """Fail if a required generated directory is absent."""
    if not path.is_dir():
        raise FileNotFoundError(f"{description} not found: {path}")


def require_existing_file(path: Path, description: str) -> None:
    """Fail if a required generated file is absent."""
    if not path.is_file():
        raise FileNotFoundError(f"{description} not found: {path}")


def find_outputs(
    workflow: Any,
    resolve_data_subdir: Callable[[Path, str], Path],
) -> tuple[Path, Path, Path, list[Path], Path]:
    """Resolve and validate the SYMFLUENCE outputs consumed by MIAOW."""
    project_dir = Path(workflow.workflow_orchestrator.project_dir)
    setup_dir = project_dir / "settings" / "NGEN"
    noah_dir = setup_dir / "NOAH"
    parameter_dir = noah_dir / "parameters"
    csv_dir = resolve_data_subdir(project_dir, "forcing") / "NGEN_input" / "csv"
    native_realization = setup_dir / "realization_config.json"

    require_existing_directory(csv_dir, "SYMFLUENCE NGEN CSV directory")
    require_existing_directory(noah_dir, "SYMFLUENCE Noah input directory")
    require_existing_directory(parameter_dir, "Noah parameter directory")
    require_existing_file(native_realization, "SYMFLUENCE NGEN realization")

    noah_inputs = sorted(noah_dir.glob("cat-*.input"), key=catchment_number)
    if not noah_inputs:
        raise FileNotFoundError(f"No Noah cat-*.input files found in {noah_dir}")

    csv_files = sorted(csv_dir.glob("*_forcing.csv"))
    if not csv_files:
        raise FileNotFoundError(f"No per-catchment forcing CSVs found in {csv_dir}")

    return project_dir, native_realization, csv_dir, noah_inputs, parameter_dir


def selected_pairs(
    noah_inputs: list[Path],
    csv_dir: Path,
    keep_first: int,
) -> list[CatchmentFiles]:
    """Pair the first numerically sorted Noah inputs with matching CSV forcings."""
    if len(noah_inputs) < keep_first:
        raise RuntimeError(
            f"SYMFLUENCE generated {len(noah_inputs)} catchment(s), but "
            f"--keep-first requested {keep_first}."
        )

    pairs = []
    for noah_input in noah_inputs[:keep_first]:
        subdomain_id = f"cat-{catchment_number(noah_input)}"
        forcing_csv = csv_dir / f"{subdomain_id}_forcing.csv"
        require_existing_file(forcing_csv, f"Forcing CSV for {subdomain_id}")
        pairs.append((subdomain_id, noah_input, forcing_csv))
    return pairs


def clear_previous_staged_files(target_dir: Path) -> None:
    """Remove generated catchment files while leaving other fixtures in place."""
    for path in (target_dir / "data" / "configs").glob("cat-*.input"):
        path.unlink()
    for path in (target_dir / "data" / "forcings").glob("*_forcing.csv"):
        path.unlink()


def replace_parameter_dir(source_dir: Path, target_dir: Path) -> None:
    """Replace target parameter tables with SYMFLUENCE-generated tables."""
    destination_dir = target_dir / "data" / "parameters"
    destination_dir.mkdir(parents=True, exist_ok=True)
    for child in destination_dir.iterdir():
        if child.is_dir() and not child.is_symlink():
            shutil.rmtree(child)
        else:
            child.unlink()

    copied_count = 0
    for child in sorted(source_dir.iterdir()):
        destination = destination_dir / child.name
        if child.is_dir():
            shutil.copytree(child, destination)
        else:
            shutil.copy2(child, destination)
        copied_count += 1

    if copied_count == 0:
        raise RuntimeError(f"No parameter files found in {source_dir}")


def rewrite_noah_input(path: Path, subdomain_id: str, forcing_name: str) -> None:
    """Rewrite generated Noah paths so they resolve from the target run dir."""
    replacements = {
        "parameter_dir": "./data/parameters/",
        "forcing_filename": f"./data/forcings/{forcing_name}",
        "output_filename": f"./output/{subdomain_id}.nc",
    }
    found = {key: False for key in replacements}
    lines = []

    for line in path.read_text(encoding="utf-8").splitlines(keepends=True):
        for key, value in replacements.items():
            pattern = rf"^(\s*{re.escape(key)}\s*=\s*)([^!]*?)(\s*!.*)?(\n?)$"
            match = re.match(pattern, line)
            if match is not None:
                line = f'{match.group(1)}"{value}"{match.group(3) or ""}'
                line += match.group(4)
                found[key] = True
                break
        lines.append(line)

    missing_keys = [key for key, was_found in found.items() if not was_found]
    if missing_keys:
        raise RuntimeError(
            f"Noah input {path} is missing key(s): {', '.join(missing_keys)}"
        )
    path.write_text("".join(lines), encoding="utf-8")


def stage_inputs(
    pairs: list[CatchmentFiles],
    parameter_dir: Path,
    target_dir: Path,
) -> list[CatchmentFiles]:
    """Copy selected SYMFLUENCE outputs into the target runtime tree."""
    config_dir = target_dir / "data" / "configs"
    forcing_dir = target_dir / "data" / "forcings"
    config_dir.mkdir(parents=True, exist_ok=True)
    forcing_dir.mkdir(parents=True, exist_ok=True)
    (target_dir / "output").mkdir(parents=True, exist_ok=True)

    clear_previous_staged_files(target_dir)
    replace_parameter_dir(parameter_dir, target_dir)

    staged_pairs = []
    for subdomain_id, source_config, source_forcing in pairs:
        staged_config = config_dir / source_config.name
        staged_forcing = forcing_dir / source_forcing.name
        shutil.copy2(source_config, staged_config)
        shutil.copy2(source_forcing, staged_forcing)
        rewrite_noah_input(staged_config, subdomain_id, staged_forcing.name)
        staged_pairs.append((subdomain_id, staged_config, staged_forcing))
    return staged_pairs


def relative_to_realization(path: Path, realization_path: Path) -> str:
    """Return a realization-relative path using the existing JSON contract."""
    relative_path = Path(os.path.relpath(path, realization_path.parent)).as_posix()
    if relative_path.startswith("."):
        return relative_path
    return f"./{relative_path}"


def write_realization(
    staged_pairs: list[CatchmentFiles],
    realization_path: Path,
) -> None:
    """Write the multi-subdomain realization consumed by MIAOW."""
    realization_path.parent.mkdir(parents=True, exist_ok=True)
    realization = {"subdomains": {}}
    for subdomain_id, config_path, forcing_path in staged_pairs:
        realization["subdomains"][subdomain_id] = {
            "config": relative_to_realization(config_path, realization_path),
            "forcing_file": relative_to_realization(forcing_path, realization_path),
            "variables_names_map": NOAH_VARIABLES_NAMES_MAP,
        }

    realization_path.write_text(
        json.dumps(realization, indent=2) + "\n",
        encoding="utf-8",
    )


def run_preprocessing(
    config_path: Path,
    force: bool,
    keep_first: int,
    target_dir: Path,
    realization_path: Path,
) -> None:
    """Run SYMFLUENCE and stage the selected catchments for MIAOW."""
    symfluence_class, resolve_data_subdir = load_symfluence()
    workflow = symfluence_class(str(config_path))
    validate_ngen_config(workflow)
    run_required_steps(workflow, force)

    project_dir, native_realization, csv_dir, noah_inputs, parameter_dir = (
        find_outputs(workflow, resolve_data_subdir)
    )
    pairs = selected_pairs(noah_inputs, csv_dir, keep_first)
    staged_pairs = stage_inputs(pairs, parameter_dir, target_dir)
    write_realization(staged_pairs, realization_path)

    LOGGER.info("SYMFLUENCE project directory: %s", project_dir)
    LOGGER.info("Native SYMFLUENCE realization: %s", native_realization)
    LOGGER.info("Staged %d catchment(s) into %s", len(staged_pairs), target_dir)
    LOGGER.info("Wrote MIAOW realization: %s", realization_path)


def main() -> int:
    """Program entrypoint."""
    parser = build_parser()
    args = parser.parse_args()
    configure_logging()

    target_dir = args.target_dir.expanduser().resolve()
    realization_path = (
        args.realization_path.expanduser().resolve()
        if args.realization_path is not None
        else target_dir / "realization.json"
    )

    run_preprocessing(
        config_path=args.config.expanduser().resolve(),
        force=args.force,
        keep_first=args.keep_first,
        target_dir=target_dir,
        realization_path=realization_path,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
