TOOLCHAIN := toolchain-mingw64.cmake
BUILD_WIN := build-win
BUILD_LINUX := build-linux

.PHONY: windows linux clean clean-windows clean-linux

windows:
	unset C_INCLUDE_PATH CPLUS_INCLUDE_PATH CPATH; \
	cmake -B $(BUILD_WIN) \
	  -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
	  -DCMAKE_BUILD_TYPE=Release && \
	cmake --build $(BUILD_WIN) -- -j$$(nproc)

linux:
	cmake -B $(BUILD_LINUX) \
	  -DCMAKE_BUILD_TYPE=Release && \
	cmake --build $(BUILD_LINUX) -- -j$$(nproc)

clean-windows:
	rm -rf $(BUILD_WIN)

clean-linux:
	rm -rf $(BUILD_LINUX)

clean: clean-windows clean-linux