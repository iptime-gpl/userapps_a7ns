# See the file LICENSE for redistribution information.
#
# Copyright (c) 1996, 2011 Oracle and/or its affiliates.  All rights reserved.
#
# $Id: byteorder.tcl,v 1.1.1.1 2012/04/24 02:56:53 thki81 Exp $
#
# Byte Order Test
# Use existing tests and run with both byte orders.
proc byteorder { method {nentries 1000} } {
	source ./include.tcl
	puts "Byteorder: $method $nentries"

	eval {test001 $method $nentries 0 0 "001" -lorder 1234}
	eval {verify_dir $testdir}
	eval {test001 $method $nentries 0 0 "001" -lorder 4321}
	eval {verify_dir $testdir}
	eval {test003 $method -lorder 1234}
	eval {verify_dir $testdir}
	eval {test003 $method -lorder 4321}
	eval {verify_dir $testdir}
	eval {test010 $method $nentries 5 "010" -lorder 1234}
	eval {verify_dir $testdir}
	eval {test010 $method $nentries 5 "010" -lorder 4321}
	eval {verify_dir $testdir}
	eval {test011 $method $nentries 5 "011" -lorder 1234}
	eval {verify_dir $testdir}
	eval {test011 $method $nentries 5 "011" -lorder 4321}
	eval {verify_dir $testdir}
	eval {test018 $method $nentries -lorder 1234}
	eval {verify_dir $testdir}
	eval {test018 $method $nentries -lorder 4321}
	eval {verify_dir $testdir}
}
