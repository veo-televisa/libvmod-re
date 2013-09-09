=======
vmod_re
=======

-------------------------------------------------------------------
Varnish Module for Regular Expression Matching with Backref Capture
-------------------------------------------------------------------

:Manual section: 3
:Author: Geoffrey Simmons
:Date: 2013-09-09
:Version: 0.1

SYNOPSIS
========

::

        import re;

        re.match(<string>, <regular expresssion>)
        re.backref(<integer>, <fallback>)
        re.version()

DESCRIPTION
===========

Varnish Module (vmod) for matching strings against regular expressions,
and for extracting captured substrings after matches.

FUNCTIONS
=========

Example VCL::

	import re;

	sub vcl_recv {
		if (re.match(req.http.Cookie, "\bfoo=(.+)\b")) {
		   set req.http.Foo = re.backref(1, "");
		}
	}


match
-----

Prototype
        re.match(<string>, <regular expression>)
Returns
        boolean
Description
        Determines whether a string matches the given regular expression;
	functionally equivalent to VCL's infix operator `~`. It is subject
	to the same limitations, for example as set by the runtime
	paramters `pcre_match_limit` and `pcre_match_limit_recursion`.
Example
        ``re.match(beresp.http.Surrogate-Control, "max-age=(\d+);mysite")``

backref
-------

Prototype
        re.backref(<integer>, <fallback>)
Returns
        String
Description
        Extracts the `nth` subexpression of the most recent successful
	call to `re.match()` in the current session, or a fallback string
	in case the extraction fails. Backref 0 indicates the full match.
	Thus this function behaves like the `\\n` symbols in `regsub`
	and `regsuball`, and the `$1`, `$2` ... variables in Perl.

	After unsuccessful matches, the `fallback` string is returned
	for any call to `re.backref()`.

	The VCL infix operators `~` and `!~` do not affect this function,
	nor do `regsub` or `regsuball`.

	`re.backref` can extract up to 10 subexpressions, in addition to
	to the full expression indicated by backref 0.
Example
        ``set beresp.ttl = std.duration(re.backref(1, "120"), 120s);``

version
-------

Prototype
        re.version()
Returns
        string
Description
        Returns the string constant version-number of the header vmod.
Example
        ``set resp.http.X-re-version = re.version();``


INSTALLATION
============

Installation requires the Varnish source tree (only the source matching the
binary installation).

1. `./autogen.sh`  (for git-installation)
2. `./configure VARNISHSRC=/path/to/your/varnish/source/varnish-cache`
3. `make`
4. `make install` (may require root: sudo make install)
5. `make check` (Optional for regression tests)

VARNISHSRCDIR is the directory of the Varnish source tree for which to
compile your vmod. Both the VARNISHSRCDIR and VARNISHSRCDIR/include
will be added to the include search paths for your module.

Optionally you can also set the vmod install dir by adding VMODDIR=DIR
(defaults to the pkg-config discovered directory from your Varnish
installation).


ACKNOWLEDGEMENTS
================

Author: Geoffrey Simmons <geoff@uplex.de>, UPLEX Nils Goroll Systemoptimierung.
The implementation was inspired by ideas from Nils Goroll's esicookies VMOD
and pmatch patch for Varnish 2, and by Kristian Lyngstøl's header VMOD.


HISTORY
=======

Version 0.1: Initial version


BUGS
====

You can't use dynamic regular expressions, which also holds true for normal
regular expressions in regsub(), but VCL isn't able to warn you about this
when it comes to vmods yet.


SEE ALSO
========

* varnishd(1)
* vcl(7)

COPYRIGHT
=========

This document is licensed under the same license as the
libvmod-header project. See LICENSE for details.

* Copyright (c) 2013 UPLEX Nils Goroll Systemoptimierung
