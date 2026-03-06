# Makefile包装器 - 将命令转发到CMake构建系统
BUILD_DIR ?= build

.PHONY: all clean check test install uninstall dist distcheck configure help

all: configure
	@echo "构建项目..."
	cmake --build $(BUILD_DIR) -- -j$$(nproc 2>/dev/null || echo 4)

configure:
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "请先运行 ./configure"; \
		exit 1; \
	fi

clean:
	@echo "清理构建文件..."
	cmake --build $(BUILD_DIR) --target clean

check test: configure
	@echo "运行测试..."
	cd $(BUILD_DIR) && ctest --output-on-failure -j$$(nproc 2>/dev/null || echo 4)

install: configure
	@echo "安装到系统..."
	cmake --install $(BUILD_DIR)

uninstall:
	@echo "卸载..."
	@if [ -f $(BUILD_DIR)/install_manifest.txt ]; then \
		xargs rm -f < $(BUILD_DIR)/install_manifest.txt; \
		echo "卸载完成"; \
	else \
		echo "未找到安装清单，无法卸载"; \
		exit 1; \
	fi

dist:
	@echo "创建源码分发包..."
	@VERSION=$$(grep -m1 'project(' CMakeLists.txt | sed 's/.*VERSION \([0-9.]*\).*/\1/'); \
	ARCHIVE="IPCSharedDataPool-$$VERSION.tar.gz"; \
	TMPDIR=$$(mktemp -d); \
	mkdir -p "$$TMPDIR/IPCSharedDataPool-$$VERSION"; \
	tar --exclude='.git' \
		--exclude='build' \
		--exclude='*.tar.gz' \
		--exclude='examples' \
		--exclude='.github' \
		-cf - . | tar -xf - -C "$$TMPDIR/IPCSharedDataPool-$$VERSION"; \
	tar -czf "$$ARCHIVE" -C "$$TMPDIR" "IPCSharedDataPool-$$VERSION"; \
	rm -rf "$$TMPDIR"; \
	echo "已创建: $$ARCHIVE"

distcheck: dist
	@echo "验证分发包..."
	@ARCHIVE=$$(ls -t IPCSharedDataPool-*.tar.gz | head -1); \
	DIR=$${ARCHIVE%.tar.gz}; \
	echo "解压 $$ARCHIVE..."; \
	tar -xzf "$$ARCHIVE"; \
	echo "配置..."; \
	cd "$$DIR" && ./configure --disable-tests; \
	echo "构建..."; \
	$(MAKE); \
	echo "验证完成"; \
	cd .. && rm -rf "$$DIR"

help:
	@echo "可用目标:"
	@echo "  all         - 构建项目 (默认)"
	@echo "  clean       - 清理构建文件"
	@echo "  check       - 运行测试"
	@echo "  test        - 运行测试 (同check)"
	@echo "  install     - 安装到系统"
	@echo "  uninstall   - 卸载"
	@echo "  dist        - 创建源码分发包"
	@echo "  distcheck   - 验证分发包"
	@echo "  configure   - 检查是否已配置"
	@echo ""
	@echo "使用方法:"
	@echo "  1. ./configure [选项]"
	@echo "  2. make"
	@echo "  3. make check (可选)"
	@echo "  4. sudo make install"
