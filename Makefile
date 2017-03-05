export SHELL = /bin/bash
export DIR = $(shell pwd)
export INCS_DIR = $(DIR)/include
export SRCS_DIR = $(DIR)/src
export LIBS_DIR = $(DIR)/lib
export DEPS_DIR = $(DIR)/dep
export OBJS_DIR = $(DIR)/obj
export TARGETS_DIR = $(DIR)/bin
export TESTS_DIR = $(DIR)/test

export INCS = $(wildcard $(INCS_DIR)/*.hpp)
export PCHS = $(patsubst %,%.gch,$(INCS))
export SRCS = $(wildcard $(SRCS_DIR)/*.cpp)
export TESTS = $(wildcard $(TESTS_DIR)/*.cpp)
export DEPS = $(patsubst $(INCS_DIR)/%,$(DEPS_DIR)/%.d,$(INCS)) $(patsubst $(SRCS_DIR)/%,$(DEPS_DIR)/%.d,$(SRCS))
export OBJS = $(patsubst $(SRCS_DIR)/%,$(OBJS_DIR)/%.o,$(SRCS))
export TESTEXES = $(patsubst $(TESTS_DIR)/%.cpp,$(TARGETS_DIR)/%,$(TESTS))

export LIBTAI = $(LIBS_DIR)/libtai.a

export TEST_LOAD ?= $(shell nproc --all)
export TEST_ARGS ?= 31 16 16 16 8 10

export CXX = clang++
# export CXX = g++-6
# export CXX = g++
export CXXFLAGS = -std=c++1z -m64 -Wall -O3 -g
ifeq ($(mode), debug) 
	CXXFLAGS += -DTAI_DEBUG
endif
export CXXFLAGS += -I$(INCS_DIR) -I/usr/local/include
export CXXFLAGS += -stdlib=libc++ -lc++ -lc++abi
export CXXFLAGS += -lm -lpthread
export CXXFLAGS += $(shell if [ `uname` = Linux ]; then echo '-lrt -laio'; fi)
# export CXXFLAGS += -fno-omit-frame-pointer -fsanitize=address
export AR = ar
export MKDIR = @mkdir -p
export CMP = cmp -b

export CP = cp -rf
export INSTALL = install
export RM = rm -rf

.PHONY: all
all: $(LIBTAI) $(TESTEXES)

$(TESTEXES): $(TARGETS_DIR)/%: $(TESTS_DIR)/%.cpp $(LIBTAI)
	$(MKDIR) $(TARGETS_DIR)
	$(CXX) $(CXXFLAGS) $< -L$(LIBS_DIR) -ltai -o $@

$(OBJS): $(OBJS_DIR)/%.o: $(SRCS_DIR)/% $(PCHS)
	$(MKDIR) $(OBJS_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(DEPS): $(DEPS_DIR)/%.d: $(SRCS_DIR)/%
	$(MKDIR) $(DEPS_DIR)
	$(CXX) $(CXXFLAGS) -MM $^ -o $@

$(PCHS): %.gch: %
	$(CXX) $(CXXFLAGS) -c $^ -o $@

$(LIBTAI): $(OBJS)
	$(MKDIR) $(LIBS_DIR)
	$(RM) $@
	$(AR) -r $@ $(OBJS)

.PHONY: test
test: all
	sudo sync
	if [ `uname` == Darwin ]; then sudo purge; fi
	if [ `uname` == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi
	$(MKDIR) tmp
	$(RM) tmp/*
	dd if=/dev/zero of=tmp/sync bs=1G count=1
	dd if=/dev/zero of=tmp/aio bs=1G count=1
	dd if=/dev/zero of=tmp/tai bs=1G count=1
	sync
	if [ `uname` == Darwin ]; then sudo purge; fi
	if [ `uname` == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi
	time (bin/fixSizedWrites 1 1024)
	time (sync tmp/sync)
	time sync
	if [ `uname` == Darwin ]; then sudo purge; fi
	if [ `uname` == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi
	time (bin/fixSizedWrites 2 1024)
	time (sync tmp/aio)
	time sync
	if [ `uname` == Darwin ]; then sudo purge; fi
	if [ `uname` == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi
	time (bin/fixSizedWrites 4 1024)
	time (sync tmp/tai)
	time sync
	$(CMP) tmp/sync tmp/tai || $(CMP) -l tmp/sync tmp/tai | wc -l
	$(CMP) tmp/sync tmp/aio || $(CMP) -l tmp/sync tmp/aio | wc -l

.PHONY: test_mt
test_mt:
	@echo
	@echo '================================'
	@echo 'Workload     = '$(TEST_LOAD)' thread(s)'
	@echo 'File Size    = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^(\1-20)/' | bc -l`' MB'
	@echo 'Read Size    = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^(\2-10)/' | bc`' KB'
	@echo 'Write Size   = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^(\3-10)/' | bc`' KB'
	@echo 'I/O Rounds   = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^\4/' | bc`
	@echo 'Sync Rate    = 1/'`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^\5/' | bc`
	@echo 'Wait Rate    = 1/'`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^\6/' | bc`
	@echo '================================'
	@echo
	sudo sync
	if [ `uname` == Darwin ]; then sudo purge; fi
	if [ `uname` == Linux ]; then sudo bash -c "echo 1 > /proc/sys/vm/drop_caches"; fi
	$(MKDIR) tmp
	$(RM) tmp/*
	for i in $$(seq 0 `expr $(TEST_LOAD) - 1`); do dd if=/dev/zero of=tmp/file$$i bs=`sed 's/\([0-9]*\).*/2^\1/' <<<"$(TEST_ARGS)" | bc` count=1; done
	for i in 4 `seq 0 4`; do for j in `seq 0 2`; do for k in `seq $(TEST_LOAD)`; do \
		if [ `uname` == Darwin ]; then sudo purge; fi; \
		if [ `uname` == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi; \
		time (bin/multi_thread_comp $$k $$i $$j $(TEST_ARGS)); \
		time (sync tmp/*); \
		time sync; \
	done done done

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),test_mt)
sinclude $(DEPS)
endif
endif

.PHONY: clean
clean:
	@$(RM) $(LIBS_DIR) $(DEPS_DIR) $(OBJS_DIR) $(TARGETS_DIR) $(PCHS) tai tai.dSYM
