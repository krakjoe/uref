uref-test-coverage:
	CCACHE_DISABLE=1 EXTRA_CXXFLAGS="-fprofile-arcs -ftest-coverage" EXTRA_CFLAGS="-fprofile-arcs -ftest-coverage" TEST_PHP_ARGS="-q" $(MAKE) clean test

uref-test-coverage-lcov: uref-test-coverage
	lcov -c --directory $(top_srcdir)/.libs --output-file $(top_srcdir)/coverage.info

uref-test-coverage-html: uref-test-coverage-lcov
	genhtml $(top_srcdir)/coverage.info --output-directory=$(top_srcdir)/html

uref-test-coverage-travis:
	CCACHE_DISABLE=1 EXTRA_CFLAGS="-fprofile-arcs -ftest-coverage" $(MAKE)
