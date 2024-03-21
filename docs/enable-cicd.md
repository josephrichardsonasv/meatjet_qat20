# Enable CI/CD

Instll `jenkins-automation`:

<div class="termy">

```console
$ pip install jenkins-automation --index-url https://ubit-artifactory-or.intel.com/artifactory/api/pypi/adoaddautomation-or-local/simple
```

</div>

## Pull request pipeline

<div class="termy">

```console
$ jenkins-cli clone -n ado-add/display-automation-pr -o ado-add/test-automation-library-template-pr -s https://github.com/intel-innersource/frameworks.validation.platform-automation.display-automation
```

</div>

```{note}
This command will prompt for your password. Make sure you have the following AGS **DevTools - - WindowsAutomationKit - Developer**. Also if you don't want it to show the password prompt everytime, then pass -sc flag to save the credential.
```

<details>
    <summary>Through GUI</summary>

- Goto [ado-add Jenkins master](https://cbjenkins-fm.devtools.intel.com/teams-ado-add/job/ado-add/).
- Click on [New Item](https://cbjenkins-fm.devtools.intel.com/teams-ado-add/job/ado-add/newJob) on left side pane.
- Enter the name of your package suffixed with `-pr`. Example if the package is `pycst` the Jenkins job name will be `pycst-pr`.
- In `Copy from` section type `test-automation-library-template-pr` and click `Okay`.
- Under `Branch source` update the repository url and click `Save`.

</details>

## Release pipeline

<div class="termy">

```console
$ jenkins-cli clone -n ado-add/display-automation-release -o ado-add/test-automation-library-template-release -s https://github.com/intel-innersource/frameworks.validation.platform-automation.display-automation
```

<div>

<details>
    <summary>Through GUI</summary>

- Goto back to [ado-add Jenkins master](https://cbjenkins-fm.devtools.intel.com/teams-ado-add/job/ado-add/).
- Again click on [New Item](https://cbjenkins-fm.devtools.intel.com/teams-ado-add/job/ado-add/newJob) on left side pane.
- Enter the name of your package suffixed with `-release`. Example if the package is `pycst` the Jenkins job name will be `pycst-release`.
- In `Copy from` section type `test-automation-library-template-release` and click `Okay`.
- Under `Branch source` update the repository url and click `Save`.

</details>

```{note}
**Good to know**:

In backend we have [jenkins-lib](https://github.com/intel-innersource/frameworks.validation.platform-automation.devops.jenkins-lib) which has all the Jenkins pipeline logic.

This internally uses [cicd-tasks](https://github.com/intel-innersource/frameworks.validation.platform-automation.devops.cicd-tasks) python package which contains the actual command which are executed in each Jenkins job stages.
```