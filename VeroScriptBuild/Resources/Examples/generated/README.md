# Generated application

Copy this directory into a writable project. `veroscript build --plan --allow-script --json` checks the recipe without executing its generator. `veroscript build --allow-script` executes the restricted VM step and produces `dist/release/portable/application/generated.vsapp`.

The template is a declared input; generated files are named outputs. Source-tree members use a typed artifact reference. Run the resulting container with `veroscript` or `veroscript-runtime`.
