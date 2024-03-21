#!/usr/bin/env groovy
@Library('jenkins-lib@main') _

pythonRelease(
    name: '{{cookiecutter.project_slug}}',
    notifyUsers: ['ado.rails.and.microservices.team@intel.com', 'ccg_cpe_wss_ado_aes_tca_ba@intel.com']
)
