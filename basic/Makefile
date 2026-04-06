CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDFLAGS = -lm

SRC = src/main.c src/lexer.c src/value.c src/symtable.c src/ast.c \
      src/parser.c src/interpreter.c src/coerce.c src/error.c \
      src/console.c src/builtins_str.c src/builtins_math.c \
      src/print_using.c src/array.c src/udt.c src/builtins_convert.c \
      src/callframe.c src/fileio.c src/system_api.c
OBJ = $(SRC:.c=.o)

fbasic: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test-phase1: fbasic
	@echo "=== Phase 1 Tests ==="
	@for f in tests/verify/phase1_*.bas; do \
		name=$$(basename $$f .bas | sed 's/phase1_//'); \
		expected="tests/verify/phase1_expected/$$name.txt"; \
		if [ -f "$$expected" ]; then \
			./fbasic "$$f" > /tmp/p1_$$name.txt 2>&1 && \
			diff -u "$$expected" /tmp/p1_$$name.txt > /dev/null 2>&1 && \
			echo "  PASS: $$name" || echo "  FAIL: $$name"; \
		else \
			echo "  SKIP: $$name (no expected output)"; \
		fi; \
	done

test-phase2: fbasic
	@echo "=== Phase 2 Tests ==="
	@for f in tests/verify/phase2_*.bas; do \
		name=$$(basename $$f .bas | sed 's/phase2_//'); \
		expected="tests/verify/phase2_expected/$$name.txt"; \
		if [ -f "$$expected" ]; then \
			./fbasic "$$f" > /tmp/p2_$$name.txt 2>&1 && \
			diff -u "$$expected" /tmp/p2_$$name.txt > /dev/null 2>&1 && \
			echo "  PASS: $$name" || echo "  FAIL: $$name"; \
		else \
			echo "  SKIP: $$name (no expected output)"; \
		fi; \
	done

test-phase3: fbasic
	@echo "=== Phase 3 Tests ==="
	@for f in tests/verify/phase3_*.bas; do \
		name=$$(basename $$f .bas | sed 's/phase3_//'); \
		expected="tests/verify/phase3_expected/$$name.txt"; \
		if [ -f "$$expected" ]; then \
			./fbasic "$$f" > /tmp/p3_$$name.txt 2>&1 && \
			diff -u "$$expected" /tmp/p3_$$name.txt > /dev/null 2>&1 && \
			echo "  PASS: $$name" || echo "  FAIL: $$name"; \
		else \
			echo "  SKIP: $$name (no expected output)"; \
		fi; \
	done

test-phase4: fbasic
	@echo "=== Phase 4 Tests ==="
	@for f in tests/verify/phase4_*.bas; do \
		name=$$(basename $$f .bas | sed 's/phase4_//'); \
		expected="tests/verify/phase4_expected/$$name.txt"; \
		if [ -f "$$expected" ]; then \
			./fbasic "$$f" > /tmp/p4_$$name.txt 2>&1 && \
			diff -u "$$expected" /tmp/p4_$$name.txt > /dev/null 2>&1 && \
			echo "  PASS: $$name" || echo "  FAIL: $$name"; \
		else \
			echo "  SKIP: $$name (no expected output)"; \
		fi; \
	done

test-phase5: fbasic
	@echo "=== Phase 5 Tests ==="
	@for f in tests/verify/phase5_*.bas; do \
		name=$$(basename $$f .bas | sed 's/phase5_//'); \
		expected="tests/verify/phase5_expected/$$name.txt"; \
		if [ -f "$$expected" ]; then \
			./fbasic "$$f" > /tmp/p5_$$name.txt 2>&1 && \
			diff -u "$$expected" /tmp/p5_$$name.txt > /dev/null 2>&1 && \
			echo "  PASS: $$name" || echo "  FAIL: $$name"; \
		else \
			echo "  SKIP: $$name (no expected output)"; \
		fi; \
	done

test-phase6: fbasic
	@echo "=== Phase 6 Tests ==="
	@for f in tests/verify/phase6_*.bas; do \
		name=$$(basename $$f .bas | sed 's/phase6_//'); \
		expected="tests/verify/phase6_expected/$$name.txt"; \
		if [ -f "$$expected" ]; then \
			./fbasic "$$f" > /tmp/p6_$$name.txt 2>&1 && \
			diff -u "$$expected" /tmp/p6_$$name.txt > /dev/null 2>&1 && \
			echo "  PASS: $$name" || echo "  FAIL: $$name"; \
		else \
			echo "  SKIP: $$name (no expected output)"; \
		fi; \
	done

test-phase7: fbasic
	@echo "=== Phase 7 Tests ==="
	@for f in tests/verify/phase7_*.bas; do \
		name=$$(basename $$f .bas | sed 's/phase7_//'); \
		expected="tests/verify/phase7_expected/$$name.txt"; \
		if [ -f "$$expected" ]; then \
			./fbasic "$$f" > /tmp/p7_$$name.txt 2>&1 && \
			diff -u "$$expected" /tmp/p7_$$name.txt > /dev/null 2>&1 && \
			echo "  PASS: $$name" || echo "  FAIL: $$name"; \
		else \
			echo "  SKIP: $$name (no expected output)"; \
		fi; \
	done

test: test-phase1 test-phase2 test-phase3 test-phase4 test-phase5 test-phase6 test-phase7
	@echo "=== All tests complete ==="

clean:
	rm -f $(OBJ) fbasic

.PHONY: test test-phase1 test-phase2 test-phase3 test-phase4 test-phase5 test-phase6 test-phase7 clean
