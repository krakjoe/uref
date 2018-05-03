# uref

uref allows the programmer to create weak references to other objects ... using dark scary magic ...

I like dark scary magic ...

# experimental

note that this is a totally unproven idea, but was a bit of fun to write and seems theoretically sound ...

# tests ...

whatever ...

# hardware

currently this will only work on x64 *nix, with zend mm (or another mm that allows permissions to be set on heap memory) ...

I hate windows, if you want to make windows work, be my guest ...

x86 support shouldn't be that difficult, but seems unnecessary ...

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
