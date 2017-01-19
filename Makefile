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
export SRCS = $(wildcard $(SRCS_DIR)/*.cpp)
export DEPS = $(patsubst $(INCS_DIR)/%,$(DEPS_DIR)/%.d,$(INCS)) $(patsubst $(SRCS_DIR)/%,$(DEPS_DIR)/%.d,$(SRCS))
export OBJS = $(patsubst $(SRCS_DIR)/%,$(OBJS_DIR)/%.o,$(SRCS))

export LIBTAI = $(LIBS_DIR)/libtai.a

# export CXX = clang++
# export CXX = g++-6
export CXX = g++
export CXXFLAGS = -std=c++1z -m64 -Wall -O3 -g
export CXXFLAGS += -I$(INCS_DIR) -I/usr/local/include
export CXXFLAGS += -stdlib=libc++ -lc++ -lc++abi
export CXXFLAGS += -lm -lpthread
export CXXFLAGS += -fno-omit-frame-pointer -fsanitize=address
export AR = ar
export MKDIR = @mkdir -p
export CMP = cmp -b

export CP = cp -rf
export INSTALL = install
export RM = rm -rf

.PHONY: all
all: $(LIBTAI) tai

tai: $(LIBTAI)
	$(CXX) $(CXXFLAGS) -L$(LIBS_DIR) -ltai -o tai

$(OBJS): $(OBJS_DIR)/%.o: $(SRCS_DIR)/%
	$(MKDIR) $(OBJS_DIR)
	$(CXX) $(CXXFLAGS) -c $^ -o $@

$(DEPS): $(DEPS_DIR)/%.d: $(SRCS_DIR)/%
	$(MKDIR) $(DEPS_DIR)
	$(CXX) $(CXXFLAGS) -MM $^ -o $@

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
	dd if=/dev/zero of=tmp/sync bs=16M count=1
	dd if=/dev/zero of=tmp/tai bs=16M count=1
	sync
	if [ `uname` == Darwin ]; then sudo purge; fi
	if [ `uname` == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi
	time (./tai 1 512 && sync tmp/sync)
	time sync
	if [ `uname` == Darwin ]; then sudo purge; fi
	if [ `uname` == Linux ]; then sudo sh -c "echo 1 > /proc/sys/vm/drop_caches"; fi
	time (./tai 2 512 && sync tmp/tai)
	time sync
	$(CMP) tmp/sync tmp/tai || $(CMP) -l tmp/sync tmp/tai | wc -l

ifneq ($(MAKECMDGOALS),clean)
sinclude $(DEPS)
endif

.PHONY: clean
clean:
	@$(RM) $(LIBS_DIR) $(DEPS_DIR) $(OBJS_DIR) $(TARGETS_DIR) tmp tai
