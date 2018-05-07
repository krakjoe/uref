# uref
[![Build Status](https://travis-ci.org/krakjoe/uref.svg?branch=master)](https://travis-ci.org/krakjoe/uref)

`uref` allows the programmer to create weak references to objects

# Requirements

 - llvm >= 4.0
 - x64 *nix
 - bravery

# API

```php
<?php
final class uref {
	public function __construct(object $object);

	public function get() : ?object;
	public function valid() : bool;
}
?>
```

*Note: `uref` does not support properties or cloning*

# Implementation

`uref` leverages `mprotect` to intercept writes to the objects store, the resulting `SIGSEGV` is handled by disassembling the current machine code instruction (using llvm) to determine it's length (and so the offset of the next instruction); The first byte of the next instruction is backed up and replaced with `0xCC` ~int3~, the store is unprotected to allow the write to go ahead, urefs are at this point updated. Upon the next instruction (which `uref` replaced), `SIGTRAP` is raised and `uref` replaces the `0xCC` that it inserted with the first byte of the original instruction and the object store is write protected again.
