BUILD ?= build

.PHONY: build test sanitize bench reproduce flamegraph clean

build:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD) -j

test: build
	cd $(BUILD) && ctest --output-on-failure

sanitize:
	cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLOB_SANITIZE=address,undefined -DLOB_BENCH=OFF
	cmake --build build-asan -j
	cd build-asan && ctest --output-on-failure
	cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DLOB_SANITIZE=thread -DLOB_BENCH=OFF
	cmake --build build-tsan -j --target lob_tests
	./build-tsan/lob_tests --gtest_filter='Spsc.*'

bench: build
	$(BUILD)/bench_book

reproduce: build
	python3 scripts/bench.py --build $(BUILD)

flamegraph: build
	./scripts/flamegraph.sh

clean:
	rm -rf build build-asan build-tsan build-prof
