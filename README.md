strclear
========

`strclear` removes or replaces embedded strings in files.  BRL-CAD uses it
when staging third party dependencies, primarily to remove build-time paths
from binaries and to rewrite paths in text metadata such as CMake package files
and pkg-config files.

Supported modes:

* `strclear -B <file>` classifies one file.  It returns `0` for binary input
  and `1` for text input, matching the historical BRL-CAD CMake contract.
* `strclear --classify [--files <list> | <file> ...]` emits one JSON Lines
  record per input path: `{"type":"TEXT","path":"..."}` or
  `{"type":"BINARY","path":"..."}`.
* `strclear [-v] <file> <target> [replacement]` rewrites one file.  Binary
  files clear matching bytes to NUL by default; text files replace `target`
  with `replacement`, or remove `target` if no replacement is supplied.
* `strclear [-v] --files <list> <target> [replacement]` applies the same
  operation to each path listed in the file.
* `--text-only` skips binary inputs, and `--binary-only` skips text inputs.
* `-p` expands a filesystem path target into common path forms before matching.
* `-b -c` and `-r` are accepted for compatibility with existing BRL-CAD CMake
  logic.  `-b -c` forces binary clearing, and `-r` forces text replacement.

The `--classify` output is intentionally JSONL so build systems can parse it
without ambiguous path escaping rules.
