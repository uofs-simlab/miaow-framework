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
import re
import shutil
import sys
from collections.abc import Callable, Mapping
from pathlib import Path
from typing import Any


LOGGER = logging.getLogger("stage_symfluence_noah_owp_inputs")

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TARGET_DIR = PROJECT_ROOT / "examples" / "04_noah_owp"
DEFAULT_REALIZATION_SUBPATH = Path("data") / "realization_config.json"
STAGED_CATCHMENTS_NAME = "catchments.geojson"
CAT_INPUT_PATTERN = re.compile(r"^cat-(\d+)\.input$")
NAMELIST_KEY_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
CatchmentFiles = tuple[str, Path, Path]
NoahModelOptionOverrides = dict[str, int]

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
        help=(
            "Staged SYMFLUENCE realization path; defaults to "
            "<target-dir>/data/realization_config.json"
        ),
    )
    return parser


def load_noah_model_option_overrides(
    config_path: Path,
) -> NoahModelOptionOverrides:
    """Load MIAOW-specific Noah model option overrides from the YAML config."""
    try:
        import yaml
    except ImportError as exception:
        raise RuntimeError("PyYAML is required to read MIAOW staging options.") from (
            exception
        )

    config = yaml.safe_load(config_path.read_text(encoding="utf-8")) or {}
    if not isinstance(config, Mapping):
        raise RuntimeError(f"Expected YAML mapping in {config_path}")

    staging_config = config.get("MIAOW_STAGING", {})
    if staging_config is None:
        return {}
    if not isinstance(staging_config, Mapping):
        raise RuntimeError("MIAOW_STAGING must be a mapping when provided.")

    noah_overrides = staging_config.get("noah_overrides", {})
    if noah_overrides is None:
        return {}
    if not isinstance(noah_overrides, Mapping):
        raise RuntimeError("MIAOW_STAGING.noah_overrides must be a mapping.")

    model_options = noah_overrides.get("model_options", {})
    if model_options is None:
        return {}
    if not isinstance(model_options, Mapping):
        raise RuntimeError(
            "MIAOW_STAGING.noah_overrides.model_options must be a mapping."
        )

    overrides: NoahModelOptionOverrides = {}
    for key, value in model_options.items():
        key_text = str(key)
        if not NAMELIST_KEY_PATTERN.match(key_text):
            raise RuntimeError(f"Invalid Noah model option name: {key_text}")
        if isinstance(value, bool) or not isinstance(value, int):
            raise RuntimeError(
                f"Noah model option {key_text} must be an integer value."
            )
        overrides[key_text] = value
    return overrides


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

    modules_selected = {
        module.strip().upper()
        for module in str(workflow.typed_config.model.ngen.modules_selected).split(",")
        if module.strip()
    }
    if modules_selected != {"NOAH"}:
        selected_text = ",".join(sorted(modules_selected)) or "<none>"
        raise RuntimeError(
            "Set NGEN_MODULES_SELECTED: NOAH in the SYMFLUENCE config "
            f"(found: {selected_text})."
        )


def run_required_steps(workflow: Any, force: bool) -> None:
    """Run pending SYMFLUENCE steps, or all selected steps when forced."""
    # Select the workflow steps required for the configured domain
    steps = list(SYMFLUENCE_STEPS)
    if workflow.typed_config.domain.definition_method != "point":
        steps.insert(1, "create_pour_point")

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


def find_outputs(
    workflow: Any,
    resolve_data_subdir: Callable[[Path, str], Path],
) -> tuple[Path, Path, Path, Path, list[Path], Path]:
    """Resolve and validate the SYMFLUENCE outputs consumed by MIAOW."""
    project_dir = Path(workflow.workflow_orchestrator.project_dir)
    setup_dir = project_dir / "settings" / "NGEN"
    noah_dir = setup_dir / "NOAH"
    parameter_dir = noah_dir / "parameters"
    csv_dir = resolve_data_subdir(project_dir, "forcing") / "NGEN_input" / "csv"
    native_realization = setup_dir / "realization_config.json"
    catchment_paths = sorted(setup_dir.glob("*_catchments.geojson"))

    # Validate the generated input paths consumed by the staged runtime
    required_directories = [
        (csv_dir, "SYMFLUENCE NGEN CSV directory"),
        (noah_dir, "SYMFLUENCE Noah input directory"),
        (parameter_dir, "Noah parameter directory"),
    ]
    for path, description in required_directories:
        if not path.is_dir():
            raise FileNotFoundError(f"{description} not found: {path}")
    if not native_realization.is_file():
        raise FileNotFoundError(
            f"SYMFLUENCE NGEN realization not found: {native_realization}"
        )
    if len(catchment_paths) != 1:
        raise FileNotFoundError(
            f"Expected one SYMFLUENCE catchments GeoJSON in {setup_dir}, "
            f"found {len(catchment_paths)}."
        )

    noah_inputs = sorted(noah_dir.glob("cat-*.input"), key=catchment_number)
    if not noah_inputs:
        raise FileNotFoundError(f"No Noah cat-*.input files found in {noah_dir}")

    if not any(csv_dir.glob("*_forcing.csv")):
        raise FileNotFoundError(f"No per-catchment forcing CSVs found in {csv_dir}")

    return (
        project_dir,
        native_realization,
        catchment_paths[0],
        csv_dir,
        noah_inputs,
        parameter_dir,
    )


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
        if not forcing_csv.is_file():
            raise FileNotFoundError(
                f"Forcing CSV for {subdomain_id} not found: {forcing_csv}"
            )
        pairs.append((subdomain_id, noah_input, forcing_csv))
    return pairs


def replace_parameter_dir(source_dir: Path, target_dir: Path) -> None:
    """Replace target parameter tables with SYMFLUENCE-generated tables."""
    destination_dir = target_dir / "data" / "parameters"
    source_children = sorted(source_dir.iterdir())
    if not source_children:
        raise RuntimeError(f"No parameter files found in {source_dir}")

    destination_dir.parent.mkdir(parents=True, exist_ok=True)
    if destination_dir.exists():
        shutil.rmtree(destination_dir)
    shutil.copytree(source_dir, destination_dir)


def rewrite_noah_input(
    path: Path,
    subdomain_id: str,
    model_option_overrides: NoahModelOptionOverrides,
) -> None:
    """Rewrite generated Noah settings for the staged MIAOW runtime tree.

    SYMFLUENCE writes Noah inputs for its own project layout, so generated paths
    may be absolute or relative to the SYMFLUENCE project. The staged inputs use
    MIAOW's example data layout, and forcing comes from the BMI/NGEN driver
    instead of a direct Noah forcing file path.
    """
    replacements: dict[str, str] = {
        "parameter_dir": '"./data/parameters/"',
        "forcing_filename": '"BMI"',
        "output_filename": f'"./output/{subdomain_id}.nc"',
    }
    replacements.update(
        {key: str(value) for key, value in model_option_overrides.items()}
    )
    found = {key: False for key in replacements}
    lines = []

    for line in path.read_text(encoding="utf-8").splitlines(keepends=True):
        for key, value in replacements.items():
            pattern = rf"^(\s*{re.escape(key)}\s*=\s*)([^!]*?)(\s*!.*)?(\n?)$"
            match = re.match(pattern, line)
            if match is not None:
                line = f"{match.group(1)}{value}{match.group(3) or ''}"
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
    model_option_overrides: NoahModelOptionOverrides,
) -> list[CatchmentFiles]:
    """Copy selected SYMFLUENCE outputs into the target runtime tree."""
    config_dir = target_dir / "data" / "configs"
    forcing_dir = target_dir / "data" / "forcings"
    config_dir.mkdir(parents=True, exist_ok=True)
    forcing_dir.mkdir(parents=True, exist_ok=True)
    (target_dir / "output").mkdir(parents=True, exist_ok=True)

    # Remove generated catchment files while leaving other fixtures in place
    for path in config_dir.glob("cat-*.input"):
        path.unlink()
    for path in forcing_dir.glob("*_forcing.csv"):
        path.unlink()

    replace_parameter_dir(parameter_dir, target_dir)

    staged_pairs = []
    for subdomain_id, source_config, source_forcing in pairs:
        staged_config = config_dir / source_config.name
        staged_forcing = forcing_dir / source_forcing.name
        shutil.copy2(source_config, staged_config)
        shutil.copy2(source_forcing, staged_forcing)
        rewrite_noah_input(staged_config, subdomain_id, model_option_overrides)
        staged_pairs.append((subdomain_id, staged_config, staged_forcing))
    return staged_pairs


def stage_catchments(
    source_path: Path,
    staged_pairs: list[CatchmentFiles],
    realization_path: Path,
) -> Path:
    """Write a selected SYMFLUENCE catchment GeoJSON next to the realization."""
    import geopandas as gpd

    selected_ids = [subdomain_id for subdomain_id, _, _ in staged_pairs]
    target_path = realization_path.parent / STAGED_CATCHMENTS_NAME
    target_path.parent.mkdir(parents=True, exist_ok=True)

    for path in [target_path, *target_path.parent.glob("*_catchments.geojson")]:
        path.unlink(missing_ok=True)

    catchments = gpd.read_file(source_path)
    if "id" not in catchments.columns:
        raise RuntimeError(f"Catchments GeoJSON is missing column 'id': {source_path}")

    order_by_id = {subdomain_id: index for index, subdomain_id in enumerate(selected_ids)}
    catchments["by_id"] = catchments["id"].astype(str).map(order_by_id)
    selected = (
        catchments[catchments["by_id"].notna()]
        .sort_values("by_id")
        .drop(columns=["by_id"])
    )
    if len(selected) != len(selected_ids):
        raise RuntimeError(
            f"Catchments GeoJSON {source_path} does not contain all selected IDs."
        )

    selected.to_file(target_path, driver="GeoJSON")
    return target_path


def write_staged_realization(source_path: Path, realization_path: Path) -> None:
    """Write a relocatable SYMFLUENCE realization for the staged data tree."""
    realization_path.parent.mkdir(parents=True, exist_ok=True)
    realization = json.loads(source_path.read_text(encoding="utf-8"))

    module_params = realization["global"]["formulations"][0]["params"]["modules"][0][
        "params"
    ]
    module_params["library_file"] = "plugins/libsurfacebmi"
    module_params["init_config"] = "configs/{{id}}.input"

    forcing = realization["global"]["forcing"]
    forcing["path"] = "forcings"
    forcing["file_pattern"] = ".*{{id}}_forcing.*\\.csv"
    realization["output_root"] = "../output"

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
    model_option_overrides = load_noah_model_option_overrides(config_path)
    symfluence_class, resolve_data_subdir = load_symfluence()
    workflow = symfluence_class(str(config_path))
    validate_ngen_config(workflow)
    run_required_steps(workflow, force)

    (
        project_dir,
        native_realization,
        source_catchments,
        csv_dir,
        noah_inputs,
        parameter_dir,
    ) = find_outputs(workflow, resolve_data_subdir)
    pairs = selected_pairs(noah_inputs, csv_dir, keep_first)
    staged_pairs = stage_inputs(
        pairs,
        parameter_dir,
        target_dir,
        model_option_overrides,
    )
    staged_catchments = stage_catchments(
        source_catchments,
        staged_pairs,
        realization_path,
    )
    write_staged_realization(native_realization, realization_path)

    LOGGER.info("SYMFLUENCE project directory: %s", project_dir)
    LOGGER.info("Native SYMFLUENCE realization: %s", native_realization)
    LOGGER.info("Native SYMFLUENCE catchments: %s", source_catchments)
    LOGGER.info("Staged %d catchment(s) into %s", len(staged_pairs), target_dir)
    LOGGER.info("Wrote staged catchments: %s", staged_catchments)
    LOGGER.info("Wrote staged SYMFLUENCE realization: %s", realization_path)


def main() -> int:
    """Program entrypoint."""
    parser = build_parser()
    args = parser.parse_args()

    # Configure concise timestamped logging for preprocessing and staging
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    target_dir = args.target_dir.expanduser().resolve()
    realization_path = (
        args.realization_path.expanduser().resolve()
        if args.realization_path is not None
        else target_dir / DEFAULT_REALIZATION_SUBPATH
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
