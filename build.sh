#! /bin/bash

eosio-cpp -I="./contract/basiccontract.hpp" -o="./build/basiccontract.wasm" -contract="BasicToken" -abigen ./contract/basiccontract.cpp