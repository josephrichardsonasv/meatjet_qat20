import shutil
from pathlib import Path

project_slug = "{{cookiecutter.project_slug}}"
is_powershell_command_wrapper = "{{ cookiecutter.powershell_wrapper }}" != "no"

command_file = Path.cwd() / "src" / project_slug / "default_command_command.py"
if not is_powershell_command_wrapper and command_file.exists():
    command_file.unlink()

is_using_jenkins_library = "{{cookiecutter.use_jenkins_library_for_ci_cd}}" == "yes"

jenkins_pipeline_scripts = Path.cwd() / "jenkins"
if not is_using_jenkins_library:
    shutil.rmtree(jenkins_pipeline_scripts)
