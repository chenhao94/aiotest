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

export CXX = clang
export CXXFLAGS = -std=c++1z -m64 -Wall -O3 -g -I$(INCS_DIR) -I/usr/local/include -stdlib=libc++ -lc++ -lc++abi -lm -lpthread
export AR = ar

export CP = cp -rf
export INSTALL = install
export RM = rm -rf

.PHONY: all
all: $(LIBTAI)
	$(CXX) $(CXXFLAGS) -L$(LIBS_DIR) -ltai -o tai

$(OBJS): $(OBJS_DIR)/%.o: $(SRCS_DIR)/%
	@mkdir -p $(OBJS_DIR)
	$(CXX) $(CXXFLAGS) -c $^ -o $@

$(DEPS): $(DEPS_DIR)/%.d: $(SRCS_DIR)/%
	@mkdir -p $(DEPS_DIR)
	$(CXX) $(CXXFLAGS) -MM $^ -o $@

$(LIBTAI): $(OBJS)
	@mkdir -p $(LIBS_DIR)
	$(RM) $@
	$(AR) -r $@ $(OBJS)

ifneq ($(MAKECMDGOALS),clean)
sinclude $(DEPS)
endif

.PHONY: clean
clean:
	@$(RM) $(LIBS_DIR) $(DEPS_DIR) $(OBJS_DIR) $(TARGETS_DIR) tai
