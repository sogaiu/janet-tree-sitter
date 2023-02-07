#!/usr/bin/env bash

# Format all code with astyle

STYLEOPTS="--style=attach --indent-switches --convert-tabs \
           --align-pointer=name --pad-header --pad-oper \
           --unpad-paren --indent-labels --formatted \
	   --indent=spaces=2"

astyle $STYLEOPTS */*.c
