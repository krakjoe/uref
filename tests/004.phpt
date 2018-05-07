--TEST--
No cloning
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

clone $u;
?>
--EXPECTF--
Fatal error: Uncaught RuntimeException: uref objects do not support cloning in %s:5
Stack trace:
#0 {main}
  thrown in %s on line 5
