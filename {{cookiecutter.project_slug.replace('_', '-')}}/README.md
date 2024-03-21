# {{cookiecutter.project_name}}
{{cookiecutter.project_description}}

## Installation
```console
pip install {{cookiecutter.project_slug.replace('_', '-')}} --index-url https://af-owr.devtools.intel.com/artifactory/api/pypi/adoaddautomation-or-local/simple
```

## Contribution

1. Make sure you have proper AGS entitlement. Go to [1Source inventory](https://1source.intel.com/inventory/explore) and search for your repository name. Click on the repository and check the AGS under `Write Teams`.
2. Create a feature branch from `main` (**NOTE**: If JIRA is there for respective feature/bug fix, then branch name can be same as the JIRA).
3. Make sure you have added/updated new/existing unit tests. You can locally run:
   1. Unit test: `pytest --cov -vv`
   2. Formatting: `black src tests` and `isort src tests`
   3. Linting: `flake8 src tests`
4. After the changes are push to feature branch, submit a pull request.
