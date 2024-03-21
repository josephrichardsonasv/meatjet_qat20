#!/usr/bin/env groovy
@Library('jenkins-lib@main') _

pythonPullRequest(name: '{{cookiecutter.project_slug}}')
