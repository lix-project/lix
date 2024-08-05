---
name: allow-unsafe-native-code-during-evaluation
internalName: enableNativeCode
type: bool
default: false
---
Enable built-in functions that allow executing native code.

In particular, this adds:
- `builtins.importNative` *path* *symbol*

  Runs function with *symbol* from a dynamic shared object (DSO) at *path*.
  This may be used to add new builtins to the Nix language.
  The procedure must have the following signature:
  ```cpp
  extern "C" typedef void (*ValueInitialiser) (EvalState & state, Value & v);
  ```

- `builtins.exec` *arguments*

  Execute a program, where *arguments* are specified as a list of strings, and parse its output as a Nix expression.
