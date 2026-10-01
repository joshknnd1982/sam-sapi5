# Notices

The code written for this project is licensed under the MIT License (see [LICENSE](LICENSE)). The material
below is not covered by that licence and stays under its own terms.

The code the licence covers is the settings utility (`src/config/`), the driver's logging, path and settings code
(`src/sapi/sam_log.*`, `src/sapi/sam_paths.*`, `src/sapi/sam_settings.*`), the installer (`installer/`), the
tools and tests (`tools/`) and the build scripts.

## Not covered: the SAM-derived material

SAM (Software Automatic Mouth) was created by Mark Barton and published by Don't Ask Software in 1982. No licence
is claimed here for the material in this repository that is derived from it, because the provenance of the Python
reference is unknown:

- **The Python implementation in `bin/`**, kept as the reference. It was taken from an NVDA add-on. It carried no
  licence header or attribution, and its author is not identified in the source. It is included here because the
  build regenerates the engine tables from it and the test suite verifies against it. If you are its author and
  want it credited differently or removed, please open an issue.
- **The C++ engine in `src/engine/`.** Its source files describe it as a port of the Python implementation
  (`sam_engine.hpp`: "C++ port of the Python port of the 1982 original"; `sam_reciter.cpp`: "Ported to match the
  Python implementation exactly"), and it is checked to render byte-identically to it.
- **The engine tables, `src/engine/sam_tables.cpp` and `src/engine/sam_tables.hpp`.** They say that they are
  generated from the Python SAM tables by `tools/gen_tables.py`.

## Not covered: SAPI5 plumbing adapted from gozaltech/BstSpeech-sapi

These files were copied or adapted from the SAPI5 wrapper [gozaltech/BstSpeech-sapi](https://github.com/gozaltech/BstSpeech-sapi),
which had no licence file in October 2026. They are not covered by this project's MIT License, and no licence is
claimed for them here.

- Identical to gozaltech's file of the same name, apart from the namespace name: `src/sapi/com.hpp`,
  `src/sapi/com.cpp`, `src/sapi/registry.hpp`, `src/sapi/registry.cpp`, `src/sapi/utils.hpp`,
  `src/sapi/ISpDataKeyImpl.hpp`, `src/sapi/ISpDataKeyImpl.cpp`.
- Adapted from gozaltech's file of the same name, with changes: `src/sapi/voice_token.hpp`,
  `src/sapi/voice_token.cpp`, `src/sapi/IEnumSpObjectTokensImpl.hpp`, `src/sapi/IEnumSpObjectTokensImpl.cpp`,
  `src/sapi/sapi_main.cpp`, `src/sapi/ISpTTSEngineImpl.hpp`.
- Reworked, but still sharing part of the text of gozaltech's file of the same name:
  `src/sapi/ISpTTSEngineImpl.cpp`, `src/sapi/voice_attributes.hpp`.

## Not covered: CMU Pronouncing Dictionary

`bin/cmudict.txt` is the CMU Pronouncing Dictionary, from Carnegie Mellon University, which is freely
redistributable. It is not covered by this project's MIT License.
