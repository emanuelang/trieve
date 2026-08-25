VCPKG_DIR=external/vcpkg
VCPKG_TOOLCHAIN=$(VCPKG_DIR)/scripts/buildsystems/vcpkg.cmake
BUILD_DIR=build
CONFIG=Debug
CMAKE_GENERATOR=Visual Studio 17 2022
CMAKE_ARCH=x64
TESSDATA_DIR=tessdata
TESSDATA_LANG=eng
TESSDATA_URL=https://github.com/tesseract-ocr/tessdata_fast/raw/main/$(TESSDATA_LANG).traineddata

.PHONY: setup-vcpkg setup-tessdata configure build test run setup clean

setup-vcpkg:
	powershell -NoProfile -ExecutionPolicy Bypass -Command "if (!(Test-Path '$(VCPKG_DIR)')) { git submodule update --init --recursive }; if (!(Test-Path '$(VCPKG_DIR)')) { New-Item -ItemType Directory -Force -Path 'external' | Out-Null; git clone https://github.com/microsoft/vcpkg.git '$(VCPKG_DIR)' }"
	powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$(VCPKG_DIR)/bootstrap-vcpkg.bat'"

setup-tessdata:
	powershell -NoProfile -ExecutionPolicy Bypass -Command "New-Item -ItemType Directory -Force -Path '$(TESSDATA_DIR)' | Out-Null; if (!(Test-Path '$(TESSDATA_DIR)/$(TESSDATA_LANG).traineddata')) { Invoke-WebRequest -Uri '$(TESSDATA_URL)' -OutFile '$(TESSDATA_DIR)/$(TESSDATA_LANG).traineddata' }"

configure: setup-vcpkg setup-tessdata
	cmake -S . -B $(BUILD_DIR) -G "$(CMAKE_GENERATOR)" -A $(CMAKE_ARCH) -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN)

build: configure
	cmake --build $(BUILD_DIR) --config $(CONFIG)

test: build
	ctest --test-dir $(BUILD_DIR) -C $(CONFIG) --output-on-failure --no-tests=error

run: build
	./$(BUILD_DIR)/$(CONFIG)/semantic_fs_backend.exe

setup: build

clean:
	cmake -E remove_directory $(BUILD_DIR)
