# Galois TCP pending pool

[![Build Status](https://www.travis-ci.org/galois-advertising/gpendingpool.svg?branch=master)](https://www.travis-ci.org/galois-advertising/gpendingpool)


![logo](./galois-tcp-pending-pool.png)

## Description
Galois TCP pending pool is a TCP connection management module based on select/poll. It provides the basic logic of queuing and overtime discarding of TCP connections in the case of large-scale connections.

## Installation

```shell
$ git clone git@github.com:galois-advertising/gpendingpool.git
$ cd gpendingpool 
$ mkdir build & cd build
$ cmake ../
$ make -j4
$ make install
```

## Usage

## Contributing

## Credits

## License

MIT
