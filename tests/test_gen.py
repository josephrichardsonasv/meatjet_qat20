from pathlib import Path

from cookiecutter.main import cookiecutter

TEMPLATE_DIRECTORY = str(Path(__file__).parent.parent)


def paths(directory):
    paths = list(Path(directory).glob("**/*"))
    paths = [r.relative_to(directory) for r in paths]
    return {str(f) for f in paths if str(f) != "."}


def test_project_slug_initialize(tmpdir: Path) -> None:
    cookiecutter(
        template=TEMPLATE_DIRECTORY,
        output_dir=str(tmpdir),
        no_input=True,
        extra_context={"project_name": "Hyper V Automation"},
    )

    assert paths(tmpdir) == {
        "hyper-v-automation",
        "hyper-v-automation\\.flake8",
        "hyper-v-automation\\.gitignore",
        "hyper-v-automation\\README.md",
        "hyper-v-automation\\jenkins",
        "hyper-v-automation\\jenkins\\pull-request.groovy",
        "hyper-v-automation\\jenkins\\release.groovy",
        "hyper-v-automation\\poetry.toml",
        "hyper-v-automation\\pyproject.toml",
        "hyper-v-automation\\src",
        "hyper-v-automation\\src\\hyper_v_automation",
        "hyper-v-automation\\src\\hyper_v_automation\\__init__.py",
        "hyper-v-automation\\src\\hyper_v_automation\\api.py",
        "hyper-v-automation\\tests",
        "hyper-v-automation\\tests\\__init__.py",
        "hyper-v-automation\\docs\\make.bat",
        "hyper-v-automation\\docs\\conf.py",
        "hyper-v-automation\\docs",
        "hyper-v-automation\\docs\\Makefile",
        "hyper-v-automation\\docs\\index.md",
        "hyper-v-automation\\.github",
        "hyper-v-automation\\.github\\pull_request_template.md",
    }

    assert 'name = "hyper-v-automation"' in (
        tmpdir / "hyper-v-automation" / "pyproject.toml"
    ).read_text(encoding="utf-8")
    assert "command-wrapper" not in (
        tmpdir / "hyper-v-automation" / "pyproject.toml"
    ).read_text(encoding="utf-8")
    assert "pythonPullRequest(name: 'hyper_v_automation')" in (
        tmpdir / "hyper-v-automation" / "jenkins" / "pull-request.groovy"
    ).read_text(encoding="utf-8")
    assert "name: 'hyper_v_automation'," in (
        tmpdir / "hyper-v-automation" / "jenkins" / "release.groovy"
    ).read_text(encoding="utf-8")
    assert 'description = ""' in (
        tmpdir / "hyper-v-automation" / "pyproject.toml"
    ).read_text(encoding="utf-8")
    assert "# Hyper V Automation" in (
        tmpdir / "hyper-v-automation" / "README.md"
    ).read_text(encoding="utf-8")
    assert (
        "pip install hyper-v-automation --index-url https://af-owr.devtools.intel.com/artifactory/api/pypi/adoaddautomation-or-local/simple"  # noqa: E501
    ) in (tmpdir / "hyper-v-automation" / "README.md").read_text(encoding="utf-8")


def test_project_skip_jenkins_pipeline(tmpdir: Path) -> None:
    cookiecutter(
        template=TEMPLATE_DIRECTORY,
        output_dir=str(tmpdir),
        no_input=True,
        extra_context={
            "project_name": "Hyper V Automation",
            "use_jenkins_library_for_ci_cd": "no",
        },
    )

    assert paths(tmpdir) == {
        "hyper-v-automation",
        "hyper-v-automation\\.flake8",
        "hyper-v-automation\\.gitignore",
        "hyper-v-automation\\README.md",
        "hyper-v-automation\\poetry.toml",
        "hyper-v-automation\\pyproject.toml",
        "hyper-v-automation\\src",
        "hyper-v-automation\\src\\hyper_v_automation",
        "hyper-v-automation\\src\\hyper_v_automation\\__init__.py",
        "hyper-v-automation\\src\\hyper_v_automation\\api.py",
        "hyper-v-automation\\tests",
        "hyper-v-automation\\tests\\__init__.py",
        "hyper-v-automation\\docs\\make.bat",
        "hyper-v-automation\\docs\\conf.py",
        "hyper-v-automation\\docs",
        "hyper-v-automation\\docs\\Makefile",
        "hyper-v-automation\\docs\\index.md",
        "hyper-v-automation\\.github",
        "hyper-v-automation\\.github\\pull_request_template.md",
    }


def test_project_config_variables(tmpdir: Path) -> None:
    cookiecutter(
        template=TEMPLATE_DIRECTORY,
        output_dir=str(tmpdir),
        no_input=True,
        extra_context={
            "project_name": "Hyper V Automation",
            "use_jenkins_library_for_ci_cd": "no",
            "pypi_index": "https://ubit-artifactory-ba.intel.com/artifactory/api/pypi/onebox_software-ba-local/simple",
            "python_version": "~3.11",
            "author": "CCG CPE C4S AES TCA BA <ccg_cpe_wss_ado_aes_tca_ba@intel.com>",
        },
    )

    assert 'url = "https://ubit-artifactory-ba.intel.com/artifactory/api/pypi/onebox_software-ba-local/simple"' in (
        tmpdir / "hyper-v-automation" / "pyproject.toml"
    ).read_text(
        encoding="utf-8"
    )
    assert 'python = "~3.11"' in (
        tmpdir / "hyper-v-automation" / "pyproject.toml"
    ).read_text(encoding="utf-8")
    assert (
        'authors = ["CCG CPE C4S AES TCA BA <ccg_cpe_wss_ado_aes_tca_ba@intel.com>"]'
        in (tmpdir / "hyper-v-automation" / "pyproject.toml").read_text(
            encoding="utf-8"
        )
    )


def test_command_wrapper_without_search_path(tmpdir: Path) -> None:
    cookiecutter(
        template=TEMPLATE_DIRECTORY,
        output_dir=str(tmpdir),
        no_input=True,
        extra_context={
            "project_name": "pyping",
            "command_name": "PING",
            "command_extension": "EXE",
            "project_description": "A Python wrapper for PING.EXE",
        },
    )
    assert paths(tmpdir) == {
        "pyping",
        "pyping\\.flake8",
        "pyping\\.gitignore",
        "pyping\\README.md",
        "pyping\\jenkins",
        "pyping\\jenkins\\pull-request.groovy",
        "pyping\\jenkins\\release.groovy",
        "pyping\\poetry.toml",
        "pyping\\pyproject.toml",
        "pyping\\src",
        "pyping\\src\\pyping",
        "pyping\\src\\pyping\\__init__.py",
        "pyping\\src\\pyping\\api.py",
        "pyping\\src\\pyping\\ping_command.py",
        "pyping\\tests",
        "pyping\\tests\\__init__.py",
        "pyping\\docs\\conf.py",
        "pyping\\docs",
        "pyping\\docs\\make.bat",
        "pyping\\docs\\Makefile",
        "pyping\\docs\\index.md",
        "pyping\\.github",
        "pyping\\.github\\pull_request_template.md",
    }
    assert 'command_name="PING.EXE"' in (
        tmpdir / "pyping" / "src" / "pyping" / "ping_command.py"
    ).read_text(encoding="utf-8")
    assert "search_paths=" not in (
        tmpdir / "pyping" / "src" / "pyping" / "ping_command.py"
    ).read_text(encoding="utf-8")
    assert 'description = "A Python wrapper for PING.EXE"' in (
        tmpdir / "pyping" / "pyproject.toml"
    ).read_text(encoding="utf-8")


def test_command_wrapper_with_search_path(tmpdir: Path) -> None:
    cookiecutter(
        template=TEMPLATE_DIRECTORY,
        output_dir=str(tmpdir),
        no_input=True,
        extra_context={
            "project_name": "pyping",
            "command_name": "PING",
            "command_extension": "EXE",
            "command_search_path": r"C:/Windows/System32;C:\Application",
            "project_description": "A Python wrapper for PING.EXE",
        },
    )
    assert paths(tmpdir) == {
        "pyping",
        "pyping\\.flake8",
        "pyping\\.gitignore",
        "pyping\\README.md",
        "pyping\\jenkins",
        "pyping\\jenkins\\pull-request.groovy",
        "pyping\\jenkins\\release.groovy",
        "pyping\\poetry.toml",
        "pyping\\pyproject.toml",
        "pyping\\src",
        "pyping\\src\\pyping",
        "pyping\\src\\pyping\\__init__.py",
        "pyping\\src\\pyping\\api.py",
        "pyping\\src\\pyping\\ping_command.py",
        "pyping\\tests",
        "pyping\\tests\\__init__.py",
        "pyping\\docs\\index.md",
        "pyping\\docs\\make.bat",
        "pyping\\docs",
        "pyping\\docs\\conf.py",
        "pyping\\docs\\Makefile",
        "pyping\\.github",
        "pyping\\.github\\pull_request_template.md",
    }
    assert "search_paths=['C:/Windows/System32', 'C:/Application']" in (
        tmpdir / "pyping" / "src" / "pyping" / "ping_command.py"
    ).read_text(encoding="utf-8")


def test_powershell_command(tmpdir: Path) -> None:
    cookiecutter(
        template=TEMPLATE_DIRECTORY,
        output_dir=str(tmpdir),
        no_input=True,
        extra_context={
            "project_name": "Hyper V Automation",
            "powershell_wrapper": "yes",
        },
    )

    assert paths(tmpdir) == {
        "hyper-v-automation",
        "hyper-v-automation\\.flake8",
        "hyper-v-automation\\.gitignore",
        "hyper-v-automation\\README.md",
        "hyper-v-automation\\jenkins",
        "hyper-v-automation\\jenkins\\pull-request.groovy",
        "hyper-v-automation\\jenkins\\release.groovy",
        "hyper-v-automation\\poetry.toml",
        "hyper-v-automation\\pyproject.toml",
        "hyper-v-automation\\src",
        "hyper-v-automation\\src\\hyper_v_automation",
        "hyper-v-automation\\src\\hyper_v_automation\\__init__.py",
        "hyper-v-automation\\src\\hyper_v_automation\\api.py",
        "hyper-v-automation\\src\\hyper_v_automation\\hyper_v_automation_command.py",
        "hyper-v-automation\\tests",
        "hyper-v-automation\\tests\\__init__.py",
        "hyper-v-automation\\docs\\conf.py",
        "hyper-v-automation\\docs",
        "hyper-v-automation\\docs\\make.bat",
        "hyper-v-automation\\docs\\Makefile",
        "hyper-v-automation\\docs\\index.md",
        "hyper-v-automation\\.github",
        "hyper-v-automation\\.github\\pull_request_template.md",
    }

    assert r'search_paths=[r"C:\WINDOWS\system32\WindowsPowerShell\v1.0"]' in (
        tmpdir
        / "hyper-v-automation"
        / "src"
        / "hyper_v_automation"
        / "hyper_v_automation_command.py"
    ).read_text(encoding="utf-8")
    assert r'command_name="powershell.exe"' in (
        tmpdir
        / "hyper-v-automation"
        / "src"
        / "hyper_v_automation"
        / "hyper_v_automation_command.py"
    ).read_text(encoding="utf-8")
    assert r"class HyperVAutomation" in (
        tmpdir
        / "hyper-v-automation"
        / "src"
        / "hyper_v_automation"
        / "hyper_v_automation_command.py"
    ).read_text(encoding="utf-8")
