--TEST--
Duplicate uref
--SKIPIF--
<?php
if (!extension_loaded('uref')) {
	echo 'skip';
}
?>
--FILE--
<?php
$std = new stdClass;

$u   = new uref($std);
$u2  = new uref($std);

var_dump($u->valid());
var_dump($u2->valid());

unset($std);

var_dump($u->valid());
var_dump($u2->valid());
?>
--EXPECT--
bool(true)
bool(true)
bool(false)
bool(false)

