# swl — convenient build/test shortcuts

BUILD     ?= build
FASTBUILD ?= build-fast
JOBS      ?= $(shell nproc)

.PHONY: build test test-fast verify clean

# Release build with tests enabled (matches CI)
build:
	cmake -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
	cmake --build $(BUILD) -j$(JOBS)

# Build (if needed) and run the full test suite
test: build
	ctest --test-dir $(BUILD) --output-on-failure

# Fast iteration loop: Release without LTO, tests run in parallel
test-fast:
	cmake -B $(FASTBUILD) -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON \
		-DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=OFF
	cmake --build $(FASTBUILD) -j$(JOBS)
	ctest --test-dir $(FASTBUILD) -j$(JOBS) --output-on-failure

# Everything: build + full test suite
verify: test

clean:
	rm -rf $(BUILD) $(FASTBUILD)
