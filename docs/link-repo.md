# Link repo

- Make sure you have followed steps in [Create Repository](create-repository.md) to create a repo.
- Make sure you have followed steps in [Create Package](create-package.md) to create a package scaffolding.
- Open your package source in your favorite IDE and run the following commands:

<div class="termy">

```console
$ git init
Initialized empty Git repository in C:/Source/pyhyperv/.git/
$ git add .
$ git commit -am "initial commit"
[master (root-commit) 68840a4] initial commit
 17 files changed, 388 insertions(+)
 create mode 100644 .cruft.json
 create mode 100644 .flake8
 create mode 100644 .github/pull_request_template.md
 create mode 100644 .gitignore
 create mode 100644 README.md
 create mode 100644 docs/Makefile
 create mode 100644 docs/conf.py
 create mode 100644 docs/index.md
 create mode 100644 docs/make.bat
 create mode 100644 jenkins/pull-request.groovy
 create mode 100644 jenkins/release.groovy
 create mode 100644 poetry.toml
 create mode 100644 pyproject.toml
 create mode 100644 src/pyhyperv/__init__.py
 create mode 100644 src/pyhyperv/api.py
 create mode 100644 src/pyhyperv/pyhyperv_command.py
 create mode 100644 tests/__init__.py
$ git branch -M main
$ git remote add origin https://github.com/username/test-repo.git
$ git push -u origin main
Enumerating objects: 17, done.
Counting objects: 100% (17/17), done.
Delta compression using up to 2 threads
Compressing objects: 100% (2/2), done.
Writing objects: 100% (16/16), 290 bytes | 290.00 KiB/s, done.
Total 17 (delta 0), reused 0 (delta 0), pack-reused 0
To https://github.com/username/test-repo.git
   a8deaf1..849b4fe  main -> main
branch 'main' set up to track 'origin/main'
```

</div>