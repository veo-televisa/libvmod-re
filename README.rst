=======
vmod_re
=======

-------------------------------------------------------------------------
Varnish Module for Regular Expression Matching with Subexpression Capture
-------------------------------------------------------------------------

:Manual section: 3
:Author: Geoffrey Simmons
:Date: 2013-09-28
:Version: 0.1

SYNOPSIS
========

::

        import re;

        re.match(<string>, <regular expresssion>)
        re.match_dyn(<string>, <regular expresssion>)
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

	sub vcl_fetch {
		if (re.match_dyn(req.http.Cookie, beresp.http.X-Regex)) {
		   set resp.http.Foo = re.backref(2, "");
		}
	}

match
-----

Prototype
        re.match(<string>, <regular expression>)
Returns
        boolean
Description
        Determines whether a string matches the given regular
	expression, which never changes for the lifetime of the VCL;
	functionally equivalent to VCL's infix operator ``~`` for
	fixed regex patterns.

	``re.match`` only matches against the pattern provided the
	first time it's called, and does not detect whether the
	pattern is changed after that.
Example
        ``re.match(beresp.http.Surrogate-Control, "max-age=(\d+);mysite")``

match_dyn
---------

Prototype
        re.match_dyn(<string>, <regular expression>)
Returns
        boolean
Description
        Determines whether a string matches the given regular
	expression, which may change during the lifetime of the VCL;
	equivalent to VCL's infix operator ``~`` for arbitrary regex
	patterns.

	``re.match_dyn`` is less efficient than ``re.match``, so if you
	are matching against a fixed regex, you should use ``re.match``.
Example
        ``re.match_dyn(req.http.Cookie, beresp.http.X-Regex)``

backref
-------

Prototype
        re.backref(<integer>, <fallback>)
Returns
        String
Description
        Extracts the `nth` subexpression of the most recent successful
	call to ``re.match`` or ``re.match_dyn`` in the same VCL
	subroutine in the current session, or a fallback string in
	case the extraction fails. Backref 0 indicates the entire
	matched string.  Thus this function behaves like the ``\n``
	symbols in ``regsub`` and ``regsuball``, and the ``$1``,
	``$2`` ...  variables in Perl.

	After unsuccessful matches, the ``fallback`` string is returned
	for any call to ``re.backref``.

	The VCL infix operators ``~`` and ``!~`` do not affect this
	function, nor do the functions ``regsub`` or ``regsuball``.

	``re.backref`` can extract up to 10 subexpressions, in
	addition to the full expression indicated by backref 0.

	If ``re.backref`` is called without any prior call to
	``re.match`` or ``re.match_dyn`` in the same VCL subroutine
	call, then the result is undefined.
Example
        ``set beresp.ttl = std.duration(re.backref(1, "120"), 120s);``

version
-------

Prototype
        re.version()
Returns
        string
Description
        Returns the version string for this vmod.
Example
        ``set resp.http.X-re-version = re.version();``


INSTALLATION
============

Installation requires the Varnish source tree, whose version must
match the version of the binary installation.

Quick start
-----------

1. ``./autogen.sh``  (for git-installation)
2. ``./configure VARNISHSRC=/path/to/your/varnish/source/varnish-cache``
3. ``make``
4. ``make check`` (regression tests)
5. ``make install`` (may require root: sudo make install)

``VARNISHSRC`` is the directory of the Varnish source tree against
which to compile the vmod.

Optionally, you can also set the vmod install dir by adding
``VMODDIR=DIR`` in the ``configure`` step (defaults to the pkg-config
discovered directory from your Varnish installation).

For developers
--------------

As with Varnish itself, you can set additional flags and macros in the
``configure`` step, and you can use any of these options:

* ``--enable-developer-warnings``
* ``--enable-extra-developer-warnings`` (for GCC 4)
* ``--enable-werror``

The vmod must always build successfully with these options enabled.

Also as with Varnish, you can add ``--enable-debugging-symbols``, so
that the vmod's symbols are available to debuggers, in core dumps and
so forth.

ACKNOWLEDGEMENTS
================

Author: Geoffrey Simmons <geoff@uplex.de>, UPLEX Nils Goroll Systemoptimierung.

The implementation was inspired by ideas from Nils Goroll's esicookies
VMOD and pmatch patch for Varnish 2, and by Kristian Lyngstøl's header
VMOD.


HISTORY
=======

Version 0.1: Initial version


LIMITATIONS
===========

The regular expressions in ``re.match`` and ``re.match_dyn`` are
compiled at run-time, so there are no errors at VCL compile-time for
invalid expressions. If an expression is invalid, then an error
message is emitted to Varnish's shared memory log using the
``VCL_error`` tag, and the match always fails.

To maintain per-session state about the most recent regex matches, the
vmod creates a table at initialization, sized to the maximum file
descriptor number (``ulimit -n``) defined for Varnish's process owner
(since Varnish 3 uses file descriptor numbers as session
IDs). Moreover, it fails an assertion (aborting Varnish) if the
process owner is able to increase its own max file descriptor.

The vmod only allocates the state data for session IDs that are
actually used; nevertheless, this may lead to an unecessarily large
memory footprint if the ``ulimit -n`` value for the Varnish user is
excessively high.

If you cannot configure the Varnish user so that it is unable to
increase its own max file descriptor, you can disable the assertion
that fails in that case by compiling the vmod with
``-DDISABLE_MAXFD_TEST``. But be warned that Varnish running the vmod
will crash if it ever uses a file descriptor for a session ID that is
larger than the value of ``ulimit -n`` at vmod initialization.

For best results, configure the Varnish user so that its max file
descriptor is just a bit larger than ``thread_pools *
thread_pool_max``, and cannot be increased by the user.

The vmod allocates space for captured subexpressions from session
workspaces. For typical usage, the default workspace size is almost
certainly enough; but if you are capturing many, long subexpressions
in each session, you might need to increase the Varnish parameter
``sess_workspace``.

Regular expression matching is subject to the same limitations that
hold for standard regexen in VCL, for example as set by the runtime
parameters ``pcre_match_limit`` and ``pcre_match_limit_recursion``.


SEE ALSO
========

* varnishd(1)
* vcl(7)
* pcre(3)

COPYRIGHT
=========

This document is licensed under the same license as the libvmod-re
project. See LICENSE for details.

* Copyright (c) 2013 UPLEX Nils Goroll Systemoptimierung
