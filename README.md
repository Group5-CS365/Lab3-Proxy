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
-------

Running the test suite requires [Kyua][] and [ATF][] to be installed.

[Kyua]: https://github.com/jmmv/kyua
[ATF]: https://github.com/jmmv/atf

Kyua is a testing framework for infrastructure software. It supports running
test case written with the ATF libraries.

ATF is an Automated Testing Framework. It consists of C/C++/sh libraries for
writing test programs.

On the SSU blue server, Kyua and ATF are made available through a publicly
accessible prefix. This is automatically configured by the `test-env` script
when running tests through the Makefile.

To run the tests and display a summary of the results:
```sh
make test
```


Advanced Testing
----------------

First, source the `test-env` script in your shell. This adds the project root
directory to the PATH environment variable, and on blue it adds the prefix for
Kyua and ATF to the PATH as well.
```sh
. ./test-env
```

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
kyua debug tests/requests:request1
```
