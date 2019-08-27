#!/bin/sh

if test ! -f install-sh ; then touch install-sh ; fi

NPROC=`cat /proc/cpuinfo | grep processor | wc -l`

MAKE=`which gnumake`
if test ! -x "$MAKE" ; then MAKE=`which gmake` ; fi
if test ! -x "$MAKE" ; then MAKE=`which make` ; fi
HAVE_GNU_MAKE=`$MAKE --version|grep -c "Free Software Foundation"`

if test "$HAVE_GNU_MAKE" != "1"; then
echo !!!! Warning: not tested with non Gnu-Make $MAKE
else
echo Found GNU Make at $MAKE ... good.
fi

echo This script runs configure and make...
echo You did remember necessary arguments for configure, right?

if test ! -x `which aclocal`
then echo you need autoconfig and automake to generate the Makefile
fi

if test ! -x `which automake`
then echo you need automake to generate the Makefile
fi

if test ! -x `which autoconf`
then echo you need autoconf to generate the Makefile
fi

if test ! -x `which libtoolize`
then echo you need libtoolize to generate the Makefile
fi

autoheader
aclocal -I m4
libtoolize --ltdl --copy --force
automake --add-missing --copy
automake
autoconf
./configure $*
$MAKE CXXFLAGS="-O3" CFLAGS="-O3" -j$NPROC

