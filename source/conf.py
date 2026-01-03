# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'OLSRT'
copyright = '2025, OverLab Group'
author = 'OverLab Group'
release = 'v1.0.1'

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
	'breathe',
	'sphinx.ext.todo',
	'sphinx.ext.coverage',
	'sphinx.ext.viewcode',
	'sphinx.ext.intersphinx'
]

templates_path = ['_templates']
exclude_patterns = []



# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'furo'
html_static_path = ['_static']
html_title = 'OLSRT Documention'

breathe_projects = { "OLSRT": "./xml" }
breathe_default_project = "OLSRT"
