CC    := /home/zly/bsc/llvm-project/install/bin/clang
INC   := -I/home/zly/bsc/llvm-project/install/include/libcbs
LIB   := -L/home/zly/bsc/llvm-project/install/lib -lstdcbs -lpthread -lssl -lcrypto
FLAGS := -Wall -Wextra -Wno-nullability-completeness -g
BINDIR := bin

all: $(BINDIR)/httpd

$(BINDIR)/httpd: src/main.cbs $(wildcard src/*.cbs) $(wildcard src/platform/*.cbs) $(wildcard include/*.hbs) | $(BINDIR) check-layers
	$(CC) $(FLAGS) $(INC) src/main.cbs -o $@ $(LIB)

# Enforce: business src/*.cbs must be _Unsafe-free (adapters in src/platform/ are exempt).
check-layers:
	@bash tests/check_no_unsafe.sh

$(BINDIR):
	mkdir -p $(BINDIR)

# Unit tests (each tests/test_<name>.cbs). Keep in sync with tests/valgrind_units.sh.
UNITS := str_util mime http_request http_response config router path_guard fs file_server handler

# Build any test:  make test-X  (compiles tests/test_X.cbs)
test-%: tests/test_%.cbs | $(BINDIR)
	$(CC) $(FLAGS) $(INC) $< -o $(BINDIR)/test_$* $(LIB)
	./$(BINDIR)/test_$*

# Build + run every unit test.
test: $(addprefix test-,$(UNITS))

# Run all unit tests under valgrind. Needed because a BSC test can PASS while
# use-after-free (see BSC-IMPROVEMENTS #1 / IJC66K) — exit codes alone miss it.
valgrind:
	bash tests/valgrind_units.sh

# curl-driven AC-2..AC-5 + traversal + 405 against a live server.
integration: $(BINDIR)/httpd
	bash tests/run_integration.sh

# Full gate: unit tests, then valgrind, then integration.
check: test valgrind integration

smoke: tests/smoke.cbs | $(BINDIR)
	$(CC) $(FLAGS) $(INC) $< -o $(BINDIR)/smoke $(LIB)
	./$(BINDIR)/smoke

run: $(BINDIR)/httpd
	./$(BINDIR)/httpd

clean:
	rm -rf $(BINDIR)

.PHONY: all clean run smoke check-layers test valgrind integration check
