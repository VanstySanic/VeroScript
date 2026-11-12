# Initialize and build a project

Create the same minimal configuration in a new or existing directory:

```sh
veroscript project init --path "./my-project"
veroscript build --path "./my-project" --plan --json
veroscript build --path "./my-project"
veroscript "./my-project/dist/release/portable/application/app.vsapp"
```

Omit `--path` to initialize the current working directory. Relative paths use the
command's starting directory; absolute paths and `--path=<directory>` also work.
Missing directories are created. The project name is derived from the directory
using lowercase ASCII letters, digits and single hyphens, with `my-project` as
the fallback for names that cannot form a valid package name.

The command writes missing `veroscript-project.json` and `veroscript-build.json`.
When creating the default build configuration it also supplies `main.vs` if
absent. Existing regular files are reported as `Kept` and retain their bytes;
an existing build configuration controls its own entry and does not cause an
extra `main.vs` to be generated. Directory or symbolic-link conflicts at file
destinations are errors. Re-running initialization fills missing configuration
without overwriting user files.

Initialization runs locally without installing dependencies, executing scripts,
or creating `veroscript.lock` or `.veroscript/`. After declaring dependencies,
run `veroscript install`; successful installation creates the lock file. Keep
project/build configuration and the lock file in version control; normally
ignore `.veroscript/` and `dist/`.

This directory is also a ready-to-copy example. Its source file retains the
repository's SPDX notice; newly initialized user source has no imposed license.
