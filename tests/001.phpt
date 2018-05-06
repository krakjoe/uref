--TEST--
Basic uref
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

var_dump($u->valid());
var_dump($u->get());

unset($std);

var_dump($u->valid());
var_dump($u->get());
?>
--EXPECT--
bool(true)
object(stdClass)#1 (0) {
}
bool(false)
NULL

