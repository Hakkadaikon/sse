clean:
    rm -rf build

fmt:
    clang-format -i $(find ./src -name '*.[ch]')

release-build:
    rm -rf build
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

debug-build:
    rm -rf build
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build

test: debug-build
    ./build/tests/unit_tests
    ./build/tests/integration_tests
    ./build/tests/api_tests

unit-test: debug-build
    ./build/tests/unit_tests

integration-test: debug-build
    ./build/tests/integration_tests

api-test: debug-build
    ./build/tests/api_tests
