
<div align="center">
Showcase interactive examples of BoxPacker_2D algo.<br>
</div>



## Building ##

### Emscripten ###

This repository already includes `Emscripten_Debug` and `Emscripten_Release` configure presets.

Configure:

```powershell
cmake --preset Emscripten_Release
```

Build:

```powershell
cmake --build build/Emscripten_Release
```

The Emscripten target is configured to emit a GitHub Pages friendly entry point:

- `build/Emscripten_Release/stage/bin/index.html`
- `build/Emscripten_Release/stage/bin/index.js`
- `build/Emscripten_Release/stage/bin/index.wasm`

For manual publishing to the `gh-pages` branch, copy the contents of `build/Emscripten_Release/stage/bin/` into the root of that branch.

Minimum required files for the current sample:

- `index.html`
- `index.js`
- `index.wasm`

If you later add preloaded assets through Emscripten, copy any generated `.data` file next to those three files as well.

After pushing the `gh-pages` branch, GitHub Pages can serve the app directly from that branch root.



## License
This code is free to use under the terms of the [MIT license](https://github.com/InCom-0/BoxPacker_showcase/blob/main/LICENSE.txt).

## Acknowledgement