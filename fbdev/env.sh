#!/usr/bin/env bash

# export project wise functions only
# for the newly create shell so to not litter up
# the current if you source this file
(
	# this is used in Makefile 
	# because it is too stupid of a software
	# to recognize the importance of spaces
	generate_clangd () {
		cat <<-'EOF' > .clangd
		CompileFlags:
		  CompilationDatabase: external_tools
		EOF
	}
	
	export -f generate_clangd
	
	packages=(
		# crap for making and running this stuff
		# whilst having decent lsp support
		# yes it is insane thanks to C ecosystem!
		bear tcc make pkg-config
		gcc-toolchain clang-toolchain
		# actual libraries
		cairo
	)

	guix shell ${packages[@]}
	:
)
