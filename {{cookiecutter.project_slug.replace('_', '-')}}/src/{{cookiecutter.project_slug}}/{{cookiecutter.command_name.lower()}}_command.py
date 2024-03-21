from __future__ import annotations

from command_wrapper.command_wrapper_sync import CommandWrapper


class {{cookiecutter.command_name.replace('_', ' ').title().split() | join('')}}(CommandWrapper):
    def __init__(self):
        super().__init__(
            {% if cookiecutter.powershell_wrapper != "no" %}
            command_name="powershell.exe",
            {% else %}
            command_name="{{cookiecutter.command_name}}.{{cookiecutter.command_extension}}",
            {%- endif %}
            {% if cookiecutter.powershell_wrapper != "no" %}
            search_paths=[r"C:\WINDOWS\system32\WindowsPowerShell\v1.0"]
            {% elif cookiecutter.command_search_path != "default_command_path" %}
            search_paths={{cookiecutter.command_search_path.replace("\\", "/").split(";")}}
            {%- endif %}
        )

    def get_command(self):
        """Just returns the command object"""
        return self
