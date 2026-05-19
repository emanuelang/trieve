VCPKG_DIR ?= C:/tools/vcpkg
VCPKG_TOOLCHAIN := $(VCPKG_DIR)/scripts/buildsystems/vcpkg.cmake
VCPKG_DIR_WIN := $(subst /,\,$(VCPKG_DIR))
VCPKG_TOOLCHAIN_WIN := $(subst /,\,$(VCPKG_TOOLCHAIN))
BUILD_DIR ?= build
CONFIG ?= Debug

.PHONY: setup-vcpkg configure build run setup clean

setup-vcpkg:
	@if not exist "$(VCPKG_TOOLCHAIN_WIN)" git submodule update --init --recursive
	@if not exist "$(VCPKG_TOOLCHAIN_WIN)" if not exist "external" mkdir external
	@if not exist "$(VCPKG_TOOLCHAIN_WIN)" git clone https://github.com/microsoft/vcpkg.git "$(VCPKG_DIR_WIN)"
	@if not exist "$(VCPKG_DIR_WIN)\vcpkg.exe" if exist "$(VCPKG_DIR_WIN)\bootstrap-vcpkg.bat" call "$(VCPKG_DIR_WIN)\bootstrap-vcpkg.bat"

configure: setup-vcpkg
	cmake -S . -B $(BUILD_DIR) -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN)

build: configure
	cmake --build $(BUILD_DIR) --config $(CONFIG)

run: build
	./$(BUILD_DIR)/$(CONFIG)/semantic_fs_backend.exe

setup: build

clean:
	cmake -E remove_directory $(BUILD_DIR)
