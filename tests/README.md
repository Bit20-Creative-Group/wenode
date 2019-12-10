# Automated Testing Documentation

## To Create Test Environment Container

From the root of the repository:

    docker build --rm=false \
        -t WeYouMe/ci-test-environment:latest \
        -f tests/scripts/Dockerfile.testenv .

## To Run The Tests

(Also in the root of the repository.)

    docker build --rm=false \
        -t weyoume/wenode-test \
        -f Dockerfile.test .

## To Troubleshoot Failing Tests

    docker run -ti \
        WeYouMe/ci-test-environment:latest \
        /bin/bash

Then, inside the container:

(These steps are taken from `/Dockerfile.test` in the
repository root.)

    git clone https://github.com/weyoume/wenode.git /usr/local/src/node
    cd /usr/local/src/node
    git checkout <branch> # e.g. 123-feature
    git submodule update --init --recursive
    mkdir -p build
    cd build
    cmake \
        -DCMAKE_BUILD_TYPE=Debug \
        -DBUILD_TESTNET=ON \
        -DLOW_MEMORY_NODE=OFF \
        -DCLEAR_VOTES=ON \
        ..
    make -j$(nproc) chain_test
    ./tests/chain_test
    cd /usr/local/src/node
    doxygen
    programs/build_helpers/check_reflect.py
