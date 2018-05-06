--TEST--
Two refs, different objects
--SKIPIF--
<?php
if (!extension_loaded('uref')) {
	echo 'skip';
}
?>
--FILE--
<?php
$std1 = new stdClass;
$std2 = new stdClass;

$u1 = new uref($std1);
$u2 = new uref($std2);

var_dump($u1->valid());
var_dump($u2->valid());

unset($std1);

var_dump($u1->valid());
var_dump($u2->valid());

unset($std2);

var_dump($u1->valid());
var_dump($u2->valid());
?>
--EXPECT--
bool(true)
bool(true)
bool(false)
bool(true)
bool(false)
bool(false)
