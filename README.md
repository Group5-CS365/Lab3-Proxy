# Lab3-Proxy
[![Build Status](https://travis-ci.org/Group5-CS365/Lab3-Proxy.svg?branch=master)](https://travis-ci.org/Group5-CS365/Lab3-Proxy)

A simple forking web proxy.


Usage
-----

To build the proxy, run
```sh
make
```

Then you can run the proxy from the current directory
```sh
./proxy -h
```


Testing
-----------

Running the test suite requires [Kyua][kyua] and [ATF][atf] to be installed, then run
```sh
make test
```

[kyua]: https://github.com/jmmv/kyua
[atf]: https://github.com/jmmv/atf

On the SSU blue server, Kyua and ATF are made available through a publicly accessible
prefix. This is automatically configured by the `.test-env` script when running tests
through the Makefile.

To manually run the tests, it is necessary to source the `.test-env` script in your shell.
```sh
. .test-env
```

This adds the project root directory to the PATH environment variable, and on blue it adds
the prefix for Kyua and ATF to the PATH as well.

To list the available tests:
```sh
kyua list
```

To run all the test suites for the project:
```sh
kyua test
```

To view a detailed report of the last test results:
```sh
kyua report --verbose
```

To run a particular test case:
```sh
kyua debug testScripts/requests:request1
```
