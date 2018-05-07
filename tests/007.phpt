--TEST--
No isset
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

isset($u->property);
?>
--EXPECTF--
Fatal error: Uncaught RuntimeException: uref objects do not support properties in %s:5
Stack trace:
#0 {main}
  thrown in %s on line 5
