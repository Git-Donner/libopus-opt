# Build the benchmark against an already-built static libopus archive.
# GNUmakefile intentionally avoids the repository-wide Makefile ignore rule.
OPUS_DIR ?= ..
OPUS_BUILD_DIR ?= $(OPUS_DIR)
OPUS_LIB ?=
CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS ?=
LDFLAGS ?=
PYTHON ?= python3

.PHONY: all clean check
all:
	$(PYTHON) scripts/build_bench.py --source-dir "$(OPUS_DIR)" --build-dir "$(OPUS_BUILD_DIR)" --library "$(OPUS_LIB)" --cc "$(CC)" --cflags="$(CFLAGS)" --cppflags="$(CPPFLAGS)" --ldflags="$(LDFLAGS)"

check: all
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v

clean:
	$(PYTHON) -c "import pathlib,shutil; shutil.rmtree('build',ignore_errors=True); pathlib.Path('opus_perf_bench').unlink(missing_ok=True)"
