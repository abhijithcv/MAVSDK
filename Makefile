JOBS := $(shell expr $(shell nproc) - 2)
all:
	@cmake -Bbuild/default -DCMAKE_BUILD_TYPE=Release -S.
	@cmake --build build/default -j$(JOBS)

install:
	@sudo cmake --build build/default --target install -j$(JOBS)

clean:
	@rm -rf build
	@echo "All build artifacts removed"

.PHONY: all install clean
