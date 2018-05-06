# uref
[![Build Status](https://travis-ci.org/krakjoe/uref.svg?branch=master)](https://travis-ci.org/krakjoe/uref)
[![Coverage Status](https://coveralls.io/repos/github/krakjoe/uref/badge.svg)](https://coveralls.io/github/krakjoe/uref)

uref allows the programmer to create weak references to other objects ... using dark scary magic ...

# Requirements

 - llvm >= 4.0
 - x64 *nix

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
