## Process this file with automake to generate a Makefile.in

bin_PROGRAMS = gixpp
gixpp_SOURCES = main.cpp popl.hpp
gixpp_CXXFLAGS = -std=c++17 -I.. -I $(top_srcdir)/common -I$(top_srcdir)/libcpputils -I$(top_srcdir)/libgixpp -I$(top_srcdir)/build-tools/grammar-tools
gixpp_LDFLAGS =
gixpp_LDADD = ../libgixpp/libgixpp.a ../libcpputils/libcpputils.a -lstdc++fs
