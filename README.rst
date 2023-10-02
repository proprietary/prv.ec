====================
ec_prv_url_shortener
====================

URL shortening service written in C++

Design goals
------------

- Extremely fast, low latency HTTP responses
- Do not block the event loop
- Support >100k concurrent requests
- Can be run on cheap hardware
- Web scale without the hardware cost


Installation
------------

This is targeted and tested on amd64 Linux only, for now, though it should not be hard to build on BSD.


Build Dependencies
==================

Use the latest gcc or clang. C++20 is a must. See `docs/INSTALLATION.rst <docs/INSTALLATION.rst>`_ for detailed build instructions.

- `RocksDB <https://github.com/facebook/rocksdb>`_ - an on-disk embedded key-value database maintained by Meta/FB
- `proxygen <https://github.com/facebook/proxygen>`_ - robust C++ web server maintained by Meta/FB
- `folly <https://github.com/facebook/folly>`_ - batteries-included C++ library maintained by Meta/FB
- `fizz <https://github.com/facebookincubator/fizz>`_ - TLS1.3 library
- `gflags <https://gflags.github.io/gflags/>`_ - Google Flags
- `gtest <https://github.com/google/googletest>`_ - Google Teset
- `glog <https://github.com/google/glog>`_ - Google Logging

A frontend built with `Next.js <https://nextjs.org/>`_ is included in `frontend/`. Building the frontend requires npm and node. Build it with::

  $ npm install
  $ npx next build



License
-------

Apache-2.0
