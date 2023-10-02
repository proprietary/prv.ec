============
Installation
============

There are several third-party dependencies, and building them all is not trivial. Future efforts will be on streamlining the build process with Bazel. As of now, manage dependencies with a combination the Linux package manager, CMake, and manually building repositories.

For simplicity, the instructions herein will only be for Debian 12 or Ubuntu 22.04 LTS, although theoretically everything should be portable to other Linux distros, MacOS, \*BSD and even Windows with some effort.

Dependencies list
-----------------

At a high-level, this application uses the following libraries:

- proxygen
- RocksDB
- folly


Building from source
--------------------

Install common dependencies using package manager (Debian 12 or Ubuntu 22.04 LTS):

.. code:: bash
	  
   sudo apt-get install \
       g++ \
       libboost-all-dev \
       libevent-dev \
       libdouble-conversion-dev \
       libgoogle-glog-dev \
       libgflags-dev \
       libiberty-dev \
       liblz4-dev \
       liblzma-dev \
       libsnappy-dev \
       make \
       zlib1g-dev \
       binutils-dev \
       libjemalloc-dev \
       libssl-dev \
       pkg-config \
       libsodium-dev \
       libfmt-dev \
       libgtest-dev \
       libgmock-dev \
       libzstd-dev \
       gperf

`Download and install the latest CMake <https://cmake.org/download/>`_ (minimum >= 3.27) (skip this if running ``$ cmake --version`` shows a version >= 3.27):

.. code:: bash
   cd /tmp
   sudo apt remove --purge cmake # Remove existing CMake installation
   curl -LO https://github.com/Kitware/CMake/releases/download/v3.27.6/cmake-3.27.6-linux-x86_64.tar.gz
   sudo tar -C /usr/local --strip-components=1 -xzf cmake-3.27.6-linux-x86_64.tar.gz

Then build and install ``folly``:

.. code:: bash
	  
   git clone https://github.com/facebook/folly
   mkdir folly/build_ && cd folly/build_
   cmake ..
   make -j $(nproc)
   sudo make install

Then build and install ``fizz``:

.. code:: bash
	  
   cd ../..
   git clone https://github.com/facebookincubator/fizz
   mkdir fizz/build_ && cd fizz/build_
   cmake ../fizz
   make -j $(nproc)
   sudo make install

Then build and install ``wangle``:

.. code:: bash
	  
   cd ../..
   git clone https://github.com/facebook/wangle
   mkdir wangle/build_ && cd wangle/build_
   cmake ../wangle
   make -j $(nproc)
   sudo make install

Then build and install ``rocksdb``:

.. code::
   cd ../..
   git clone https://github.com/facebook/rocksdb
   mkdir rocksdb/build_ && cd rocksdb/build_
   cmake ..
   make -j $(nproc)
   sudo make install

Then build this application:

.. code:: bash
	  
   cd frontend/
   export NEXT_PUBLIC__EC_PRV_URL_SHORTENER__RECAPTCHA_V2_SITE_KEY=<recaptcha V2 site key (shown to public, not your secret key!)>
   export NEXT_PUBLIC__EC_PRV_URL_SHORTENER__BASE_URL=https://prv.ec # change to your domain/brand
   export NEXT_PUBLIC__EC_PRV_URL_SHORTENER__API_BASE_URL=https://prv.ec # change to your domain/brand
   npm run build
   cd ../
   mkdir build_ && cd build_
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make -j $(nproc)
   # Go and edit sample_app_config.yml -> app_config.yml
   ./web_server --config_file=$(pwd)/app_config.yml

And the URL shortening service should be running.
