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

export INCS = $(wildcard $(INCS_DIR)/*.hpp)
export PCHS = $(patsubst %,%.gch,$(INCS))
export SRCS = $(wildcard $(SRCS_DIR)/*.cpp)
export TESTS = $(wildcard $(TESTS_DIR)/*.cpp)
export DEPS = $(patsubst $(INCS_DIR)/%,$(DEPS_DIR)/%.d,$(INCS)) $(patsubst $(SRCS_DIR)/%,$(DEPS_DIR)/%.d,$(SRCS))
export OBJS = $(patsubst $(SRCS_DIR)/%,$(OBJS_DIR)/%.o,$(SRCS))
export TESTOBJS = $(patsubst $(TESTS_DIR)/%,$(OBJS_DIR)/%.o,$(TESTS))
export TESTEXES = $(patsubst $(TESTS_DIR)/%.cpp,$(TARGETS_DIR)/%,$(TESTS))

export LIBTAI = $(LIBS_DIR)/libtai.a

export TEST_LOAD ?= $(shell nproc --all)
export TEST_ARGS ?= 30 64 64 14 8 10
export TEST_TYPE ?= 0 2 3 # $(shell seq 0 4)
# For test_mt only:
#     read size, write size (KB)
#     file size, io round, sync rate, wait rate (2^x)

export LD = lld
# export CXX = clang++
# export CXX = g++-7
export CXX = g++
export CXXFLAGS = -std=c++1z -m64 -O3
# export CXXFLAGS += $(shell if [ $(OS) = Linux ]; then echo '-fuse-ld=lld'; fi)
# export CXXFLAGS += -flto -fwhole-program-vtables
export CXXFLAGS += -D_FILE_OFFSET_BITS=64
ifeq ($(mode), debug) 
	CXXFLAGS += -DTAI_DEBUG
endif
export CXXFLAGS += -I$(INCS_DIR) -I/root/usr/include  -I/usr/local/include
# export CXXFLAGS += -stdlib=libc++ -lc++ -lc++abi
# export CXXFLAGS += -DTAI_JEMALLOC -ljemalloc
export CXXFLAGS += -lm -pthread
export CXXFLAGS += $(shell if [ $(OS) = Linux ]; then echo '-L/root/usr/lib -lrt -laio'; fi)
# export CXXFLAGS += -Wall -g -fno-omit-frame-pointer -fsanitize=address -mllvm -asan-use-private-alias
export AR = ar
# export AR = llvm-ar
export MKDIR = mkdir -p
export CMP = cmp -b

export CP = cp -rf
export MV = mv -f
export INSTALL = install
export RM = rm -rf

export TEST_NAME = $(shell echo $(CUR_TIME) | sed 's/.*\///')

.PHONY: all
# all: $(LIBTAI) $(TESTEXES)
all: $(TESTEXES)

$(TESTEXES): $(TARGETS_DIR)/%: $(OBJS_DIR)/%.cpp.o $(LIBTAI)
	@$(MKDIR) $(TARGETS_DIR)
	$(CXX) $^ -o $@ $(CXXFLAGS) 

$(TESTOBJS): $(OBJS_DIR)/%.o: $(TESTS_DIR)/% $(PCHS)
	@$(MKDIR) $(OBJS_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJS): $(OBJS_DIR)/%.o: $(SRCS_DIR)/% $(PCHS)
	@$(MKDIR) $(OBJS_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(DEPS): $(DEPS_DIR)/%.d: $(SRCS_DIR)/%
	@$(MKDIR) $(DEPS_DIR)
	$(CXX) $(CXXFLAGS) -MM $^ -o $@

$(PCHS): %.gch: %
	$(CXX) $(CXXFLAGS) -c $^ -o $@

$(LIBTAI): $(OBJS)
	@$(MKDIR) $(LIBS_DIR)
	@$(RM) $@
	$(AR) cr $@ $(OBJS)

.PHONY: pre_test
pre_test:
	@$(RM) tmp/*
	@$(MKDIR) tmp/log
	@echo 'Source Build '$(SRC_BUILD) | tee -a tmp/log/info.log
	@echo 'Test Build '$(TEST_BUILD) | tee -a tmp/log/info.log
	@echo '================================'  | tee -a tmp/log/info.log
	@echo 'Workload     = '$(TEST_LOAD)' thread(s)' | tee -a tmp/log/info.log
	@echo 'File Size    = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^(\1-20)/' | bc -l`' MB' | tee -a tmp/log/info.log
	@echo 'Read Size    = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/\2/' | bc`' KB' | tee -a tmp/log/info.log
	@echo 'Write Size   = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/\3/' | bc`' KB' | tee -a tmp/log/info.log
	@echo 'I/O Rounds   = '`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^\4/' | bc` | tee -a tmp/log/info.log
	@echo 'Sync Rate    = 1/'`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^\5/' | bc` | tee -a tmp/log/info.log
	@echo 'Wait Rate    = 1/'`xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^\6/' | bc` | tee -a tmp/log/info.log
	@echo '================================' | tee -a tmp/log/info.log
	@echo
	@sudo sync
	@if [ $(OS) == Darwin ]; then sudo purge; fi
	@if [ $(OS) == Linux ]; then sudo bash -c "echo 1 > /proc/sys/vm/drop_caches"; fi
	@for i in $$(seq 0 `expr $(TEST_LOAD) - 1`); do                                                                                              \
	    if [ $(OS) == Darwin ]; then dd if=/dev/zero of=tmp/file$$i bs=`xargs<<<'$(TEST_ARGS)' | sed 's/\([0-9]*\).*/2^\1/' | bc` count=1;      \
	    else dd if=/dev/zero of=tmp/file$$i bs=1048576 count=`xargs<<<'$(TEST_ARGS)' | sed 's/\([0-9]*\).*/(2^\1+(2^20-1))\/2^20/' | bc`; fi;   \
	done
	@sync
	@if [ $(OS) == Darwin ]; then sudo purge; fi
	@if [ $(OS) == Linux ]; then sudo bash -c "echo 1 > /proc/sys/vm/drop_caches"; fi

.PHONY: test
test: pre_test
	@$(MV) tmp/file0 tmp/sync
	@for i in aio tai; do if [ $(OS) == Darwin ]; then dd if=tmp/sync of=tmp/$$i bs=`xargs<<<'$(TEST_ARGS)' | sed 's/\([0-9]*\).*/2^\1/' | bc` count=1; else $(CP) tmp/sync tmp/$$i; fi; done
	@sync
	@for i in 1 2 4; do                                                                                                         \
	    if [ $(OS) == Darwin ]; then sudo purge; fi;                                                                            \
	    if [ $(OS) == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi;                                         \
	    time (bin/fixSizedWrites $$i `xargs <<<'$(TEST_ARGS)' | sed 's/\(.*\) \(.*\) \(.*\) \(.*\) \(.*\) \(.*\)/2^\4/' | bc`); \
	    time sync;                                                                                                              \
	done
	$(CMP) tmp/sync tmp/tai || $(CMP) -l tmp/sync tmp/tai | wc -l
	$(CMP) tmp/sync tmp/aio || $(CMP) -l tmp/sync tmp/aio | wc -l

.PHONY: test_mt
test_mt: pre_test
	#@for i in $(TEST_TYPE); do                                                                        
	@$(MKDIR) log/$(CUR_TIME)
	@rsync -a log/others/$(TEST_NAME) log/$(CUR_TIME)/
	@for i in 3; do                                                                          \
	    for j in `seq 0 2`; do                                                                          \
		    for k in `seq $(TEST_LOAD)`; do                                                             \
			    for l in `seq 0 1`; do                                                                  \
	                if [ $(OS) == Darwin ]; then sudo purge; fi;                                        \
	                if [ $(OS) == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi;     \
	                $(MKDIR) tmp/log/$$l/$$i/$$j log/$(CUR_TIME);                                              \
	                time (`if [ $(OS) == _Linux ]; then echo 'sudo perf stat -age cs'; fi`              \
	                    bin/multi_thread_comp $$i $$j $$k $$l $(TEST_ARGS) 2>&1                         \
						| tee tmp/log/$$l/$$i/$$j/$$k.log                                               \
	                );                                                                                  \
	                time (sync tmp/*);                                                                  \
	                $(MV) tmp/log/$$l/$$i/0 tmp/log/$$l/$$i/r 2>/dev/null;                              \
	                $(MV) tmp/log/$$l/$$i/1 tmp/log/$$l/$$i/w 2>/dev/null;                              \
	                $(MV) tmp/log/$$l/$$i/2 tmp/log/$$l/$$i/rw 2>/dev/null;                             \
	                $(MV) tmp/log/$$l/0 tmp/log/$$l/Posix 2>/dev/null;                                  \
	                $(MV) tmp/log/$$l/1 tmp/log/$$l/DIO 2>/dev/null;                                    \
	                $(MV) tmp/log/$$l/2 tmp/log/$$l/Fstream 2>/dev/null;                                \
	                $(MV) tmp/log/$$l/3 tmp/log/$$l/PosixAIO 2>/dev/null;                               \
	                $(MV) tmp/log/$$l/4 tmp/log/$$l/LibAIO 2>/dev/null;                                 \
	                $(MV) tmp/log/$$l/6 tmp/log/$$l/Tai 2>/dev/null;                                    \
	                $(MV) tmp/log/0 tmp/log/Multi 2>/dev/null;                                          \
	                $(MV) tmp/log/1 tmp/log/Single 2>/dev/null;                                         \
					$(MKDIR) log/$(CUR_TIME);                                                                  \
	                rsync -a log/$(CUR_TIME)/ tmp/log/;                                                        \
	                $(MV) log/$(CUR_TIME) log/~last;                                                           \
	                $(MV) tmp/log log/$(CUR_TIME);                                                             \
	                $(RM) log/~last;                                                                    \
	done done done done

.PHONY: test_tx
test_tx: pre_test
	@for i in $(TEST_TYPE); do for k in `seq $(TEST_LOAD)`; do                              \
	    if [ $(OS) == Darwin ]; then sudo purge; fi;                                        \
	    if [ $(OS) == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi;     \
	    $(MKDIR) tmp/log/$$i log/last;                                                      \
	    time (`if [ $(OS) == _Linux ]; then echo 'sudo perf stat -age cs'; fi`              \
	    bin/transaction $$i 0 $$k 1 $(TEST_ARGS) 2>&1                                       \
	    | tee tmp/log/$$i/$$k.log                                                           \
	    );                                                                                  \
	    $(MV) tmp/log/0 tmp/log/Posix 2>/dev/null;                                          \
	    $(MV) tmp/log/1 tmp/log/DIO 2>/dev/null;                                            \
	    $(MV) tmp/log/2 tmp/log/Fstream 2>/dev/null;                                        \
	    $(MV) tmp/log/3 tmp/log/PosixAIO 2>/dev/null;                                       \
	    $(MV) tmp/log/4 tmp/log/LibAIO 2>/dev/null;                                         \
	    $(MV) tmp/log/5 tmp/log/TaiAIO 2>/dev/null;                                         \
	    $(MV) tmp/log/6 tmp/log/Tai 2>/dev/null;                                            \
	    $(MKDIR) log/last;                                                                  \
	    rsync -a log/last/ tmp/log/;                                                        \
	    $(MV) log/last log/~last;                                                           \
	    $(MV) tmp/log log/last;                                                             \
	    $(RM) log/~last;                                                                    \
	done done

.PHONY: test_lat
test_lat: pre_test
	@for i in $(TEST_TYPE); do for j in `seq 0 1`; do                                   \
	    sync;                                                                           \
	    if [ $(OS) == Darwin ]; then sudo purge; fi;                                    \
	    if [ $(OS) == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi; \
	    bin/latency $$i 1 1 $$j $(TEST_ARGS);                                           \
	    sync tmp/*;                                                                     \
	done done
	

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
	@$(RM) $(LIBS_DIR) $(DEPS_DIR) $(OBJS_DIR) $(TARGETS_DIR) $(PCHS) $(patsubst %,%-*,$(PCHS)) tai tai.dSYM
