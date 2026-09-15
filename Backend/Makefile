VCPKG_DIR=external/vcpkg
VCPKG_TOOLCHAIN=$(VCPKG_DIR)/scripts/buildsystems/vcpkg.cmake
BUILD_DIR=build-nmake
CONFIG=Debug
TESSDATA_DIR=tessdata
TESSDATA_LANG=eng
TESSDATA_URL=https://github.com/tesseract-ocr/tessdata_fast/raw/main/$(TESSDATA_LANG).traineddata
VCPKG_INSTALLED_DIR=C:/Users/54343/OneDrive/Desktop/trieve/Backend/build/vcpkg_installed
VCVARS=C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat
LLM_MODEL=qwen2.5vl:3b
QUESTION=de que trata esta imagen y cual es su direccion?
OLLAMA_EXE=C:/Users/54343/AppData/Local/Programs/Ollama/ollama.exe
OLLAMA_TIMEOUT=300
OLLAMA_NUM_CTX=1024
OLLAMA_NUM_PREDICT=128

.PHONY: setup-vcpkg setup-tessdata setup-ollama serve-ollama pull-llm-model configure build run ingest-image ingest-folder ask-answer ask-file ask-last run-answer setup clean

setup-vcpkg:
	powershell -NoProfile -ExecutionPolicy Bypass -Command "if (!(Test-Path '$(VCPKG_DIR)')) { git submodule update --init --recursive }; if (!(Test-Path '$(VCPKG_DIR)')) { New-Item -ItemType Directory -Force -Path 'external' | Out-Null; git clone https://github.com/microsoft/vcpkg.git '$(VCPKG_DIR)' }"
	cd $(VCPKG_DIR) && bootstrap-vcpkg.bat

setup-tessdata:
	powershell -NoProfile -ExecutionPolicy Bypass -Command "New-Item -ItemType Directory -Force -Path '$(TESSDATA_DIR)' | Out-Null; if (!(Test-Path '$(TESSDATA_DIR)/$(TESSDATA_LANG).traineddata')) { Invoke-WebRequest -Uri '$(TESSDATA_URL)' -OutFile '$(TESSDATA_DIR)/$(TESSDATA_LANG).traineddata' }"

setup-ollama:
	powershell -NoProfile -ExecutionPolicy Bypass -Command "if (!(Test-Path '$(OLLAMA_EXE)') -and !(Get-Command ollama -ErrorAction SilentlyContinue)) { irm https://ollama.com/install.ps1 | iex }"

serve-ollama:
	powershell -NoProfile -ExecutionPolicy Bypass -Command "try { Invoke-RestMethod http://localhost:11434/api/tags -TimeoutSec 2 | Out-Null } catch { Start-Process -WindowStyle Hidden '$(OLLAMA_EXE)' -ArgumentList 'serve'; Start-Sleep -Seconds 3 }"

pull-llm-model: setup-ollama serve-ollama
	powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-RestMethod -Uri 'http://localhost:11434/api/pull' -Method Post -ContentType 'application/json' -Body '{\"model\":\"$(LLM_MODEL)\",\"stream\":false}' | ConvertTo-Json -Depth 4"

configure: setup-tessdata
	cmd /c call "$(VCVARS)" ^>nul ^&^& cmake -S . -B $(BUILD_DIR) -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN) -DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_INSTALLED_DIR=$(VCPKG_INSTALLED_DIR)

build: configure
	cmd /c call "$(VCVARS)" ^>nul ^&^& set MAKEFLAGS= ^&^& set MFLAGS= ^&^& cmake --build $(BUILD_DIR)

run: build
	.\$(BUILD_DIR)\semantic_fs_backend.exe

ingest-image: build
	.\$(BUILD_DIR)\semantic_fs_backend.exe ingest "$(IMAGE)"

ingest-folder: build
	.\$(BUILD_DIR)\semantic_fs_backend.exe ingest-folder "$(FOLDER)"

ask-answer: build serve-ollama
	powershell -NoProfile -ExecutionPolicy Bypass -Command "$$env:SEMANTIC_FS_LLM_MODEL='$(LLM_MODEL)'; $$env:SEMANTIC_FS_OLLAMA_EXE='$(OLLAMA_EXE)'; $$env:SEMANTIC_FS_OLLAMA_TIMEOUT='$(OLLAMA_TIMEOUT)'; $$env:SEMANTIC_FS_NUM_CTX='$(OLLAMA_NUM_CTX)'; $$env:SEMANTIC_FS_NUM_PREDICT='$(OLLAMA_NUM_PREDICT)'; .\$(BUILD_DIR)\semantic_fs_backend.exe ask '$(QUESTION)'"

ask-file: build serve-ollama
	powershell -NoProfile -ExecutionPolicy Bypass -Command "$$env:SEMANTIC_FS_LLM_MODEL='$(LLM_MODEL)'; $$env:SEMANTIC_FS_OLLAMA_EXE='$(OLLAMA_EXE)'; $$env:SEMANTIC_FS_OLLAMA_TIMEOUT='$(OLLAMA_TIMEOUT)'; $$env:SEMANTIC_FS_NUM_CTX='$(OLLAMA_NUM_CTX)'; $$env:SEMANTIC_FS_NUM_PREDICT='$(OLLAMA_NUM_PREDICT)'; .\$(BUILD_DIR)\semantic_fs_backend.exe ask '$(QUESTION)' '$(IMAGE)'"

ask-last: build serve-ollama
	powershell -NoProfile -ExecutionPolicy Bypass -Command "$$last = Get-Content 'data\loaded_files.txt' | Select-Object -Last 1; $$env:SEMANTIC_FS_LLM_MODEL='$(LLM_MODEL)'; $$env:SEMANTIC_FS_OLLAMA_EXE='$(OLLAMA_EXE)'; $$env:SEMANTIC_FS_OLLAMA_TIMEOUT='$(OLLAMA_TIMEOUT)'; $$env:SEMANTIC_FS_NUM_CTX='$(OLLAMA_NUM_CTX)'; $$env:SEMANTIC_FS_NUM_PREDICT='$(OLLAMA_NUM_PREDICT)'; .\$(BUILD_DIR)\semantic_fs_backend.exe ask '$(QUESTION)' $$last"

run-answer: build serve-ollama
	powershell -NoProfile -ExecutionPolicy Bypass -Command "$$env:SEMANTIC_FS_LLM_MODEL='$(LLM_MODEL)'; $$env:SEMANTIC_FS_OLLAMA_EXE='$(OLLAMA_EXE)'; $$env:SEMANTIC_FS_OLLAMA_TIMEOUT='$(OLLAMA_TIMEOUT)'; $$env:SEMANTIC_FS_NUM_CTX='$(OLLAMA_NUM_CTX)'; $$env:SEMANTIC_FS_NUM_PREDICT='$(OLLAMA_NUM_PREDICT)'; .\$(BUILD_DIR)\semantic_fs_backend.exe '$(IMAGE)' '$(QUESTION)'"

setup: build setup-ollama pull-llm-model

clean:
	cmake -E remove_directory $(BUILD_DIR)
