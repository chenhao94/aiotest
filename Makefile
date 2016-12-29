export SHELL = /bin/bash
export DIR = $(shell pwd)
export INCS_DIR = $(DIR)/include
export SRCS_DIR = $(DIR)/src
export LIBS_DIR = $(DIR)/lib
export DEPS_DIR = $(DIR)/dep
export OBJS_DIR = $(DIR)/obj
export TARGETS_DIR = $(DIR)/bin
export TESTS_DIR = $(DIR)/test

export INCS = $(wildcard $(INCS_DIR)/*.h) $(wildcard $(INCS_DIR)/*.hpp)
export SRCS = $(wildcard $(SRCS_DIR)/*.cpp)

export CXX = clang
export CXXFLAGS = -std=c++1z -m64 -Wall -O3 -g -I$(INCS_DIR) -I/usr/local/include -stdlib=libc++ -lc++ -lm
export AR = ar

export CP = cp -rf
export INSTALL = install
export RM = rm -rf

.PHONY: all
all: $(INCS) $(SRCS)
	# $(CXX) $(CXXFLAGS) $(INCS)
	# $(CXX) $(CXXFLAGS) $(SRCS) -c
	$(RM) libtai.a
	# $(AR) -r libtai.a $(wildcard *.o)
	# $(CXX) $(CXXFLAGS) -L. -ltai -o tai
	$(CXX) $(CXXFLAGS) $(SRCS) -o tai

.PHONY: clean
clean:
	@$(MAKE) --no-print-directory -C $(SRCS_DIR)/ clean
	@$(RM) $(DEPS_DIR) $(OBJS_DIR) $(TARGETS_DIR) tmp
