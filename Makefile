CC    := /home/zly/bsc/llvm-project/build/bin/clang
INC   := -I/home/zly/bsc/llvm-project/install/include/libcbs
LIB   := -L/home/zly/bsc/llvm-project/install/lib -lstdcbs -lpthread
FLAGS := -Wall -Wextra -Wno-nullability-completeness -g
BINDIR := bin

all: $(BINDIR)/httpd

$(BINDIR)/httpd: src/main.cbs $(wildcard src/*.cbs) $(wildcard src/platform/*.cbs) $(wildcard include/*.hbs) $(wildcard include/platform/*.hbs) | $(BINDIR)
	$(CC) $(FLAGS) $(INC) src/main.cbs -o $@ $(LIB)

$(BINDIR):
	mkdir -p $(BINDIR)

# build any unit test: make test-X  (compiles tests/test_X.cbs)
test-%: tests/test_%.cbs | $(BINDIR)
	$(CC) $(FLAGS) $(INC) $< -o $(BINDIR)/test_$* $(LIB)
	./$(BINDIR)/test_$*

smoke: tests/smoke.cbs | $(BINDIR)
	$(CC) $(FLAGS) $(INC) $< -o $(BINDIR)/smoke $(LIB)
	./$(BINDIR)/smoke

run: $(BINDIR)/httpd
	./$(BINDIR)/httpd

clean:
	rm -rf $(BINDIR)

.PHONY: all clean run smoke
