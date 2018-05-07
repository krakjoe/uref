--TEST--
Cloning
--SKIPIF--
<?php
if (!extension_loaded('uref')) {
	echo 'skip';
}
?>
--FILE--
<?php
$s = new stdClass;
$u = new uref($s);

$u2 = clone $u;

var_dump($u->valid(), $u2->valid());

unset($s);

var_dump($u->valid(), $u2->valid());
?>
--EXPECT--
bool(true)
bool(true)
bool(false)
bool(false)
