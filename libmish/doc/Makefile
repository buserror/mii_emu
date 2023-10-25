#
# 	Copyright 2008, 2009 Michel Pollet <buserror@gmail.com>
#
#	This file is part of libmish.
#
# SPDX-License-Identifier: Apache-2.0

# For the callgraph, you need Graphviz, ruby and exuberant ctags.
# This is not generated in the normal code build


all:	libmish_callgraph.pdf

libmish_callgraph.pdf:
	ctags -f .tags ../src/*.[ch] 2>/dev/null && \
		ruby ./tags_to_dot.rb .tags \
			../src/*.c ../src/mish.h >.tags.dot && \
				dot -Tpdf .tags.dot -o $@

clean:
	rm -f .tags*
