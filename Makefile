export SHELL = /bin/bash
export OS = $(shell uname)
export DIR = $(shell pwd)
export INCS_DIR = $(DIR)/include
export SRCS_DIR = $(DIR)/src
export LIBS_DIR = $(DIR)/lib
export DEPS_DIR = $(DIR)/dep
export OBJS_DIR = $(DIR)/obj
export TARGETS_DIR = $(DIR)/bin
export TESTS_DIR = $(DIR)/test
export TESTLIBS_DIR = $(DIR)/test/testlib
export TESTOBJS_DIR = $(OBJS_DIR)/test

export INCS = $(wildcard $(INCS_DIR)/*.hpp)
export PCHS = $(patsubst %,%.gch,$(INCS))
export SRCS = $(wildcard $(SRCS_DIR)/*.cpp)
export TESTS = $(wildcard $(TESTS_DIR)/*.cpp)
export TESTLIBS = $(wildcard $(TESTLIBS_DIR)/*.cpp)
export DEPS = $(patsubst $(INCS_DIR)/%,$(DEPS_DIR)/%.d,$(INCS)) $(patsubst $(SRCS_DIR)/%,$(DEPS_DIR)/%.d,$(SRCS))
export OBJS = $(patsubst $(SRCS_DIR)/%,$(OBJS_DIR)/%.o,$(SRCS))
export TESTOBJS = $(patsubst $(TESTLIBS_DIR)/%.cpp,$(TESTOBJS_DIR)/%.o,$(TESTLIBS))
export TESTEXES = $(patsubst $(TESTS_DIR)/%.cpp,$(TARGETS_DIR)/%,$(TESTS))

export LIBTAI = $(LIBS_DIR)/libtai.a

export TEST_LOAD ?= $(shell nproc --all)
export TEST_ARGS ?= 30 64 64 14 8 10
# For test_mt only:
#     read size, write size (KB)
#     file size, io round, sync rate, wait rate (2^x)

export LD = lld
export CXX = clang++
# export CXX = g++-6
# export CXX = g++
export CXXFLAGS = -std=c++1z -m64 -Wall -O0 -g
export CXXFLAGS += $(shell if [ $(OS) = Linux ]; then echo '-fuse-ld=lld'; fi)
export CXXFLAGS += -flto -fwhole-program-vtables
export CXXFLAGS += -D_FILE_OFFSET_BITS=64
ifeq ($(mode), debug) 
	CXXFLAGS += -DTAI_DEBUG
endif
export CXXFLAGS += -I$(INCS_DIR) -I/usr/local/include
export CXXFLAGS += -stdlib=libc++ -lc++ -lc++abi
# export CXXFLAGS += -DTAI_JEMALLOC -ljemalloc
export CXXFLAGS += -lm -lpthread
export CXXFLAGS += $(shell if [ $(OS) = Linux ]; then echo '-lrt -laio'; fi)
# export CXXFLAGS += -fno-omit-frame-pointer -fsanitize=address
export AR = llvm-ar
export MKDIR = @mkdir -p
export CMP = cmp -b

export CP = cp -rf
export MV = mv -f
export INSTALL = install
export RM = rm -rf

.PHONY: all
all: $(LIBTAI) $(TESTEXES)

$(TESTEXES): $(TARGETS_DIR)/%: $(TESTS_DIR)/%.cpp $(TESTOBJS) $(LIBTAI)
	$(MKDIR) $(TARGETS_DIR)
	$(CXX) $(CXXFLAGS) $< -L$(LIBS_DIR) $(TESTOBJS) -ltai -o $@

$(TESTOBJS): $(TESTOBJS_DIR)/%.o: $(TESTLIBS_DIR)/%.cpp $(LIBTAI)
	$(MKDIR) $(TESTOBJS_DIR)
	$(CXX) $(CXXFLAGS) -c $< -L$(LIBS_DIR) -ltai -o $@

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
	$(AR) cr $@ $(OBJS)

.PHONY: pre_test
pre_test:
	@echo
	@echo '================================'
	@echo 'Workload     = '$(TEST_LOAD)' thread(s)'
	@echo 'File Size    = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^(\1-20)/' | bc -l`' MB'
	@echo 'Read Size    = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/\2/' | bc`' KB'
	@echo 'Write Size   = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/\3/' | bc`' KB'
	@echo 'I/O Rounds   = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^\4/' | bc`
	@echo 'Sync Rate    = 1/'`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^\5/' | bc`
	@echo 'Wait Rate    = 1/'`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^\6/' | bc`
	@echo '================================'
	@echo
	sudo sync
	if [ $(OS) == Darwin ]; then sudo purge; fi
	if [ $(OS) == Linux ]; then sudo bash -c "echo 1 > /proc/sys/vm/drop_caches"; fi
	$(MKDIR) tmp
	$(RM) tmp/*
	for i in $$(seq 0 `expr $(TEST_LOAD) - 1`); do                                                                                              \
	    if [ $(OS) == Darwin ]; then dd if=/dev/zero of=tmp/file$$i bs=`xargs<<<'$(TEST_ARGS)' | sed 's/\([0-9]*\).*/2^\1/' | bc` count=1;      \
	    else dd if=/dev/zero of=tmp/file$$i bs=1048576 count=`xargs<<<'$(TEST_ARGS)' | sed 's/\([0-9]*\).*/(2^\1+(2^20-1))\/2^20/' | bc`; fi;   \
	done
	sync
	if [ $(OS) == Darwin ]; then sudo purge; fi
	if [ $(OS) == Linux ]; then sudo bash -c "echo 1 > /proc/sys/vm/drop_caches"; fi

.PHONY: test
test: pre_test
	$(MV) tmp/file0 tmp/sync
	for i in aio tai; do if [ $(OS) == Darwin ]; then dd if=tmp/sync of=tmp/$$i bs=`xargs<<<'$(TEST_ARGS)' | sed 's/\([0-9]*\).*/2^\1/' | bc` count=1; else $(CP) tmp/sync tmp/$$i; fi; done
	sync
	for i in 1 2 4; do                                                                                                          \
	    if [ $(OS) == Darwin ]; then sudo purge; fi;                                                                            \
	    if [ $(OS) == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi;                                         \
	    time (bin/fixSizedWrites $$i `xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^\4/' | bc`); \
	    time sync;                                                                                                              \
	done
	$(CMP) tmp/sync tmp/tai || $(CMP) -l tmp/sync tmp/tai | wc -l
	$(CMP) tmp/sync tmp/aio || $(CMP) -l tmp/sync tmp/aio | wc -l

.PHONY: test_mt
test_mt: pre_test
	for i in 4; do for j in `seq 0 2`; do for k in `seq $(TEST_LOAD)`; do                                               \
	    if [ $(OS) == Darwin ]; then sudo purge; fi;                                                                            \
	    if [ $(OS) == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi;                                         \
	    time (`if [ $(OS) == _Linux ]; then echo 'sudo perf stat -age cs'; fi` bin/multi_thread_comp $$k $$i $$j $(TEST_ARGS));  \
	    time (sync tmp/*);                                                                                                      \
	    time sync;                                                                                                              \
	done done done

.PHONY: test_lat
test_lat: pre_test
	for i in `seq 0 4`; do                                                              \
	    sync;                                                                           \
	    if [ $(OS) == Darwin ]; then sudo purge; fi;                                    \
	    if [ $(OS) == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi; \
	    bin/latency 1 $$i 1 $(TEST_ARGS);                                               \
	    sync tmp/*;                                                                     \
	    sync;                                                                           \
	done 
	

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),pre_test)
ifneq ($(MAKECMDGOALS),test)
ifneq ($(MAKECMDGOALS),test_mt)
ifneq ($(MAKECMDGOALS),test_lat)
sinclude $(DEPS)
endif
endif
endif
endif
endif

.PHONY: clean
clean:
	@$(RM) $(LIBS_DIR) $(DEPS_DIR) $(OBJS_DIR) $(TARGETS_DIR) $(PCHS) tai tai.dSYM
