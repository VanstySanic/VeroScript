# SDL3 dependency notices

SDL 3.4.18 is retained without source modifications. The SDK installs SDL's original license as `licenses/SDL3/LICENSE.txt`, alongside the pinned revision in `SDL3.version.json`.

The following original component notices accompany the native SDK. Components used depend on platform and build configuration:

- [Embedded mathematics, standard-library and Khronos notices](SDL-embedded-notices.txt).
- [Apache License 2.0](Apache-2.0.txt), from <https://www.apache.org/licenses/LICENSE-2.0.txt>, for the Khronos headers declaring that license.
- [HIDAPI BSD license option](HIDAPI-BSD.txt).
- [yuv2rgb BSD license](yuv2rgb-BSD.txt).

These texts retain their upstream terms; VeroScript's MIT license covers its own implementation.

Optional WebGPU has separate notices: native builds install `licenses/WgpuNative/WGPU-NOTICES.txt`
with wgpu-native v29.0.1.1 and its pinned Cargo.lock dependency notices; browser builds install
`licenses/Emdawnwebgpu/EMDAWN-NOTICES.txt` for the pinned Emdawn v20251002.162335 port.
These optional texts are not placed in the SDL3 notices directory and are omitted when WebGPU is disabled.
The pinned Emdawn remote-port file retains its original Emscripten copyright/license header.
