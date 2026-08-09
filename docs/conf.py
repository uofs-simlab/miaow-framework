from __future__ import annotations

from pathlib import Path
import shutil
import subprocess

project = "MIAOW"
author = "MIAOW contributors, University of Saskatchewan"
html_show_sphinx = False
html_show_copyright = False
html_baseurl = "https://tjbmontoya.com/miaow-framework/"
html_copy_source = False

extensions = ["breathe"]

source_suffix = {
    ".rst": "restructuredtext",
}
master_doc = "index"
exclude_patterns = ["_build", ".venv", "Thumbs.db", ".DS_Store"]
nitpick_ignore = [
    ("cpp:identifier", "bmi_api"),
    ("cpp:identifier", "bmi_api::Bmi"),
    ("cpp:identifier", "caf"),
    ("cpp:identifier", "caf::actor_system"),
    ("cpp:identifier", "caf::config_value"),
    ("cpp:identifier", "caf::result<TaskResult>"),
    # Breathe does not create a standalone target for the enclosing namespace
    ("cpp:identifier", "miaow"),
]

docs_dir = Path(__file__).parent.resolve()
repo_root = docs_dir.parent
doxygen_output_dir = docs_dir / "_build" / "doxygen"
doxygen_xml_dir = docs_dir / "_build" / "doxygen" / "xml"

# Generate the Doxygen XML consumed by Breathe
shutil.rmtree(doxygen_output_dir, ignore_errors=True)
doxygen_output_dir.mkdir(parents=True, exist_ok=True)
subprocess.run(
    ["doxygen", str(docs_dir / "Doxyfile")],
    cwd=repo_root,
    check=True,
)

breathe_projects = {"MIAOW": str(doxygen_xml_dir)}
breathe_default_project = "MIAOW"
breathe_domain_by_extension = {"hpp": "cpp"}
breathe_default_members = ("members",)

html_title = "MIAOW documentation"
html_logo = "images/miaow_logo.svg"
html_static_path = ["static"]
html_theme_options = {
    "font_size": "14px",
}
