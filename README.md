# uref
[![Build Status](https://travis-ci.org/krakjoe/uref.svg?branch=master)](https://travis-ci.org/krakjoe/uref)

`uref` allows the programmer to create weak references to objects

# Requirements

 - llvm >= 4.0
 - x86/x64
   - *nix
   - Darwin
   - FreeBSD
 - bravery

# LLVM

Following components must be available:

 - Target
 - Native
 - MCDisassembler

`llvm-config` should be in path, or `LLVM_CONFIG` set in `ENV` (for `configure`)

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

*Note: `uref` does not support properties*

# Implementation

`uref` leverages `mprotect` to intercept writes to the objects store, the resulting `SIGSEGV`⚔ is handled by disassembling the current machine code instruction (using llvm) to determine it's length (and so the offset of the next instruction); The first byte of the next instruction is backed up and replaced with `0xCC` [<sub>int3</sub>](https://en.wikipedia.org/wiki/INT_(x86_instruction)#INT_3), the store is unprotected to allow the write to go ahead, urefs are at this point updated. Upon the next instruction (which `uref` replaced), `SIGTRAP` is raised and `uref` replaces the `0xCC` that it inserted with the first byte of the original instruction and the object store is write protected again.

⚔ Darwin may raise `SIGBUS`, depending on the position of the moon relative to the fourteenth ... no but, we don't know why ...

# Thanks

Thanks to @bwoebi for help developing the idea, and then providing me access to a mac to make it work there ...
