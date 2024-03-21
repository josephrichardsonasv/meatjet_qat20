from cicd_tasks.tasks.github import deploy_github_pages
from cicd_tasks.tasks.sphinx import make_doc

make_doc(makefile="docs/make.bat")

deploy_github_pages(branch="gh-pages", src_dir="docs/_build/html")
