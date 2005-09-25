
#!/bin/bash
WANT_AUTOMAKE="1.8" aclocal && libtoolize -c -f && \
aclocal && autoconf && automake -a -c --foreign \
&& chmod +x ltmain.sh
#&& ( \
#	ENTER_INTO=src
#	echo "Entering $ENTER_INTO"
#	cd $ENTER_INTO
#	WANT_AUTOMAKE="1.8" aclocal && \
#		autoconf && \
#		autoheader && \
#		automake -a -c --foreign
#	cd ..
#) && ( \
#	ENTER_INTO=utils
#	echo "Entering $ENTER_INTO"
#	cd $ENTER_INTO
#	WANT_AUTOMAKE="1.8" aclocal && \
#		autoconf && \
#		autoheader && \
#		automake -a -c --foreign
#	cd ..
#) && ( \
#	ENTER_INTO=manpages
#	echo "Entering $ENTER_INTO"
#	cd $ENTER_INTO
#	WANT_AUTOMAKE="1.8" aclocal && \
#		autoconf && \
#		automake -a -c --foreign
#	cd ..
#) \

		#libtoolize -c -f && \
